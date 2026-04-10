#include "NetworkService.h"
#include <time.h>

void NetworkService::begin(const char* ssid, const char* pass) {
    if (WiFi.status() == WL_CONNECTED) return;
    
    // NVS Persistence: Recuperar datos históricos acumulados
    _prefs.begin("net_stats", false);
    _historicalReconnects = _prefs.getInt("t_recon", 0);
    _historicalUptime = _prefs.getULong("t_uptime", 0);
    
    // Cargar reconexiones de la sesión actual (o última conocida)
    _reconnectCount = _prefs.getInt("recon", 0);
    _prefs.end();
    
    _startTime = millis();
    _lastSaveTime = millis();
    _lastConnectedTime = millis();
    
    WiFi.begin(ssid, pass);
    configTime(0, 0, "pool.ntp.org");
    logEvent("SYSTEM_START", "NetworkService Initialized");
}

void NetworkService::update() {
    bool connected = (WiFi.status() == WL_CONNECTED);

    // Auto-Recovery: Watchdog de Red (Hard Reset tras 15m offline)
    if (connected) {
        _lastConnectedTime = millis();
    } else {
        if (millis() - _lastConnectedTime > _maxDisconnectTime) {
            logEvent("SYS_CRITICAL", "Reconnection Failed > 15m. Resetting Hardware...");
            delay(1000); // Dar tiempo al Serial/Logs
            ESP.restart();
        }
    }

    // NVS Persistence: Guardado periódico de telemetría (Wear Leveling)
    if (millis() - _lastSaveTime > _saveInterval) {
        unsigned long sessionUptime = millis() - _lastSaveTime;
        _historicalUptime += sessionUptime;
        _lastSaveTime = millis();
        
        _prefs.begin("net_stats", false);
        _prefs.putULong("t_uptime", _historicalUptime);
        _prefs.end();
        logEvent("SYS_MAINT", "Historical Uptime Persisted");
    }

    // State Machine: Gestión de reconexión con Exponential Backoff
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
            logEvent("LINK_DOWN", "Attempting Reconnection");
        }
    } else {
        if (_reconnectInterval != 10000) {
            logEvent("RECOVERY", "Connection Re-established");
            _reconnectInterval = 10000;
        }

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
    Serial.printf("[%02d:%02d:%02d] EVENT: %s | %s\n", 
                  timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, type, data);
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
    // Cálculo basado en historial acumulado
    float totalHours = (_historicalUptime + (millis() - _startTime)) / 3600000.0;
    if (totalHours < 0.01) return 0.0;
    return (float)(_historicalReconnects + _reconnectCount) / totalHours;
}

bool NetworkService::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}
