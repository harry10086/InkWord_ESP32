#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <SD_MMC.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include <esp_sntp.h>
#include <driver/rtc_io.h>
#include <driver/gpio.h>
#include "soc/rtc.h"
#include <time.h>
#include <sys/time.h>
#include <vector>
#include <map>

// ----------------------------------------------------
// 硬件引脚定义 (ESP32-S3)
// ----------------------------------------------------
// 墨水屏 SPI 引脚 (SE2417FS051 4.2寸三色屏，UC8176 主控，与 V1.0 硬件电路 100% 对齐)
const int PIN_CS = 9;
const int PIN_SCLK = 10;
const int PIN_SDI = 11;
const int PIN_RST = 18;
const int PIN_BUSY = 8;
const int PIN_D_C = 17;

// TF 卡 SDMMC 引脚 (4-bit 高速模式)
const int PIN_SD_CLK = 6;
const int PIN_SD_CMD = 7;
const int PIN_SD_DAT0 = 5;
const int PIN_SD_DAT1 = 4;
const int PIN_SD_DAT2 = 16;
const int PIN_SD_DAT3 = 15;

// 电源与按键引脚
const int PIN_BAT_ADC = 3;  // 电池电压检测 (1/4分压)
const int PIN_CHRG = 1;     // 充电指示 (正常高电平，充电低电平，充满由低变高)
const int PIN_KEY1 = 39;    // 键1: WiFi开关 / 单击进入 SmartConfig 配网
const int PIN_KEY2 = 40;    // 键2: 上一个单词 / 长按切顺序或随机模式 (带 NVS 记忆)
const int PIN_KEY3 = 41;    // 键3: 下一个单词 / 释义翻页 / 长按切词库 (带 NVS 记忆)

#include "LunarCalendar.h"
#include "WebManager.h"
#include <Adafruit_GFX.h>
#include <GxEPD2_3C.h>
#include <U8g2_for_Adafruit_GFX.h>

// ----------------------------------------------------
// 墨水屏驱动配置 (原生三色高对比度驱动: 纯正浓黑 + 鲜艳中国红)
// ----------------------------------------------------
#define EPD_CLASS GxEPD2_420c
GxEPD2_3C<EPD_CLASS, EPD_CLASS::HEIGHT> display(EPD_CLASS(PIN_CS, PIN_D_C, PIN_RST, -1));
#define COLOR_RED GxEPD_RED

SPIClass customSPI(HSPI); // 墨水屏专用SPI
U8G2_FOR_ADAFRUIT_GFX u8g2Fonts; // U8g2 中文字体引擎绑定
WebManager webManager;           // 实例化 Web 网页文件服务器

// ----------------------------------------------------
// 全局状态变量与时钟引擎
// ----------------------------------------------------
Preferences preferences; // NVS 持久化存储对象
bool wifiEnabled = false;
bool smartConfigMode = false;
bool apConfigMode = false;
int lastDailySyncYday = -1;

// ----------------------------------------------------
// 多 Wi-Fi 网络凭据管理系统 (Multi-AP 支持)
// ----------------------------------------------------
struct KnownWifi {
  String ssid;
  String pass;
};

const int MAX_SAVED_WIFI = 8; // 最多记忆 8 个常用 Wi-Fi (家、办公室、热点等)

std::vector<KnownWifi> getSavedWifiList() {
  std::vector<KnownWifi> list;
  preferences.begin("inkwifi", true);
  String jsonStr = preferences.getString("wifilist", "");
  String oldSSID = preferences.getString("ssid", "");
  String oldPass = preferences.getString("pass", "");
  preferences.end();

  if (jsonStr.length() > 0) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, jsonStr);
    if (!err && doc.is<JsonArray>()) {
      JsonArray arr = doc.as<JsonArray>();
      for (JsonObject item : arr) {
        String s = item["ssid"].as<String>();
        String p = item["pass"].as<String>();
        if (s.length() > 0) {
          list.push_back({s, p});
        }
      }
    }
  }

  // 向后兼容旧版本仅有单一 "ssid" 的存储
  if (list.empty() && oldSSID.length() > 0) {
    list.push_back({oldSSID, oldPass});
  }

  return list;
}

void saveWifiCredentials(const String& ssid, const String& pass) {
  if (ssid.length() == 0) return;

  std::vector<KnownWifi> list = getSavedWifiList();

  // 若已存在该 SSID，先移除旧记录
  for (auto it = list.begin(); it != list.end(); ) {
    if (it->ssid == ssid) {
      it = list.erase(it);
    } else {
      ++it;
    }
  }

  // 插入到最前面作为最近优先网络 (MRU)
  list.insert(list.begin(), {ssid, pass});

  // 超出上限则剔除最旧的
  while (list.size() > MAX_SAVED_WIFI) {
    list.pop_back();
  }

  // 序列化回 NVS
  JsonDocument doc;
  JsonArray arr = doc.to<JsonArray>();
  for (const auto& w : list) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = w.ssid;
    obj["pass"] = w.pass;
  }
  String jsonStr;
  serializeJson(doc, jsonStr);

  preferences.begin("inkwifi", false);
  preferences.putString("wifilist", jsonStr);
  preferences.putString("ssid", ssid); // 保持旧键兼容
  preferences.putString("pass", pass);
  preferences.end();

  Serial.printf(">>> [Wi-Fi 凭据管理] 已更新并持久化已知网络列表 (当前共 %d 个):\n", (int)list.size());
  for (size_t i = 0; i < list.size(); i++) {
    Serial.printf(">>>   [%d] SSID: '%s'%s\n", (int)(i + 1), list[i].ssid.c_str(), (i == 0) ? " (当前首选)" : "");
  }
}

String getSavedWifiSSID() {
  std::vector<KnownWifi> list = getSavedWifiList();
  if (!list.empty()) return list[0].ssid;
  return "";
}

String getSavedWifiPass() {
  std::vector<KnownWifi> list = getSavedWifiList();
  if (!list.empty()) return list[0].pass;
  return "";
}
unsigned long lastWifiActivity = 0;
int wifiAutoCloseMin = 6; // Wi-Fi 无数据传输自动关闭时间 (单位: 分钟，可通过 Web 自由配置)
bool nightSilentMode = true; // 深夜 23:00 ~ 07:00 静默节能模式 (默认开启，深夜 0 物理刷屏)

// 用户活跃与动态刷新周期管理
static unsigned long lastUserActivityTime = 0;

// 高精度连续运行毫秒计数器 (基于硬件 RTC 定时器，在 Light Sleep 浅度睡眠期间依然精准累加，绝不冻结！)
inline unsigned long getUptimeMs() {
  return (unsigned long)(esp_timer_get_time() / 1000ULL);
}

// 时钟与基准点 (永不倒退自维持时钟体系)
RTC_DATA_ATTR static time_t rtc_saved_epoch = 0;
RTC_DATA_ATTR static unsigned long rtc_saved_millis = 0;
RTC_DATA_ATTR static uint64_t rtc_saved_ticks = 0;
RTC_DATA_ATTR static int rtc_current_word_index = -1; // 单词进度 RTC 硬件级记忆，休眠与重启不丢失！
static time_t baseSyncEpoch = 0;
static unsigned long baseSyncMillis = 0;

// ----------------------------------------------------
// 词库与单词管理
// ----------------------------------------------------
String currentDictName = "";
std::vector<String> dictFileList;
int currentDictIndex = -1;

struct WordItem {
  String word;
  String phonetic;
  std::vector<String> definitions;
};

std::vector<uint32_t> wordOffsets;
int totalWordCount = 0;

// ----------------------------------------------------
// 国际开源词典标准 8x16 点阵字模库 (GNU Unifont / Doulos SIL 源)
// 包含所有 44 个标准国际音标 (IPA) 字符字形 (左侧留有安全呼吸边距，防粘连)
// ----------------------------------------------------
struct IPAGlyphData {
  uint16_t unicode;
  uint8_t width;
  uint8_t bitmap[16];
};

static const IPAGlyphData IPA_FONT_TABLE[] = {
  // ə (U+0259 / U+018F) - Schwa
  {0x0259, 8, {0x00, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x02, 0x3E, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00}},
  {0x018F, 8, {0x00, 0x00, 0x00, 0x00, 0x1C, 0x22, 0x02, 0x3E, 0x42, 0x42, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ɪ (U+026A) - Small Capital I
  {0x026A, 7, {0x00, 0x00, 0x00, 0x00, 0x7C, 0x10, 0x10, 0x10, 0x10, 0x10, 0x7C, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // æ (U+00E6) - Ash 合体字
  {0x00E6, 11, {0x00, 0x00, 0x00, 0x00, 0x36, 0x49, 0x0F, 0x38, 0x48, 0x49, 0x3E, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ʌ (U+028C) - Turned V (右移1位，确保左侧1px安全字距，杜绝笔画粘连)
  {0x028C, 8, {0x00, 0x00, 0x00, 0x00, 0x08, 0x14, 0x14, 0x22, 0x22, 0x41, 0x41, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ɒ (U+0252) - Turned A (右移1位)
  {0x0252, 8, {0x00, 0x00, 0x00, 0x00, 0x3C, 0x42, 0x42, 0x3E, 0x02, 0x02, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ɔ (U+0254) - Open O
  {0x0254, 8, {0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x04, 0x04, 0x04, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ʊ (U+028A) - Upsilon (右移1位)
  {0x028A, 8, {0x00, 0x00, 0x00, 0x00, 0x42, 0x42, 0x42, 0x42, 0x42, 0x24, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ɜ / ɝ / ɚ (U+025C / U+025D / U+025A) - Reversed Open E
  {0x025C, 8, {0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x04, 0x38, 0x04, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00}},
  {0x025D, 8, {0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x04, 0x38, 0x04, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00}},
  {0x025A, 8, {0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x04, 0x38, 0x04, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ʃ (U+0283) - Esh (右移1位)
  {0x0283, 6, {0x00, 0x0C, 0x12, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x50, 0x20, 0x00, 0x00, 0x00}},
  // ʒ (U+0292) - Ezh
  {0x0292, 7, {0x00, 0x00, 0x00, 0x00, 0x7C, 0x08, 0x10, 0x08, 0x04, 0x04, 0x44, 0x38, 0x40, 0x00, 0x00, 0x00}},
  // θ (U+03B8) - Theta
  {0x03B8, 8, {0x00, 0x00, 0x00, 0x38, 0x44, 0x44, 0x7C, 0x44, 0x44, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ð (U+00F0) - Eth
  {0x00F0, 8, {0x00, 0x04, 0x3C, 0x08, 0x3C, 0x44, 0x44, 0x44, 0x44, 0x44, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ŋ (U+014B) - Eng
  {0x014B, 8, {0x00, 0x00, 0x00, 0x00, 0x4C, 0x64, 0x44, 0x44, 0x44, 0x44, 0x44, 0x04, 0x08, 0x70, 0x00, 0x00}},
  // ˈ (U+02C8) - 主重音符号
  {0x02C8, 4, {0x00, 0x30, 0x30, 0x30, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ˌ (U+02CC) - 次重音符号
  {0x02CC, 4, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x30, 0x30, 0x30, 0x00, 0x00}},
  // ː (U+02D0) / ˑ (U+02D1) - 长音符号
  {0x02D0, 4, {0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  {0x02D1, 4, {0x00, 0x00, 0x00, 0x00, 0x00, 0x30, 0x30, 0x00, 0x30, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
  // ɡ (U+0261) - 单层手写体 g
  {0x0261, 8, {0x00, 0x00, 0x00, 0x00, 0x3C, 0x44, 0x44, 0x3C, 0x04, 0x04, 0x44, 0x38, 0x00, 0x00, 0x00, 0x00}},
  // ɑ (U+0251) - 单层手写体 a
  {0x0251, 8, {0x00, 0x00, 0x00, 0x00, 0x38, 0x44, 0x44, 0x44, 0x44, 0x44, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00}},
};

static const size_t IPA_FONT_TABLE_SIZE = sizeof(IPA_FONT_TABLE) / sizeof(IPA_FONT_TABLE[0]);

// 绘制单个 8x16 专业点阵音标
int drawIPABitmap(int x, int y, uint16_t unicode, uint16_t color) {
  for (size_t i = 0; i < IPA_FONT_TABLE_SIZE; i++) {
    if (IPA_FONT_TABLE[i].unicode == unicode) {
      const uint8_t* bmp = IPA_FONT_TABLE[i].bitmap;
      int w = IPA_FONT_TABLE[i].width;
      for (int row = 0; row < 16; row++) {
        uint8_t byteVal = bmp[row];
        if (byteVal == 0) continue;
        for (int col = 0; col < 8; col++) {
          if (byteVal & (0x80 >> col)) {
            display.drawPixel(x + col, y - 11 + row, color);
          }
        }
      }
      return w;
    }
  }
  return 0; // 未命中
}

void drawPhoneticString(int startX, int startY, const String& rawPhonetic) {
  u8g2Fonts.setFont(u8g2_font_unifont_tf);
  u8g2Fonts.setForegroundColor(COLOR_RED);
  int curX = startX;
  size_t len = rawPhonetic.length();

  for (size_t i = 0; i < len;) {
    uint8_t c = (uint8_t)rawPhonetic[i];
    uint16_t u = 0;
    size_t advBytes = 1;

    if (c < 0x80) {
      u = c;
      advBytes = 1;
    } else if ((c & 0xE0) == 0xC0) { // 2-byte UTF-8
      if (i + 1 < len) {
        uint8_t c2 = (uint8_t)rawPhonetic[i + 1];
        u = ((uint16_t)(c & 0x1F) << 6) | (c2 & 0x3F);
        advBytes = 2;
      }
    } else if ((c & 0xF0) == 0xE0) { // 3-byte UTF-8
      if (i + 2 < len) {
        uint8_t c2 = (uint8_t)rawPhonetic[i + 1];
        uint8_t c3 = (uint8_t)rawPhonetic[i + 2];
        u = ((uint16_t)(c & 0x0F) << 12) | ((uint16_t)(c2 & 0x3F) << 6) | (c3 & 0x3F);
        advBytes = 3;
      }
    }

    if (u < 0x80) { // 标准 ASCII 字符由 unifont 直接绘制 (增加 +1px 舒适字间距，防止笔画粘连)
      char buf[2] = {(char)u, 0};
      u8g2Fonts.setCursor(curX, startY);
      u8g2Fonts.print(buf);
      curX += u8g2Fonts.getUTF8Width(buf) + 1;
    } else { // 国际音标扩展区使用官方词典标准 8x16 点阵字模
      int adv = drawIPABitmap(curX, startY, u, COLOR_RED);
      if (adv > 0) {
        // 主次重音符号留 1px，常规音标留 2px 舒适阅读间隔
        int gap = (u == 0x02C8 || u == 0x02CC) ? 1 : 2;
        curX += adv + gap;
      } else {
        // 兜底尝试 U8g2 打印
        String s = rawPhonetic.substring(i, i + advBytes);
        u8g2Fonts.setCursor(curX, startY);
        u8g2Fonts.print(s);
        curX += u8g2Fonts.getUTF8Width(s.c_str()) + 2;
      }
    }

    i += advBytes;
  }
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
}

WordItem getWordItem(int index);
int currentWordIndex = 0;
bool randomMode = false; // 默认顺序循环
int wordSwitchIntervalMin = 3; // 单词切换基础间隔 (单位: 分钟，可通过 Web 自由配置)
std::vector<int> shuffleIndex;

// 日程备忘提醒字典 (Key: YYYY-MM-DD, Value: 提醒文本)
std::map<String, String> scheduleMap;
void loadSchedules();

// 单词多义翻页管理
int definitionPage = 0;

// 每日名言缓存
String quoteEng = "The secret of getting ahead is getting started.";
String quoteChn = "千里之行，始于足下。";

// 定时任务计数器
unsigned long lastWordSwitchTime = 0;
unsigned long lastBatteryCheckTime = 0;
float batteryPercent = 100.0;
bool isCharging = false;

// NVS 持久化存储对象 (已在全局顶部声明 preferences)
void saveSettings();
void loadSettingsAndDict();
void initAndRestoreTime();
void saveTimeState(time_t epoch);
time_t getCurrentTime();
unsigned long getWordSwitchInterval();

// ----------------------------------------------------
// 辅助定义与申明
// ----------------------------------------------------
const int LINES_PER_PAGE = 7; // 每页 7 行释义，单页信息量大
const int MAX_DEF_PIXEL_WIDTH = 195;
std::vector<String> formatDefinitionLines(const WordItem& item);
void drawScreen(const char* caller = "unknown");
void renderScreenInternal();
void screenTaskFunc(void* pvParameters);
bool connectWifi(int timeoutSec = 15);
bool syncTimeAndQuote();
void loadCachedQuote();
void switchDict();
void loadDictionary(String filename);
void generateShuffleIndex();
void loadOrGenerateShuffleIndex();
void regenerateAndSaveShuffleIndex();
void toggleWifi(bool enable, bool syncAfterConnect = true, bool forceAP = false);
void startConfigPortal();

// ----------------------------------------------------
// FreeRTOS 异步三色刷屏系统 (运行在 Core 0，0卡顿)
// ----------------------------------------------------
static TaskHandle_t screenTaskHandle = NULL;
static volatile bool screenUpdatePending = false;
static volatile bool isScreenBusy = false; // 刷屏进行中强互斥锁 (杜绝休眠掐断 SPI)
static SemaphoreHandle_t sdMutex = NULL;

void screenTaskFunc(void* pvParameters) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    while (screenUpdatePending) {
      screenUpdatePending = false;
      isScreenBusy = true; // 上锁：告知 Core 1 正在进行 15s SPI 刷屏，禁止休眠！
      renderScreenInternal();
      isScreenBusy = false; // 解锁：刷屏完全结束，墨水屏已休眠，允许 Core 1 进入休眠
    }
  }
}

void drawScreen(const char* caller) {
  Serial.printf("=== 触发异步三色刷屏 (caller: %s) ===\n", caller);
  screenUpdatePending = true;
  if (screenTaskHandle != NULL) {
    xTaskNotifyGive(screenTaskHandle);
  }
}

// ----------------------------------------------------
// 电池电压与电量计算 (16次中值滤波 + 锂电池安全截止曲线)
// ----------------------------------------------------
float getBatteryVoltage() {
  const int SAMPLES = 16;
  uint32_t raw_vals[SAMPLES];
  for (int i = 0; i < SAMPLES; i++) {
    raw_vals[i] = analogReadMilliVolts(PIN_BAT_ADC);
    delayMicroseconds(200);
  }
  // 升序排序，剔除瞬态尖峰毛刺
  for (int i = 0; i < SAMPLES - 1; i++) {
    for (int j = i + 1; j < SAMPLES; j++) {
      if (raw_vals[i] > raw_vals[j]) {
        uint32_t tmp = raw_vals[i];
        raw_vals[i] = raw_vals[j];
        raw_vals[j] = tmp;
      }
    }
  }
  // 剔除前4个最小值和后4个最大值，取中间8个计算平均值
  uint32_t sum = 0;
  for (int i = 4; i < 12; i++) {
    sum += raw_vals[i];
  }
  uint32_t avg_mv = (sum / 8) * 4; // 1/4分压电阻映射
  return avg_mv / 1000.0f;
}

float calculateBatteryPercent() {
  float raw_v = getBatteryVoltage();
  // 保护：若硬件分压电阻未接或悬空 (电压 < 1.0V)，默认作为100%满电处理，杜绝误触发低电休眠关机！
  if (raw_v < 1.0f) {
    return 100.0f;
  }
  // 单节锂电池标准工作放电区间：3.10V 为 0% 截止保护点，4.20V 为 100% 满电
  if (raw_v > 4.20f) raw_v = 4.20f;
  if (raw_v < 3.10f) raw_v = 3.10f;
  float pct = ((raw_v - 3.10f) / 1.10f) * 100.0f;
  return round(pct / 5.0f) * 5.0f; // 5%步进
}

// ----------------------------------------------------
// 动态切词周期计算 (夜间与闲置智能拉长，省电)
// ----------------------------------------------------
unsigned long getWordSwitchInterval() {
  time_t rawtime = getCurrentTime();
  struct tm* tinfo = localtime(&rawtime);

  bool isNight = (tinfo && (tinfo->tm_hour >= 23 || tinfo->tm_hour < 7));
  unsigned long idleTime = getUptimeMs() - lastUserActivityTime;

  // 深夜静默模式：23:00 ~ 07:00 期间且无按键操作，彻底停止自动刷屏切词 (0 物理刷新，极度省电)
  if (nightSilentMode && isNight && idleTime > 15 * 60 * 1000) {
    return 0; // 返回 0 表示深夜静默不切词
  }

  unsigned long baseMs = (unsigned long)wordSwitchIntervalMin * 60 * 1000;
  if (idleTime > 2 * 3600 * 1000) {
    return baseMs + 60 * 1000; // 白天连续2小时无操作：稍微延长1分钟切词
  }
  return baseMs; // 正常活跃：按用户设置的分钟数切词
}

// ----------------------------------------------------
// 主程序 SETUP 初始化
// ----------------------------------------------------
void setup() {
  setCpuFrequencyMhz(80); // 开机运行于 80MHz 节能频率
  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("=================================");
  Serial.println("   InkWordESP32 Firmware Start   ");
  Serial.println("=================================");

  // 恢复 RTC / NVS 时钟，确保任何情况下绝不倒退到 1970/12:00
  initAndRestoreTime();

  // 1. 初始化 3 个按键及电量检测 (先调用 gpio_reset_pin 彻底解除 ESP32-S3 默认 JTAG 占用)
  rtc_gpio_hold_dis((gpio_num_t)PIN_CHRG); // 解除可能存在的深度休眠 RTC 锁存
  gpio_reset_pin((gpio_num_t)PIN_KEY1);
  gpio_reset_pin((gpio_num_t)PIN_KEY2);
  gpio_reset_pin((gpio_num_t)PIN_KEY3);
  gpio_reset_pin((gpio_num_t)PIN_CHRG);
  pinMode(PIN_KEY1, INPUT_PULLUP);
  pinMode(PIN_KEY2, INPUT_PULLUP);
  pinMode(PIN_KEY3, INPUT_PULLUP);
  pinMode(PIN_CHRG, INPUT_PULLUP);
  delay(20); // 硬件滤波电容上电缓冲
  Serial.printf(">>> 按键硬件电平初检: KEY1(GPIO %d)=%d, KEY2(GPIO %d)=%d, KEY3(GPIO %d)=%d (未按下正常为 1)\n",
                PIN_KEY1, digitalRead(PIN_KEY1), PIN_KEY2, digitalRead(PIN_KEY2), PIN_KEY3, digitalRead(PIN_KEY3));
  gpio_wakeup_enable((gpio_num_t)PIN_KEY1, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_KEY2, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_KEY3, GPIO_INTR_LOW_LEVEL);
  gpio_wakeup_enable((gpio_num_t)PIN_CHRG, GPIO_INTR_LOW_LEVEL); // 插入充电线自动唤醒
  esp_sleep_enable_gpio_wakeup();
  lastUserActivityTime = getUptimeMs();

  int rawChrg = digitalRead(PIN_CHRG);
  isCharging = (rawChrg == LOW);
  batteryPercent = calculateBatteryPercent();
  Serial.printf(">>> 硬件初检: PIN_CHRG (GPIO %d) 电平 = %d [%s], 电池电压 = %.2f V, 电量 = %.0f%%\n",
                PIN_CHRG, rawChrg, isCharging ? "正在充电 ⚡" : "未充电/已充满", getBatteryVoltage(), batteryPercent);

  // 2. 以 4-bit 模式初始化 SDMMC 卡
  SD_MMC.setPins(PIN_SD_CLK, PIN_SD_CMD, PIN_SD_DAT0, PIN_SD_DAT1, PIN_SD_DAT2, PIN_SD_DAT3);
  if (!SD_MMC.begin("/sdcard", false)) {
    Serial.println("TF卡 (SDMMC) 挂载失败！请检查硬件。");
  } else {
    Serial.printf("TF卡初始化成功，容量: %llu MB\n", SD_MMC.cardSize() / (1024 * 1024));
    if (!SD_MMC.exists("/dict")) SD_MMC.mkdir("/dict");
    if (!SD_MMC.exists("/quote")) SD_MMC.mkdir("/quote");
  }

  // 3. 恢复上次保存的词库选择、系统参数、日程备忘与离线名言缓存
  loadSettingsAndDict();
  loadSchedules();
  loadCachedQuote();

  // 绑定 Web 控制中心回调与日程数据
  webManager.setScheduleMap(&scheduleMap);
  webManager.getSettingsCb = [](String& curDict, int& intv, bool& rand, int& wclose, bool& nightSil, std::vector<String>& dList) {
    curDict = currentDictName;
    intv = wordSwitchIntervalMin;
    rand = randomMode;
    wclose = wifiAutoCloseMin;
    nightSil = nightSilentMode;
    dList = dictFileList;
  };
  webManager.setSettingsCb = [](const String& newDict, int intv, bool rand, int wclose, bool nightSil) {
    wordSwitchIntervalMin = intv;
    if (rand != randomMode) {
      randomMode = rand;
      if (randomMode) {
        loadOrGenerateShuffleIndex();
      }
    }
    wifiAutoCloseMin = wclose;
    nightSilentMode = nightSil;
    if (newDict.length() > 0 && newDict != currentDictName) {
      currentDictName = newDict;
      currentWordIndex = 0;
      rtc_current_word_index = 0;
      definitionPage = 0;
      loadDictionary("/dict/" + currentDictName);
    }
    lastWordSwitchTime = getUptimeMs(); // 重置自动切词计时，避免保存后立刻切词
    saveSettings();
    drawScreen("Web Settings Saved");
  };
  webManager.onScheduleUpdated = []() {
    drawScreen("Web Schedule Updated");
  };
  webManager.onActivity = []() {
    lastWifiActivity = getUptimeMs();
  };
  webManager.onWifiConfigured = [](const String& ssid, const String& pass) {
    Serial.printf(">>> 收到网页配网请求: SSID='%s' ...\n", ssid.c_str());
    WiFi.softAPdisconnect(true); // 关闭 AP，释放射频硬件
    WiFi.disconnect(false);
    WiFi.mode(WIFI_STA);         // 切换为纯客户端 STA 模式
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_19_5dBm);
    WiFi.begin(ssid.c_str(), pass.c_str());
    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 24) {
      delay(500);
      retries++;
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf(">>> 配网成功！已成功连接到 %s, IP: %s\n", ssid.c_str(), WiFi.localIP().toString().c_str());
      saveWifiCredentials(ssid, pass); // 持久化保存至 NVS
      smartConfigMode = false;
      apConfigMode = false;
      wifiEnabled = true;
      lastWordSwitchTime = getUptimeMs(); // 重置切词计时
      syncTimeAndQuote();
      drawScreen("WiFi Config Success");
    } else {
      Serial.println(">>> 无法连接到该 Wi-Fi，请检查名称和密码。重新开启热点...");
      startConfigPortal();
    }
  };

  // 4. 初始化 Eink 硬件 SPI 与 U8g2 字体引擎
  customSPI.begin(PIN_SCLK, -1, PIN_SDI, PIN_CS);
  display.epd2.selectSPI(customSPI, SPISettings(4000000, MSBFIRST, SPI_MODE0));
  display.init(115200, true, 2, false);
  display.setRotation(0);
  u8g2Fonts.begin(display);
  u8g2Fonts.setFontMode(1); // 透明背景模式
  u8g2Fonts.setFontDirection(0);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setBackgroundColor(GxEPD_WHITE);

  // 5. 初始化 SD 互斥锁与 FreeRTOS 异步刷屏任务
  sdMutex = xSemaphoreCreateMutex();
  xTaskCreatePinnedToCore(screenTaskFunc, "ScreenTask", 8192, NULL, 1, &screenTaskHandle, 0);

  // 6. 电池初检
  lastBatteryCheckTime = getUptimeMs();
  batteryPercent = calculateBatteryPercent();

  // 7. 启动策略：若已配置已知网络且(插着充电线或系统时间尚未校准)，开机自动联网对时一次；否则离线节能
  std::vector<KnownWifi> savedList = getSavedWifiList();
  if (savedList.empty() && WiFi.SSID().length() > 0) {
    saveWifiCredentials(WiFi.SSID(), WiFi.psk());
    savedList = getSavedWifiList();
  }

  if (!savedList.empty() && (baseSyncEpoch < 1700000000 || isCharging)) {
    Serial.printf(">>> 开机初检：已保存 %d 个已知网络，首选 [%s]，自动联网对时与同步名言...\n",
                  (int)savedList.size(), savedList[0].ssid.c_str());
    syncTimeAndQuote();
  } else {
    if (savedList.empty()) {
      Serial.println("==================================================");
      Serial.println(">>> 新设备硬件初检：当前尚未配置 Wi-Fi 网络！");
      Serial.println(">>> 请按 KEY1 (GPIO 39) 开启热点与配网模式");
      Serial.println("==================================================");
    }
    toggleWifi(false);
  }

  // 8. 开机首屏三色渲染
  drawScreen("setup");
  lastWordSwitchTime = getUptimeMs();
}

// ----------------------------------------------------
// 主循环 LOOP 业务调度
// ----------------------------------------------------
void loop() {
  // 驱动 SmartConfig 配网完成检测
  if (smartConfigMode) {
    if (WiFi.smartConfigDone() || WiFi.status() == WL_CONNECTED) {
      smartConfigMode = false;
      apConfigMode = false;
      WiFi.softAPdisconnect(true);
      saveWifiCredentials(WiFi.SSID(), WiFi.psk()); // 持久化保存至 NVS
      Serial.printf(">>> 配网成功！已连接至: %s, 本地 IP: %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
      webManager.begin();
      lastWordSwitchTime = getUptimeMs();
      syncTimeAndQuote(); // 连接成功后立刻进行时间及名言同步
      drawScreen("WiFi Config Success");
    }
  }

  // Web 服务器请求处理
  if (wifiEnabled) {
    webManager.handleClient();
  }

  // 检查 WiFi 自动关闭超时 (节能：如果在传输文件或升级固件，绝对不关闭！)
  unsigned long now = getUptimeMs();
  if (wifiEnabled && !apConfigMode) {
    unsigned long closeTimeoutMs = (unsigned long)wifiAutoCloseMin * 60 * 1000;
    if (!webManager.isTransferring && (now - lastWifiActivity > closeTimeoutMs)) {
      Serial.printf("Wi-Fi 已达到 %d 分钟无操作超时，自动关闭以节能...\n", wifiAutoCloseMin);
      toggleWifi(false);
      drawScreen("WiFi Auto Close");
    }
  } else if (wifiEnabled && apConfigMode) {
    // 热点配网模式给足 10 分钟充足时间，超时自动关闭防止彻夜跑电
    if (now - lastWifiActivity > 600000) {
      Serial.println("配网热点已超过 10 分钟无操作，自动关闭以节能...");
      toggleWifi(false);
      drawScreen("AP Timeout Close");
    }
  }

  // 1. 自动单词切换定时器
  if (totalWordCount > 0) {
    unsigned long switchInterval = getWordSwitchInterval();
    if (switchInterval > 0 && (now - lastWordSwitchTime >= switchInterval)) {
      lastWordSwitchTime = now;
      definitionPage = 0;
      currentWordIndex = currentWordIndex + 1;
      if (currentWordIndex >= totalWordCount) {
        currentWordIndex = 0;
        if (randomMode) {
          regenerateAndSaveShuffleIndex(); // 仅当全部单词完整遍历一轮后才重新洗牌！
        }
      }
      rtc_current_word_index = currentWordIndex;
      if (currentWordIndex % 10 == 0) {
        saveSettings(); // 每 10 词持久化一次 NVS，兼顾 Flash 寿命与断电恢复
      }
      drawScreen("Word Switch Auto");
    }
  }

  // 2. 电池电量每小时更新一次 (3600000 毫秒)
  if (now - lastBatteryCheckTime >= 3600000) {
    lastBatteryCheckTime = now;
    batteryPercent = calculateBatteryPercent();

    // 仅在真实测得有效极低电压 (<3.15V) 且未充电时，触发低电保护深度休眠
    float curBatV = getBatteryVoltage();
    if (curBatV >= 2.5f && curBatV < 3.15f && batteryPercent <= 5.0 && digitalRead(PIN_CHRG) == HIGH) {
      Serial.printf(">>> 电池电量极低 (%.2fV, %.0f%%)，执行最后一次屏幕刷新并进入深度休眠等待充电...\n", curBatV, batteryPercent);
      
      // 1. 同步执行完整页面渲染 (主屏幕保留完整的状态栏、时钟、日历、单词与名言，电池图标显示红框[!])
      renderScreenInternal();
      
      // 2. 墨水屏彻底断电休眠 (<1uA)
      display.hibernate();
      
      // 3. 关闭外设与无线硬件
      SD_MMC.end();
      WiFi.disconnect(true);
      WiFi.mode(WIFI_OFF);
      esp_wifi_stop();
      
      // 4. 配置 PIN_CHRG (GPIO 1) RTC 域上拉与锁存，配置插入充电线(低电平)唤醒
      rtc_gpio_init((gpio_num_t)PIN_CHRG);
      rtc_gpio_set_direction((gpio_num_t)PIN_CHRG, RTC_GPIO_MODE_INPUT_ONLY);
      rtc_gpio_pullup_en((gpio_num_t)PIN_CHRG);
      rtc_gpio_pulldown_dis((gpio_num_t)PIN_CHRG);
      rtc_gpio_hold_en((gpio_num_t)PIN_CHRG); // 锁存上拉，杜绝深休眠期间引脚浮空误唤醒
      
      esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_CHRG, 0); // 插入充电线(GPIO1拉低)自动唤醒
      esp_deep_sleep_start();
    }

    drawScreen("Hourly Battery Check");
  }

  // 3. 每日 7 点自动对时和拉取金山名言 (联网优先，离线自动从 TF 卡轮换名言)
  static unsigned long lastDailySyncAttempt = 0;
  static int lastDailyQuoteYday = -1; // 记录今日是否已完成名言轮换 (联网或离线)
  time_t rawtime = getCurrentTime();
  struct tm *timeinfo = localtime(&rawtime);
  if (timeinfo && timeinfo->tm_year > 100) {
    if (timeinfo->tm_hour >= 7 && (lastDailySyncYday != timeinfo->tm_yday || lastDailyQuoteYday != timeinfo->tm_yday)) {
      if (now - lastDailySyncAttempt >= 300000) { // 失败时至少间隔 5 分钟重试
        lastDailySyncAttempt = now;
        Serial.printf(">>> 触发每日晨间 7:00+ 自动联网同步 (当前时间: %02d:%02d, 上次同步天: %d, 名言轮换天: %d, 当前天: %d)\n",
                      timeinfo->tm_hour, timeinfo->tm_min, lastDailySyncYday, lastDailyQuoteYday, timeinfo->tm_yday);
        bool ok = syncTimeAndQuote();
        if (ok) {
          lastDailySyncYday = timeinfo->tm_yday; // 仅在同步真正成功时才锁定今日！
          lastDailyQuoteYday = timeinfo->tm_yday;
          Serial.printf(">>> 今日 7:00+ 自动同步成功，锁定天数: %d\n", lastDailySyncYday);
          drawScreen("Daily 7:00 AM Sync");
        } else {
          Serial.println(">>> 自动同步未成功 (网络不可用或对时超时)");
          // 关键兜底：如果今日名言尚未轮换，立即从 TF 卡历史名言中轮换并刷新屏幕！
          if (lastDailyQuoteYday != timeinfo->tm_yday) {
            Serial.println(">>> 启动离线名言自动轮换机制：从 TF 卡抽取新名言并刷新屏幕...");
            loadCachedQuote();
            lastDailyQuoteYday = timeinfo->tm_yday; // 锁定今日名言已轮换，防止 5 分钟后重复闪屏
            drawScreen("Daily Offline Quote Switch");
          }
          Serial.println(">>> 保持对时未锁定，后续将继续尝试静默对时...");
        }
      }
    }
  }

  // ----------------------------------------------------
  // 充电状态实时硬件检测 (插上/拔出/充满自动感知并即时刷新)
  // ----------------------------------------------------
  static int lastChrgVal = digitalRead(PIN_CHRG); // 启动对齐当前真实电平，杜绝开机首轮误触发 Charger Plugged 刷屏
  int currChrgVal = digitalRead(PIN_CHRG);
  if (currChrgVal != lastChrgVal) {
    delay(40); // 硬件消抖
    currChrgVal = digitalRead(PIN_CHRG);
    if (currChrgVal != lastChrgVal) {
      lastChrgVal = currChrgVal;
      isCharging = (currChrgVal == LOW);
      batteryPercent = calculateBatteryPercent(); // 重新测量当前电量
      Serial.printf(">>> 充电状态跳变: %s (PIN_CHRG GPIO %d 电平=%d, 电量=%.0f%%, 电压=%.2f V)\n",
                    isCharging ? "充电中 [⚡]" : "未充电 / 已充满", PIN_CHRG, currChrgVal, batteryPercent, getBatteryVoltage());
      drawScreen(isCharging ? "Charger Plugged" : "Charger Unplugged/Full");
    }
  }

  // 充电期间每 5 分钟检测一次电量读数，仅当电量变化 >= 5% 时才刷新
  static unsigned long lastChrgProgressCheck = getUptimeMs();
  static float lastChrgReportedPct = batteryPercent;
  if (isCharging && (now - lastChrgProgressCheck >= 300000)) {
    lastChrgProgressCheck = now;
    float newBat = calculateBatteryPercent();
    if (abs(newBat - lastChrgReportedPct) >= 5.0f) {
      lastChrgReportedPct = newBat;
      batteryPercent = newBat;
      drawScreen("Charging Progress");
    }
  }

  // ----------------------------------------------------
  // 高可靠性的 3 按键非阻塞扫描状态机
  // ----------------------------------------------------
  static unsigned long keyTimers[3] = {0, 0, 0};
  static bool keyPrevStates[3] = {HIGH, HIGH, HIGH};
  static bool keyInited = false;
  if (!keyInited) {
    keyPrevStates[0] = digitalRead(PIN_KEY1);
    keyPrevStates[1] = digitalRead(PIN_KEY2);
    keyPrevStates[2] = digitalRead(PIN_KEY3);
    keyInited = true;
  }
  int keyPins[3] = {PIN_KEY1, PIN_KEY2, PIN_KEY3};

  for (int i = 0; i < 3; i++) {
    bool currState = digitalRead(keyPins[i]);
    if (currState == LOW && keyPrevStates[i] == HIGH) { // 按键按下瞬间
      keyTimers[i] = getUptimeMs();
      keyPrevStates[i] = LOW;
      Serial.printf(">>> [按键检测] KEY%d (GPIO %d) 按下\n", i + 1, keyPins[i]);
    } else if (currState == HIGH && keyPrevStates[i] == LOW) { // 按键释放瞬间
      unsigned long pressDuration = getUptimeMs() - keyTimers[i];
      keyPrevStates[i] = HIGH;
      Serial.printf(">>> [按键检测] KEY%d (GPIO %d) 释放，按压时长: %lu ms\n", i + 1, keyPins[i], pressDuration);
      if (pressDuration > 30) {                     // 优化消抖为 30ms，轻点即响应
        lastUserActivityTime = getUptimeMs();            // 刷新用户活跃时间戳
        bool isLongPress = (pressDuration >= 1500); // 1.5秒定义为长按

        // 防挤压与多键冲突过滤：如果检测到当前有其他按键同时处于被按下的低电平状态，忽略误触发
        bool otherKeyPressed = false;
        for (int k = 0; k < 3; k++) {
          if (k != i && digitalRead(keyPins[k]) == LOW) {
            otherKeyPressed = true;
            break;
          }
        }
        if (otherKeyPressed) {
          Serial.printf(">>> [按键防误触] 忽略多键同时被挤压按下 (KEY%d)\n", i + 1);
          continue;
        }

        if (i == 0) { // KEY 1 (GPIO 39): Wi-Fi 开关 / 配网模式
          if (isLongPress) {
            // 长按 KEY1 (>=1.5s)：秒级直接强制开启 AP 热点配网模式，跳过已知网络连接等待！
            Serial.println(">>> [KEY1 长按] 秒级强制开启 InkWord-Setup 热点配网模式");
            if (wifiEnabled) {
              toggleWifi(false);
              delay(30);
            }
            toggleWifi(true, false, true); // forceAP = true
            drawScreen("KEY1 Force AP Hotspot");
          } else {
            // 短按 KEY1：正常切换 Wi-Fi 开/关 (开时尝试连接已知网络并同步)
            if (!wifiEnabled) {
              toggleWifi(true, true, false);
            } else {
              toggleWifi(false);
            }
            drawScreen("KEY1 Wifi Toggle");
          }
        } else if (i == 1) { // KEY 2 (GPIO 40): 上一个单词 / 长按切换模式
          if (isLongPress) {
            randomMode = !randomMode;
            if (randomMode) loadOrGenerateShuffleIndex();
            saveSettings();
            drawScreen("KEY2 Mode Toggle");
          } else {
            if (totalWordCount > 0) {
              definitionPage = 0;
              currentWordIndex = (currentWordIndex - 1 + totalWordCount) % totalWordCount;
              rtc_current_word_index = currentWordIndex;
              lastWordSwitchTime = getUptimeMs();
              drawScreen("KEY2 Prev Word");
            }
          }
        } else if (i == 2) { // KEY 3 (GPIO 41): 下一个单词 / 翻页 / 长按切词库
          if (isLongPress) {
            switchDict();
            lastWordSwitchTime = getUptimeMs();
            drawScreen("KEY3 Switch Dict");
          } else {
            if (totalWordCount > 0) {
              int realIdx = currentWordIndex;
              if (randomMode) {
                if (shuffleIndex.size() != (size_t)totalWordCount) {
                  loadOrGenerateShuffleIndex();
                }
                if (currentWordIndex >= 0 && currentWordIndex < (int)shuffleIndex.size()) {
                  realIdx = shuffleIndex[currentWordIndex];
                }
              }
              WordItem item = getWordItem(realIdx);
              std::vector<String> lines = formatDefinitionLines(item);
              int maxPages = (lines.size() + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
              if (maxPages < 1) maxPages = 1;

              if (definitionPage < maxPages - 1) {
                definitionPage++;
              } else {
                definitionPage = 0;
                currentWordIndex = currentWordIndex + 1;
                if (currentWordIndex >= totalWordCount) {
                  currentWordIndex = 0;
                  if (randomMode) regenerateAndSaveShuffleIndex();
                }
                rtc_current_word_index = currentWordIndex;
                saveSettings();
              }

              lastWordSwitchTime = getUptimeMs();
              drawScreen("KEY3 Next Word/Page");
            }
          }
        }
      }
    }
  }

  // 核心进阶节能：仅在非充电、无网络、无配网、屏幕空闲且无按键按下时进入 Light Sleep
  bool anyKeyPressed = (digitalRead(PIN_KEY1) == LOW || digitalRead(PIN_KEY2) == LOW || digitalRead(PIN_KEY3) == LOW);
  if (!isCharging && !wifiEnabled && !smartConfigMode && !apConfigMode && !screenUpdatePending && !isScreenBusy && !anyKeyPressed) {
    unsigned long interval = getWordSwitchInterval();
    unsigned long nowMs = getUptimeMs();
    unsigned long elapsed = nowMs - lastWordSwitchTime;
    unsigned long remainingMs = 1000;
    if (interval > 0) {
      remainingMs = (elapsed < interval) ? (interval - elapsed) : 100;
    }
    
    // 电池供电待机期间单次睡眠上限 1 秒，按键即点即应、绝不丢键！
    if (remainingMs > 1000) remainingMs = 1000;
    if (remainingMs < 50) remainingMs = 50;

    esp_sleep_enable_timer_wakeup((uint64_t)remainingMs * 1000ULL);
    esp_light_sleep_start();
  } else {
    delay(20); // 充电中、联网中、配网中或后台刷屏中微量让步，保证 CPU 活跃与 USB 串口通信正常
  }
}

// ----------------------------------------------------
// WiFi 开关 与配网管理逻辑
// ----------------------------------------------------
void startConfigPortal() {
  setCpuFrequencyMhz(240);
  WiFi.disconnect(true);        // 彻底切断任何旧的 STA，防止后台信道抢占
  WiFi.mode(WIFI_AP);           // 纯 AP 模式！单物理射频 100% 专职发射 SoftAP，杜绝 STA 信道冲突与信标受损
  
  // 关键：必须先启动 softAP，再调用 setTxPower 和 setSleep，否则会因网络接口未启动而配置失败！
  WiFi.softAP("InkWord-Setup"); // 默认信道 1，无密码，标准开放广播
  WiFi.setSleep(false);         // softAP 启动后禁用射频休眠
  WiFi.setTxPower(WIFI_POWER_19_5dBm); // softAP 启动后射频功率拉满 19.5dBm
  
  IPAddress apIP = WiFi.softAPIP();
  wifiEnabled = true;
  smartConfigMode = false;
  apConfigMode = true;
  lastWifiActivity = getUptimeMs();
  webManager.begin();

  Serial.println("==================================================");
  Serial.println(">>> 已启动【AP 网页独立配网模式】！");
  Serial.printf(">>> 1. 手机/电脑请连接热点: InkWord-Setup (无密码, 纯 AP 信标广播)\n");
  Serial.printf(">>> 2. 浏览器打开: http://%s 进行可视化配网\n", apIP.toString().c_str());
  Serial.println("==================================================");
  // 屏幕刷新由调用方(KEY1)统一执行，杜绝双重刷屏
}

bool connectWifi(int timeoutSec) {
  std::vector<KnownWifi> knownList = getSavedWifiList();
  if (knownList.empty()) {
    String curSSID = WiFi.SSID();
    String curPass = WiFi.psk();
    if (curSSID.length() > 0) {
      knownList.push_back({curSSID, curPass});
    }
  }
  if (knownList.empty()) {
    Serial.println(">>> 当前未保存任何已知 Wi-Fi 网络凭据！");
    return false;
  }

  setCpuFrequencyMhz(240); // 提频至 240MHz 满血性能
  WiFi.mode(WIFI_STA);    // 纯客户端 STA 模式，杜绝 SoftAP 信道干扰
  WiFi.setSleep(false);   // 禁用射频休眠
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  String targetSSID = "";
  String targetPass = "";

  if (knownList.size() == 1) {
    targetSSID = knownList[0].ssid;
    targetPass = knownList[0].pass;
    Serial.printf(">>> 当前仅有 1 个已知网络，直接尝试连接 [%s]...\n", targetSSID.c_str());
  } else {
    // 多网络环境：先进行快速扫描匹配周边最强已知网络
    Serial.printf(">>> 当前已记忆 %d 个已知网络，正在扫描周边热点以自动匹配...\n", (int)knownList.size());
    int n = WiFi.scanNetworks(false, false, false, 250); // 快速扫描（每信道250ms）
    int bestRssi = -999;

    if (n > 0) {
      for (int i = 0; i < n; i++) {
        String scanSSID = WiFi.SSID(i);
        int scanRssi = WiFi.RSSI(i);
        for (const auto& kw : knownList) {
          if (kw.ssid == scanSSID) {
            if (scanRssi > bestRssi) {
              bestRssi = scanRssi;
              targetSSID = kw.ssid;
              targetPass = kw.pass;
            }
          }
        }
      }
      WiFi.scanDelete(); // 及时释放扫描结果内存
    }

    if (targetSSID.length() > 0) {
      Serial.printf(">>> 智能匹配成功！检测到已知网络 [%s] (信号: %d dBm)，优先连接...\n", targetSSID.c_str(), bestRssi);
    } else {
      Serial.println(">>> 周边未明确扫描到已知网络，将按最近使用优先级依次尝试连接...");
    }
  }

  // 1. 如果匹配到了明确的目标网络，优先连接
  if (targetSSID.length() > 0) {
    WiFi.disconnect(false);
    if (targetPass.length() > 0) {
      WiFi.begin(targetSSID.c_str(), targetPass.c_str());
    } else {
      WiFi.begin(targetSSID.c_str());
    }
    wifiEnabled = true;
    lastWifiActivity = getUptimeMs();

    int retries = 0;
    int maxRetries = timeoutSec * 2;
    while (WiFi.status() != WL_CONNECTED && retries < maxRetries) {
      delay(500);
      retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf(">>> Wi-Fi 连接成功！SSID: %s, IP: %s\n", targetSSID.c_str(), WiFi.localIP().toString().c_str());
      saveWifiCredentials(targetSSID, targetPass); // 更新为当前最近首选 (MRU)
      webManager.begin();
      return true;
    }
    Serial.printf(">>> 优先连接 [%s] 超时失败\n", targetSSID.c_str());
  }

  // 2. 回退机制：若多网络中未匹配到或优先连接失败，依次尝试已知列表里的其他网络
  for (const auto& kw : knownList) {
    if (kw.ssid == targetSSID) continue; // 跳过刚才已失败的目标
    Serial.printf(">>> 尝试回退连接已知网络 [%s]...\n", kw.ssid.c_str());
    WiFi.disconnect(false);
    if (kw.pass.length() > 0) {
      WiFi.begin(kw.ssid.c_str(), kw.pass.c_str());
    } else {
      WiFi.begin(kw.ssid.c_str());
    }
    wifiEnabled = true;
    lastWifiActivity = getUptimeMs();

    int retries = 0;
    while (WiFi.status() != WL_CONNECTED && retries < 8) { // 每个网络尝试 4 秒
      delay(500);
      retries++;
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf(">>> Wi-Fi 回退连接成功！SSID: %s, IP: %s\n", kw.ssid.c_str(), WiFi.localIP().toString().c_str());
      saveWifiCredentials(kw.ssid, kw.pass); // 移至首位
      webManager.begin();
      return true;
    }
  }

  Serial.println(">>> 所有已知 Wi-Fi 网络均未能连接！");
  return false;
}

void toggleWifi(bool enable, bool syncAfterConnect, bool forceAP) {
  if (enable) {
    if (forceAP) {
      Serial.println(">>> 强制启动 AP 热点配网模式...");
      startConfigPortal();
      return;
    }
    std::vector<KnownWifi> list = getSavedWifiList();
    if (list.empty() && WiFi.SSID().length() == 0) {
      Serial.println(">>> 当前未配置 Wi-Fi，直接启动 AP 配网热点...");
      startConfigPortal();
      return;
    }
    bool ok = connectWifi(8); // 优化连接等待时间为 8 秒，兼顾扫描与握手
    if (!ok) {
      Serial.println(">>> 无法连接到已知 Wi-Fi，自动切换至 AP 热点配网模式...");
      startConfigPortal();
    } else {
      if (syncAfterConnect) {
        syncTimeAndQuote();
      }
    }
  } else {
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(false); // 保持已存网络凭据，严禁传 true 误擦除 AP 配置
    WiFi.mode(WIFI_OFF);
    esp_wifi_stop(); // 彻底关闭 Wi-Fi 射频 PHY 与基带时钟，切断射频电路静态功耗
    setCpuFrequencyMhz(80); // 降频至 80MHz 节能模式
    wifiEnabled = false;
    smartConfigMode = false;
    apConfigMode = false;
    Serial.println("Wi-Fi 硬件完全关闭，CPU 降频至 80MHz 节能模式");
  }
}

// ----------------------------------------------------
// RTC 慢速内存与 NVS 时钟恢复引擎 (永不倒退、离线自增自维持)
// ----------------------------------------------------
void saveTimeState(time_t epoch) {
  preferences.begin("inktime", false);
  preferences.putUInt("epoch", (uint32_t)epoch);
  preferences.putUInt("ms", (uint32_t)getUptimeMs());
  preferences.end();
}

time_t getCurrentTime() {
  time_t sysTime = time(nullptr);
  if (sysTime > 1700000000) {
    if (baseSyncEpoch < 1700000000) {
      baseSyncEpoch = sysTime;
      baseSyncMillis = getUptimeMs();
    }
    return sysTime;
  }

  unsigned long nowMs = getUptimeMs();
  if (baseSyncEpoch > 1700000000) {
    unsigned long elapsedSec = (nowMs >= baseSyncMillis) ? ((nowMs - baseSyncMillis) / 1000) : 0;
    return baseSyncEpoch + elapsedSec;
  }

  if (rtc_saved_epoch > 1700000000 && rtc_saved_ticks > 0) {
    uint64_t current_ticks = rtc_time_get();
    if (current_ticks >= rtc_saved_ticks) {
      uint64_t elapsed_ticks = current_ticks - rtc_saved_ticks;
      uint32_t cal = rtc_clk_cal(RTC_CAL_RTC_MUX, 1024);
      uint64_t elapsed_us = rtc_time_slowclk_to_us(elapsed_ticks, cal);
      return rtc_saved_epoch + (time_t)(elapsed_us / 1000000ULL);
    }
  }

  return sysTime;
}

void initAndRestoreTime() {
  setenv("TZ", "CST-8", 1);
  tzset();

  time_t cur = time(nullptr);
  if (cur > 1700000000) {
    baseSyncEpoch = cur;
    baseSyncMillis = getUptimeMs();
    Serial.printf(">>> 系统硬件 RTC 正常计时中 (当前 Epoch: %u)\n", (uint32_t)cur);
    return;
  }

  // 1. 尝试从芯片内部始终保持计数的硬件 RTC 慢速时钟计数器恢复 (软件重启、异常复位均绝不丢失时间！)
  if (rtc_saved_epoch > 1700000000 && rtc_saved_ticks > 0) {
    uint64_t current_ticks = rtc_time_get();
    if (current_ticks >= rtc_saved_ticks) {
      uint64_t elapsed_ticks = current_ticks - rtc_saved_ticks;
      uint32_t cal = rtc_clk_cal(RTC_CAL_RTC_MUX, 1024);
      uint64_t elapsed_us = rtc_time_slowclk_to_us(elapsed_ticks, cal);
      time_t recovered = rtc_saved_epoch + (time_t)(elapsed_us / 1000000ULL);
      struct timeval tv = { .tv_sec = recovered, .tv_usec = 0 };
      settimeofday(&tv, nullptr);
      baseSyncEpoch = recovered;
      baseSyncMillis = getUptimeMs();
      Serial.printf(">>> 成功从硬件 RTC 计数器恢复实时时钟 (Epoch: %u, 已走过: %llu 秒)\n", (uint32_t)recovered, elapsed_us / 1000000ULL);
      return;
    }
  }

  // 2. 尝试从 NVS 掉电持久化存储恢复
  preferences.begin("inktime", true);
  uint32_t nvs_epoch = preferences.getUInt("epoch", 0);
  preferences.end();

  if (nvs_epoch > 1700000000) {
    baseSyncEpoch = (time_t)nvs_epoch;
    baseSyncMillis = getUptimeMs();
    struct timeval tv = { .tv_sec = (time_t)nvs_epoch, .tv_usec = 0 };
    settimeofday(&tv, nullptr);
    Serial.printf(">>> 成功从 NVS 恢复上次保存的时钟 (Epoch: %u)\n", nvs_epoch);
    return;
  }
}

// ----------------------------------------------------
// 离线名言单文件快速加载引擎 (从 /quote/quotes.json 读取历史最新)
// ----------------------------------------------------
void loadSchedules() {
  scheduleMap.clear();
  if (!SD_MMC.exists("/schedule.json")) return;
  File f = SD_MMC.open("/schedule.json", FILE_READ);
  if (!f) return;
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (!err && doc.is<JsonObject>()) {
    JsonObject obj = doc.as<JsonObject>();
    for (JsonPair kv : obj) {
      scheduleMap[String(kv.key().c_str())] = kv.value().as<String>();
    }
    Serial.printf(">>> 成功加载日程备忘，共 %d 条\n", scheduleMap.size());
  }
}

// ----------------------------------------------------
// 每日名言智能加载引擎 (今日已联网显示今日，未联网随机抽取历史名言)
// ----------------------------------------------------
void loadCachedQuote() {
  quoteEng = "The secret of getting ahead is getting started.";
  quoteChn = "千里之行，始于足下。";

  if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);

  if (!SD_MMC.exists("/quote/quotes.json")) {
    if (SD_MMC.exists("/quote/quote.json")) {
      File f = SD_MMC.open("/quote/quote.json", FILE_READ);
      if (f) {
        JsonDocument doc;
        if (!deserializeJson(doc, f)) {
          if (doc["content"].is<String>()) quoteEng = doc["content"].as<String>();
          if (doc["note"].is<String>()) quoteChn = doc["note"].as<String>();
        }
        f.close();
      }
    }
    if (sdMutex) xSemaphoreGive(sdMutex);
    return;
  }

  // 获取今天日期字符串 (格式: YYYY-MM-DD)
  time_t rawtime = getCurrentTime();
  struct tm* tinfo = localtime(&rawtime);
  char todayBuf[32] = "";
  if (tinfo && tinfo->tm_year > 100) {
    sprintf(todayBuf, "%04d-%02d-%02d", tinfo->tm_year + 1900, tinfo->tm_mon + 1, tinfo->tm_mday);
  }
  String todayDateStr = String(todayBuf);

  File f = SD_MMC.open("/quote/quotes.json", FILE_READ);
  if (f) {
    std::vector<String> allLines;
    String todayLine = "";

    while (f.available()) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() > 0) {
        allLines.push_back(line);
        if (todayDateStr.length() >= 8 && line.indexOf(todayDateStr) != -1) {
          todayLine = line;
        }
      }
    }
    f.close();

    String selectedLine = "";
    if (todayLine.length() > 0) {
      selectedLine = todayLine;
      Serial.printf(">>> 今日已记录专属名言 (日期: %s)\n", todayDateStr.c_str());
    } else if (allLines.size() > 0) {
      // 离线/今日未同步：随机/轮播从历史记录中挑选一条
      int r = (int)(esp_random() % allLines.size());
      selectedLine = allLines[r];
      // 若历史库有多条名言，且随机抽取的恰好是当前正在显示的这条，选择下一条保证切实切换
      if (allLines.size() > 1 && selectedLine.indexOf(quoteEng) != -1) {
        r = (r + 1) % allLines.size();
        selectedLine = allLines[r];
      }
      Serial.printf(">>> 离线状态：从历史名言库抽取第 %d/%d 条\n", r + 1, (int)allLines.size());
    }

    if (selectedLine.length() > 0) {
      JsonDocument doc;
      DeserializationError err = deserializeJson(doc, selectedLine);
      if (!err && doc["content"].is<String>() && doc["note"].is<String>()) {
        quoteEng = doc["content"].as<String>();
        quoteChn = doc["note"].as<String>();
      }
    }
  }
  if (sdMutex) xSemaphoreGive(sdMutex);
}

// ----------------------------------------------------
// 每日名言联网同步与单文件持续追加归档引擎 (汇总在同一个 quotes.json)
// 返回 true 表示 NTP 对时或名言拉取成功；返回 false 表示联网或接口失败
// ----------------------------------------------------
bool syncTimeAndQuote() {
  bool originalWifiState = wifiEnabled;
  bool syncSuccess = false;

  // 若 Wi-Fi 未开启，则尝试自动联网 (给足 15 秒连接时间，绝不进入 SmartConfig 干扰)
  if (!wifiEnabled) {
    if (!connectWifi(15)) {
      Serial.println(">>> 自动同步失败: 无法在 15 秒内连上已知 Wi-Fi 网络");
      toggleWifi(false);
      return false;
    }
  }

  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    lastWifiActivity = getUptimeMs();
    Serial.println(">>> 正在启动 NTP 网络授时 (阿里云 + NTP池)...");

    if (esp_sntp_enabled()) {
      esp_sntp_stop();
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "cn.pool.ntp.org");
    esp_sntp_setservername(2, "pool.ntp.org");
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_init();
    setenv("TZ", "CST-8", 1);
    tzset();

    // 真正等待 SNTP 握手完成并更新系统时钟 (最多等待 6 秒)
    int sntpWait = 0;
    while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && sntpWait < 60) {
      delay(100);
      sntpWait++;
    }

    if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
      time_t nowSec = time(nullptr);
      struct tm timeinfo;
      localtime_r(&nowSec, &timeinfo);
      baseSyncEpoch = nowSec;
      baseSyncMillis = getUptimeMs();
      rtc_saved_epoch = nowSec;
      rtc_saved_millis = getUptimeMs();
      rtc_saved_ticks = rtc_time_get();
      saveTimeState(nowSec);
      syncSuccess = true;
      lastDailySyncYday = timeinfo.tm_yday;
      Serial.printf(">>> NTP 对时成功并锁定基准点: %04d-%02d-%02d %02d:%02d:%02d (天数锁定: %d)\n",
                    timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                    timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, lastDailySyncYday);
    } else {
      Serial.println(">>> NTP 授时响应超时 (可能局域网拦截 UDP 123)，准备通过 HTTP 协议校准时间...");
    }

    // HTTP 拉取每日名言 (同时提供精准 HTTP Date 兜底对时)
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const char* headerKeys[] = {"Date"};
    http.collectHeaders(headerKeys, 1);

    if (http.begin(client, "https://open.iciba.com/dsapi/")) {
      int httpCode = http.GET();
      if (httpCode == HTTP_CODE_OK) {
        // 若 NTP 未成功，利用 HTTP 服务器 Date 响应头秒级精准对时
        if (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED && http.hasHeader("Date")) {
          String httpDateStr = http.header("Date");
          int mday = 0, year = 0, hour = 0, min = 0, sec = 0;
          char monthStr[8] = "";
          if (sscanf(httpDateStr.c_str(), "%*[^,], %d %s %d %d:%d:%d", &mday, monthStr, &year, &hour, &min, &sec) == 6) {
            const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
            int mon = -1;
            for (int m = 0; m < 12; m++) {
              if (strncasecmp(monthStr, months[m], 3) == 0) {
                mon = m;
                break;
              }
            }
            if (mon >= 0 && year >= 2024) {
              struct tm tmHttp = {0};
              tmHttp.tm_year = year - 1900;
              tmHttp.tm_mon = mon;
              tmHttp.tm_mday = mday;
              tmHttp.tm_hour = hour + 8; // UTC 转北京时间
              tmHttp.tm_min = min;
              tmHttp.tm_sec = sec;
              tmHttp.tm_isdst = 0;
              time_t httpEpoch = mktime(&tmHttp);
              if (httpEpoch > 1700000000) {
                struct timeval tv = { .tv_sec = httpEpoch, .tv_usec = 0 };
                settimeofday(&tv, NULL);
                baseSyncEpoch = httpEpoch;
                baseSyncMillis = getUptimeMs();
                rtc_saved_epoch = httpEpoch;
                rtc_saved_millis = getUptimeMs();
                rtc_saved_ticks = rtc_time_get();
                saveTimeState(httpEpoch);
                syncSuccess = true;
                struct tm timeinfo;
                localtime_r(&httpEpoch, &timeinfo);
                lastDailySyncYday = timeinfo.tm_yday;
                Serial.printf(">>> HTTP Date 兜底对时成功！当前标准时间: %04d-%02d-%02d %02d:%02d:%02d\n",
                              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
                              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
              }
            }
          }
        }
        String payload = http.getString();
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, payload);
        if (!err && doc["content"].is<String>() && doc["note"].is<String>()) {
          quoteEng = doc["content"].as<String>();
          quoteChn = doc["note"].as<String>();
          syncSuccess = true;

          String dateStr = doc["dateline"].as<String>();
          if (dateStr.length() < 8) {
            time_t rawtime = getCurrentTime();
            struct tm* tinfo = localtime(&rawtime);
            char dateBuf[32];
            sprintf(dateBuf, "%04d-%02d-%02d", tinfo->tm_year + 1900, tinfo->tm_mon + 1, tinfo->tm_mday);
            dateStr = String(dateBuf);
          }

          bool alreadyExists = false;
          if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
          if (SD_MMC.exists("/quote/quotes.json")) {
            File fCheck = SD_MMC.open("/quote/quotes.json", FILE_READ);
            if (fCheck) {
              size_t fSize = fCheck.size();
              size_t readStart = (fSize > 512) ? (fSize - 512) : 0;
              fCheck.seek(readStart);
              String tail = fCheck.readString();
              fCheck.close();
              if (tail.indexOf(dateStr) != -1) {
                alreadyExists = true;
              }
            }
          }

          if (!alreadyExists) {
            File fAppend = SD_MMC.open("/quote/quotes.json", FILE_APPEND);
            if (fAppend) {
              JsonDocument entryDoc;
              entryDoc["date"] = dateStr;
              entryDoc["content"] = quoteEng;
              entryDoc["note"] = quoteChn;
              String line;
              serializeJson(entryDoc, line);
              fAppend.println(line);
              fAppend.close();
              Serial.printf(">>> 每日名言已成功追加归档至单文件 /quote/quotes.json (日期: %s)\n", dateStr.c_str());
            }
          }
          if (sdMutex) xSemaphoreGive(sdMutex);
        }
      }
      http.end();
    }
  }

  if (!originalWifiState)
    toggleWifi(false);

  return syncSuccess;
}

// ----------------------------------------------------
// NVS 配置持久化读写
// ----------------------------------------------------
void saveSettings() {
  preferences.begin("inkword", false);
  preferences.putString("dict", currentDictName);
  preferences.putInt("widx", currentWordIndex);
  preferences.putBool("rand", randomMode);
  preferences.putInt("intv", wordSwitchIntervalMin);
  preferences.putInt("wclose", wifiAutoCloseMin);
  preferences.putBool("nightSilent", nightSilentMode);
  preferences.end();
  Serial.printf("配置已掉电保存: 词库=%s, 单词序号=%d, 随机=%d, 间隔=%d分, WiFi关闭=%d分, 深夜静默=%d\n",
                currentDictName.c_str(), currentWordIndex, randomMode, wordSwitchIntervalMin, wifiAutoCloseMin, nightSilentMode);
}

void loadSettingsAndDict() {
  preferences.begin("inkword", true);
  String savedDictName = preferences.getString("dict", "");
  int savedWordIdx = preferences.getInt("widx", 0);
  randomMode = preferences.getBool("rand", false);
  wordSwitchIntervalMin = preferences.getInt("intv", 3);
  if (wordSwitchIntervalMin < 1) wordSwitchIntervalMin = 3;
  wifiAutoCloseMin = preferences.getInt("wclose", 6);
  if (wifiAutoCloseMin < 1) wifiAutoCloseMin = 6;
  nightSilentMode = preferences.getBool("nightSilent", true); // 默认开启深夜静默模式
  preferences.end();

  dictFileList.clear();
  File root = SD_MMC.open("/dict");
  if (root) {
    File file = root.openNextFile();
    while (file) {
      if (!file.isDirectory() && String(file.name()).endsWith(".json")) {
        dictFileList.push_back(String(file.name()));
      }
      file = root.openNextFile();
    }
  }

  if (dictFileList.size() == 0) {
    currentDictName = "No Dict Found";
    wordOffsets.clear();
    totalWordCount = 0;
    currentWordIndex = 0;
    definitionPage = 0;
    return;
  }

  int targetIdx = 0;
  if (savedDictName.length() > 0) {
    for (size_t i = 0; i < dictFileList.size(); i++) {
      if (dictFileList[i] == savedDictName) {
        targetIdx = i;
        break;
      }
    }
  }

  currentDictIndex = targetIdx;
  currentDictName = dictFileList[currentDictIndex];
  loadDictionary("/dict/" + currentDictName);
  if (totalWordCount > 0) {
    if (rtc_current_word_index >= 0 && rtc_current_word_index < totalWordCount) {
      currentWordIndex = rtc_current_word_index;
    } else {
      currentWordIndex = savedWordIdx % totalWordCount;
      rtc_current_word_index = currentWordIndex;
    }
    if (randomMode) {
      loadOrGenerateShuffleIndex();
    }
  }
}

// ----------------------------------------------------
// 词库管理与词条扫描
// ----------------------------------------------------
void switchDict() {
  dictFileList.clear();
  File root = SD_MMC.open("/dict");
  if (!root) return;
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory() && String(file.name()).endsWith(".json")) {
      dictFileList.push_back(String(file.name()));
    }
    file = root.openNextFile();
  }

  if (dictFileList.size() == 0) {
    currentDictName = "No Dict Found";
    wordOffsets.clear();
    totalWordCount = 0;
    currentWordIndex = 0;
    definitionPage = 0;
    return;
  }

  currentDictIndex = (currentDictIndex + 1) % dictFileList.size();
  currentDictName = dictFileList[currentDictIndex];
  currentWordIndex = 0;
  rtc_current_word_index = 0;
  definitionPage = 0;
  loadDictionary("/dict/" + currentDictName);
  saveSettings();
}

void loadDictionary(String filename) {
  if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
  wordOffsets.clear();
  totalWordCount = 0;
  File file = SD_MMC.open(filename, FILE_READ);
  if (!file) {
    if (sdMutex) xSemaphoreGive(sdMutex);
    return;
  }

  const size_t bufSize = 1024;
  uint8_t* buffer = (uint8_t*)malloc(bufSize);
  if (!buffer) {
    file.close();
    if (sdMutex) xSemaphoreGive(sdMutex);
    return;
  }

  int braceDepth = 0;
  bool inString = false;
  bool escaped = false;
  uint32_t pos = 0;

  while (file.available()) {
    size_t bytesRead = file.read(buffer, bufSize);
    for (size_t i = 0; i < bytesRead; i++) {
      char c = (char)buffer[i];
      pos++;
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\') {
        escaped = true;
        continue;
      }
      if (c == '"') {
        inString = !inString;
        continue;
      }
      if (!inString) {
        if (c == '{') {
          if (braceDepth == 0) { // 顶级词条对象起始点 (完美兼容 JSONL 与 JSON 数组)
            wordOffsets.push_back(pos - 1);
          }
          braceDepth++;
        } else if (c == '}') {
          braceDepth--;
        }
      }
    }
  }

  free(buffer);
  file.close();
  totalWordCount = wordOffsets.size();
  if (sdMutex) xSemaphoreGive(sdMutex);
  if (randomMode) {
    loadOrGenerateShuffleIndex();
  }
}

// ----------------------------------------------------
// 高可靠持久化硬件真随机 (TRNG) 全循环无重复洗牌算法
// ----------------------------------------------------
void loadOrGenerateShuffleIndex() {
  if (totalWordCount <= 0) {
    shuffleIndex.clear();
    return;
  }

  shuffleIndex.resize(totalWordCount);
  String shuffleFilePath = "/dict/" + currentDictName + ".rnd";

  bool loadedFromSD = false;
  if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
  if (SD_MMC.exists(shuffleFilePath)) {
    File f = SD_MMC.open(shuffleFilePath, FILE_READ);
    if (f && f.size() == (size_t)(totalWordCount * sizeof(uint16_t))) {
      std::vector<uint16_t> temp(totalWordCount);
      size_t readBytes = f.read((uint8_t*)temp.data(), totalWordCount * sizeof(uint16_t));
      f.close();
      if (readBytes == (size_t)(totalWordCount * sizeof(uint16_t))) {
        for (int i = 0; i < totalWordCount; i++) {
          shuffleIndex[i] = temp[i];
        }
        loadedFromSD = true;
        Serial.printf(">>> 成功加载持久化乱序列表: %s (共 %d 词)\n", shuffleFilePath.c_str(), totalWordCount);
      }
    } else if (f) {
      f.close();
    }
  }

  if (!loadedFromSD) {
    // 采用芯片内部硬件真随机数发生器 (TRNG) 执行标准 Fisher-Yates 洗牌算法
    for (int i = 0; i < totalWordCount; i++) {
      shuffleIndex[i] = i;
    }
    for (int i = totalWordCount - 1; i > 0; i--) {
      int r = esp_random() % (i + 1);
      int tmp = shuffleIndex[i];
      shuffleIndex[i] = shuffleIndex[r];
      shuffleIndex[r] = tmp;
    }

    // 持久化保存至 SD 卡，重启/休眠/断电绝不打乱既有播放次序
    File f = SD_MMC.open(shuffleFilePath, FILE_WRITE);
    if (f) {
      std::vector<uint16_t> temp(totalWordCount);
      for (int i = 0; i < totalWordCount; i++) {
        temp[i] = (uint16_t)shuffleIndex[i];
      }
      f.write((const uint8_t*)temp.data(), totalWordCount * sizeof(uint16_t));
      f.close();
      Serial.printf(">>> 已生成全新硬件真随机洗牌列表并持久化: %s (共 %d 词)\n", shuffleFilePath.c_str(), totalWordCount);
    }
  }
  if (sdMutex) xSemaphoreGive(sdMutex);
}

void regenerateAndSaveShuffleIndex() {
  if (totalWordCount <= 0) return;
  shuffleIndex.resize(totalWordCount);
  for (int i = 0; i < totalWordCount; i++) {
    shuffleIndex[i] = i;
  }
  for (int i = totalWordCount - 1; i > 0; i--) {
    int r = esp_random() % (i + 1);
    int tmp = shuffleIndex[i];
    shuffleIndex[i] = shuffleIndex[r];
    shuffleIndex[r] = tmp;
  }

  String shuffleFilePath = "/dict/" + currentDictName + ".rnd";
  if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
  File f = SD_MMC.open(shuffleFilePath, FILE_WRITE);
  if (f) {
    std::vector<uint16_t> temp(totalWordCount);
    for (int i = 0; i < totalWordCount; i++) {
      temp[i] = (uint16_t)shuffleIndex[i];
    }
    f.write((const uint8_t*)temp.data(), totalWordCount * sizeof(uint16_t));
    f.close();
    Serial.printf(">>> 词库 %d 词已完整播放一轮！已重新生成下一轮全新洗牌列表: %s\n", totalWordCount, shuffleFilePath.c_str());
  }
  if (sdMutex) xSemaphoreGive(sdMutex);
}

void generateShuffleIndex() {
  loadOrGenerateShuffleIndex();
}

// ----------------------------------------------------
// 智能释义排版与按行折行引擎 (按 195px 屏幕宽度精准拆行)
// ----------------------------------------------------
std::vector<String> formatDefinitionLines(const WordItem& item) {
  std::vector<String> lines;
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);

  for (size_t d = 0; d < item.definitions.size(); d++) {
    String text = item.definitions[d];
    size_t len = text.length();
    size_t byteIdx = 0;

    while (byteIdx < len) {
      size_t lineStart = byteIdx;
      size_t lastValidByte = byteIdx;

      while (byteIdx < len) {
        uint8_t c = (uint8_t)text[byteIdx];
        size_t charBytes = 1;
        if ((c & 0xE0) == 0xC0) charBytes = 2;
        else if ((c & 0xF0) == 0xE0) charBytes = 3;
        else if ((c & 0xF8) == 0xF0) charBytes = 4;

        if (byteIdx + charBytes > len) charBytes = len - byteIdx;

        String testLine = text.substring(lineStart, byteIdx + charBytes);
        int w = u8g2Fonts.getUTF8Width(testLine.c_str());
        if (w > MAX_DEF_PIXEL_WIDTH && lastValidByte > lineStart) {
          break;
        }

        byteIdx += charBytes;
        lastValidByte = byteIdx;
      }

      if (lastValidByte > lineStart) {
        lines.push_back(text.substring(lineStart, lastValidByte));
        byteIdx = lastValidByte;
      } else {
        byteIdx += 1;
      }
    }
  }
  return lines;
}

WordItem getWordItem(int index) {
  WordItem item;
  item.word = "Error";
  if (index < 0 || index >= totalWordCount || index >= (int)wordOffsets.size())
    return item;
  if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY);
  File file = SD_MMC.open("/dict/" + currentDictName, FILE_READ);
  if (!file) {
    if (sdMutex) xSemaphoreGive(sdMutex);
    return item;
  }
  file.seek(wordOffsets[index]);
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  file.close();
  if (sdMutex) xSemaphoreGive(sdMutex);
  if (!error) {
    JsonObject obj = doc.as<JsonObject>();
    if (!obj["word"].isNull()) {
      item.word = obj["word"].as<String>();
    } else if (!obj["headWord"].isNull()) {
      item.word = obj["headWord"].as<String>();
    } else if (!obj["name"].isNull()) {
      item.word = obj["name"].as<String>();
    } else {
      item.word = "Unknown";
    }

    String pStr = "";
    if (!obj["phonetic1"].isNull()) pStr = obj["phonetic1"].as<String>();
    else if (!obj["phonetic"].isNull()) pStr = obj["phonetic"].as<String>();
    else if (!obj["usphone"].isNull()) pStr = obj["usphone"].as<String>();
    else if (!obj["ukphone"].isNull()) pStr = obj["ukphone"].as<String>();
    else if (!obj["phonetic0"].isNull()) pStr = obj["phonetic0"].as<String>();
    else if (!obj["phone"].isNull()) pStr = obj["phone"].as<String>();
    if (pStr.length() > 0 && pStr != "null") {
      pStr.trim();
      if (!pStr.startsWith("/")) pStr = "/" + pStr;
      if (!pStr.endsWith("/")) pStr = pStr + "/";
      item.phonetic = pStr;
    } else {
      item.phonetic = "";
    }

    item.definitions.clear();
    if (obj["trans"].is<JsonArray>()) {
      JsonArray transArr = obj["trans"].as<JsonArray>();
      for (JsonVariant tVar : transArr) {
        if (tVar.is<JsonObject>()) {
          JsonObject transItem = tVar.as<JsonObject>();
          String pos = transItem["pos"].as<String>();
          String cn = transItem["cn"].as<String>();
          if (pos == "null") pos = "";
          if (cn == "null") cn = "";
          pos.trim(); cn.trim();
          if (pos.length() > 0 && cn.length() > 0) {
            item.definitions.push_back(pos + " " + cn);
          } else if (cn.length() > 0) {
            item.definitions.push_back(cn);
          } else if (pos.length() > 0) {
            item.definitions.push_back(pos);
          }
        } else if (tVar.is<const char*>() || tVar.is<String>()) {
          String s = tVar.as<String>();
          if (s != "null" && s.length() > 0) {
            item.definitions.push_back(s);
          }
        }
      }
    } else if (obj["translation"].is<JsonArray>()) {
      JsonArray transArr = obj["translation"].as<JsonArray>();
      for (JsonVariant v : transArr) {
        String s = v.as<String>();
        if (s != "null" && s.length() > 0) item.definitions.push_back(s);
      }
    } else if (!obj["translation"].isNull()) {
      String s = obj["translation"].as<String>();
      if (s != "null" && s.length() > 0) item.definitions.push_back(s);
    } else if (obj["means"].is<JsonArray>()) {
      JsonArray meansArr = obj["means"].as<JsonArray>();
      for (JsonVariant v : meansArr) {
        String s = v.as<String>();
        if (s != "null" && s.length() > 0) item.definitions.push_back(s);
      }
    } else if (!obj["definition"].isNull()) {
      String s = obj["definition"].as<String>();
      if (s != "null" && s.length() > 0) item.definitions.push_back(s);
    }
  }
  return item;
}
// ----------------------------------------------------
// EINK 墨水屏高性能多功能渲染
// ----------------------------------------------------
void renderScreenInternal() {
  Serial.println("=== 开始后台异步绘制墨水屏 (Core 0 三色高对比度) ===");
  display.init(115200, true, 2, false); // 硬件复位并唤醒 UC8176 (initial=true 确保从 hibernate 状态正确复位)
  display.setRotation(0);
  u8g2Fonts.begin(display);
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);

  // 1. 绘制顶端状态栏 (30px)
  display.drawFastHLine(0, 30, 400, GxEPD_BLACK);
  display.setTextSize(1);
  display.setCursor(6, 12);
  if (apConfigMode)
    display.print("AP:InkWord");
  else if (!wifiEnabled)
    display.print("WiFi:Off");
  else if (smartConfigMode)
    display.print("SmartCfg");
  else if (WiFi.status() == WL_CONNECTED)
    display.printf("IP:%s", WiFi.localIP().toString().c_str());
  else
    display.print("WiFi:Disconn");

  // 播放模式图标徽章 (x=160)
  int modeX = 160;
  if (!randomMode) {
    display.drawRoundRect(modeX, 5, 24, 19, 3, GxEPD_BLACK);
    display.drawFastHLine(modeX + 4, 14, 11, GxEPD_BLACK);
    display.drawLine(modeX + 15, 14, modeX + 11, 10, GxEPD_BLACK);
    display.drawLine(modeX + 15, 14, modeX + 11, 18, GxEPD_BLACK);
  } else {
    display.drawRoundRect(modeX, 5, 24, 19, 3, COLOR_RED);
    display.drawLine(modeX + 4, 10, modeX + 9, 10, COLOR_RED);
    display.drawLine(modeX + 9, 10, modeX + 15, 18, COLOR_RED);
    display.drawLine(modeX + 4, 18, modeX + 9, 18, COLOR_RED);
    display.drawLine(modeX + 9, 18, modeX + 15, 10, COLOR_RED);
    display.drawLine(modeX + 15, 10, modeX + 12, 9, COLOR_RED);
    display.drawLine(modeX + 15, 10, modeX + 12, 12, COLOR_RED);
    display.drawLine(modeX + 15, 18, modeX + 12, 16, COLOR_RED);
    display.drawLine(modeX + 15, 18, modeX + 12, 19, COLOR_RED);
  }

  // 词库名显示 (x=200)
  u8g2Fonts.setFont(u8g2_font_wqy14_t_gb2312);
  u8g2Fonts.setCursor(200, 20);
  String cleanDictName = currentDictName;
  if (cleanDictName.endsWith(".json")) {
    cleanDictName = cleanDictName.substring(0, cleanDictName.length() - 5);
  }
  u8g2Fonts.print(cleanDictName);

  // 电池图标 (保留精美电池外框 + 内部纯白底显示，极低电量显示警告符号[!])
  isCharging = (digitalRead(PIN_CHRG) == LOW);
  bool isLow = (batteryPercent <= 20.0);
  bool isCritical = (batteryPercent <= 5.0 && !isCharging);
  uint16_t themeColor = (isLow || isCritical) ? COLOR_RED : GxEPD_BLACK;

  int batX = 354, batY = 6;
  int batW = 40, batH = 18;

  display.fillRect(batX - 3, batY + 5, 3, 8, themeColor);
  display.drawRoundRect(batX, batY, batW, batH, 3, themeColor);

  int pct = (int)batteryPercent;
  if (pct < 0) pct = 0;
  if (pct > 100) pct = 100;
  String numStr = String(pct);

  u8g2Fonts.setFont(u8g2_font_wqy14_t_gb2312);

  if (isCharging) {
    int lx = batX + 4, ly = batY + 4;
    display.drawLine(lx + 4, ly,     lx + 1, ly + 5, COLOR_RED);
    display.drawLine(lx + 1, ly + 5, lx + 4, ly + 5, COLOR_RED);
    display.drawLine(lx + 4, ly + 5, lx + 2, ly + 10, COLOR_RED);
    display.drawLine(lx + 5, ly,     lx + 2, ly + 5, COLOR_RED);
    display.drawLine(lx + 2, ly + 5, lx + 5, ly + 5, COLOR_RED);
    display.drawLine(lx + 5, ly + 5, lx + 3, ly + 10, COLOR_RED);

    u8g2Fonts.setForegroundColor(COLOR_RED);
    u8g2Fonts.setCursor(batX + 13, batY + 14);
    u8g2Fonts.print(numStr);
  } else if (isCritical) {
    // 极低电量：在电池框内绘制红色警告感叹号 [!] 并紧跟电量数字
    // 感叹号竖条 (宽3高6):
    display.fillRect(batX + 6, batY + 4, 3, 6, COLOR_RED);
    // 感叹号点 (宽3高2):
    display.fillRect(batX + 6, batY + 12, 3, 2, COLOR_RED);
    
    u8g2Fonts.setForegroundColor(COLOR_RED);
    u8g2Fonts.setCursor(batX + 13, batY + 14);
    u8g2Fonts.print(numStr);
  } else {
    u8g2Fonts.setForegroundColor(themeColor);
    int textW = u8g2Fonts.getUTF8Width(numStr.c_str());
    int textX = batX + (batW - textW) / 2;
    u8g2Fonts.setCursor(textX, batY + 14);
    u8g2Fonts.print(numStr);
  }
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);

  // 2. 绘制分割线（左时间区，右单词区，起点为 30px）
  display.drawFastVLine(180, 30, 190, GxEPD_BLACK);

  // 3. 渲染左侧时间与农历 (采用连续高精度时钟引擎 getCurrentTime，绝不发生重置)
  time_t rawtime = getCurrentTime();
  struct tm todayInfo;
  struct tm *tPtr = localtime(&rawtime);
  if (tPtr) todayInfo = *tPtr;
  else memset(&todayInfo, 0, sizeof(todayInfo));
  struct tm *timeinfo = &todayInfo;

  String timeStr = "12:00";
  String dateStr = "2026-05-31";
  String weekStr = "周日";
  String lunarMonth = "正月";
  String lunarDay = "初一";

  if (timeinfo->tm_year > 100) {
    char buf[32];
    sprintf(buf, "%02d:%02d", timeinfo->tm_hour, timeinfo->tm_min);
    timeStr = String(buf);
    sprintf(buf, "%04d-%02d-%02d", timeinfo->tm_year + 1900,
            timeinfo->tm_mon + 1, timeinfo->tm_mday);
    dateStr = String(buf);
    const char *weeks[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    weekStr = String(weeks[timeinfo->tm_wday]);
    solarToLunar(timeinfo->tm_year + 1900, timeinfo->tm_mon + 1,
                 timeinfo->tm_mday, lunarMonth, lunarDay);
  }

  // 绘制大时钟
  u8g2Fonts.setFont(u8g2_font_logisoso28_tf);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  int16_t timeW = u8g2Fonts.getUTF8Width(timeStr.c_str());
  u8g2Fonts.setCursor((180 - timeW) / 2, 72);
  u8g2Fonts.print(timeStr);

  // 绘制公历日期与星期
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  String dateWeekStr = dateStr + " " + weekStr;
  int16_t dateW = u8g2Fonts.getUTF8Width(dateWeekStr.c_str());
  u8g2Fonts.setCursor((180 - dateW) / 2, 92);
  u8g2Fonts.print(dateWeekStr);

  // 绘制农历 (红色高亮)
  u8g2Fonts.setForegroundColor(COLOR_RED);
  String lunarStr = lunarMonth + lunarDay;
  int16_t lunarW = u8g2Fonts.getUTF8Width(lunarStr.c_str());
  u8g2Fonts.setCursor((180 - lunarW) / 2, 116);
  u8g2Fonts.print(lunarStr);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);

  // 绘制周历（自适应日程提醒：有日程时取消第3排日期显示日程提醒，无日程显示3周21天）
  u8g2Fonts.setFont(u8g2_font_wqy14_t_gb2312);
  const char* dayHeaders[] = {"一", "二", "三", "四", "五", "六", "日"};
  for (int col = 0; col < 7; col++) {
    int x = 10 + col * 23;
    u8g2Fonts.setCursor(x + 2, 145);
    u8g2Fonts.print(dayHeaders[col]);
  }

  display.drawFastHLine(10, 151, 160, GxEPD_BLACK);
  int currentWday = timeinfo->tm_wday;
  int wdayMonBased = (currentWday == 0) ? 6 : (currentWday - 1);
  time_t mondayTime = rawtime - wdayMonBased * 86400;
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);

  // 仅检查今日是否存在日程提醒 (精准当天提醒，不提前触发)
  String todayScheduleText = "";
  if (scheduleMap.find(dateStr) != scheduleMap.end()) {
    todayScheduleText = scheduleMap[dateStr];
  }

  bool hasSchedule = (todayScheduleText.length() > 0);
  int maxRows = hasSchedule ? 2 : 3; // 今日有日程只画前2排 (14天)，第3排位置让给日程文本

  // 日历起始周计算：
  // 1. 无日程 (3周显示): 上周 (第0排)、当前周/包含今天 (第1排居中周)、下周 (第2排)
  // 2. 有日程 (2周显示 + 1排日程): 当前周/包含今天 (第0排)、下周 (第1排)、第2排为日程框
  time_t startMonday = hasSchedule ? mondayTime : (mondayTime - 7 * 86400);

  for (int row = 0; row < maxRows; row++) {
    for (int col = 0; col < 7; col++) {
      int dayIdx = row * 7 + col;
      time_t dayTime = startMonday + dayIdx * 86400;
      struct tm *dayInfo = localtime(&dayTime);
      int dayNum = dayInfo ? dayInfo->tm_mday : 1;
      bool isToday = (timeinfo->tm_year > 100 && dayInfo &&
                      dayInfo->tm_year == timeinfo->tm_year &&
                      dayInfo->tm_mon == timeinfo->tm_mon &&
                      dayInfo->tm_mday == timeinfo->tm_mday);
      int x = 12 + col * 23;
      int y = 168 + row * 20;
      if (isToday) {
        display.fillCircle(x + 7, y - 5, 8, COLOR_RED);
        u8g2Fonts.setForegroundColor(GxEPD_WHITE);
        u8g2Fonts.setBackgroundColor(COLOR_RED);
        u8g2Fonts.setCursor(x + (dayNum < 10 ? 4 : 1), y);
        u8g2Fonts.printf("%d", dayNum);
        u8g2Fonts.setForegroundColor(GxEPD_BLACK);
        u8g2Fonts.setBackgroundColor(GxEPD_WHITE);
      } else {
        u8g2Fonts.setCursor(x + (dayNum < 10 ? 3 : 0), y);
        u8g2Fonts.printf("%d", dayNum);
      }
    }
  }

  // 若今日有日程：在第 3 排区域绘制红色圆角框，优雅居中展示日程纯文本内容 (无需额外前缀)
  if (hasSchedule) {
    display.drawRoundRect(10, 195, 160, 19, 3, COLOR_RED);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(COLOR_RED);
    int16_t schW = u8g2Fonts.getUTF8Width(todayScheduleText.c_str());
    int schX = (160 - schW) / 2 + 10;
    if (schX < 14) schX = 14;
    u8g2Fonts.setCursor(schX, 209);
    u8g2Fonts.print(todayScheduleText);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  }

  // 4. 渲染右侧单词释义区 (紧凑排版，最大化利用空间)
  if (totalWordCount > 0) {
    int realIdx = currentWordIndex;
    if (randomMode) {
      if (shuffleIndex.size() != (size_t)totalWordCount) {
        loadOrGenerateShuffleIndex();
      }
      if (currentWordIndex >= 0 && currentWordIndex < (int)shuffleIndex.size()) {
        realIdx = shuffleIndex[currentWordIndex];
      }
    }
    WordItem item = getWordItem(realIdx);

    // 单词显示 (单行自适应缩放，长词不换行自动平滑缩小字号)
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    u8g2Fonts.setFont(u8g2_font_logisoso24_tf);
    int wordW = u8g2Fonts.getUTF8Width(item.word.c_str());

    if (wordW <= 200) {
      u8g2Fonts.setCursor(195, 58);
      u8g2Fonts.print(item.word);
    } else {
      u8g2Fonts.setFont(u8g2_font_logisoso18_tf);
      wordW = u8g2Fonts.getUTF8Width(item.word.c_str());
      if (wordW <= 200) {
        u8g2Fonts.setCursor(195, 56);
        u8g2Fonts.print(item.word);
      } else {
        u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
        wordW = u8g2Fonts.getUTF8Width(item.word.c_str());
        if (wordW <= 200) {
          u8g2Fonts.setCursor(195, 56);
          u8g2Fonts.print(item.word);
        } else {
          u8g2Fonts.setFont(u8g2_font_wqy14_t_gb2312);
          u8g2Fonts.setCursor(195, 56);
          u8g2Fonts.print(item.word);
        }
      }
    }

    // 音标 (y=78px, 红色，使用真 IPA Unicode 矢量点阵渲染)
    if (item.phonetic.length() > 0) {
      drawPhoneticString(195, 78, item.phonetic);
    }

    // 释义展示 (使用智能排版引擎，按真实屏幕行数精准分页)
    std::vector<String> lines = formatDefinitionLines(item);
    int totalLines = lines.size();
    int maxPages = (totalLines + LINES_PER_PAGE - 1) / LINES_PER_PAGE;
    if (maxPages < 1) maxPages = 1;
    if (definitionPage >= maxPages) definitionPage = 0;

    // 如果总行数超过 7 行，在单词框右下角绘制精美页码标识徽章 [1/3]
    if (maxPages > 1) {
      int badgeX = 350, badgeY = 202;
      display.drawRoundRect(badgeX, badgeY, 44, 15, 3, COLOR_RED);
      u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
      u8g2Fonts.setForegroundColor(COLOR_RED);
      u8g2Fonts.setCursor(badgeX + 8, badgeY + 12);
      u8g2Fonts.printf("%d/%d", definitionPage + 1, maxPages);
      u8g2Fonts.setForegroundColor(GxEPD_BLACK);
    }

    // 绘制当前页的 7 行释义 (绝不截断、字迹清晰、行间距舒适)
    int startY = 96;
    int startLineIdx = definitionPage * LINES_PER_PAGE;
    int endLineIdx = min(startLineIdx + LINES_PER_PAGE, totalLines);
    u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
    u8g2Fonts.setForegroundColor(GxEPD_BLACK);

    for (int i = startLineIdx; i < endLineIdx; i++) {
      u8g2Fonts.setCursor(195, startY);
      u8g2Fonts.print(lines[i]);
      startY += 17;
    }
  } else {
    switchDict();
    if (totalWordCount > 0) {
      drawScreen("recursive scan");
      return;
    }
    u8g2Fonts.setFont(u8g2_font_wqy14_t_gb2312);
    u8g2Fonts.setCursor(200, 100);
    u8g2Fonts.print("Please upload JSON vocabulary");
    u8g2Fonts.setCursor(200, 125);
    u8g2Fonts.print("into /dict directory");
    u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  }

  // 5. 绘制底部名言区分隔线
  display.drawFastHLine(0, 220, 400, GxEPD_BLACK);

  // 渲染每日名言 (英文：wqy16 大字红色，按像素宽度自动换行)
  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  u8g2Fonts.setForegroundColor(COLOR_RED);
  int maxWidth = 380;
  int quoteY = 238;

  String remaining = quoteEng;
  int quoteLines = 0;
  while (remaining.length() > 0 && quoteLines < 2) {
    String testLine = "";
    String bestLine = "";
    int wordStart = 0;
    bool done = false;
    for (int j = 0; j <= (int)remaining.length(); j++) {
      if (j == (int)remaining.length() || remaining[j] == ' ') {
        testLine = remaining.substring(0, j);
        int16_t tw = u8g2Fonts.getUTF8Width(testLine.c_str());
        if (tw > maxWidth && bestLine.length() > 0) {
          done = true;
          break;
        }
        bestLine = testLine;
        wordStart = j + 1;
      }
    }

    if (!done) bestLine = remaining;
    u8g2Fonts.setCursor(10, quoteY);
    u8g2Fonts.print(bestLine);
    quoteY += 20;
    quoteLines++;
    if (bestLine.length() >= remaining.length()) break;
    remaining = remaining.substring(bestLine.length() + 1);
  }

  // 渲染名言中文翻译 (wqy12 小字黑色)
  u8g2Fonts.setFont(u8g2_font_wqy12_t_gb2312);
  u8g2Fonts.setForegroundColor(GxEPD_BLACK);
  u8g2Fonts.setCursor(10, quoteY + 4);
  u8g2Fonts.print(quoteChn);

  u8g2Fonts.setFont(u8g2_font_wqy16_t_gb2312);
  display.display(false); // 执行 15 秒三色深度物理全刷
  display.hibernate();    // 芯片断电休眠 (< 1uA)
}
