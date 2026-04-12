#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <WiFi.h>
#include <ESP32Ping.h>
#include <Preferences.h>
#include <WebServer.h>
#include <DNSServer.h>
#include "FileLogger.h"
#include "CommonTypes.h"

class DashboardRenderer; // Forward declaration

class NetworkService {
public:
    NetworkService();

    void begin(const char* ssid, const char* pass);
    void update(FileLogger& logger, DashboardRenderer& renderer);
    NetworkData getData();
    bool isConnected();
    
    // UI & State
    String getUptimeString();
    int getReconnectCount();
    void setQuality(int score, int jitter, int loss, int snr, float efficiency);
    int getBootPhase() { return _bootPhase; }
    bool consumeConnectionTrigger() { bool t = _connectionTrigger; _connectionTrigger = false; return t; }

private:
    unsigned long _startTime = 0;
    int _reconnectCount = 0;
    unsigned long _lastPingTime = 0;
    int _lastPingGW = -1;
    int _lastPingInternet = -1;
    
    unsigned long _lastReconnectAttempt = 0;
    unsigned long _lastConnectedTime = 0;
    unsigned long _historicalUptime = 0;
    
    bool _isConfigMode = false;
    bool _connectionTrigger = false;
    int _bootPhase = 0; // 0: WiFi Only, 1: FS, 2: Web, 3: Full
    int _gmtOffset = -5;
    int _lastScore = 0;
    int _lastJitter = 0;
    int _lastPacketLoss = 0;
    int _lastSNR = 0;
    float _lastLinkEfficiency = 0;
    String _lastBSSID = "00:00:00:00:00:00";
    String _lastPhyMode = "N/A";
    unsigned long _lastExtraUpdate = 0;
    
    Preferences _prefs;
    WebServer* _server = nullptr;
    DNSServer* _dnsServer = nullptr;
    
    void _performPing();
    void _setupWebServer(FileLogger& logger, DashboardRenderer& renderer);
    void _handleRoot(FileLogger& logger);
    void _handleLogs(FileLogger& logger);
    void _handleConfig();
};

#endif
