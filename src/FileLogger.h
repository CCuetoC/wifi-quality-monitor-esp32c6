#ifndef FILE_LOGGER_H
#define FILE_LOGGER_H

#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>

class FileLogger {
public:
    FileLogger();
    bool begin();
    
    // Core Logging
    void logEvent(const char* type, const char* data);
    void logEventWithTime(time_t t, const char* type, const char* data);
    
    // Industrial Auditing
    void checkStartupReason();
    void estimateLastPowerOff();
    
    // Heartbeat System
    void sendHeartbeat(int rssi, String ip);
    
    // Trends & RAM Persistence
    bool saveTrend(const int* history, int size, int index);
    bool loadTrend(int* history, int size, int* index);
    bool saveRam(const int* history, int index);
    bool loadRam(int* history, int* index);

private:
    bool _fsReady = false;
    unsigned long _lastHeartbeatSent = 0;
    bool _bootReasonLogged = false;
    bool _powerOffLogged = false;
    
    void _rotateLogs();
};

#endif
