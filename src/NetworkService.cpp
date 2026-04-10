#include "NetworkService.h"
#include <time.h>
#include <deque>

NetworkService::NetworkService() {}

void NetworkService::begin(const char* ssid, const char* pass) {
    Serial.println("\n[PHASING] Step 0: Minimal Core Start...");
    _prefs.begin("net_stats", true);
    String savedSSID = _prefs.getString("w_ssid", ssid);
    String savedPASS = _prefs.getString("w_pass", pass);
    _prefs.end();
    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    _startTime = millis();
    _lastConnectedTime = millis();
    _bootPhase = 0;
}

void NetworkService::update() {
    unsigned long uptime = millis() - _startTime;

    if (_bootPhase == 0 && uptime > 10000) {
        _fsReady = LittleFS.begin(true);
        _prefs.begin("net_stats", false);
        _historicalReconnects = _prefs.getInt("t_recon", 0);
        _historicalUptime = _prefs.getULong("t_uptime", 0);
        _reconnectCount = _prefs.getInt("recon", 0);
        _gmtOffset = _prefs.getInt("gmt", -5);
        _prefs.end();
        _bootPhase = 1;
        logEvent("SYS_STATUS", "FileSystem & Stats Ready");
    }

    if (_bootPhase == 1 && uptime > 15000) {
        if (!_server) _server = new WebServer(80);
        if (!_dnsServer) _dnsServer = new DNSServer();
        _setupWebServer();
        _bootPhase = 2;
        logEvent("SYS_STATUS", "Web Interface Live");
    }

    if (_bootPhase == 2 && uptime > 20000) {
        configTime(_gmtOffset * 3600, 0, "pool.ntp.org");
        _bootPhase = 3;
        logEvent("SYS_STATUS", "Clock Synchronized");
    }

    if (_server) _server->handleClient();
    bool connected = (WiFi.status() == WL_CONNECTED);
    if (_isConfigMode && _dnsServer) _dnsServer->processNextRequest();

    if (connected) {
        _lastConnectedTime = millis();
        if (_isConfigMode) { WiFi.softAPdisconnect(true); _isConfigMode = false; }
        if (_bootPhase >= 1 && (millis() - _lastPingTime > 5000)) { _lastPingTime = millis(); _performPing(); }
        if (_bootPhase >= 1 && (millis() - _lastSaveTime > _saveInterval)) {
            _historicalUptime += (millis() - _lastSaveTime);
            _lastSaveTime = millis();
            _prefs.begin("net_stats", false);
            _prefs.putULong("t_uptime", _historicalUptime);
            _prefs.end();
        }
    } else {
        if (uptime > 30000 && !connected && !_isConfigMode && _bootPhase >= 2) {
            _isConfigMode = true;
            WiFi.softAP("WiFi-Monitor-C6");
            if (_dnsServer) _dnsServer->start(53, "*", WiFi.softAPIP());
        }
        if (millis() - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = millis();
            _reconnectInterval = (unsigned long)min((int)_reconnectInterval * 2, (int)_maxReconnectInterval);
            WiFi.disconnect(); WiFi.begin();
            _reconnectCount++;
            if (_bootPhase >= 1) {
                _prefs.begin("net_stats", false);
                _prefs.putInt("recon", _reconnectCount);
                _prefs.putInt("t_recon", _historicalReconnects + _reconnectCount);
                _prefs.end();
            }
        }
        if (millis() - _lastConnectedTime > _maxDisconnectTime) {
            logEvent("CRITICAL", "Watchdog Reboot");
            delay(1000); ESP.restart();
        }
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    char dateStr[16] = "BOOTING";
    char timeStr[16] = "BOOTING";
    time_t now; time(&now);
    if (now > 1000000) {
        struct tm timeinfo; gmtime_r(&now, &timeinfo);
        strftime(dateStr, sizeof(dateStr), "%Y-%m-%d", &timeinfo);
        strftime(timeStr, sizeof(timeStr), "%H:%M:%S", &timeinfo);
    }
    
    // Formato delimitado para parsing: FECHA|HORA|TIPO|DATOS
    char fullMsg[128];
    sprintf(fullMsg, "%s|%s|%s|%s", dateStr, timeStr, type, data);
    Serial.println(fullMsg);
    
    if (_fsReady) {
        File file = LittleFS.open("/log.txt", FILE_APPEND);
        if (file) { file.println(fullMsg); file.close(); }
    }
}

void NetworkService::_setupWebServer() {
    if (!_server) return;
    _server->on("/", [this]() { _handleRoot(); });
    _server->on("/logs", [this]() { _handleLogs(); });
    _server->on("/config", [this]() { _handleConfig(); });
    _server->on("/status", [this]() {
        NetworkData d = getData();
        String json = "{";
        json += "\"uptime\":\"" + getUptimeString() + "\",";
        json += "\"rssi\":" + String(d.rssi) + ",";
        json += "\"ip\":\"" + d.ip + "\",";
        json += "\"recon\":" + String(_reconnectCount);
        json += "}";
        _server->send(200, "application/json", json);
    });
    
    _server->on("/save", HTTP_POST, [this]() {
        String s = _server->arg("ssid"), p = _server->arg("pass"), g = _server->arg("gmt");
        if (s.length() > 0) {
            _prefs.begin("net_stats", false);
            _prefs.putString("w_ssid", s); _prefs.putString("w_pass", p);
            _prefs.putInt("gmt", g.toInt()); _prefs.end();
            _server->send(200, "text/html", "<b>Config OK</b>. Rebooting...");
            delay(1000); ESP.restart();
        }
    });
    _server->begin();
}

void NetworkService::_handleRoot() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    h += "<style>body{background:#0a0a0a;color:#eee;font-family:sans-serif;text-align:center;padding:20px;}";
    h += ".card{background:#111;padding:20px;border-radius:15px;border:1px solid #00ffcc;margin:10px;display:inline-block;min-width:180px;}";
    h += ".label{font-size:0.8em;color:#888;margin-bottom:5px;} .val{font-size:1.8em;font-weight:bold;color:#fff;}";
    h += "h1{color:#00ffcc;text-transform:uppercase;letter-spacing:2px;} a{color:#00ffcc;text-decoration:none;font-weight:bold;margin:0 10px; border:1px solid #00ffcc; padding:5px 10px; border-radius:5px;}";
    h += "</style><script>function up(){fetch('/status').then(r=>r.json()).then(d=>{";
    h += "document.getElementById('u').innerText=d.uptime;document.getElementById('r').innerText=d.rssi+' dBm';";
    h += "document.getElementById('re').innerText=d.recon;});} setInterval(up,2000);</script></head><body>";
    h += "<h1>Industrial WiFi Monitor</h1>";
    h += "<div class='card'><div class='label'>UPTIME ACUMULADO</div><div class='val' id='u'>" + getUptimeString() + "</div></div>";
    h += "<div class='card'><div class='label'>SEÑAL ACTUAL</div><div class='val' id='r'>-- dBm</div></div>";
    h += "<div class='card'><div class='label'>RECONEXIONES</div><div class='val' id='re'>" + String(_reconnectCount) + "</div></div>";
    h += "<br><br><br><a href='/logs'>VIEW SYSTEM LOGS</a><a href='/config'>SETTINGS</a></body></html>";
    _server->send(200, "text/html", h);
}

void NetworkService::_handleLogs() {
    if (!_fsReady) { _server->send(500, "text/plain", "Flash Storage Error"); return; }
    
    File file = LittleFS.open("/log.txt", FILE_READ);
    std::deque<String> lastLogs;
    while (file.available()) {
        String line = file.readStringUntil('\n');
        if (line.length() > 5) {
            lastLogs.push_back(line);
            if (lastLogs.size() > 50) lastLogs.pop_front();
        }
    }
    file.close();

    String h = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='30'><style>";
    h += "body{background:#0d0d0f;color:#e0e0e0;font-family:monospace;padding:20px;}";
    h += "table{width:100%; border-collapse:collapse; background:#16161a; border-radius:8px; overflow:hidden;}";
    h += "th{background:#00ffcc; color:#000; padding:12px; text-align:left;}";
    h += "td{padding:10px; border-bottom:1px solid #2d2d35;} tr:nth-child(even){background:#1c1c21;}";
    h += ".tag{padding:2px 6px; border-radius:3px; font-weight:bold; font-size:0.85em;}";
    h += ".CRITICAL{background:#ff4d4d; color:white;} .STATE_CHANGE{background:#00ffcc; color:black;}";
    h += ".SYS_STATUS{background:#4d94ff; color:white;} .SYS_TIME{background:#ffaa00; color:black;}";
    h += "</style></head><body><h2>System Event Log (Most Recent Top)</h2><table>";
    h += "<tr><th>DATE</th><th>TIME</th><th>EVENT</th><th>DATA</th></tr>";

    // Renderizado inverso (Top-Down)
    for (int i = lastLogs.size() - 1; i >= 0; i--) {
        String l = lastLogs[i];
        int s1 = l.indexOf('|'), s2 = l.indexOf('|', s1+1), s3 = l.indexOf('|', s2+1);
        if (s1 > 0 && s2 > 0 && s3 > 0) {
            String date = l.substring(0, s1), time = l.substring(s1+1, s2), type = l.substring(s2+1, s3), data = l.substring(s3+1);
            h += "<tr><td>"+date+"</td><td>"+time+"</td><td><span class='tag "+type+"'>"+type+"</span></td><td>"+data+"</td></tr>";
        } else {
            h += "<tr><td colspan='4' style='color:#666'>"+l+"</td></tr>";
        }
    }
    h += "</table><br><a href='/' style='color:#00ffcc; text-decoration:none;'>&larr; BACK TO DASHBOARD</a></body></html>";
    _server->send(200, "text/html", h);
}

void NetworkService::_handleConfig() {
    String h = "<html><body style='background:#0a0a0a;color:#fff;text-align:center;font-family:sans-serif;'>";
    h += "<h1>Network Settings</h1><form action='/save' method='POST' style='display:inline-block;text-align:left;'>";
    h += "SSID:<br><input type='text' name='ssid' style='width:250px;padding:8px;'><br><br>";
    h += "PASSWORD:<br><input type='password' name='pass' style='width:250px;padding:8px;'><br><br>";
    h += "GMT OFFSET:<br><input type='number' name='gmt' value='"+String(_gmtOffset)+"' style='width:250px;padding:8px;'><br><br>";
    h += "<input type='submit' value='SAVE & APPLY' style='background:#00ffcc;padding:12px 30px;border:none;border-radius:5px;font-weight:bold;cursor:pointer;'>";
    h += "</form></body></html>";
    _server->send(200, "text/html", h);
}

String NetworkService::getUptimeString() {
    unsigned long totalSec = (_historicalUptime + (millis() - _startTime)) / 1000;
    char buffer[16]; sprintf(buffer, "%02luh %02lum %02lus", totalSec/3600, (totalSec%3600)/60, totalSec%60);
    return String(buffer);
}

void NetworkService::_performPing() {
    static bool toggle = false;
    IPAddress gateway = WiFi.gatewayIP();
    if (!toggle) { _lastPingGW = Ping.ping(gateway) ? Ping.averageTime() : -1; }
    else { _lastPingInternet = Ping.ping("8.8.8.8") ? Ping.averageTime() : -1; }
    toggle = !toggle;
}

NetworkService::NetworkData NetworkService::getData() {
    NetworkData data; data.connected = (WiFi.status() == WL_CONNECTED);
    if (data.connected) {
        data.rssi = WiFi.RSSI(); data.ip = WiFi.localIP().toString();
        data.channel = WiFi.channel(); data.pingGW = _lastPingGW; data.pingInternet = _lastPingInternet;
    } else {
        data.rssi = -100; data.ip = "0.0.0.0"; data.channel = 0; data.pingGW = -1; data.pingInternet = -1;
    }
    return data;
}

int NetworkService::getReconnectCount() { return _reconnectCount; }
float NetworkService::getDisconnectRate() {
    float totalHours = (_historicalUptime + (millis() - _startTime)) / 3600000.0;
    return (totalHours < 0.01) ? 0.0 : (float)(_historicalReconnects + _reconnectCount) / totalHours;
}
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
