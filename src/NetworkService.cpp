#include "NetworkService.h"
#include <time.h>

NetworkService::NetworkService() {}

void NetworkService::begin(const char* ssid, const char* pass) {
    Serial.println("\n[PHASING] Step 0: Minimal Core Start...");
    
    // Almacenar credenciales por defecto (con prevención de NVS crash)
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

    // FASE 1: NVS & Stats (T+10s)
    if (_bootPhase == 0 && uptime > 10000) {
        _fsReady = LittleFS.begin(true);
        _prefs.begin("net_stats", false);
        _historicalReconnects = _prefs.getInt("t_recon", 0);
        _historicalUptime = _prefs.getULong("t_uptime", 0);
        _reconnectCount = _prefs.getInt("recon", 0);
        _gmtOffset = _prefs.getInt("gmt", -5); // Default Lima
        _prefs.end();
        _bootPhase = 1;
        logEvent("SYS_PHASE", "NVS Services Ready");
    }

    // FASE 2: Servidores (T+15s)
    if (_bootPhase == 1 && uptime > 15000) {
        if (!_server) _server = new WebServer(80);
        if (!_dnsServer) _dnsServer = new DNSServer();
        _setupWebServer();
        _bootPhase = 2;
        logEvent("SYS_PHASE", "Web Panel Active");
    }

    // FASE 3: NTP con GMT Dinámico (T+20s)
    if (_bootPhase == 2 && uptime > 20000) {
        char buf[32]; sprintf(buf, "Syncing NTP (GMT %d)", _gmtOffset);
        logEvent("SYS_TIME", buf);
        configTime(_gmtOffset * 3600, 0, "pool.ntp.org");
        _bootPhase = 3;
    }

    // --- OPERACION ---
    if (_server) _server->handleClient();
    bool connected = (WiFi.status() == WL_CONNECTED);
    if (_isConfigMode && _dnsServer) _dnsServer->processNextRequest();

    if (connected) {
        _lastConnectedTime = millis();
        if (_isConfigMode) { WiFi.softAPdisconnect(true); _isConfigMode = false; }
        if (_bootPhase >= 1 && (millis() - _lastPingTime > 5000)) { 
            _lastPingTime = millis(); _performPing(); 
        }
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
            logEvent("SYS_CRITICAL", "Watchdog Reset");
            delay(1000); ESP.restart();
        }
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    char timeStr[25] = "BOOTING"; 
    time_t now; time(&now);
    if (now > 1000000) {
        struct tm timeinfo; gmtime_r(&now, &timeinfo);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    char fullMsg[128]; sprintf(fullMsg, "[%s] EVENT: %s | %s", timeStr, type, data);
    Serial.println(fullMsg);
    if (_fsReady) {
        File f = LittleFS.open("/log.txt", FILE_APPEND);
        if (f) { f.println(fullMsg); f.close(); }
    }
}

void NetworkService::_setupWebServer() {
    if (!_server) return;
    _server->on("/", [this]() { _handleRoot(); });
    _server->on("/logs", [this]() { _handleLogs(); });
    _server->on("/config", [this]() { _handleConfig(); });
    _server->on("/save", HTTP_POST, [this]() {
        String s = _server->arg("ssid");
        String p = _server->arg("pass");
        String g = _server->arg("gmt");
        if (s.length() > 0) {
            _prefs.begin("net_stats", false);
            _prefs.putString("w_ssid", s);
            _prefs.putString("w_pass", p);
            _prefs.putInt("gmt", g.toInt());
            _prefs.end();
            _server->send(200, "text/html", "<b>Settings Saved</b>. Rebooting...");
            delay(1000); ESP.restart();
        }
    });
    _server->onNotFound([this]() {
        if (_isConfigMode) { _server->sendHeader("Location", "/config", true); _server->send(302, "text/plain", ""); }
        else { _server->send(404, "text/plain", "Not Found"); }
    });
    _server->begin();
}

void NetworkService::_handleRoot() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    h += "<style>body{background:#0d0d0d;color:#eee;text-align:center;font-family:sans-serif;padding:20px;}";
    h += ".card{background:#1a1a1a;padding:15px;border-radius:10px;border:1px solid #00ffcc;margin:10px;}";
    h += "h1{color:#00ffcc;}</style></head><body><h1>WiFi Monitor Pro</h1>";
    h += "<div class='card'><div>UPTIME</div><div style='font-size:2em; font-weight:bold;'>" + getUptimeString() + "</div></div>";
    h += "<div class='card'><div>ZONE (GMT)</div><div style='font-size:1.5em;'>" + String(_gmtOffset) + "</div></div>";
    h += "<br><a href='/logs' style='color:#00ffcc'>View Logs</a> | <a href='/config' style='color:#00ffcc'>Settings</a></body></html>";
    _server->send(200, "text/html", h);
}

void NetworkService::_handleLogs() {
    if (!_fsReady) { _server->send(500, "text/plain", "No FS"); return; }
    File f = LittleFS.open("/log.txt", FILE_READ);
    if (!f) { _server->send(200, "text/plain", "Empty"); return; }
    _server->streamFile(f, "text/plain"); f.close();
}

void NetworkService::_handleConfig() {
    String h = "<html><body style='background:#111;color:#fff;text-align:center;'><h1>Configuration</h1>";
    h += "<form action='/save' method='POST' style='display:inline-block; text-align:left;'>";
    h += "WiFi SSID:<br><input type='text' name='ssid' style='width:200px;'><br><br>";
    h += "WiFi PASS:<br><input type='password' name='pass' style='width:200px;'><br><br>";
    h += "GMT Offset (Peru is -5):<br><input type='number' name='gmt' value='"+String(_gmtOffset)+"' style='width:200px;'><br><br>";
    h += "<input type='submit' value='SAVE & REBOOT' style='background:#00ffcc; padding:10px 20px; border:none; border-radius:5px; font-weight:bold;'>";
    h += "</form></body></html>";
    _server->send(200, "text/html", h);
}

String NetworkService::getUptimeString() {
    unsigned long sec = (_historicalUptime + (millis() - _startTime)) / 1000;
    char b[16]; sprintf(b, "%02luh %02lum %02lus", sec/3600, (sec%3600)/60, sec%60);
    return String(b);
}

void NetworkService::_performPing() {
    static bool t = false; IPAddress gw = WiFi.gatewayIP();
    if (!t) { _lastPingGW = Ping.ping(gw) ? Ping.averageTime() : -1; }
    else { _lastPingInternet = Ping.ping("8.8.8.8") ? Ping.averageTime() : -1; }
    t = !t;
}

NetworkService::NetworkData NetworkService::getData() {
    NetworkData d; d.connected = (WiFi.status() == WL_CONNECTED);
    if (d.connected) {
        d.rssi = WiFi.RSSI(); d.ip = WiFi.localIP().toString();
        d.channel = WiFi.channel(); d.pingGW = _lastPingGW; d.pingInternet = _lastPingInternet;
    } else {
        d.rssi = -100; d.ip = "0.0.0.0"; d.channel = 0; d.pingGW = -1; d.pingInternet = -1;
    }
    return d;
}

int NetworkService::getReconnectCount() { return _reconnectCount; }
float NetworkService::getDisconnectRate() {
    float h = (_historicalUptime + (millis() - _startTime)) / 3600000.0;
    return (h < 0.01) ? 0.0 : (float)(_historicalReconnects + _reconnectCount) / h;
}
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
