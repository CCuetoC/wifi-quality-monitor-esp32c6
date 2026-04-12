#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <Arduino.h>

// 1. Estados de Salud Lógica
enum HealthState { CRITICAL, DEGRADED, GOOD, EXCELLENT };

// 2. Métricas de Salud DEM
struct HealthMetrics {
    int score;           // 0-100%
    HealthState state;   // Categoría lógica
    const char* label;   // "EXCELLENT", "GOOD", etc.
    bool isStable;       // ¿La señal fluctúa poco?
    int jitter;          // Variabilidad en ms
    int packetLoss;      // % de pérdidas (ventana actual)
    int snr;             // SNR estimado (dB)
    float linkEfficiency; // Tasa de éxito de transmisión (0.0 a 1.0)
};

// 3. Datos de Red Enterprise
struct NetworkData {
    bool connected;
    int rssi;
    int pingGW;
    int pingInternet;
    String ip;
    int channel;
    int score;
    int jitter;
    int packetLoss;
    int snr;
    float linkEfficiency;
    String bssid;
    String phyMode;
};

#endif
