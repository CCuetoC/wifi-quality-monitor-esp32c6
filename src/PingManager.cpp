#include "PingManager.h"

void PingManager::performPings() {
    if (WiFi.status() != WL_CONNECTED) {
        _lastPingGW = -1;
        _lastPingInternet = -1;
        return;
    }

    // Ventana Deslizante v7.5.0: Reset cada 200 ciclos (800 paquetes total)
    if (_totalPings > (WINDOW_SIZE * 4)) {
        Serial.println("[NET] Resetting Ping Sliding Window to maintain reactivity");
        _totalPings = 0;
        _lostPings = 0;
    }

    IPAddress gw = WiFi.gatewayIP();
    _totalPings += 4; // 2 Gateway + 2 Internet

    // Ping Gateway (2 paquetes)
    bool gwOk = Ping.ping(gw, 2);
    _lastPingGW = gwOk ? Ping.averageTime() : -1;
    if (!gwOk) _lostPings += 2;
    
    yield();
    
    // Ping Google (2 paquetes)
    bool extOk = Ping.ping("www.google.com", 2);
    _lastPingInternet = extOk ? Ping.averageTime() : -1;
    if (!extOk) _lostPings += 2;
}

PingResult PingManager::getResults() {
    float loss = 0.0f;
    if (_totalPings > 0) {
        loss = (_lostPings * 100.0f) / _totalPings;
    }

    return {
        _lastPingGW,
        _lastPingInternet,
        loss,
        WiFi.status() == WL_CONNECTED
    };
}
