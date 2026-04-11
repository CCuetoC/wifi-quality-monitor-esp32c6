#include "QualityAnalyzer.h"

QualityAnalyzer::HealthMetrics QualityAnalyzer::calculateHealth(int rssi, int pingMs) {
    HealthMetrics metrics;
    
    // Warm-up: Inicialización de buffers con el primer dato real para evitar rampa desde cero
    if (!_isInitialized) {
        for(int i=0; i<MA_SIZE; i++) {
            _rssiBuffer[i] = rssi;
            _pingBuffer[i] = (pingMs == -1 ? 500 : pingMs);
        }
        _isInitialized = true;
    }

    // Convergence: Promedios móviles para mitigar ruido en la capa física
    int avgRSSI = _addToMovingAverage(_rssiBuffer, rssi, _rssiIndex, MA_SIZE);
    int avgPing = _addToMovingAverage(_pingBuffer, (pingMs == -1 ? 500 : pingMs), _pingIndex, MA_SIZE);

    // Analytics: Cálculo de Jitter basado en el rango real del buffer (sin sesgo de ceros)
    int minR = _rssiBuffer[0], maxR = _rssiBuffer[0];
    for(int i=1; i<MA_SIZE; i++) {
        if(_rssiBuffer[i] < minR) minR = _rssiBuffer[i];
        if(_rssiBuffer[i] > maxR) maxR = _rssiBuffer[i];
    }
    metrics.jitter = maxR - minR;
    metrics.isStable = (metrics.jitter <= 10); 

    // Score Mapping: Estandardización de métricas
    int rssiScore = _mapRSSI(avgRSSI);
    int pingScore = _mapLatency(avgPing);
    
    // Weighted QoS: Ponderación 60/40
    metrics.score = (rssiScore * 0.6) + (pingScore * 0.4);

    // SINCERIDAD INDUSTRIAL: Comentado temporalmente para diagnóstico (V2.10)
    /*
    if (pingMs == -1) {
        metrics.score = 0;
    }
    */
    
    metrics.score = constrain(metrics.score, 0, 100);

    // State Machine: Histéresis funcional de 5 puntos
    int margin = 5;
    HealthState nextState = _lastState;

    if (metrics.score >= 91) {
        nextState = EXCELLENT;
    } 
    else if (metrics.score >= 71) {
        // Histéresis EXCELLENT -> GOOD: Solo baja si score < 86 (91 - 5)
        if (_lastState == EXCELLENT && metrics.score >= (91 - margin)) {
            nextState = EXCELLENT;
        } else {
            nextState = GOOD;
        }
    } 
    else if (metrics.score >= 41) {
        // Histéresis GOOD -> DEGRADED: Solo baja si score < 66 (71 - 5)
        if (_lastState == GOOD && metrics.score >= (71 - margin)) {
            nextState = GOOD;
        } else {
            nextState = DEGRADED;
        }
    } 
    else {
        // Histéresis DEGRADED -> CRITICAL: Solo baja si score < 36 (41 - 5)
        if (_lastState == DEGRADED && metrics.score >= (41 - margin)) {
            nextState = DEGRADED;
        } else {
            nextState = CRITICAL;
        }
    }

    metrics.state = nextState;
    _lastState = nextState;

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

void QualityAnalyzer::addRamSample(int percent) {
    _ramHistory[_ramIndex] = percent;
    _ramIndex = (_ramIndex + 1) % HISTORY_SIZE;
}

void QualityAnalyzer::loadHistory(const int* data, int size, int index) {
    if (!data || size != HISTORY_SIZE) return;
    memcpy(_history, data, size * sizeof(int));
    _historyIndex = index % HISTORY_SIZE;
}

void QualityAnalyzer::resetBuffers() {
    _isInitialized = false;
    _rssiIndex = 0;
    _pingIndex = 0;
}

int QualityAnalyzer::_mapRSSI(int rssi) {
    if (rssi > -50) return 100;
    if (rssi < -95) return 0;
    return map(rssi, -95, -50, 0, 100);
}

int QualityAnalyzer::_mapLatency(int ms) {
    if (ms < 0) return 0;       
    if (ms <= 50) return 100;   
    if (ms >= 500) return 0;    
    return map(ms, 50, 500, 100, 0);
}
