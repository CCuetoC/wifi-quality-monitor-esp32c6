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

    HealthMetrics calculateHealth(int rssi, int pingMs);

private:
    int _mapRSSI(int rssi);
    int _mapLatency(int ms);
};

#endif
