#include "NetworkService.h"
#include <time.h>
#include <deque>

NetworkService::NetworkService() {}

// Helper para CSS compartido
String getCommonCSS() {
    String c = "<style>body{background:#0a0a0a;color:#eee;font-family:sans-serif;text-align:center;padding:0;margin:0;}";
    c += ".nav{background:#111;padding:15px;border-bottom:1px solid #00ffcc;margin-bottom:20px;display:flex;justify-content:center;gap:15px;}";
    c += ".nav a{color:#00ffcc;text-decoration:none;font-weight:bold;font-size:0.9em;padding:5px 10px;border:1px solid transparent;border-radius:4px;}";
    c += ".nav a:hover{background:#00ffcc22;border-color:#00ffcc;}";
    c += ".card{background:#111;padding:20px;border-radius:15px;border:1px solid #333;margin:10px;display:inline-block;min-width:180px;}";
    c += "h1,h2{color:#00ffcc;text-transform:uppercase;letter-spacing:1px;} .val{font-size:1.8em;font-weight:bold;color:#fff;}";
    c += "table{width:95%;margin:0 auto;border-collapse:collapse;background:#111;font-family:monospace;font-size:0.9em;}";
    c += "th{background:#00ffcc;color:#000;padding:10px;} td{padding:8px;border-bottom:1px solid #222;}";
    c += ".tag{padding:2px 5px;border-radius:3px;font-size:0.8em;font-weight:bold;}";
    c += ".CRITICAL{background:#ff4444;color:#fff;} .STATE_CHANGE{background:#00ffcc;color:#000;}";
    c += "</style>";
    return c;
}

String getNav() {
    return "<div class='nav'><a href='/'>DASHBOARD</a><a href='/logs'>SYSTEM LOGS</a><a href='/config'>SETTINGS</a></div>";
}

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
        logEvent("SYS_STATUS", "NVS and FS Initialized");
    }

    if (_bootPhase == 1 && uptime > 15000) {
        if (!_server) _server = new WebServer(80);
        if (!_dnsServer) _dnsServer = new DNSServer();
        _setupWebServer();
        _bootPhase = 2;
        logEvent("SYS_STATUS", "Web Panel Active");
    }

    if (_bootPhase == 2 && uptime > 20000) {
        // Forzar sincronización con offset regional
        configTime(_gmtOffset * 3600, 0, "pool.ntp.org", "time.google.com");
        _bootPhase = 3;
        logEvent("SYS_STATUS", "Regional Clock Synced");
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
            logEvent("CRITICAL", "Link Dead. Rebooting...");
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
            _server->send(200, "text/html", "<b>Settings Saved</b>. Rebooting...");
            delay(1000); ESP.restart();
        }
    });
    _server->begin();
}

void NetworkService::_handleRoot() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    h += getCommonCSS();
    h += "<script>function up(){fetch('/status').then(r=>r.json()).then(d=>{";
    h += "document.getElementById('u').innerText=d.uptime;document.getElementById('r').innerText=d.rssi+' dBm';";
    h += "document.getElementById('re').innerText=d.recon;});} setInterval(up,2000);</script></head><body>";
    h += getNav();
    h += "<h1>Monitor Dashboard</h1>";
    h += "<div class='card'><div style='color:#888'>UPTIME</div><div class='val' id='u'>" + getUptimeString() + "</div></div>";
    h += "<div class='card'><div style='color:#888'>SIGNAL</div><div class='val' id='r'>-- dBm</div></div>";
    h += "<div class='card'><div style='color:#888'>RECONNECTS</div><div class='val' id='re'>" + String(_reconnectCount) + "</div></div>";
    h += "</body></html>";
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

    String h = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='30'>";
    h += getCommonCSS();
    h += "</head><body>";
    h += getNav();
    h += "<h2>System Event Log</h2><table><tr><th>DATE</th><th>TIME</th><th>EVENT</th><th>DATA</th></tr>";
    for (int i = lastLogs.size() - 1; i >= 0; i--) {
        String l = lastLogs[i];
        int s1 = l.indexOf('|'), s2 = l.indexOf('|', s1+1), s3 = l.indexOf('|', s2+1);
        if (s1 > 0 && s2 > 0 && s3 > 0) {
            String date = l.substring(0, s1), time = l.substring(s1+1, s2), type = l.substring(s2+1, s3), data = l.substring(s3+1);
            h += "<tr><td>"+date+"</td><td>"+time+"</td><td><span class='tag "+type+"'>"+type+"</span></td><td>"+data+"</td></tr>";
        }
    }
    h += "</table><br><br></body></html>";
    _server->send(200, "text/html", h);
}

void NetworkService::_handleConfig() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    h += getCommonCSS();
    h += "</head><body>";
    h += getNav();
    h += "<h1>Network Settings</h1><form action='/save' method='POST' style='display:inline-block;text-align:left;'>";
    h += "WiFi SSID:<br><input type='text' name='ssid' style='width:250px;padding:8px;background:#222;color:#fff;border:1px solid #444;'><br><br>";
    h += "WiFi PASS:<br><input type='password' name='pass' style='width:250px;padding:8px;background:#222;color:#fff;border:1px solid #444;'><br><br>";
    h += "GMT Offset:<br><input type='number' name='gmt' value='"+String(_gmtOffset)+"' style='width:250px;padding:8px;background:#222;color:#fff;border:1px solid #444;'><br><br>";
    h += "<input type='submit' value='SAVE & APPLY' style='width:250px;background:#00ffcc;padding:12px;border:none;border-radius:5px;font-weight:bold;cursor:pointer;'>";
    h += "</form></body></html>";
    _server->send(200, "text/html", h);
}

String NetworkService::getUptimeString() {
    unsigned long totalSec = (_historicalUptime + (millis() - _startTime)) / 1000;
    char buffer[16]; sprintf(buffer, "%02luh %02lum %02lus", totalSec/3600, (totalSec%3600)/60, totalSec%60);
    return String(buffer);
}

void NetworkService::_performPing() {
    static bool toggle = false; IPAddress gateway = WiFi.gatewayIP();
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
