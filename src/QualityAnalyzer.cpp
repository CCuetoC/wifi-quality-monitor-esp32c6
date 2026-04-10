#include "QualityAnalyzer.h"

// Colores simplificados (se mapearán en el Renderer si es necesario)
#define VAL_GREEN  0x07E0
#define VAL_YELLOW 0xFFE0
#define VAL_ORANGE 0xFD20
#define VAL_RED    0xF800

QualityAnalyzer::HealthMetrics QualityAnalyzer::calculateHealth(int rssi, int pingMs) {
    HealthMetrics metrics;
    
    // Aplicar Promedio Móvil
    int avgRSSI = _addToMovingAverage(_rssiBuffer, rssi, _rssiIndex, MA_SIZE);
    int avgPing = _addToMovingAverage(_pingBuffer, (pingMs == -1 ? 500 : pingMs), _pingIndex, MA_SIZE);

    // Calcular Estabilidad (Jitter) basado en el rango del buffer de RSSI
    int minR = 0, maxR = -110;
    for(int i=0; i<MA_SIZE; i++) {
        if(_rssiBuffer[i] < minR) minR = _rssiBuffer[i];
        if(_rssiBuffer[i] > maxR) maxR = _rssiBuffer[i];
    }
    metrics.jitter = (minR == 0) ? 0 : (maxR - minR);
    metrics.isStable = (metrics.jitter <= 10); // Umbral de 10dBm de variablidad

    int rssiScore = _mapRSSI(avgRSSI);
    int pingScore = _mapLatency(avgPing);
    
    // Si no hay conexión, el score es 0
    if (pingMs == -1 && avgPing >= 500) {
        rssiScore = 0;
        pingScore = 0;
    }

    // Ponderación Industrial sugerida: 60% RSSI, 40% Latencia
    metrics.score = (rssiScore * 0.6) + (pingScore * 0.4);
    metrics.score = constrain(metrics.score, 0, 100);

    // Nuevos Umbrales Operativos
    if (metrics.score >= 91) {
        metrics.label = "EXCELLENT";
        metrics.state = EXCELLENT;
    } else if (metrics.score >= 71) {
        metrics.label = "GOOD";
        metrics.state = GOOD; 
    } else if (metrics.score >= 41) {
        metrics.label = "DEGRADED";
        metrics.state = DEGRADED;
    } else {
        metrics.label = "CRITICAL";
        metrics.state = CRITICAL;
    }

    return metrics;
}

int QualityAnalyzer::_addToMovingAverage(int* buffer, int newValue, int& index, int size) {
    buffer[index] = newValue;
    index = (index + 1) % size;
    
    long sum = 0;
    for (int i = 0; i < size; i++) sum += buffer[i];
    return sum / size;
}

void QualityAnalyzer::addSample(int score) {
    _history[_historyIndex] = score;
    _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
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
