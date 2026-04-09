#ifndef QUALITY_ANALYZER_H
#define QUALITY_ANALYZER_H

#include <Arduino.h>

class QualityAnalyzer {
public:
    struct HealthMetrics {
        int score;           // 0-100%
        const char* label;   // "EXCELLENT", "GOOD", etc.
        uint16_t color;      // TFT Color code
    };

    static const int HISTORY_SIZE = 50;
    
    HealthMetrics calculateHealth(int rssi, int pingMs);
    void addSample(int score);
    const int* getHistory() const { return _history; }
    int getHistorySize() const { return HISTORY_SIZE; }

private:
    int _mapRSSI(int rssi);
    int _mapLatency(int ms);
    int _history[HISTORY_SIZE] = {0};
    int _historyIndex = 0;
};

#endif
