#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <WiFi.h>
#include <ESP32Ping.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <LittleFS.h>

class NetworkService {
public:
    NetworkService();

    struct NetworkData {
        bool connected;
        int rssi;
        int pingGW;          // Ping al Router (Local)
        int pingInternet;    // Ping a 8.8.8.8 (Internet)
        String ip;
        int channel;
        int score;           // QoS Score (0-100)
        int jitter;          // Signal Stability
    };

    void begin(const char* ssid, const char* pass);
    void update();
    NetworkData getData();
    bool isConnected();
    
    // Industrial Logging & Persistence
    void logEvent(const char* type, const char* data);
    String getUptimeString();
    int getBootPhase() { return _bootPhase; }
    int getReconnectCount();
    float getDisconnectRate();
    bool consumeConnectionTrigger() { bool t = _connectionTrigger; _connectionTrigger = false; return t; }
    void setQuality(int score, int jitter);

    // Trend Persistence
    bool saveTrend(const int* history, int size, int index);
    bool loadTrend(int* history, int size, int* index);

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
    const unsigned long _saveInterval = 60000;          // 1 minuto para pruebas y precisión
    const unsigned long _maxDisconnectTime = 900000;    // 15 minutos -> ESP.restart()
    
    unsigned long _lastSaveTime = 0;
    unsigned long _lastConnectedTime = 0;
    unsigned long _historicalUptime = 0;
    int _historicalReconnects = 0;
    
    bool _isConfigMode = false;
    bool _fsReady = false;
    bool _connectionTrigger = false;
    int _bootPhase = 0; // 0: WiFi Only, 1: FS, 2: Web, 3: Full
    int _gmtOffset = -5;
    int _lastScore = 0;
    int _lastJitter = 0;
    char _apSSID[32];
    
    Preferences _prefs;
    WebServer* _server = nullptr;
    DNSServer* _dnsServer = nullptr;
    
    void _performPing();
    void _setupWebServer();
    void _handleRoot();
    void _handleLogs();
    void _handleConfig();
    void _rotateLogs();
};

#endif
