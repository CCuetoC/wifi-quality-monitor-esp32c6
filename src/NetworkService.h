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
    void setSupabaseConfig(String url, String key);
    
    // UI & State
    String getUptimeString();
    int getReconnectCount();
    void setQuality(int score, int jitter, int loss, int snr, float efficiency);
    void setPingResults(int gw, int internet, int loss) { 
        _lastPingGW = gw; 
        _lastPingInternet = internet; 
        _lastPacketLoss = loss; 
    }
    void setHistory(const int* data, int size, int index);
    int getBootPhase() { return _bootPhase; }
    bool consumeConnectionTrigger() { bool t = _connectionTrigger; _connectionTrigger = false; return t; }
    void setMutex(SemaphoreHandle_t m) { _resMutex = m; }

private:
    int _lastPingGW = -1;
    int _lastPingInternet = -1;
    unsigned long _bootTime = 0;
    unsigned long _lastHeartbeat = 0;
    unsigned long _startTime = 0;
    unsigned long _lastResourceAudit;
    int _reconnectCount = 0;
    
    unsigned long _lastReconnectAttempt = 0;
    unsigned long _lastConnectedTime = 0;
    unsigned long _historicalUptime = 0;
    
    bool _isConfigMode = false;
    bool _connectionTrigger = false;
    int _bootPhase = 0; 
    int _webHistory[24] = {0};
    int _gmtOffset = -5;
    int _lastScore = 0;
    int _lastJitter = 0;
    int _lastPacketLoss = 0;
    int _lastSNR = 0;
    float _lastLinkEfficiency = 0;
    String _lastBSSID = "00:00:00:00:00:00";
    String _lastPhyMode = "N/A";
    unsigned long _lastExtraUpdate = 0;
    unsigned long _lastCloudPush = 0;
    
    String _supabaseUrl = "";
    String _supabaseKey = "";
    
    Preferences _prefs;
    WebServer* _server = nullptr;
    DNSServer* _dnsServer = nullptr;
    SemaphoreHandle_t _resMutex;
    
    void _sendCommonCSS();
    void _setupWebServer(FileLogger& logger, DashboardRenderer& renderer);
    void _handleRoot(FileLogger& logger);
    void _handleLogs(FileLogger& logger);
    void _handleConfig();
    void _pushToCloud();
};

#endif
