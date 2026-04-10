#include "NetworkService.h"
#include <time.h>

NetworkService::NetworkService() {}

void NetworkService::begin(const char* ssid, const char* pass) {
    // FASE 0: Solo lo vital para el booteo instantáneo
    Serial.println("\n[PHASING] Step 0: Minimal Core Start...");
    
    // Almacenar credenciales por defecto (se sobreescriben en Fase 1 si hay NVS)
    _prefs.begin("net_stats", true); // Solo lectura inicial
    String savedSSID = _prefs.getString("w_ssid", ssid);
    String savedPASS = _prefs.getString("w_pass", pass);
    _prefs.end();

    WiFi.mode(WIFI_STA);
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    
    _startTime = millis();
    _lastConnectedTime = millis();
    _bootPhase = 0;
    
    Serial.println("[PHASING] Core alive. Dashboard should be visible.");
}

void NetworkService::update() {
    unsigned long uptime = millis() - _startTime;

    // FASE 1: Inicialización de Archivos y NVS (T+10s)
    if (_bootPhase == 0 && uptime > 10000) {
        Serial.println("[PHASING] Step 1: FileSystem & Stats...");
        _fsReady = LittleFS.begin(true);
        
        _prefs.begin("net_stats", false);
        _historicalReconnects = _prefs.getInt("t_recon", 0);
        _historicalUptime = _prefs.getULong("t_uptime", 0);
        _reconnectCount = _prefs.getInt("recon", 0);
        _prefs.end();
        
        _bootPhase = 1;
        logEvent("SYS_PHASE", "FileSystem & Stats Ready");
    }

    // FASE 2: Servidores Web y DNS (T+15s)
    if (_bootPhase == 1 && uptime > 15000) {
        Serial.println("[PHASING] Step 2: Web & DNS Servers...");
        if (!_server) _server = new WebServer(80);
        if (!_dnsServer) _dnsServer = new DNSServer();
        _setupWebServer();
        _bootPhase = 2;
        logEvent("SYS_PHASE", "Web Interface Ready");
    }

    // FASE 3: NTP & Time Sync (T+20s)
    if (_bootPhase == 2 && uptime > 20000) {
        Serial.println("[PHASING] Step 3: NTP Sync...");
        configTime(0, 0, "pool.ntp.org");
        _bootPhase = 3;
        logEvent("SYS_PHASE", "All Systems Operational");
    }

    // --- OPERACION NORMAL ---
    if (_server) _server->handleClient();
    
    bool connected = (WiFi.status() == WL_CONNECTED);
    
    if (_isConfigMode && _dnsServer) {
        _dnsServer->processNextRequest();
    }

    if (connected) {
        _lastConnectedTime = millis();
        if (_isConfigMode) {
            WiFi.softAPdisconnect(true);
            _isConfigMode = false;
            logEvent("SYS_MODE", "Portal Closed");
        }
        
        // Pings solo tras Fase 1
        if (_bootPhase >= 1 && (millis() - _lastPingTime > 5000)) {
            _lastPingTime = millis();
            _performPing();
        }

        // Persistencia Uptime
        if (_bootPhase >= 1 && (millis() - _lastSaveTime > _saveInterval)) {
            _historicalUptime += (millis() - _lastSaveTime);
            _lastSaveTime = millis();
            _prefs.begin("net_stats", false);
            _prefs.putULong("t_uptime", _historicalUptime);
            _prefs.end();
        }
    } else {
        // Portal Cautivo si no conecta en 30s
        if (uptime > 30000 && !connected && !_isConfigMode && _bootPhase >= 2) {
            _isConfigMode = true;
            WiFi.softAP("WiFi-Monitor-C6");
            if (_dnsServer) _dnsServer->start(53, "*", WiFi.softAPIP());
            logEvent("SYS_MODE", "AP Active: 192.168.4.1");
        }

        // Reconexión con Backoff
        if (millis() - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = millis();
            _reconnectInterval = (unsigned long)min((int)_reconnectInterval * 2, (int)_maxReconnectInterval);
            WiFi.disconnect();
            WiFi.begin();
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
            delay(1000);
            ESP.restart();
        }
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    char timeStr[25] = "2026-00-00 00:00:00"; 
    time_t now;
    time(&now);
    if (now > 1000000) {
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    }
    
    char fullMsg[128];
    sprintf(fullMsg, "[%s] EVENT: %s | %s", timeStr, type, data);
    Serial.println(fullMsg);
    
    if (_fsReady) {
        File file = LittleFS.open("/log.txt", FILE_APPEND);
        if (file) {
            file.println(fullMsg);
            file.close();
        }
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
        if (s.length() > 0) {
            _prefs.begin("net_stats", false);
            _prefs.putString("w_ssid", s);
            _prefs.putString("w_pass", p);
            _prefs.end();
            _server->send(200, "text/html", "<b>Config OK</b>. Rebooting...");
            delay(1000);
            ESP.restart();
        }
    });
    _server->onNotFound([this]() {
        if (_isConfigMode) { _server->sendHeader("Location", "/config", true); _server->send(302, "text/plain", ""); }
        else { _server->send(404, "text/plain", "Not Found"); }
    });
    _server->begin();
}

void NetworkService::_handleRoot() {
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>WiFi Monitor Pro</title><style>body{font-family:sans-serif;background:#0d0d0d;color:#eee;text-align:center;padding:10px;}";
    html += ".card{background:#1a1a1a;padding:15px;border-radius:10px;border:1px solid #00ffcc;margin:10px;}";
    html += "h1{color:#00ffcc;font-size:1.5em;} .val{font-size:2em;color:#fff;font-weight:bold;}</style></head><body>";
    html += "<h1>Industrial WiFi Monitor</h1>";
    html += "<div class='card'><div>UPTIME</div><div class='val'>" + getUptimeString() + "</div></div>";
    html += "<br><a href='/logs' style='color:#00ffcc'>View System Logs</a> | <a href='/config' style='color:#00ffcc'>Settings</a>";
    html += "</body></html>";
    _server->send(200, "text/html", html);
}

void NetworkService::_handleLogs() {
    if (!_fsReady) { _server->send(500, "text/plain", "FS Error"); return; }
    File file = LittleFS.open("/log.txt", FILE_READ);
    if (!file) { _server->send(200, "text/plain", "Log Empty"); return; }
    _server->streamFile(file, "text/plain");
    file.close();
}

void NetworkService::_handleConfig() {
    String html = "<h1>Network Config</h1><form action='/save' method='POST'>";
    html += "SSID: <input type='text' name='ssid'><br>PASS: <input type='password' name='pass'><br><input type='submit' value='SAVE'></form>";
    _server->send(200, "text/html", html);
}

String NetworkService::getUptimeString() {
    unsigned long totalSec = (_historicalUptime + (millis() - _startTime)) / 1000;
    char buffer[16];
    sprintf(buffer, "%02luh %02lum %02lus", totalSec/3600, (totalSec%3600)/60, totalSec%60);
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
    NetworkData data;
    data.connected = (WiFi.status() == WL_CONNECTED);
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
