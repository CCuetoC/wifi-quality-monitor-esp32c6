#include "PingManager.h"

void PingManager::performPings() {
    if (WiFi.status() != WL_CONNECTED) {
        _lastPingGW = -1;
        _lastPingInternet = -1;
        return;
    }

    IPAddress gw = WiFi.gatewayIP();
    
    // Ping Gateway
    bool gwOk = Ping.ping(gw, 2);
    _lastPingGW = gwOk ? Ping.averageTime() : -1;
    
    yield(); // Permitir tareas del sistema
    
    // Ping Google (Hostname para veracidad absoluta)
    bool extOk = Ping.ping("www.google.com", 2);
    _lastPingInternet = extOk ? Ping.averageTime() : -1;
}

PingResult PingManager::getResults() {
    return {
        _lastPingGW,
        _lastPingInternet,
        WiFi.status() == WL_CONNECTED
    };
}
