#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <WiFi.h>
#include <ESP32Ping.h>
#include <Preferences.h>

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
    const unsigned long _saveInterval = 600000;         // 10 minutos (NVS Wear Leveling)
    const unsigned long _maxDisconnectTime = 900000;    // 15 minutos -> ESP.restart()
    
    unsigned long _lastSaveTime = 0;
    unsigned long _lastConnectedTime = 0;
    unsigned long _historicalUptime = 0;
    int _historicalReconnects = 0;
    
    Preferences _prefs;
    void _performPing();
};

#endif
