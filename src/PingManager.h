#ifndef PING_MANAGER_H
#define PING_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <ESP32Ping.h>

struct PingResult {
    int lastPingGW;
    int lastPingInternet;
    float lossPercentage;
    bool isConnected;
};

class PingManager {
public:
    PingManager() : _lastPingGW(-1), _lastPingInternet(-1), _totalPings(0), _lostPings(0) {}

    void performPings();
    PingResult getResults();

private:
    static const int WINDOW_SIZE = 200; // Reset cada 200 muestras para reactividad
    int _lastPingGW;
    int _lastPingInternet;
    unsigned long _lastPingTime = 0;
    
    // Contadores de Resiliencia (Ventana Deslizante)
    unsigned long _totalPings;
    unsigned long _lostPings;
};

#endif
