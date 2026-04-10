#include "NetworkService.h"

void NetworkService::begin(const char* ssid, const char* pass) {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.begin(ssid, pass);
}

void NetworkService::update() {
    if (WiFi.status() == WL_CONNECTED) {
        // Al conectar con éxito, reseteamos el intervalo de backoff
        if (_reconnectInterval != 10000) {
            Serial.printf("NetworkService: WiFi stabilized. Resetting backoff to 10s.\n");
            _reconnectInterval = 10000;
        }

        if (millis() - _lastPingTime >= _pingInterval || _lastPingTime == 0) {
            _performPing();
            _lastPingTime = millis();
        }
    } else {
        // Implementación de Backoff Exponencial
        if (millis() - _lastReconnectAttempt > _reconnectInterval) { 
            Serial.printf("NetworkService: Connection lost. Retrying in %lu s...\n", _reconnectInterval / 1000);
            WiFi.reconnect();
            _lastReconnectAttempt = millis();
            
            // Duplicar el intervalo para el siguiente intento, con un tope de 5 min
            _reconnectInterval = min(_reconnectInterval * 2, _maxReconnectInterval);
        }
    }
}

void NetworkService::_performPing() {
    IPAddress gateway = WiFi.gatewayIP();
    
    // 1. Ping al Gateway (Local)
    if (Ping.ping(gateway)) {
        _lastPingGW = Ping.averageTime();
    } else {
        _lastPingGW = -1;
    }

    // 2. Ping a Internet (Google)
    if (Ping.ping("8.8.8.8")) {
        _lastPingInternet = Ping.averageTime();
    } else {
        _lastPingInternet = -1;
    }
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

bool NetworkService::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}
