#ifndef WEB_MANAGER_H
#define WEB_MANAGER_H

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <FS.h>
#include <SD_MMC.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <vector>
#include <map>

// 静态网页模版 (保存于 Flash PROGMEM)
static const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>InkWordESP32 | 控制中心与全盘文件管理</title>
    <style>
        :root {
            --bg-color: #0b0f19;
            --glass-bg: rgba(255, 255, 255, 0.04);
            --glass-border: rgba(255, 255, 255, 0.08);
            --primary-color: #ff3b30;
            --primary-glow: rgba(255, 59, 48, 0.4);
            --blue-color: #007aff;
            --green-color: #34c759;
            --yellow-color: #ff9f0a;
            --text-color: #f5f5f7;
            --text-secondary: #86868b;
            --card-radius: 16px;
        }

        * {
            box-sizing: border-box;
            margin: 0;
            padding: 0;
            font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
            -webkit-font-smoothing: antialiased;
        }

        body {
            background-color: var(--bg-color);
            background-image: 
                radial-gradient(circle at 10% 20%, rgba(255, 59, 48, 0.08) 0%, transparent 40%),
                radial-gradient(circle at 90% 80%, rgba(0, 122, 255, 0.08) 0%, transparent 40%);
            color: var(--text-color);
            min-height: 100vh;
            display: flex;
            flex-direction: column;
            align-items: center;
            padding: 30px 16px;
        }

        .container {
            width: 100%;
            max-width: 960px;
            display: grid;
            grid-template-columns: 1fr;
            gap: 20px;
        }

        header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 8px 0 16px 0;
            border-bottom: 1px solid var(--glass-border);
        }

        h1 {
            font-size: 22px;
            font-weight: 700;
            background: linear-gradient(135deg, #fff 0%, #a1a1a6 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }

        .subtitle {
            font-size: 13px;
            color: var(--text-secondary);
            margin-top: 2px;
        }

        .ip-badge {
            background: var(--glass-bg);
            border: 1px solid var(--glass-border);
            padding: 6px 14px;
            border-radius: 20px;
            font-size: 12px;
            font-family: monospace;
            color: #34c759;
        }

        .card {
            background: var(--glass-bg);
            border: 1px solid var(--glass-border);
            border-radius: var(--card-radius);
            padding: 22px;
            backdrop-filter: blur(20px);
            -webkit-backdrop-filter: blur(20px);
            box-shadow: 0 8px 32px 0 rgba(0, 0, 0, 0.3);
        }

        .card-title {
            font-size: 17px;
            font-weight: 600;
            margin-bottom: 16px;
            display: flex;
            justify-content: space-between;
            align-items: center;
            color: #fff;
        }

        .form-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 16px;
            margin-bottom: 16px;
        }

        .form-group {
            display: flex;
            flex-direction: column;
            gap: 6px;
        }

        label {
            font-size: 13px;
            color: var(--text-secondary);
            font-weight: 500;
        }

        select, input[type="text"], input[type="date"], input[type="number"] {
            background: rgba(255, 255, 255, 0.05);
            border: 1px solid var(--glass-border);
            border-radius: 8px;
            padding: 10px 12px;
            color: #fff;
            font-size: 14px;
            outline: none;
            transition: border-color 0.2s;
        }

        select option {
            background: #1c1c1e;
            color: #fff;
        }

        select:focus, input:focus {
            border-color: var(--primary-color);
        }

        .btn {
            background: var(--primary-color);
            color: #fff;
            border: none;
            border-radius: 8px;
            padding: 9px 16px;
            font-size: 13px;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.2s;
            display: inline-flex;
            align-items: center;
            justify-content: center;
            gap: 6px;
        }

        .btn:hover {
            opacity: 0.9;
            transform: translateY(-1px);
        }

        .btn-blue { background: var(--blue-color); }
        .btn-green { background: var(--green-color); }
        .btn-yellow { background: var(--yellow-color); color: #000; }

        .btn-sm {
            padding: 4px 8px;
            font-size: 12px;
            border-radius: 6px;
        }

        .btn-danger {
            background: rgba(255, 59, 48, 0.15);
            color: #ff3b30;
            border: 1px solid rgba(255, 59, 48, 0.3);
        }

        .btn-danger:hover {
            background: #ff3b30;
            color: #fff;
        }

        .schedule-list {
            width: 100%;
            border-collapse: collapse;
            margin-top: 12px;
        }

        .schedule-list th, .schedule-list td {
            padding: 10px 12px;
            text-align: left;
            border-bottom: 1px solid var(--glass-border);
            font-size: 13px;
        }

        .schedule-list th {
            color: var(--text-secondary);
            font-weight: 500;
        }

        .badge-date {
            background: rgba(255, 59, 48, 0.15);
            color: #ff453a;
            padding: 3px 8px;
            border-radius: 6px;
            font-family: monospace;
            font-size: 12px;
        }

        .storage-info {
            display: flex;
            justify-content: space-between;
            margin-bottom: 8px;
            font-size: 13px;
            color: var(--text-secondary);
        }

        .progress-bar {
            width: 100%;
            height: 6px;
            background: rgba(255, 255, 255, 0.05);
            border-radius: 3px;
            overflow: hidden;
            margin-bottom: 16px;
        }

        .progress-fill {
            height: 100%;
            width: 0%;
            background: linear-gradient(90deg, var(--primary-color) 0%, #ff453a 100%);
            transition: width 0.3s ease;
        }

        .path-bar {
            display: flex;
            align-items: center;
            justify-content: space-between;
            background: rgba(255, 255, 255, 0.03);
            border: 1px solid var(--glass-border);
            border-radius: 8px;
            padding: 8px 12px;
            margin-bottom: 12px;
            font-family: monospace;
            font-size: 13px;
        }

        .path-display {
            color: #34c759;
            font-weight: 600;
            overflow: hidden;
            text-overflow: ellipsis;
            white-space: nowrap;
        }

        .file-table {
            width: 100%;
            border-collapse: collapse;
        }

        .file-table th, .file-table td {
            padding: 10px 12px;
            text-align: left;
            border-bottom: 1px solid var(--glass-border);
            font-size: 13px;
        }

        .file-table th {
            color: var(--text-secondary);
        }

        .row-folder {
            cursor: pointer;
            transition: background 0.15s;
        }
        .row-folder:hover td {
            background: rgba(255, 255, 255, 0.04);
            color: #007aff;
        }

        .upload-zone {
            border: 2px dashed var(--glass-border);
            border-radius: var(--card-radius);
            padding: 20px;
            text-align: center;
            cursor: pointer;
            transition: all 0.2s ease;
            background: rgba(255, 255, 255, 0.01);
            margin-bottom: 14px;
        }

        .upload-zone:hover, .upload-zone.dragover {
            border-color: var(--primary-color);
            background: rgba(255, 59, 48, 0.08);
            transform: scale(1.01);
        }

        .status-msg {
            margin-top: 8px;
            font-size: 13px;
            color: var(--text-secondary);
        }
        .status-msg.success { color: var(--green-color); }
        .status-msg.error { color: var(--primary-color); }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <div>
                <h1>InkWordESP32 控制中心</h1>
                <div class="subtitle">三色水墨屏单词卡 • 播放设置 & 日历日程 & 全盘文件管理</div>
            </div>
            <div class="ip-badge" id="ip-display">Connected</div>
        </header>

        <!-- 0. Wi-Fi 网络配置与连接卡片 -->
        <div class="card" id="wifi-config-card">
            <div class="card-title">
                <span>📶 Wi-Fi 网络连接与配置</span>
                <button class="btn btn-sm btn-blue" onclick="scanWifi()">🔍 扫描周边 Wi-Fi</button>
            </div>
            <div class="form-grid" style="grid-template-columns: 1fr 1fr auto; align-items:flex-end;">
                <div class="form-group">
                    <label for="wifi-ssid-input">Wi-Fi 名称 (SSID)</label>
                    <input type="text" id="wifi-ssid-input" placeholder="输入名称或点击下方下拉选择">
                    <select id="wifi-ssid-select" style="display:none; margin-top:6px;" onchange="onSsidSelect(this.value)">
                        <option value="">-- 选择扫描到的网络 --</option>
                    </select>
                </div>
                <div class="form-group">
                    <label for="wifi-pass-input">Wi-Fi 密码</label>
                    <input type="password" id="wifi-pass-input" placeholder="无密码请留空">
                </div>
                <div>
                    <button class="btn btn-green" onclick="connectWifiSubmit()">🚀 连接并保存</button>
                </div>
            </div>
            <div id="wifi-status" class="status-msg"></div>
        </div>

        <!-- 1. 单词播放与系统设置卡片 -->
        <div class="card">
            <div class="card-title">
                <span>📖 单词播放与系统设置</span>
            </div>
            <div class="form-grid">
                <div class="form-group">
                    <label for="dict-select">当前学习词库 (自动过滤 /dict/*.json)</label>
                    <select id="dict-select"></select>
                </div>
                <div class="form-group">
                    <label for="mode-select">播放模式</label>
                    <select id="mode-select">
                        <option value="0">➔ 顺序播放 (按词库顺序)</option>
                        <option value="1">🔀 随机乱序 (Shuffle 洗牌)</option>
                    </select>
                </div>
                <div class="form-group">
                    <label for="interval-input">单词切换间隔 (分钟)</label>
                    <input type="number" id="interval-input" min="1" max="120" value="3">
                </div>
                <div class="form-group">
                    <label for="wifi-close-input">Wi-Fi 自动关闭时间 (分钟)</label>
                    <input type="number" id="wifi-close-input" min="1" max="60" value="6">
                </div>
                <div class="form-group" style="grid-column: 1 / -1;">
                    <label for="night-silent-select">🌙 深夜静默节能模式 (23:00 ~ 07:00 停止自动刷屏切词)</label>
                    <select id="night-silent-select">
                        <option value="1">🌙 开启深夜静默 (23点至早7点不刷屏，极大压榨续航)</option>
                        <option value="0">☀️ 关闭深夜静默 (24小时全天按设定间隔持续切词)</option>
                    </select>
                </div>
            </div>
            <div style="display:flex; justify-content:space-between; align-items:center;">
                <button class="btn btn-blue" onclick="saveSettings()">💾 保存并立即刷新屏幕</button>
                <span id="settings-status" class="status-msg"></span>
            </div>
        </div>

        <!-- 2. 日历日程与备忘提醒卡片 -->
        <div class="card">
            <div class="card-title">
                <span>📅 日历日程与备忘提醒</span>
            </div>
            <div class="form-grid" style="grid-template-columns: 180px 1fr auto; align-items:flex-end;">
                <div class="form-group">
                    <label for="schedule-date">选择日期</label>
                    <input type="date" id="schedule-date">
                </div>
                <div class="form-group">
                    <label for="schedule-text">提醒内容 (<span id="char-count">0</span>/12字，在日历底部自适应展示)</label>
                    <input type="text" id="schedule-text" maxlength="12" placeholder="例：下午3点项目会议" oninput="updateCharCount()">
                </div>
                <div>
                    <button class="btn" onclick="addSchedule()">➕ 添加日程</button>
                </div>
            </div>

            <table class="schedule-list">
                <thead>
                    <tr>
                        <th style="width: 140px;">日期</th>
                        <th>提醒内容</th>
                        <th style="width: 80px; text-align:right;">操作</th>
                    </tr>
                </thead>
                <tbody id="schedule-tbody">
                    <tr><td colspan="3" style="text-align:center; color:var(--text-secondary);">加载中...</td></tr>
                </tbody>
            </table>
            <div id="schedule-status" class="status-msg"></div>
        </div>

        <!-- 3. TF 卡全盘文件管理卡片 -->
        <div class="card">
            <div class="card-title">
                <span>📁 TF 卡全盘文件管理器</span>
                <div style="display:flex; gap:8px;">
                    <button class="btn btn-sm btn-yellow" onclick="createNewFolder()">📁 新建文件夹</button>
                    <button class="btn btn-sm btn-blue" onclick="loadFiles()">🔄 刷新列表</button>
                </div>
            </div>
            
            <div class="storage-info">
                <span>存储空间占用</span>
                <span id="storage-text">加载中...</span>
            </div>
            <div class="progress-bar">
                <div class="progress-fill" id="storage-progress"></div>
            </div>

            <div class="path-bar">
                <div class="path-display">当前路径: <span id="current-path-text">/</span></div>
                <button class="btn btn-sm btn-blue" id="btn-up-dir" onclick="goUpDir()" style="display:none;">⬆️ 返回上一级</button>
            </div>

            <!-- 支持点击选择与直接拖拽上传 -->
            <div class="upload-zone" id="file-drop-zone" onclick="document.getElementById('file-input').click()">
                <div style="font-size: 24px; margin-bottom: 4px;">📤</div>
                <div>点击或拖拽文件上传至当前目录</div>
                <div style="font-size:12px; color:var(--text-secondary); margin-top:4px;">支持任意格式 (词库请放入 /dict 目录)</div>
                <input type="file" id="file-input" style="display: none;" onchange="uploadFile(this.files[0])">
            </div>
            <div class="progress-bar" id="file-upload-progress" style="display: none; margin-top: 4px; margin-bottom: 8px;">
                <div class="progress-fill" id="file-upload-progress-fill"></div>
            </div>
            <div id="file-upload-status" class="status-msg" style="text-align: center; margin-bottom: 12px;"></div>

            <table class="file-table">
                <thead>
                    <tr>
                        <th>名称</th>
                        <th style="width: 100px;">大小 / 类型</th>
                        <th style="width: 140px; text-align: right;">操作</th>
                    </tr>
                </thead>
                <tbody id="file-tbody">
                    <tr><td colspan="3" style="text-align:center; color:var(--text-secondary);">加载中...</td></tr>
                </tbody>
            </table>
        </div>

        <!-- 4. OTA 固件升级卡片 -->
        <div class="card">
            <div class="card-title">
                <span>⚡ 固件无线 OTA 升级</span>
            </div>
            <div class="upload-zone" id="ota-drop-zone" onclick="document.getElementById('ota-input').click()" style="padding:16px;">
                <div>点击或拖拽编译好的 <b>firmware.bin</b> 固件包一键无线刷入</div>
                <input type="file" id="ota-input" accept=".bin" style="display: none;" onchange="uploadOTA(this.files[0])">
            </div>
            <div class="progress-bar" id="ota-progress" style="display: none;">
                <div class="progress-fill" id="ota-progress-fill"></div>
            </div>
            <div id="ota-status" class="status-msg"></div>
        </div>
    </div>

    <script>
        let currentPath = '/';

        document.addEventListener('DOMContentLoaded', () => {
            const today = new Date().toISOString().split('T')[0];
            document.getElementById('schedule-date').value = today;

            setupDragAndDrop();
            loadSettings();
            loadSchedules();
            loadStorage();
            loadFiles();
        });

        // 初始化拖拽上传监听
        function setupDragAndDrop() {
            const fileZone = document.getElementById('file-drop-zone');
            const otaZone = document.getElementById('ota-drop-zone');

            [fileZone, otaZone].forEach(zone => {
                ['dragenter', 'dragover'].forEach(name => {
                    zone.addEventListener(name, (e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        zone.classList.add('dragover');
                    }, false);
                });

                ['dragleave', 'drop'].forEach(name => {
                    zone.addEventListener(name, (e) => {
                        e.preventDefault();
                        e.stopPropagation();
                        zone.classList.remove('dragover');
                    }, false);
                });
            });

            fileZone.addEventListener('drop', (e) => {
                const dt = e.dataTransfer;
                if (dt && dt.files && dt.files.length > 0) {
                    uploadFile(dt.files[0]);
                }
            }, false);

            otaZone.addEventListener('drop', (e) => {
                const dt = e.dataTransfer;
                if (dt && dt.files && dt.files.length > 0) {
                    uploadOTA(dt.files[0]);
                }
            }, false);
        }

        function updateCharCount() {
            const val = document.getElementById('schedule-text').value;
            document.getElementById('char-count').innerText = val.length;
        }

        // 0. Wi-Fi 扫描与配置
        async function scanWifi() {
            const status = document.getElementById('wifi-status');
            const select = document.getElementById('wifi-ssid-select');
            status.style.color = '#ff9f0a';
            status.innerText = '正在扫描周围 2.4GHz Wi-Fi 信号，请稍候...';
            try {
                const res = await fetch('/api/wifi/scan');
                const list = await res.json();
                select.innerHTML = '<option value="">-- 扫描到 ' + list.length + ' 个网络，点击选择 --</option>';
                list.forEach(w => {
                    const opt = document.createElement('option');
                    opt.value = w.ssid;
                    opt.innerText = w.ssid + ' (' + w.rssi + ' dBm ' + (w.secure ? '🔒' : '🔓') + ')';
                    select.appendChild(opt);
                });
                select.style.display = 'block';
                status.style.color = '#34c759';
                status.innerText = '扫描完成，共找到 ' + list.length + ' 个 Wi-Fi 网络';
            } catch (e) {
                status.style.color = '#ff3b30';
                status.innerText = '扫描失败: ' + e;
            }
        }

        function onSsidSelect(val) {
            if (val) {
                document.getElementById('wifi-ssid-input').value = val;
            }
        }

        async function connectWifiSubmit() {
            const ssid = document.getElementById('wifi-ssid-input').value.trim();
            const password = document.getElementById('wifi-pass-input').value;
            const status = document.getElementById('wifi-status');
            if (!ssid) {
                status.style.color = '#ff3b30';
                status.innerText = '请输入或选择 Wi-Fi 名称！';
                return;
            }
            status.style.color = '#ff9f0a';
            status.innerText = '正在发送配网请求，设备将尝试连接该网络并同步时间，请留意墨水屏显示...';
            try {
                await fetch('/api/wifi/connect', {
                    method: 'POST',
                    headers: {'Content-Type': 'application/json'},
                    body: JSON.stringify({ssid: ssid, password: password})
                });
                status.style.color = '#34c759';
                status.innerText = '指令已送达！若连接成功，墨水屏将显示新 IP 地址并关闭配置热点。';
            } catch (e) {
                status.style.color = '#ff9f0a';
                status.innerText = '网络切换中，请稍后检查设备屏幕显示。';
            }
        }

        // 1. 系统与单词播放设置
        async function loadSettings() {
            try {
                const res = await fetch('/api/settings');
                const data = await res.json();
                
                const dictSelect = document.getElementById('dict-select');
                dictSelect.innerHTML = '';
                if (data.dictList && data.dictList.length > 0) {
                    data.dictList.forEach(d => {
                        const opt = document.createElement('option');
                        opt.value = d;
                        opt.innerText = d;
                        if (d === data.currentDict) opt.selected = true;
                        dictSelect.appendChild(opt);
                    });
                } else {
                    dictSelect.innerHTML = '<option value="">未找到词库 (请上传至 /dict)</option>';
                }

                document.getElementById('mode-select').value = data.randomMode ? "1" : "0";
                document.getElementById('interval-input').value = data.intervalMin || 3;
                document.getElementById('wifi-close-input').value = data.wifiAutoCloseMin || 6;
                document.getElementById('night-silent-select').value = (data.nightSilent !== false) ? "1" : "0";
            } catch (e) {
                console.error(e);
            }
        }

        async function saveSettings() {
            const status = document.getElementById('settings-status');
            status.className = 'status-msg';
            status.innerText = '正在保存设置...';

            const payload = {
                dict: document.getElementById('dict-select').value,
                randomMode: document.getElementById('mode-select').value === "1",
                intervalMin: parseInt(document.getElementById('interval-input').value) || 3,
                wifiAutoCloseMin: parseInt(document.getElementById('wifi-close-input').value) || 6,
                nightSilent: document.getElementById('night-silent-select').value === "1"
            };

            try {
                const res = await fetch('/api/settings', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(payload)
                });
                if (res.ok) {
                    status.className = 'status-msg success';
                    status.innerText = '✓ 设置已保存，屏幕已同步刷新！';
                    setTimeout(() => status.innerText = '', 3000);
                } else {
                    status.className = 'status-msg error';
                    status.innerText = '保存失败';
                }
            } catch (e) {
                status.className = 'status-msg error';
                status.innerText = '网络连接错误';
            }
        }

        // 2. 日程备忘管理
        async function loadSchedules() {
            const tbody = document.getElementById('schedule-tbody');
            try {
                const res = await fetch('/api/schedules');
                const data = await res.json();
                tbody.innerHTML = '';

                const dates = Object.keys(data).sort();
                if (dates.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="3" style="text-align:center; color:var(--text-secondary); padding:16px;">暂无日程备忘</td></tr>';
                    return;
                }

                dates.forEach(date => {
                    const text = data[date];
                    const tr = document.createElement('tr');
                    tr.innerHTML = `
                        <td><span class="badge-date">${date}</span></td>
                        <td>${text}</td>
                        <td style="text-align:right;">
                            <button class="btn btn-sm btn-danger" onclick="deleteSchedule('${date}')">删除</button>
                        </td>
                    `;
                    tbody.appendChild(tr);
                });
            } catch (e) {
                tbody.innerHTML = '<tr><td colspan="3" style="text-align:center; color:var(--primary-color);">加载日程失败</td></tr>';
            }
        }

        async function addSchedule() {
            const date = document.getElementById('schedule-date').value;
            const text = document.getElementById('schedule-text').value.trim();
            const status = document.getElementById('schedule-status');

            if (!date || !text) {
                alert('请选择日期并填写提醒内容！');
                return;
            }

            status.className = 'status-msg';
            status.innerText = '正在添加日程...';

            try {
                const res = await fetch('/api/schedules', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ date, text })
                });
                if (res.ok) {
                    document.getElementById('schedule-text').value = '';
                    updateCharCount();
                    status.className = 'status-msg success';
                    status.innerText = '✓ 日程已成功添加并在日历中生效！';
                    setTimeout(() => status.innerText = '', 3000);
                    loadSchedules();
                } else {
                    status.className = 'status-msg error';
                    status.innerText = '添加失败';
                }
            } catch (e) {
                status.className = 'status-msg error';
                status.innerText = '网络连接错误';
            }
        }

        async function deleteSchedule(date) {
            if (!confirm(`确定删除 ${date} 的日程提醒吗？`)) return;
            try {
                await fetch('/api/schedules/delete', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ date })
                });
                loadSchedules();
            } catch (e) {
                alert('删除失败');
            }
        }

        // 3. 全盘文件管理器 (目录跳转 / 新建文件夹 / 格式化识别)
        async function loadStorage() {
            try {
                const res = await fetch('/api/storage');
                const data = await res.json();
                const totalMB = (data.total / (1024 * 1024)).toFixed(1);
                const usedMB = (data.used / (1024 * 1024)).toFixed(1);
                const pct = ((data.used / data.total) * 100).toFixed(1);

                document.getElementById('storage-text').innerText = `${usedMB} MB / ${totalMB} MB (${pct}%)`;
                document.getElementById('storage-progress').style.width = `${pct}%`;
            } catch (e) {}
        }

        async function loadFiles(dirPath = currentPath) {
            currentPath = dirPath;
            document.getElementById('current-path-text').innerText = currentPath;
            document.getElementById('btn-up-dir').style.display = (currentPath === '/' ? 'none' : 'inline-flex');

            const tbody = document.getElementById('file-tbody');
            try {
                const res = await fetch(`/api/list?dir=${encodeURIComponent(currentPath)}`);
                const data = await res.json();
                tbody.innerHTML = '';

                if (data.length === 0) {
                    tbody.innerHTML = '<tr><td colspan="3" style="text-align:center; color:var(--text-secondary); padding:16px;">当前目录为空</td></tr>';
                    return;
                }

                data.sort((a, b) => {
                    if (a.isDir === b.isDir) return a.name.localeCompare(b.name);
                    return a.isDir ? -1 : 1;
                });

                data.forEach(item => {
                    const tr = document.createElement('tr');
                    const fullItemPath = (currentPath === '/' ? '/' : currentPath + '/') + item.name;

                    if (item.isDir) {
                        tr.className = 'row-folder';
                        tr.innerHTML = `
                            <td onclick="enterFolder('${fullItemPath}')">📁 <b>${item.name}</b></td>
                            <td onclick="enterFolder('${fullItemPath}')" style="color:var(--yellow-color);">文件夹</td>
                            <td style="text-align:right;">
                                <button class="btn btn-sm btn-blue" onclick="enterFolder('${fullItemPath}')">打开</button>
                                <button class="btn btn-sm btn-danger" onclick="deleteItem('${fullItemPath}')">删除</button>
                            </td>
                        `;
                    } else {
                        let icon = '📄';
                        if (item.name.endsWith('.json')) icon = '📊';
                        else if (item.name.endsWith('.bin')) icon = '⚡';
                        else if (item.name.endsWith('.txt')) icon = '📝';

                        tr.innerHTML = `
                            <td>${icon} ${item.name}</td>
                            <td>${formatBytes(item.size)}</td>
                            <td style="text-align:right;">
                                <a href="/api/download?file=${encodeURIComponent(fullItemPath)}" download="${item.name}" class="btn btn-sm btn-blue" style="text-decoration:none;">下载</a>
                                <button class="btn btn-sm btn-danger" onclick="deleteItem('${fullItemPath}')">删除</button>
                            </td>
                        `;
                    }
                    tbody.appendChild(tr);
                });
            } catch (e) {
                tbody.innerHTML = '<tr><td colspan="3" style="text-align:center; color:var(--primary-color);">加载文件列表失败</td></tr>';
            }
        }

        function enterFolder(folderPath) {
            loadFiles(folderPath);
        }

        function goUpDir() {
            if (currentPath === '/') return;
            const parts = currentPath.split('/').filter(p => p.length > 0);
            parts.pop();
            const parent = '/' + parts.join('/');
            loadFiles(parent);
        }

        async function createNewFolder() {
            const name = prompt('请输入新文件夹名称（例如 books）:');
            if (!name || !name.trim()) return;
            const newPath = (currentPath === '/' ? '/' : currentPath + '/') + name.trim();

            try {
                const res = await fetch(`/api/mkdir?path=${encodeURIComponent(newPath)}`, { method: 'POST' });
                if (res.ok) {
                    loadFiles();
                } else {
                    alert('创建文件夹失败');
                }
            } catch (e) { alert('网络连接错误'); }
        }

        async function deleteItem(path) {
            if (!confirm(`确定要永久删除 ${path} 吗？`)) return;
            try {
                const res = await fetch(`/api/delete?file=${encodeURIComponent(path)}`, { method: 'POST' });
                if (res.ok) {
                    loadFiles();
                    loadStorage();
                    loadSettings();
                } else {
                    alert('删除失败');
                }
            } catch (e) { alert('删除请求出错'); }
        }

        function uploadFile(file) {
            if (!file) return;
            const progress = document.getElementById('file-upload-progress');
            const fill = document.getElementById('file-upload-progress-fill');
            const status = document.getElementById('file-upload-status');

            progress.style.display = 'block';
            fill.style.width = '0%';
            fill.style.background = 'linear-gradient(90deg, var(--primary-color) 0%, #ff453a 100%)';
            status.className = 'status-msg';
            status.innerText = `正在上传 ${file.name} (0%)...`;

            const formData = new FormData();
            formData.append('file', file, file.name);

            const xhr = new XMLHttpRequest();
            xhr.open('POST', `/api/upload?path=${encodeURIComponent(currentPath)}`, true);

            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const pct = Math.round((e.loaded / e.total) * 100);
                    fill.style.width = `${pct}%`;
                    status.innerText = `正在传输 ${file.name}: ${pct}% (${formatBytes(e.loaded)} / ${formatBytes(e.total)})...`;
                }
            };

            xhr.onload = () => {
                if (xhr.status === 200) {
                    fill.style.width = '100%';
                    fill.style.background = '#34c759';
                    status.className = 'status-msg success';
                    status.innerText = `✓ 文件 ${file.name} 已成功上传至 ${currentPath}！`;
                    loadFiles();
                    loadStorage();
                    loadSettings();
                    setTimeout(() => {
                        progress.style.display = 'none';
                        status.innerText = '';
                    }, 4000);
                } else {
                    status.className = 'status-msg error';
                    status.innerText = `上传失败 (代码: ${xhr.status})！`;
                }
            };

            xhr.onerror = () => {
                status.className = 'status-msg error';
                status.innerText = '网络连接断开，文件上传失败！';
            };

            xhr.send(formData);
        }

        function uploadOTA(file) {
            if (!file || !file.name.endsWith('.bin')) return alert('仅支持 .bin 固件！');
            const status = document.getElementById('ota-status');
            const progress = document.getElementById('ota-progress');
            const fill = document.getElementById('ota-progress-fill');

            status.className = 'status-msg';
            status.innerText = '正在上传固件，请勿断电...';
            progress.style.display = 'block';
            fill.style.width = '0%';

            const formData = new FormData();
            formData.append('update', file, file.name);

            const xhr = new XMLHttpRequest();
            xhr.open('POST', '/update', true);
            xhr.upload.onprogress = (e) => {
                if (e.lengthComputable) {
                    const pct = Math.round((e.loaded / e.total) * 100);
                    fill.style.width = `${pct}%`;
                    status.innerText = `传输中: ${pct}%...`;
                }
            };
            xhr.onload = () => {
                if (xhr.status === 200) {
                    status.className = 'status-msg success';
                    status.innerText = '✓ 固件升级成功！设备正在重启，请10秒后刷新。';
                } else {
                    status.className = 'status-msg error';
                    status.innerText = '升级失败！';
                }
            };
            xhr.send(formData);
        }

        function formatBytes(bytes) {
            if (bytes === 0) return '0 B';
            const k = 1024;
            const sizes = ['B', 'KB', 'MB', 'GB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
        }
    </script>
</body>
</html>)rawliteral";

class WebManager {
public:
    WebServer server;
    std::function<void()> onOtaStart = nullptr;
    std::function<void()> onSettingsUpdated = nullptr;
    std::function<void()> onScheduleUpdated = nullptr;
    std::function<void()> onActivity = nullptr; // 数据活动心跳回调，重置 Wi-Fi 超时计时
    std::function<void(const String& ssid, const String& pass)> onWifiConfigured = nullptr;

    volatile bool isTransferring = false; // 正在上传文件或 OTA 固件的保护锁，绝不被 Wi-Fi 超时掐断

    // 回调钩子：获取与设置词库、模式、间隔、夜间静默
    std::function<void(String& currentDict, int& intervalMin, bool& randomMode, int& wifiAutoCloseMin, bool& nightSilent, std::vector<String>& dictList)> getSettingsCb = nullptr;
    std::function<void(const String& currentDict, int intervalMin, bool randomMode, int wifiAutoCloseMin, bool nightSilent)> setSettingsCb = nullptr;

    // 日程字典引用
    std::map<String, String>* pScheduleMap = nullptr;

    WebManager() : server(80) {}

    void setScheduleMap(std::map<String, String>* sMap) {
        pScheduleMap = sMap;
    }

    inline void notifyActivity() {
        if (onActivity) onActivity();
    }

    void begin() {
        // 1. 注册主页与手机热点认证 Captive Portal 重定向
        server.on("/", HTTP_GET, [this]() {
            server.send_P(200, "text/html", INDEX_HTML);
        });
        server.on("/hotspot-detect.html", HTTP_GET, [this]() {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        });
        server.on("/generate_204", HTTP_GET, [this]() {
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        });

        // 1.1 Wi-Fi 扫描接口 (GET /api/wifi/scan)
        server.on("/api/wifi/scan", HTTP_GET, [this]() {
            notifyActivity();
            int n = WiFi.scanNetworks();
            JsonDocument doc;
            JsonArray arr = doc.to<JsonArray>();
            for (int i = 0; i < n; ++i) {
                JsonObject item = arr.add<JsonObject>();
                item["ssid"] = WiFi.SSID(i);
                item["rssi"] = WiFi.RSSI(i);
                item["secure"] = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
            }
            String response;
            serializeJson(doc, response);
            server.send(200, "application/json", response);
        });

        // 1.2 Wi-Fi 连接配置接口 (POST /api/wifi/connect)
        server.on("/api/wifi/connect", HTTP_POST, [this]() {
            notifyActivity();
            if (!server.hasArg("plain")) {
                server.send(400, "text/plain", "Missing body");
                return;
            }
            String body = server.arg("plain");
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                server.send(400, "text/plain", "Invalid JSON");
                return;
            }

            String ssid = doc["ssid"].as<String>();
            String pass = doc["password"].as<String>();
            if (ssid.length() == 0) {
                server.send(400, "text/plain", "SSID cannot be empty");
                return;
            }

            server.send(200, "application/json", "{\"status\":\"connecting\"}");
            delay(100);

            if (onWifiConfigured) {
                onWifiConfigured(ssid, pass);
            }
        });

        // 2. 播放设置获取接口 (GET /api/settings)
        server.on("/api/settings", HTTP_GET, [this]() {
            JsonDocument doc;
            String curDict = "";
            int intervalMin = 3;
            bool randMode = false;
            int wifiCloseMin = 6;
            bool nightSil = true;
            std::vector<String> dList;

            if (getSettingsCb) {
                getSettingsCb(curDict, intervalMin, randMode, wifiCloseMin, nightSil, dList);
            }

            doc["currentDict"] = curDict;
            doc["intervalMin"] = intervalMin;
            doc["randomMode"] = randMode;
            doc["wifiAutoCloseMin"] = wifiCloseMin;
            doc["nightSilent"] = nightSil;
            JsonArray arr = doc["dictList"].to<JsonArray>();
            for (const String& d : dList) {
                arr.add(d);
            }

            String response;
            serializeJson(doc, response);
            server.send(200, "application/json", response);
        });

        // 3. 播放设置保存接口 (POST /api/settings)
        server.on("/api/settings", HTTP_POST, [this]() {
            if (!server.hasArg("plain")) {
                server.send(400, "text/plain", "Missing body");
                return;
            }
            String body = server.arg("plain");
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                server.send(400, "text/plain", "Invalid JSON");
                return;
            }

            String newDict = doc["dict"].as<String>();
            int newInterval = doc["intervalMin"].as<int>();
            bool newRand = doc["randomMode"].as<bool>();
            int newWifiClose = doc["wifiAutoCloseMin"].as<int>();
            bool newNightSil = doc["nightSilent"] | true;

            if (newInterval < 1) newInterval = 1;
            if (newInterval > 120) newInterval = 120;
            if (newWifiClose < 1) newWifiClose = 1;
            if (newWifiClose > 60) newWifiClose = 60;

            if (setSettingsCb) {
                setSettingsCb(newDict, newInterval, newRand, newWifiClose, newNightSil);
            }

            if (onSettingsUpdated) {
                onSettingsUpdated();
            }

            server.send(200, "application/json", "{\"status\":\"ok\"}");
        });

        // 4. 获取日程接口 (GET /api/schedules)
        server.on("/api/schedules", HTTP_GET, [this]() {
            JsonDocument doc;
            JsonObject obj = doc.to<JsonObject>();
            if (pScheduleMap) {
                for (auto const& [date, text] : *pScheduleMap) {
                    obj[date] = text;
                }
            }
            String response;
            serializeJson(doc, response);
            server.send(200, "application/json", response);
        });

        // 5. 保存/新增日程接口 (POST /api/schedules)
        server.on("/api/schedules", HTTP_POST, [this]() {
            if (!server.hasArg("plain")) {
                server.send(400, "text/plain", "Missing body");
                return;
            }
            String body = server.arg("plain");
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                server.send(400, "text/plain", "Invalid JSON");
                return;
            }

            String date = doc["date"].as<String>();
            String text = doc["text"].as<String>();
            text.trim();
            if (date.length() >= 8 && text.length() > 0 && pScheduleMap) {
                (*pScheduleMap)[date] = text;
                saveSchedulesToSD();
                if (onScheduleUpdated) {
                    onScheduleUpdated();
                }
            }

            server.send(200, "application/json", "{\"status\":\"ok\"}");
        });

        // 6. 删除日程接口 (POST /api/schedules/delete)
        server.on("/api/schedules/delete", HTTP_POST, [this]() {
            if (!server.hasArg("plain")) {
                server.send(400, "text/plain", "Missing body");
                return;
            }
            String body = server.arg("plain");
            JsonDocument doc;
            if (!deserializeJson(doc, body)) {
                String date = doc["date"].as<String>();
                if (pScheduleMap && pScheduleMap->count(date)) {
                    pScheduleMap->erase(date);
                    saveSchedulesToSD();
                    if (onScheduleUpdated) {
                        onScheduleUpdated();
                    }
                }
            }
            server.send(200, "application/json", "{\"status\":\"ok\"}");
        });

        // 7. 存储空间读取 (GET /api/storage)
        server.on("/api/storage", HTTP_GET, [this]() {
            uint64_t total = SD_MMC.totalBytes();
            uint64_t used = SD_MMC.usedBytes();
            JsonDocument doc;
            doc["total"] = total;
            doc["used"] = used;
            String response;
            serializeJson(doc, response);
            server.send(200, "application/json", response);
        });

        // 8. 全盘文件目录读取 (GET /api/list)
        server.on("/api/list", HTTP_GET, [this]() {
            String dirPath = "/";
            if (server.hasArg("dir")) {
                dirPath = server.arg("dir");
            }
            if (!dirPath.startsWith("/")) dirPath = "/" + dirPath;

            File root = SD_MMC.open(dirPath);
            if (!root || !root.isDirectory()) {
                server.send(500, "text/plain", "Failed to open directory");
                return;
            }

            JsonDocument doc;
            JsonArray array = doc.to<JsonArray>();

            File file = root.openNextFile();
            while (file) {
                String name = String(file.name());
                if (!name.startsWith("._") && name != "System Volume Information") {
                    JsonObject item = array.add<JsonObject>();
                    item["name"] = name;
                    item["isDir"] = file.isDirectory();
                    item["size"] = file.isDirectory() ? 0 : file.size();
                }
                file = root.openNextFile();
            }

            String response;
            serializeJson(doc, response);
            server.send(200, "application/json", response);
        });

        // 9. 创建新文件夹 (POST /api/mkdir)
        server.on("/api/mkdir", HTTP_POST, [this]() {
            if (!server.hasArg("path")) {
                server.send(400, "text/plain", "Missing path parameter");
                return;
            }
            String path = server.arg("path");
            if (!path.startsWith("/")) path = "/" + path;
            
            bool ok = SD_MMC.mkdir(path);
            if (ok) {
                server.send(200, "text/plain", "OK");
            } else {
                server.send(500, "text/plain", "Failed to create directory");
            }
        });

        // 10. 文件下载 (GET /api/download) - 精确设置 Content-Disposition 响应头以保留原始文件名与后缀
        server.on("/api/download", HTTP_GET, [this]() {
            if (!server.hasArg("file")) {
                server.send(400, "text/plain", "Missing file parameter");
                return;
            }
            String path = server.arg("file");
            if (!path.startsWith("/")) path = "/" + path;

            if (!SD_MMC.exists(path)) {
                server.send(404, "text/plain", "File not found");
                return;
            }

            File file = SD_MMC.open(path, FILE_READ);
            if (!file || file.isDirectory()) {
                server.send(500, "text/plain", "Cannot read directory or invalid file");
                return;
            }

            // 提取纯文件名 (例如从 "/dict/cet4.json" 提取 "cet4.json")
            String filename = path;
            int lastSlash = filename.lastIndexOf('/');
            if (lastSlash != -1) {
                filename = filename.substring(lastSlash + 1);
            }

            // 设置 Content-Disposition 响应头确保浏览器精准识别文件名与扩展名
            String dispHeader = "attachment; filename=\"" + filename + "\"";
            server.sendHeader("Content-Disposition", dispHeader);
            server.sendHeader("Connection", "close");
            server.streamFile(file, "application/octet-stream");
            file.close();
        });

        // 11. 文件/文件夹删除 (POST /api/delete)
        server.on("/api/delete", HTTP_POST, [this]() {
            if (!server.hasArg("file")) {
                server.send(400, "text/plain", "Missing file parameter");
                return;
            }
            String path = server.arg("file");
            if (!path.startsWith("/")) path = "/" + path;

            if (!SD_MMC.exists(path)) {
                server.send(404, "text/plain", "Not exists");
                return;
            }

            bool success = false;
            File f = SD_MMC.open(path);
            if (f.isDirectory()) {
                f.close();
                success = SD_MMC.rmdir(path);
            } else {
                f.close();
                success = SD_MMC.remove(path);
            }

            if (success) {
                server.send(200, "text/plain", "OK");
            } else {
                server.send(500, "text/plain", "Failed to delete");
            }
        });

        // 12. 文件上传至任意目录 (POST /api/upload)
        server.on("/api/upload", HTTP_POST, [this]() {
            isTransferring = false;
            notifyActivity();
            server.send(200, "text/plain", "Upload finished");
        }, [this]() {
            HTTPUpload& upload = server.upload();
            static File uploadFile;
            notifyActivity(); // 每次接收数据分块，持续刷新活跃心跳

            if (upload.status == UPLOAD_FILE_START) {
                isTransferring = true;
                String path = "/";
                if (server.hasArg("path")) {
                    path = server.arg("path");
                }
                if (!path.startsWith("/")) path = "/" + path;
                if (!path.endsWith("/")) path += "/";
                String fullPath = path + upload.filename;
                
                uploadFile = SD_MMC.open(fullPath, FILE_WRITE);
                if (!uploadFile) {
                    Serial.printf("无法创建文件: %s\n", fullPath.c_str());
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                isTransferring = true;
                if (uploadFile) {
                    uploadFile.write(upload.buf, upload.currentSize);
                }
            } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
                isTransferring = false;
                if (uploadFile) {
                    uploadFile.close();
                    Serial.printf("成功上传文件，大小: %d 字节\n", upload.totalSize);
                }
            }
        });

        // 13. OTA 固件升级 (POST /update)
        server.on("/update", HTTP_POST, [this]() {
            isTransferring = false;
            notifyActivity();
            server.sendHeader("Connection", "close");
            server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
            delay(1000);
            ESP.restart();
        }, [this]() {
            HTTPUpload& upload = server.upload();
            notifyActivity(); // 每次接收固件分块，持续刷新活跃心跳

            if (upload.status == UPLOAD_FILE_START) {
                isTransferring = true;
                Serial.printf("OTA 升级已启动: %s\n", upload.filename.c_str());
                if (onOtaStart) {
                    onOtaStart();
                }
                if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_WRITE) {
                isTransferring = true;
                if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                    Update.printError(Serial);
                }
            } else if (upload.status == UPLOAD_FILE_END || upload.status == UPLOAD_FILE_ABORTED) {
                isTransferring = false;
                if (Update.end(true)) {
                    Serial.printf("OTA 升级成功！写入大小: %u 字节。系统正在重新启动...\n", upload.totalSize);
                } else {
                    Update.printError(Serial);
                }
            }
        });

        // 14. 捕获热点未命中路由并重定向至首页 (Captive Portal 强制门户)
        server.onNotFound([this]() {
            notifyActivity();
            server.sendHeader("Location", "http://192.168.4.1/", true);
            server.send(302, "text/plain", "");
        });

        server.begin();
        MDNS.addService("http", "tcp", 80);
        Serial.println("局域网 Web 控制中心已启动，端口: 80");
    }

    void handleClient() {
        server.handleClient();
    }

private:
    void saveSchedulesToSD() {
        if (!pScheduleMap) return;
        File f = SD_MMC.open("/schedule.json", FILE_WRITE);
        if (f) {
            JsonDocument doc;
            for (auto const& [date, text] : *pScheduleMap) {
                doc[date] = text;
            }
            serializeJson(doc, f);
            f.close();
            Serial.println(">>> 日程备忘已持久化保存至 /schedule.json");
        }
    }
};

#endif // WEB_MANAGER_H
