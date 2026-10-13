#ifndef LUNAR_CALENDAR_H
#define LUNAR_CALENDAR_H

#include <Arduino.h>

// 1900-2100 年权威农历天文编码表 (1900-2100 共201年)
static const uint32_t LUNAR_INFO[] = {
    0x04bd8, 0x04ae0, 0x0a570, 0x054d5, 0x0d260, 0x0d950, 0x16554, 0x056a0, 0x09ad0, 0x055d2, // 1900-1909
    0x04ae0, 0x0a5b6, 0x0a4d0, 0x0d250, 0x1d255, 0x0b540, 0x0d6a0, 0x0ada2, 0x095b0, 0x14977, // 1910-1919
    0x04970, 0x0a4b0, 0x0b4b5, 0x06a50, 0x06d40, 0x1ab54, 0x02b60, 0x09570, 0x052f2, 0x04970, // 1920-1929
    0x06566, 0x0d4a0, 0x0ea50, 0x16a95, 0x05ad0, 0x02b60, 0x186e3, 0x092e0, 0x1c8d7, 0x0c950, // 1930-1939
    0x0d4a0, 0x1d8a6, 0x0b550, 0x056a0, 0x1a5b4, 0x025d0, 0x092d0, 0x0d2b2, 0x0a950, 0x0b557, // 1940-1949
    0x06ca0, 0x0b550, 0x15355, 0x04da0, 0x0a5b0, 0x14573, 0x052b0, 0x0a9a8, 0x0e950, 0x06aa0, // 1950-1959
    0x0aea6, 0x0ab50, 0x04b60, 0x0aae4, 0x0a570, 0x05260, 0x0f263, 0x0d950, 0x05b57, 0x056a0, // 1960-1969
    0x096d0, 0x04dd5, 0x04ad0, 0x0a4d0, 0x0d4d4, 0x0d250, 0x0d558, 0x0b540, 0x0b6a0, 0x195a6, // 1970-1979
    0x095b0, 0x049b0, 0x0a974, 0x0a4b0, 0x0b27a, 0x06a50, 0x06d40, 0x0af46, 0x0ab60, 0x09570, // 1980-1989
    0x04af5, 0x04970, 0x064b0, 0x074a3, 0x0ea50, 0x06b58, 0x05ac0, 0x0ab60, 0x096d5, 0x092e0, // 1990-1999
    0x0c960, 0x0d954, 0x0d4a0, 0x0da50, 0x07552, 0x056a0, 0x0abb7, 0x025d0, 0x092d0, 0x0cab5, // 2000-2009
    0x0a950, 0x0b4a0, 0x0baa4, 0x0ad50, 0x055d9, 0x04ba0, 0x0a5b0, 0x15176, 0x052b0, 0x0a930, // 2010-2019
    0x07954, 0x06aa0, 0x0ad50, 0x05b52, 0x04b60, 0x0a6e6, 0x0a4e0, 0x0d260, 0x0ea65, 0x0d530, // 2020-2029
    0x05aa0, 0x076a3, 0x096d0, 0x04afb, 0x04ad0, 0x0a4d0, 0x1d0b6, 0x0d250, 0x0d520, 0x0dd45, // 2030-2039
    0x0b5a0, 0x056d0, 0x055b2, 0x049b0, 0x0a577, 0x0a4b0, 0x0aa50, 0x1b255, 0x06d20, 0x0ada0, // 2040-2049
    0x14b63, 0x09370, 0x049f8, 0x04970, 0x064b0, 0x168a6, 0x0ea50, 0x06aa0, 0x1a6c4, 0x0aae0, // 2050-2059
    0x092e0, 0x0d2e3, 0x0c960, 0x0d557, 0x0d4a0, 0x0da50, 0x05d55, 0x056a0, 0x0a6d0, 0x055d4, // 2060-2069
    0x052d0, 0x0a9b8, 0x0a950, 0x0b4a0, 0x0b6a6, 0x0ad50, 0x055a0, 0x0aba4, 0x0a5b0, 0x052b0, // 2070-2079
    0x0b273, 0x06930, 0x07337, 0x06aa0, 0x0ad50, 0x14b55, 0x04b60, 0x0a570, 0x054e4, 0x0d160, // 2080-2089
    0x0e968, 0x0d520, 0x0daa0, 0x16aa6, 0x056d0, 0x04ae0, 0x0a9d4, 0x0a2d0, 0x0d150, 0x0f252, // 2090-2099
    0x0d520  // 2100
};

// 获取闰月月份，1-12，若无则返回 0
static inline int getLeapMonth(int y) {
    if (y < 1900 || y > 2100) return 0;
    return LUNAR_INFO[y - 1900] & 0xf;
}

// 获取闰月的天数，若无闰月则返回 0
static inline int getLeapDays(int y) {
    if (getLeapMonth(y)) {
        return (LUNAR_INFO[y - 1900] & 0x10000) ? 30 : 29;
    }
    return 0;
}

// 获取非闰月指定月份的天数，m范围 1-12
static inline int getMonthDays(int y, int m) {
    if (y < 1900 || y > 2100 || m < 1 || m > 12) return 0;
    return (LUNAR_INFO[y - 1900] & (0x10000 >> m)) ? 30 : 29;
}

// 获取农历一整年的总天数（包含闰月）
static inline int getYearDays(int y) {
    if (y < 1900 || y > 2100) return 0;
    int sum = 348;
    for (int i = 0x8000; i > 0x8; i >>= 1) {
        sum += (LUNAR_INFO[y - 1900] & i) ? 1 : 0;
    }
    return sum + getLeapDays(y);
}

// 判断公历年份是否为闰年
static inline bool isLeapYear(int year) {
    return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

/**
 * @brief 公历日期转农历日期字符
 * @param year 公历年 (1900-2100)
 * @param month 公历月 (1-12)
 * @param day 公历日 (1-31)
 * @param lunarMonthStr 转换后的农历月引用（如"正月"、"闰四月"）
 * @param lunarDayStr 转换后的农历日引用（如"初一"、"廿八"）
 * @return 转换成功返回 true，否则返回 false
 */
static inline bool solarToLunar(int year, int month, int day, String &lunarMonthStr, String &lunarDayStr) {
    if (year < 1900 || year > 2100) return false;

    // 1. 计算目标公历日期自 1900-01-31 (基准农历正月初一) 的天数偏移量
    int offset = 0;
    for (int i = 1900; i < year; i++) {
        offset += isLeapYear(i) ? 366 : 365;
    }
    const int monthDaysList[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for (int i = 0; i < month - 1; i++) {
        if (i == 1 && isLeapYear(year)) {
            offset += 29;
        } else {
            offset += monthDaysList[i];
        }
    }
    offset += day;
    offset -= 31; // 1900-01-31 为基准日 0

    // 2. 依次扣减农历年份的天数，锁定目标年份
    int lYear = 1900;
    int daysInYear = 0;
    for (lYear = 1900; lYear <= 2100; lYear++) {
        daysInYear = getYearDays(lYear);
        if (offset < daysInYear) {
            break;
        }
        offset -= daysInYear;
    }

    // 3. 依次扣减月份天数（考虑闰月），锁定月份与日
    int leap = getLeapMonth(lYear);
    bool isLeapMonthFlag = false;
    int lMonth = 1;
    
    for (lMonth = 1; lMonth <= 12; lMonth++) {
        // 正常农历月
        int daysInMonth = getMonthDays(lYear, lMonth);
        if (offset < daysInMonth) {
            break;
        }
        offset -= daysInMonth;
        
        // 若当前月是闰月对应的正常月，紧接着扣减闰月天数
        if (lMonth == leap) {
            daysInMonth = getLeapDays(lYear);
            if (offset < daysInMonth) {
                isLeapMonthFlag = true;
                break;
            }
            offset -= daysInMonth;
        }
    }

    // 农历日 (从1开始计数)
    int lDay = offset + 1;
    int maxDays = isLeapMonthFlag ? getLeapDays(lYear) : getMonthDays(lYear, lMonth);
    if (maxDays > 0 && lDay > maxDays) lDay = maxDays;
    if (lDay < 1) lDay = 1;

    // 4. 映射为中文中文字符
    static const String monthsName[] = {
        "正", "二", "三", "四", "五", "六", "七", "八", "九", "十", "十一", "腊"
    };
    static const String daysName[] = {
        "初一", "初二", "初三", "初四", "初五", "初六", "初七", "初八", "初九", "初十",
        "十一", "十二", "十三", "十四", "十五", "十六", "十七", "十八", "十九", "二十",
        "廿一", "廿二", "廿三", "廿四", "廿五", "廿六", "廿七", "廿八", "廿九", "三十"
    };

    lunarMonthStr = (isLeapMonthFlag ? "闰" : "") + monthsName[lMonth - 1] + "月";
    if (lDay >= 1 && lDay <= 30) {
        lunarDayStr = daysName[lDay - 1];
    } else {
        lunarDayStr = "初一"; // 安全兜底
    }

    return true;
}

#endif // LUNAR_CALENDAR_H
