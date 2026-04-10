#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <WiFi.h>
#include <ESP32Ping.h>

class NetworkService {
public:
    struct NetworkData {
        bool connected;
        int rssi;
        int pingGW;          // Ping al Router (Local)
        int pingInternet;    // Ping a 8.8.8.8 (Internet)
        String ip;
        int channel;
    };

    void begin(const char* ssid, const char* pass);
    void update();
    NetworkData getData();
    bool isConnected();
    
    // Industrial Logging & Persistence
    void logEvent(const char* type, const char* data);
    String getUptimeString();
    int getReconnectCount();
    float getDisconnectRate();

private:
    unsigned long _startTime = 0;
    int _reconnectCount = 0;
    unsigned long _lastPingTime = 0;
    const unsigned long _pingInterval = 5000;
    int _lastPingGW = -1;
    int _lastPingInternet = -1;
    
    // Backoff Exponencial
    unsigned long _reconnectInterval = 10000; 
    unsigned long _lastReconnectAttempt = 0;
    const unsigned long _maxReconnectInterval = 300000; // 5 minutos
    
    void _performPing();
};

#endif
