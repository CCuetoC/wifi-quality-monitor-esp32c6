#include "NetworkService.h"

NetworkService::NetworkService() {}

void NetworkService::begin(const char* ssid, const char* pass) {
    // 0. Reset de estado para evitar fantasmas de memoria
    Serial.begin(115200);
    delay(100);
    Serial.println("\n[SAFE_START] Step 1: Basic WiFi Init...");
    Serial.flush();

    // 1. Solo WiFi - Sin extras
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    
    _startTime = millis();
    _lastConnectedTime = millis();
    
    Serial.println("[SAFE_START] WiFi command issued. Waiting for loop...");
    Serial.flush();
}

void NetworkService::update() {
    // 2. Nacimiento Diferido (Solo después de 10 segundos de estabilidad)
    if (millis() - _startTime > 10000 && _server == nullptr) {
        Serial.println("[SAFE_START] Step 2: Delayed Web Server Init...");
        Serial.flush();
        _server = new WebServer(80);
        _setupWebServer();
        Serial.println("[SAFE_START] Web Server Ready.");
        Serial.flush();
    }

    if (_server) _server->handleClient();
    
    bool connected = (WiFi.status() == WL_CONNECTED);
    
    // Auto-reconexión simple
    if (!connected && (millis() - _lastReconnectAttempt > 10000)) {
        _lastReconnectAttempt = millis();
        WiFi.begin();
        _reconnectCount++;
    }

    // Ping espaciado
    if (connected && (millis() - _lastPingTime > 5000)) {
        _lastPingTime = millis();
        _performPing();
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    Serial.print("LOG: ["); Serial.print(type); Serial.print("] ");
    Serial.println(data);
    Serial.flush();
}

void NetworkService::_setupWebServer() {
    if (!_server) return;
    _server->on("/", [this]() {
        String h = "<h1>WiFi Monitor - Stable Boot</h1><p>Uptime: " + String(millis()/1000) + "s</p>";
        _server->send(200, "text/html", h);
    });
    _server->begin();
}

void NetworkService::_handleRoot() {}
void NetworkService::_handleLogs() {}
void NetworkService::_handleConfig() {}

String NetworkService::getUptimeString() {
    unsigned long s = millis() / 1000;
    char b[16]; sprintf(b, "%02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);
    return String(b);
}

void NetworkService::_performPing() {
    IPAddress gw = WiFi.gatewayIP();
    _lastPingGW = Ping.ping(gw) ? Ping.averageTime() : -1;
}

NetworkService::NetworkData NetworkService::getData() {
    NetworkData d; d.connected = (WiFi.status() == WL_CONNECTED);
    d.rssi = d.connected ? WiFi.RSSI() : -100;
    d.ip = d.connected ? WiFi.localIP().toString() : "0.0.0.0";
    d.pingGW = _lastPingGW; d.pingInternet = -1;
    return d;
}

int NetworkService::getReconnectCount() { return _reconnectCount; }
float NetworkService::getDisconnectRate() { return 0.0; }
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
