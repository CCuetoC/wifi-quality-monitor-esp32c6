#include "QualityAnalyzer.h"

// Colores simplificados (se mapearán en el Renderer si es necesario)
#define VAL_GREEN  0x07E0
#define VAL_YELLOW 0xFFE0
#define VAL_ORANGE 0xFD20
#define VAL_RED    0xF800

QualityAnalyzer::HealthMetrics QualityAnalyzer::calculateHealth(int rssi, int pingMs) {
    HealthMetrics metrics;
    
    // Convergence: Implementación de promedios móviles para mitigar el ruido en la capa física
    int avgRSSI = _addToMovingAverage(_rssiBuffer, rssi, _rssiIndex, MA_SIZE);
    int avgPing = _addToMovingAverage(_pingBuffer, (pingMs == -1 ? 500 : pingMs), _pingIndex, MA_SIZE);

    // Analytics: Cálculo de Jitter (variabilidad) basado en el rango del buffer histórico
    int minR = 0, maxR = -110;
    for(int i=0; i<MA_SIZE; i++) {
        if(_rssiBuffer[i] < minR) minR = _rssiBuffer[i];
        if(_rssiBuffer[i] > maxR) maxR = _rssiBuffer[i];
    }
    metrics.jitter = (minR == 0) ? 0 : (maxR - minR);
    metrics.isStable = (metrics.jitter <= 10); 

    // Score Mapping: Transformación lineal de métricas crudas a índices porcentuales
    int rssiScore = _mapRSSI(avgRSSI);
    int pingScore = _mapLatency(avgPing);
    
    // Exception: Manejo de pérdida total de paquetes ICMP
    if (pingMs == -1 && avgPing >= 500) {
        rssiScore = 0;
        pingScore = 0;
    }

    // Weighted QoS: Ponderación basada en impacto operativo (60% RSSI, 40% Latency)
    metrics.score = (rssiScore * 0.6) + (pingScore * 0.4);
    metrics.score = constrain(metrics.score, 0, 100);

    // State Machine: Clasificación basada en umbrales con Histéresis (Zona muerta de 5 puntos)
    int margin = 5;
    
    if (metrics.score >= 91) {
        metrics.state = EXCELLENT;
    } else if (metrics.score >= 71) {
        // Solo bajar a DEGRADED si cae por debajo de 66 (71 - 5)
        if (_lastState == EXCELLENT || metrics.score >= 71) metrics.state = GOOD;
        else if (_lastState == GOOD && metrics.score < 66) metrics.state = DEGRADED;
    } else if (metrics.score >= 41) {
        // Solo bajar a CRITICAL si cae por debajo de 36 (41 - 5)
        if (_lastState == GOOD || metrics.score >= 41) metrics.state = DEGRADED;
        else if (_lastState == DEGRADED && metrics.score < 36) metrics.state = CRITICAL;
    } else {
        metrics.state = CRITICAL;
    }

    _lastState = metrics.state;

    // Label Mapping
    switch(metrics.state) {
        case EXCELLENT: metrics.label = "EXCELLENT"; break;
        case GOOD:      metrics.label = "GOOD"; break;
        case DEGRADED:  metrics.label = "DEGRADED"; break;
        case CRITICAL:  metrics.label = "CRITICAL"; break;
    }

    return metrics;
}

int QualityAnalyzer::_addToMovingAverage(int* buffer, int newValue, int& index, int size) {
    // Memory Management: Actualización de buffer circular in-place
    buffer[index] = newValue;
    index = (index + 1) % size;
    
    long sum = 0;
    for (int i = 0; i < size; i++) sum += buffer[i];
    return sum / size;
}

void QualityAnalyzer::addSample(int score) {
    // History Persistence: Indexación circular para series temporales
    _history[_historyIndex] = score;
    _historyIndex = (_historyIndex + 1) % HISTORY_SIZE;
}

int QualityAnalyzer::_mapRSSI(int rssi) {
    if (rssi > -50) return 100;
    if (rssi < -95) return 0;
    // Linear Mapping: Estandarización de niveles de potencia RF
    return map(rssi, -95, -50, 0, 100);
}

int QualityAnalyzer::_mapLatency(int ms) {
    if (ms < 0) return 0;       
    if (ms <= 50) return 100;   
    if (ms >= 500) return 0;    
    // Inverse Mapping: A mayor latencia, menor puntaje de salud
    return map(ms, 50, 500, 100, 0);
}
