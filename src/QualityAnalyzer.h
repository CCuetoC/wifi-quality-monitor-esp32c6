#ifndef QUALITY_ANALYZER_H
#define QUALITY_ANALYZER_H

#include <Arduino.h>

class QualityAnalyzer {
public:
    enum HealthState { CRITICAL, DEGRADED, GOOD, EXCELLENT };

    static const char* getStateName(HealthState state) {
        switch(state) {
            case EXCELLENT: return "EXCELLENT";
            case GOOD:      return "GOOD";
            case DEGRADED:  return "DEGRADED";
            case CRITICAL:  return "CRITICAL";
            default:        return "UNKNOWN";
        }
    }

    struct HealthMetrics {
        int score;           // 0-100%
        HealthState state;   // Categoría lógica
        const char* label;   // "EXCELLENT", "GOOD", etc.
        bool isStable;       // ¿La señal fluctúa poco?
        int jitter;          // Variabilidad en dBm
    };

    static const int HISTORY_SIZE = 50;
    
    HealthMetrics calculateHealth(int rssi, int pingMs);
    void addSample(int score);
    void loadHistory(const int* data, int size, int index);
    void resetBuffers();
    const int* getHistory() const { return _history; }
    int getHistorySize() const { return HISTORY_SIZE; }
    int getHistoryIndex() const { return _historyIndex; } 

private:
    int _mapRSSI(int rssi);
    int _mapLatency(int ms);
    int _addToMovingAverage(int* buffer, int newValue, int& index, int size);
    
    int _history[HISTORY_SIZE] = {0};
    int _historyIndex = 0;
    
    bool _isInitialized = false;

    // Buffers de Promedio Móvil
    static const int MA_SIZE = 10;
    int _rssiBuffer[MA_SIZE] = {0};
    int _pingBuffer[MA_SIZE] = {0};
    int _rssiIndex = 0;
    int _pingIndex = 0;

    HealthState _lastState = CRITICAL;
};

#endif
