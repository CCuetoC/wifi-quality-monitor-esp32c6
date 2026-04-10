#include "NetworkService.h"
#include <time.h>

NetworkService::NetworkService() {}

void NetworkService::begin(const char* ssid, const char* pass) {
    Serial.println("\n[SYSTEM] Initializing NetworkService (Secure Boot)...");
    
    // 1. Inicialización Dinámica de Servidores (Solución al crash de booteo)
    if (_server == nullptr) _server = new WebServer(80);
    if (_dnsServer == nullptr) _dnsServer = new DNSServer();

    // 2. Sistema de Archivos con validación manual
    _fsReady = LittleFS.begin(true);
    if (!_fsReady) {
        Serial.println("[SYSTEM] LittleFS Fail. File logging disabled.");
    }

    // 3. Recuperar Credenciales y Estadísticas
    _prefs.begin("net_stats", false);
    _historicalReconnects = _prefs.getInt("t_recon", 0);
    _historicalUptime = _prefs.getULong("t_uptime", 0);
    _reconnectCount = _prefs.getInt("recon", 0);
    String savedSSID = _prefs.getString("w_ssid", ssid);
    String savedPASS = _prefs.getString("w_pass", pass);
    _prefs.end();
    
    _startTime = millis();
    _lastSaveTime = millis();
    _lastConnectedTime = millis();
    
    // 4. Configurar Rutas Web
    _setupWebServer();
    
    // 5. Iniciar WiFi
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    configTime(0, 0, "pool.ntp.org");
    
    logEvent("SYSTEM_START", "NetworkService Stable");
}

void NetworkService::update() {
    if (_server) _server->handleClient();
    
    bool connected = (WiFi.status() == WL_CONNECTED);
    
    if (_isConfigMode && _dnsServer) {
        _dnsServer->processNextRequest();
    }

    // Resiliencia
    if (connected) {
        _lastConnectedTime = millis();
        if (_isConfigMode) {
            WiFi.softAPdisconnect(true);
            _isConfigMode = false;
        }
    } else {
        if (millis() - _startTime > 30000 && !connected && !_isConfigMode) {
            _isConfigMode = true;
            WiFi.softAP("WiFi-Monitor-C6");
            if (_dnsServer) _dnsServer->start(53, "*", WiFi.softAPIP());
            logEvent("SYS_MODE", "Portal 192.168.4.1 Active");
        }

        if (millis() - _lastConnectedTime > _maxDisconnectTime) {
            logEvent("SYS_CRITICAL", "Watchdog Reset");
            delay(1000);
            ESP.restart();
        }
    }

    // NVS & Reconexión
    if (connected) {
        if (millis() - _lastSaveTime > _saveInterval) {
            _historicalUptime += (millis() - _lastSaveTime);
            _lastSaveTime = millis();
            _prefs.begin("net_stats", false);
            _prefs.putULong("t_uptime", _historicalUptime);
            _prefs.end();
        }

        if (millis() - _lastPingTime > _pingInterval) {
            _lastPingTime = millis();
            _performPing(); 
        }
    } else {
        if (millis() - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = millis();
            _reconnectInterval = min(_reconnectInterval * 2, _maxReconnectInterval);
            WiFi.disconnect();
            WiFi.begin();
            _reconnectCount++;
            
            _prefs.begin("net_stats", false);
            _prefs.putInt("recon", _reconnectCount);
            _prefs.putInt("t_recon", _historicalReconnects + _reconnectCount);
            _prefs.end();
        }
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    char timeStr[25] = "[NoTime]";
    time_t now;
    if (time(&now) > 1000000000) { // Solo si el tiempo es válido
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
            _server->send(200, "text/html", "<b>Config OK.</b> Rebooting...");
            delay(1000);
            ESP.restart();
        }
    });

    _server->onNotFound([this]() {
        if (_isConfigMode) {
            _server->sendHeader("Location", "/config", true);
            _server->send(302, "text/plain", "");
        } else {
            _server->send(404, "text/plain", "Not Found");
        }
    });
    
    _server->begin();
}

void NetworkService::_handleRoot() {
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Monitor WiFi</title><style>body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding:20px;}";
    html += ".card{background:#222;padding:20px;border-radius:15px;border:1px solid #00ffcc;display:inline-block;margin:10px;min-width:220px;}";
    html += "h1{color:#00ffcc;} .val{font-size:2.5em;font-weight:bold;color:#fff;} a{color:#00ffcc;text-decoration:none;}</style></head><body>";
    html += "<h1>Industrial Monitor C6</h1>";
    html += "<div class='card'><div>UPTIME</div><div class='val'>" + getUptimeString() + "</div></div>";
    html += "<div class='card'><div>CONEXION</div><div class='val'>" + String(isConnected()?"ONLINE":"OFFLINE") + "</div></div>";
    html += "<br><br><a href='/logs'>[ LOGS ]</a> | <a href='/config'>[ CONFIG ]</a>";
    html += "</body></html>";
    _server->send(200, "text/html", html);
}

void NetworkService::_handleLogs() {
    if (!_fsReady) { _server->send(500, "text/plain", "No FS"); return; }
    File file = LittleFS.open("/log.txt", FILE_READ);
    if (!file) { _server->send(200, "text/plain", "Empty"); return; }
    _server->streamFile(file, "text/plain");
    file.close();
}

void NetworkService::_handleConfig() {
    String html = "<html><body style='background:#111;color:#fff;text-align:center;'>";
    html += "<h1>Config</h1><form action='/save' method='POST'>";
    html += "SSID:<br><input type='text' name='ssid'><br><br>";
    html += "PASS:<br><input type='password' name='pass'><br><br>";
    html += "<input type='submit' value='SAVE'>";
    html += "</form></body></html>";
    _server->send(200, "text/html", html);
}

String NetworkService::getUptimeString() {
    unsigned long totalSec = (_historicalUptime + (millis() - _startTime)) / 1000;
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    char buffer[16];
    sprintf(buffer, "%02dh %02dm %02ds", h, m, s);
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
    float hours = (_historicalUptime + (millis() - _startTime)) / 3600000.0;
    return (hours < 0.01) ? 0.0 : (float)(_historicalReconnects + _reconnectCount) / hours;
}
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
