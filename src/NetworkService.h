#ifndef NETWORK_SERVICE_H
#define NETWORK_SERVICE_H

#include <WiFi.h>
#include <ESP32Ping.h>

class NetworkService {
public:
    struct NetworkData {
        bool connected;
        int rssi;
        int pingMs;
        String ip;
        int channel;
    };

    void begin(const char* ssid, const char* pass);
    void update();
    NetworkData getData();
    bool isConnected();

private:
    unsigned long _lastPingTime = 0;
    const unsigned long _pingInterval = 5000;
    int _lastPingMs = -1;
    
    void _performPing();
};

#endif
