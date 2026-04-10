#include "NetworkService.h"

void NetworkService::begin(const char* ssid, const char* pass) {
    if (WiFi.status() == WL_CONNECTED) return;
    WiFi.begin(ssid, pass);
}

void NetworkService::update() {
    if (WiFi.status() == WL_CONNECTED) {
        if (millis() - _lastPingTime >= _pingInterval || _lastPingTime == 0) {
            _performPing();
            _lastPingTime = millis();
        }
    } else {
        // Si no está conectado, intentamos reconectar periódicamente
        static unsigned long lastReconnectAttempt = 0;
        if (millis() - lastReconnectAttempt > 10000) { 
            Serial.println("NetworkService: Connection lost. Attempting reconnect...");
            WiFi.reconnect();
            lastReconnectAttempt = millis();
        }
    }
}

void NetworkService::_performPing() {
    if (Ping.ping("8.8.8.8")) {
        _lastPingMs = Ping.averageTime();
    } else {
        _lastPingMs = -1;
    }
}

NetworkService::NetworkData NetworkService::getData() {
    NetworkData data;
    data.connected = (WiFi.status() == WL_CONNECTED);
    if (data.connected) {
        data.rssi = WiFi.RSSI();
        data.ip = WiFi.localIP().toString();
        data.channel = WiFi.channel();
        data.pingMs = _lastPingMs;
    } else {
        data.rssi = -100;
        data.ip = "0.0.0.0";
        data.channel = 0;
        data.pingMs = -1;
    }
    return data;
}

bool NetworkService::isConnected() {
    return WiFi.status() == WL_CONNECTED;
}
