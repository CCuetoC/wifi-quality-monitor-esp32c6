#include "QualityAnalyzer.h"

// Colores simplificados (se mapearán en el Renderer si es necesario)
#define VAL_GREEN  0x07E0
#define VAL_YELLOW 0xFFE0
#define VAL_ORANGE 0xFD20
#define VAL_RED    0xF800

QualityAnalyzer::HealthMetrics QualityAnalyzer::calculateHealth(int rssi, int pingMs) {
    HealthMetrics metrics;
    
    int rssiScore = _mapRSSI(rssi);
    int pingScore = _mapLatency(pingMs);
    
    // Ponderación: 60% RSSI, 40% Latencia
    metrics.score = (rssiScore * 0.6) + (pingScore * 0.4);
    metrics.score = constrain(metrics.score, 0, 100);

    // Determinar etiqueta y color basado en el score final
    if (metrics.score >= 85) {
        metrics.label = "EXCELLENT";
        metrics.color = VAL_GREEN;
    } else if (metrics.score >= 65) {
        metrics.label = "GOOD";
        metrics.color = VAL_GREEN; // Usamos un verde amarillento en el renderer
    } else if (metrics.score >= 40) {
        metrics.label = "FAIR";
        metrics.color = VAL_YELLOW;
    } else if (metrics.score >= 20) {
        metrics.label = "POOR";
        metrics.color = VAL_ORANGE;
    } else {
        metrics.label = "CRITICAL";
        metrics.color = VAL_RED;
    }

    return metrics;
}

int QualityAnalyzer::_mapRSSI(int rssi) {
    if (rssi > -50) return 100;
    if (rssi < -95) return 0;
    // Mapeo lineal entre -95 y -50
    return map(rssi, -95, -50, 0, 100);
}

int QualityAnalyzer::_mapLatency(int ms) {
    if (ms < 0) return 0;       // Ping fallido
    if (ms <= 50) return 100;   // Excelente
    if (ms >= 500) return 0;    // Muy lento
    // Mapeo inverso: a más ms, menos score
    return map(ms, 50, 500, 100, 0);
}
