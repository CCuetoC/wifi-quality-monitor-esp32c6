#include "NetworkService.h"
#include <time.h>

void NetworkService::begin(const char* ssid, const char* pass) {
    // 1. Inicializar Sistema de Archivos para Logs
    if (!LittleFS.begin(true)) {
        Serial.println("LittleFS Mount Failed");
    }

    // 2. Recuperar Datos Históricos
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
    
    char logBuf[64];
    sprintf(logBuf, "Recovered CPU Uptime: %lu ms | Recon: %d", _historicalUptime, _historicalReconnects);
    logEvent("SYS_LOAD", logBuf);
    
    // 3. Configurar Servidor Web ANTES de WiFi para asegurar que esté listo
    _setupWebServer();
    
    // 4. Iniciar Conexión
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str());
    configTime(0, 0, "pool.ntp.org");
    
    logEvent("SYSTEM_START", "NetworkService Initialized");
}

void NetworkService::update() {
    // Prioridad Máxima: Atender peticiones web
    _server.handleClient();
    
    bool connected = (WiFi.status() == WL_CONNECTED);
    
    if (_isConfigMode) {
        _dnsServer.processNextRequest();
    }

    // Auto-Recovery: Watchdog de Red
    if (connected) {
        _lastConnectedTime = millis();
        if (_isConfigMode) {
            WiFi.softAPdisconnect(true);
            _isConfigMode = false;
            logEvent("SYS_MODE", "Switching back to STA Mode");
        }
    } else {
        // Si no hay conexión tras 30s, activar modo AP/Config
        if (millis() - _startTime > 30000 && !connected && !_isConfigMode) {
            _isConfigMode = true;
            WiFi.softAP("WiFi-Monitor-C6");
            _dnsServer.start(53, "*", WiFi.softAPIP());
            logEvent("SYS_MODE", "Config Portal Active: 192.168.4.1");
        }

        if (millis() - _lastConnectedTime > _maxDisconnectTime) {
            logEvent("SYS_CRITICAL", "Link Lost > 15m. Rebooting for stability...");
            delay(1000);
            ESP.restart();
        }
    }

    // Persistencia NVS
    if (millis() - _lastSaveTime > _saveInterval) {
        _historicalUptime += (millis() - _lastSaveTime);
        _lastSaveTime = millis();
        _prefs.begin("net_stats", false);
        _prefs.putULong("t_uptime", _historicalUptime);
        _prefs.end();
        logEvent("SYS_MAINT", "Stats persisted to NVS");
    }

    // Lógica de Reconexión & Pings (Solo si no estamos en modo AP puro de falla)
    if (!connected) {
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
    } else {
        if (_reconnectInterval != 10000) _reconnectInterval = 10000;
        
        // Pings espaciados para no saturar el WebServer
        if (millis() - _lastPingTime > _pingInterval) {
            _lastPingTime = millis();
            _performPing(); 
        }
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    time_t now;
    time(&now);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char timeStr[25];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
    
    char fullMsg[128];
    sprintf(fullMsg, "[%s] EVENT: %s | %s", timeStr, type, data);
    Serial.println(fullMsg);
    
    File file = LittleFS.open("/log.txt", FILE_APPEND);
    if (file) {
        file.println(fullMsg);
        file.close();
    }
}

void NetworkService::_setupWebServer() {
    _server.on("/", [this]() { _handleRoot(); });
    _server.on("/logs", [this]() { _handleLogs(); });
    _server.on("/config", [this]() { _handleConfig(); });
    
    _server.on("/save", HTTP_POST, [this]() {
        String s = _server.arg("ssid");
        String p = _server.arg("pass");
        if (s.length() > 0) {
            _prefs.begin("net_stats", false);
            _prefs.putString("w_ssid", s);
            _prefs.putString("w_pass", p);
            _prefs.end();
            _server.send(200, "text/html", "<b>Config OK.</b> Rebooting...");
            delay(1000);
            ESP.restart();
        }
    });

    _server.onNotFound([this]() {
        if (_isConfigMode) {
            _server.sendHeader("Location", "/config", true);
            _server.send(302, "text/plain", "");
        } else {
            _server.send(404, "text/plain", "Not Found");
        }
    });
    
    _server.begin();
    logEvent("WEB_READY", "HTTP Server listening on Port 80");
}

void NetworkService::_handleRoot() {
    String html = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>WiFi Monitor</title><style>body{font-family:sans-serif;background:#111;color:#eee;text-align:center;padding:20px;}";
    html += ".card{background:#222;padding:20px;border-radius:15px;border:1px solid #00ffcc;display:inline-block;margin:10px;min-width:220px;}";
    html += "h1{color:#00ffcc;} .val{font-size:2.5em;font-weight:bold;color:#fff;} a{color:#00ffcc;text-decoration:none;font-weight:bold;}</style></head><body>";
    html += "<h1>Industrial WiFi Monitor</h1>";
    html += "<div class='card'><div>UPTIME ACUMULADO</div><div class='val'>" + getUptimeString() + "</div></div>";
    html += "<div class='card'><div>CONEXION</div><div class='val'>" + String(isConnected()?"ONLINE":"OFFLINE") + "</div></div>";
    html += "<br><br><a href='/logs'>[ VER HISTORIAL DE LOGS ]</a><br><br><a href='/config'>[ RECONFIGURAR RED ]</a>";
    html += "</body></html>";
    _server.send(200, "text/html", html);
}

void NetworkService::_handleLogs() {
    File file = LittleFS.open("/log.txt", FILE_READ);
    if (!file) {
        _server.send(500, "text/plain", "Logs not available");
        return;
    }
    _server.streamFile(file, "text/plain");
    file.close();
}

void NetworkService::_handleConfig() {
    String html = "<html><head><meta name='viewport' content='width=device-width, initial-scale=1'><style>body{font-family:sans-serif;text-align:center;padding:20px;}</style></head><body>";
    html += "<h1>Network Config</h1><form action='/save' method='POST'>";
    html += "SSID:<br><input type='text' name='ssid' style='width:80%;'><br><br>";
    html += "PASSWORD:<br><input type='password' name='pass' style='width:80%;'><br><br>";
    html += "<input type='submit' value='SAVE & CONNECT' style='background:#00ffcc;padding:10px 20px;border:none;border-radius:5px;cursor:pointer;'>";
    html += "</form></body></html>";
    _server.send(200, "text/html", html);
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
    if (!toggle) {
        _lastPingGW = Ping.ping(gateway) ? Ping.averageTime() : -1;
    } else {
        _lastPingInternet = Ping.ping("8.8.8.8") ? Ping.averageTime() : -1;
    }
    toggle = !toggle;
}

NetworkService::NetworkData NetworkService::getData() {
    NetworkData data;
    data.connected = (WiFi.status() == WL_CONNECTED);
    if (data.connected) {
        data.rssi = WiFi.RSSI();
        data.ip = WiFi.localIP().toString();
        data.channel = WiFi.channel();
        data.pingGW = _lastPingGW;
        data.pingInternet = _lastPingInternet;
    } else {
        data.rssi = -100;
        data.ip = "0.0.0.0";
        data.channel = 0;
        data.pingGW = -1;
        data.pingInternet = -1;
    }
    return data;
}

int NetworkService::getReconnectCount() {
    return _reconnectCount;
}

float NetworkService::getDisconnectRate() {
    float totalHours = (_historicalUptime + (millis() - _startTime)) / 3600000.0;
    if (totalHours < 0.01) return 0.0;
    return (float)(_historicalReconnects + _reconnectCount) / totalHours;
}

bool NetworkService::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}
