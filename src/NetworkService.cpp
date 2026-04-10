#include "NetworkService.h"
#include <time.h>
#include <Preferences.h>

Preferences preferences;

void NetworkService::begin(const char* ssid, const char* pass) {
    if (WiFi.status() == WL_CONNECTED) return;
    
    // NVS Persistence: Recuperar contador de reconexiones históricas
    preferences.begin("net_stats", false);
    _reconnectCount = preferences.getInt("recon", 0);
    
    _startTime = millis();
    
    // PHY Layer: Inicio de negociación WiFi 6
    WiFi.begin(ssid, pass);
    
    // NTP Synchronization: Configuración de marca de tiempo para logs
    configTime(0, 0, "pool.ntp.org");
    logEvent("SYSTEM_START", "NetworkService Initialized");
}

void NetworkService::update() {
    // State Machine: Gestión de reconexión con Exponential Backoff
    if (WiFi.status() != WL_CONNECTED) {
        if (millis() - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = millis();
            _reconnectInterval = min(_reconnectInterval * 2, _maxReconnectInterval);
            WiFi.disconnect();
            WiFi.begin();
            _reconnectCount++;
            preferences.putInt("recon", _reconnectCount);
            logEvent("LINK_DOWN", "Attempting Reconnection");
        }
    } else {
        // Reset Backoff upon successful handshake
        if (_reconnectInterval != 10000) {
            logEvent("RECOVERY", "Connection Re-established");
            _reconnectInterval = 10000;
        }

        // Diagnostics Cycle: Ejecución temporizada del Ping Dual
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
    
    // Serial Logging: Formato estándar para auditoria técnica
    Serial.printf("[%02d:%02d:%02d] EVENT: %s | %s\n", 
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, type, data);
}

String NetworkService::getUptimeString() {
    unsigned long sec = (millis() - _startTime) / 1000;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    char buffer[12];
    sprintf(buffer, "%02dh %02dm %02ds", h, m, s);
    return String(buffer);
}

void NetworkService::_performPing() {
    static bool toggle = false;
    IPAddress gateway = WiFi.gatewayIP();
    
    if (!toggle) {
        // Ciclo A: Ping al Gateway (Local)
        _lastPingGW = Ping.ping(gateway) ? Ping.averageTime() : -1;
    } else {
        // Ciclo B: Ping a Internet (Google)
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
    // Cálculo de Disconnect Rate: Desconexiones por hora
    float uptimeHours = (millis() - _startTime) / 3600000.0;
    if (uptimeHours < 0.01) return 0.0; // Evitar división por cero o datos prematuros
    return (float)_reconnectCount / uptimeHours;
}

bool NetworkService::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}
