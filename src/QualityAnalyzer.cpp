#include "QualityAnalyzer.h"

HealthMetrics QualityAnalyzer::calculateHealth(int rssi, int pingMs) {
    HealthMetrics metrics;
    
    // 1. Warm-up
    if (!_isInitialized) {
        for(int i=0; i<MA_SIZE; i++) {
            _rssiBuffer[i] = rssi;
            _pingBuffer[i] = (pingMs == -1 ? 500 : pingMs);
        }
        _isInitialized = true;
    }

    // 2. Convergence (Ventana de 10 para promedios de cálculo)
    int avgRSSI = _addToMovingAverage(_rssiBuffer, rssi, _rssiIndex, MA_SIZE);
    
    // 3. Auditoría de Packet Loss (Ventana de 50 histórica)
    int losses = 0;
    for(int i=0; i<HISTORY_SIZE; i++) {
        if(_history[i] == -1) losses++;
    }
    float lossRate = (float)losses / HISTORY_SIZE;
    metrics.packetLoss = (int)(lossRate * 100);
    metrics.linkEfficiency = 1.0f - lossRate;

    // 4. Cálculo de Jitter (EMA de variaciones absolutas)
    static int lastValidPing = -1;
    static float internalJitter = 0;
    if (pingMs != -1 && lastValidPing != -1) {
        float diff = abs(pingMs - lastValidPing);
        internalJitter = (internalJitter * 0.8f) + (diff * 0.2f);
    }
    if (pingMs != -1) lastValidPing = pingMs;
    metrics.jitter = (int)internalJitter;
    metrics.isStable = (metrics.jitter <= 15); // Umbral industrial de estabilidad

    // 5. Capa Física (SNR Estimado)
    metrics.snr = rssi + 96; // Ref floor: -96dBm

    // 6. Score Mapping (60% SNR/RSSI + 40% Latency)
    int rssiScore = _mapRSSI(avgRSSI);
    int pingScore = _mapLatency(pingMs);
    int baseScore = (rssiScore * 0.6) + (pingScore * 0.4);
    
    // 7. SINCERIDAD INDUSTRIAL V5.0: Penalización por eficiencia
    metrics.score = (int)((float)baseScore * metrics.linkEfficiency);
    metrics.score = constrain(metrics.score, 0, 100);

    // 8. State Machine (Histéresis funcional)
    int margin = 5;
    HealthState nextState = _lastState;
    if (metrics.score >= 91) nextState = EXCELLENT;
    else if (metrics.score >= 71) {
        if (_lastState == EXCELLENT && metrics.score >= (91 - margin)) nextState = EXCELLENT;
        else nextState = GOOD;
    } 
    else if (metrics.score >= 41) {
        if (_lastState == GOOD && metrics.score >= (71 - margin)) nextState = GOOD;
        else nextState = DEGRADED;
    } 
    else {
        if (_lastState == DEGRADED && metrics.score >= (41 - margin)) nextState = DEGRADED;
        else nextState = CRITICAL;
    }

    metrics.state = nextState;
    _lastState = nextState;
    metrics.label = getStateName(nextState);

    // 9. Actualizar historial con LATENCIA (v5.0) en lugar de score
    addSample(pingMs);

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
