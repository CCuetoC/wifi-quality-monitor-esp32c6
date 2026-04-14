#ifndef PING_MANAGER_H
#define PING_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>

struct PingResult {
    int lastPingGW;
    int lastPingInternet;
    bool isConnected;
};

class PingManager {
public:
    PingManager() : _lastPingGW(-1), _lastPingInternet(-1) {}

    void performPings();
    PingResult getResults();

private:
    int _lastPingGW;
    int _lastPingInternet;
    unsigned long _lastPingTime = 0;
};

#endif
