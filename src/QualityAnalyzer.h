    enum HealthState { CRITICAL, DEGRADED, GOOD, EXCELLENT };

    struct HealthMetrics {
        int score;           // 0-100%
        HealthState state;   // Categoría lógica
        const char* label;   // "EXCELLENT", "GOOD", etc.
    };

    static const int HISTORY_SIZE = 50;
    
    HealthMetrics calculateHealth(int rssi, int pingMs);
    void addSample(int score);
    const int* getHistory() const { return _history; }
    int getHistorySize() const { return HISTORY_SIZE; }
    int getHistoryIndex() const { return _historyIndex; } // NUEVO: Para dibujo cronológico

private:
    int _mapRSSI(int rssi);
    int _mapLatency(int ms);
    int _addToMovingAverage(int* buffer, int newValue, int& index, int size);
    
    int _history[HISTORY_SIZE] = {0};
    int _historyIndex = 0;
    
    // Buffers de Promedio Móvil
    static const int MA_SIZE = 10;
    int _rssiBuffer[MA_SIZE] = {0};
    int _pingBuffer[MA_SIZE] = {0};
    int _rssiIndex = 0;
    int _pingIndex = 0;
    bool _bufferFull = false;
};

#endif
