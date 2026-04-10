#include "DashboardRenderer.h"

DashboardRenderer::LGFX_C6::LGFX_C6(void) {
    {
        auto cfg = _bus_instance.config();
        cfg.spi_host = SPI2_HOST;
        cfg.spi_mode = 0;
        cfg.freq_write = 20000000;
        cfg.pin_sclk = 7;
        cfg.pin_mosi = 6;
        cfg.pin_dc   = 15;
        _bus_instance.config(cfg);
        _panel_instance.setBus(&_bus_instance);
    }
    {
        auto cfg = _panel_instance.config();
        cfg.pin_cs           = 14;
        cfg.pin_rst          = 21;
        cfg.panel_width      = 172;
        cfg.panel_height     = 320;
        cfg.offset_x         = 34;
        cfg.invert           = true;
        cfg.rgb_order        = false; 
        _panel_instance.config(cfg);
    }
    {
        auto cfg = _light_instance.config();
        cfg.pin_bl = 22;
        _light_instance.config(cfg);
        _panel_instance.setLight(&_light_instance);
    }
    setPanel(&_panel_instance);
}

void DashboardRenderer::begin() {
    Serial.println("DashboardRenderer: Inicializando TFT...");
    _tft.init();
    _tft.setRotation(1);
    
    Serial.println("DashboardRenderer: Configurando Lienzo (Sprite)...");
    _canvas.setColorDepth(16);
    
    if (!_canvas.createSprite(_tft.width(), _tft.height())) {
        Serial.println("!!! ERROR: Memoria insuficiente para el Sprite !!!");
    } else {
        Serial.printf("DashboardRenderer: Sprite creado. Heap libre: %d bytes\n", ESP.getFreeHeap());
    }
}

void DashboardRenderer::drawBootScreen(const char* state) {
    _canvas.fillScreen(TFT_BLACK);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setTextSize(2);
    _canvas.setTextDatum(middle_center);
    _canvas.drawString(state, _canvas.width()/2, _canvas.height()/2);
    _canvas.pushSprite(&_tft, 0, 0);
}

void DashboardRenderer::drawDashboard(const NetworkService::NetworkData& net, 
                               const QualityAnalyzer::HealthMetrics& health, 
                               const int* history, int historySize, int circularIndex,
                               String uptime, int reconnects, float reliability) {
    _canvas.fillScreen(TFT_BLACK);
    
    uint16_t statusColor = _getColorForState(health.state);
    
    _drawHeader(health.score, health.label, statusColor);
    _drawSignalBar(health.score, statusColor);
    _drawHistoryGraph(history, historySize, circularIndex, statusColor);

    // Etiqueta de Ventana Temporal (Claridad Operativa)
    _canvas.setTextSize(1);
    _canvas.setTextColor(TFT_DARKGREY);
    _canvas.setTextDatum(top_right);
    _canvas.drawString("TREND: 60 SAMPLES (30s)", _canvas.width() - 22, 62);
    
    _drawFooter(net, health, uptime, reconnects, reliability);
    
    _canvas.pushSprite(&_tft, 0, 0);
}

uint16_t DashboardRenderer::_getColorForState(QualityAnalyzer::HealthState state) {
    switch (state) {
        case QualityAnalyzer::EXCELLENT: return 0x07E0; // TFT_GREEN
        case QualityAnalyzer::GOOD:      return 0x07E0; // TFT_GREEN (Podría ser CYAN 0x07FF)
        case QualityAnalyzer::DEGRADED:  return 0xFFE0; // TFT_YELLOW
        case QualityAnalyzer::CRITICAL:  return 0xF800; // TFT_RED
        default:                         return 0xFFFF; // TFT_WHITE
    }
}

void DashboardRenderer::_drawHeader(int score, const char* label, uint16_t color) {
    _canvas.setTextSize(4);
    _canvas.setTextColor(color);
    _canvas.setTextDatum(top_left);
    char pctBuffer[10];
    sprintf(pctBuffer, "%d%%", score);
    _canvas.drawString(pctBuffer, 20, 5); 

    _canvas.setTextSize(2);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setTextDatum(top_right);
    _canvas.drawString(label, _canvas.width() - 20, 15);
}

void DashboardRenderer::_drawSignalBar(int score, uint16_t color) {
    int barY = 42; 
    int barWidth = 280;
    int barHeight = 8;
    int x = (_canvas.width() - barWidth) / 2;
    
    _canvas.drawRect(x, barY, barWidth, barHeight, 0x3186);
    _canvas.fillRect(x + 1, barY + 1, ((barWidth - 2) * score) / 100, barHeight - 2, color);
}

void DashboardRenderer::_drawHistoryGraph(const int* history, int size, int circularIndex, uint16_t color) {
    int graphX = 20;
    int graphY = 60;
    int graphW = 280;
    int graphH = 80;

    _canvas.drawRect(graphX, graphY, graphW, graphH, 0x18C3);
    for(int i=1; i<4; i++) {
        _canvas.drawFastHLine(graphX, graphY + (graphH*i/4), graphW, 0x10A2);
    }

    int step = graphW / (size - 1);
    for (int i = 0; i < size - 1; i++) {
        // Obtenemos índices circulares para dibujo cronológico
        int idx1 = (circularIndex + i) % size;
        int idx2 = (circularIndex + i + 1) % size;
        
        int y1 = graphY + graphH - (history[idx1] * graphH / 100);
        int y2 = graphY + graphH - (history[idx2] * graphH / 100);
        
        _canvas.drawLine(graphX + (i * step), y1 + 1, graphX + ((i+1) * step), y2 + 1, 0x0000); 
        _canvas.drawLine(graphX + (i * step), y1, graphX + ((i+1) * step), y2, color);
    }
}

void DashboardRenderer::_drawFooter(const NetworkService::NetworkData& net, const QualityAnalyzer::HealthMetrics& health, String uptime, int reconnects, float reliability) {
    _canvas.setTextSize(1);
    _canvas.setTextColor(TFT_LIGHTGREY);
    _canvas.setTextDatum(bottom_left);
    
    // Rasterization: Primera línea de diagnóstico (PHY y Red)
    _canvas.setCursor(20, 142);
    _canvas.printf("RSSI: %d dBm | IP: %s | CH: %d", net.rssi, net.ip.c_str(), net.channel);
    
    // Rasterization: Segunda línea (ICMP Stack y Estabilidad)
    _canvas.setCursor(20, 154);
    const char* stabilityTxt = health.isStable ? "STEADY" : "JITTERY";
    uint16_t stabColor = health.isStable ? 0x07E0 : 0xFFE0; 
    
    _canvas.print("GW: ");
    _canvas.setTextColor(net.pingGW == -1 ? TFT_RED : TFT_GREEN);
    _canvas.print(net.pingGW == -1 ? "FAIL" : String(net.pingGW).c_str());
    _canvas.setTextColor(TFT_LIGHTGREY);
    _canvas.print(" | ");
    
    _canvas.print("EXT: ");
    _canvas.setTextColor(net.pingInternet == -1 ? TFT_RED : TFT_GREEN);
    _canvas.print(net.pingInternet == -1 ? "FAIL" : String(net.pingInternet).c_str());
    _canvas.setTextColor(TFT_LIGHTGREY);
    _canvas.print(" | ");
    
    _canvas.setTextColor(stabColor);
    _canvas.print(stabilityTxt);
    
    // Bus Arbitration: Tercera línea (Métricas Industriales / Reliability)
    _canvas.setTextColor(TFT_DARKGREY);
    _canvas.setCursor(20, 166);
    _canvas.print("UPTIME: ");
    _canvas.print(uptime);
    _canvas.print(" | REL: ");
    _canvas.printf("%.1f/hr", reliability);
}

void DashboardRenderer::drawDisconnected() {
    _canvas.fillScreen(TFT_BLACK);
    _canvas.setTextColor(TFT_RED);
    _canvas.setTextSize(5);
    _canvas.setTextDatum(top_center);
    _canvas.drawString("OFF", _canvas.width() / 2, 15);
    
    _canvas.setTextSize(2);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.drawString("DISCONNECTED", _canvas.width() / 2, 85);
    
    _canvas.setTextSize(1);
    _canvas.setTextColor(TFT_LIGHTGREY);
    _canvas.setCursor(20, 150);
    _canvas.printf("SYSTEM IDLE | WAITING FOR CONNECTION...");
    
    _canvas.pushSprite(&_tft, 0, 0);
}
