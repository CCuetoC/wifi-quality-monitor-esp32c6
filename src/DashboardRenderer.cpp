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
                               String uptime, int reconnects, float disconnectRate) {
    _canvas.fillScreen(TFT_BLACK);
    
    // ZONA 1: Lag Spikes Trend (Arriba)
    _drawLagChart(history, historySize, circularIndex);
    
    // ZONA 2: Overall Quality Bar (Centro)
    _drawHealthBar(health.score, health.state);
    
    // ZONA 3: Metrics Grid (Abajo)
    _drawMetricsGrid(net, health, uptime, disconnectRate);
    
    // Firma de Versión (Validación Force Deploy)
    _canvas.setTextColor(0x52AA); // Gris
    _canvas.setTextSize(1);
    _canvas.setCursor(_canvas.width() - 48, _canvas.height() - 8);
    _canvas.print("V5.3-DEM");
    
    _canvas.pushSprite(&_tft, 0, 0);
}

void DashboardRenderer::_drawLagChart(const int* history, int size, int circularIndex) {
    int x = 10, y = 10, w = 300, h = 65; 
    
    // Fondo y Guías Log-Step
    _canvas.drawRect(x, y, w, h, 0x18C3); 
    
    int g20 = y + h - _mapLatencyToY(20, h);
    int g100 = y + h - _mapLatencyToY(100, h);
    int g500 = y + h - _mapLatencyToY(500, h);
    
    _canvas.drawFastHLine(x, g20, w, 0x0100); 
    _canvas.drawFastHLine(x, g100, w, 0x10A2);
    _canvas.drawFastHLine(x, g500, w, 0x5000);
    
    // ESCALA LIBRE: Números en x+2, barras inician en x+26
    _canvas.setTextSize(1);
    _canvas.setTextColor(0x7BEF); // Gris suave
    _canvas.setCursor(x + 2, g20 - 7); _canvas.print("20");
    _canvas.setCursor(x + 2, g100 - 7); _canvas.print("100");
    _canvas.setCursor(x + 2, g500 - 7); _canvas.print("500");

    int barW = 4;
    int gap = 2;
    int startOffset = 26; // Despeje para la escala
    for (int i = 0; i < size; i++) {
        int idx = (circularIndex + i) % size;
        int val = history[idx];
        if (val <= 0) continue; 

        int barH = _mapLatencyToY(val, h);
        uint16_t color = 0x07FF; 
        if (val > 100) color = TFT_YELLOW;
        if (val > 500 || val == -1) color = TFT_RED;
        
        _canvas.fillRect(x + startOffset + (i * (barW + gap)), y + h - barH, barW, barH, color);
    }
    
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setCursor(x + 30, y + 2);
    _canvas.print("WAN LATENCY (ms) - 2.5m HISTORY");
}

int DashboardRenderer::_mapLatencyToY(int ms, int h) {
    if (ms <= 0) return 0;
    if (ms == -1) return h; 
    if (ms <= 20) return map(ms, 0, 20, 0, 15);
    if (ms <= 100) return 15 + map(ms, 20, 100, 0, 15);
    if (ms <= 500) return 30 + map(ms, 100, 500, 0, 20);
    if (ms <= 2500) return 50 + map(ms, 500, 2500, 0, 15);
    return h;
}

void DashboardRenderer::_drawHealthBar(int score, QualityAnalyzer::HealthState state) {
    int xArr = 10, yArr = 85, wArr = 240, hArr = 14; 
    _canvas.drawRect(xArr, yArr, wArr, hArr, 0x7BEF); // Gris claro para el marco
    
    int segments = 20;
    int filledSegments = (score * segments) / 100;
    int segW = (wArr - 4) / segments;
    
    for (int i = 0; i < segments; i++) {
        uint16_t segColor;
        if (i < 5) segColor = TFT_RED;
        else if (i < 10) segColor = TFT_ORANGE;
        else if (i < 15) segColor = 0xFFE0;
        else segColor = 0x07E0;
        
        bool isActive = (i < filledSegments);
        uint16_t finalColor = isActive ? segColor : 0x10A2; // Azul muy oscuro apagado
        _canvas.fillRect(xArr + 2 + (i * segW), yArr + 2, segW - 1, hArr - 4, finalColor);
    }
    
    // Porcentaje nítido junto a la barra
    _canvas.setTextSize(2);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.setCursor(xArr + wArr + 10, yArr - 1);
    _canvas.printf("%d%%", score);
    
    _canvas.setTextSize(1);
    _canvas.setTextColor(0x52AA);
    _canvas.setCursor(xArr, yArr + 18);
    _canvas.print("OVERALL QUALITY INDEX (46 SAMPLES TREND)");
}

void DashboardRenderer::_drawMetricsGrid(const NetworkService::NetworkData& net, const QualityAnalyzer::HealthMetrics& health, String uptime, float disconnectRate) {
    int startX = 10, startY = 118; // Bajado ligeramente
    int boxW = 145, boxH = 28; // Cajas más altas
    
    auto drawBox = [&](int col, int row, const char* label, String val1, uint16_t col1, String val2, uint16_t col2) {
        int bx = startX + (col * (boxW + 10));
        int by = startY + (row * (boxH + 5));
        _canvas.drawRect(bx, by, boxW, boxH, 0x2104); 
        _canvas.setTextSize(1);
        _canvas.setTextColor(0x7BEF); 
        _canvas.setCursor(bx + 5, by + 4); _canvas.print(label);
        
        // Centrado vertical (+17px)
        _canvas.setCursor(bx + 5, by + 17); 
        _canvas.setTextColor(col1); _canvas.print(val1);
        _canvas.setCursor(bx + boxW/2 + 10, by + 17); // Más aire horizontal
        _canvas.setTextColor(col2); _canvas.print(val2);
    };

    uint16_t cS = (net.rssi < -67) ? TFT_ORANGE : TFT_WHITE;
    uint16_t cSNR = (health.snr < 25) ? TFT_RED : TFT_CYAN;
    uint16_t cL = (health.packetLoss > 1) ? TFT_RED : TFT_WHITE;
    uint16_t cJ = (health.jitter > 10) ? TFT_ORANGE : TFT_WHITE;
    uint16_t cGW = (net.pingGW > 50) ? TFT_RED : TFT_GREEN;
    if (net.pingGW == -1) cGW = TFT_RED;
    uint16_t cEXT = (net.pingInternet > 50) ? TFT_RED : TFT_GREEN;
    if (net.pingInternet == -1) cEXT = TFT_RED;

    // BOX 1: Phys - Separación mejorada
    String sig = "S:" + String(net.rssi);
    String ch = "CH:" + String(net.channel) + " (" + net.phyMode + ")";
    drawBox(0, 0, "PHYS (Signal/Link)", sig, cS, ch, TFT_CYAN);
    _canvas.setTextColor(cSNR); _canvas.setCursor(startX + 120, startY + 4); _canvas.printf("%d", health.snr);

    // BOX 2: Net
    drawBox(1, 0, "QUAL (Loss/Jit)", "L:" + String(health.packetLoss) + "%", cL, "J:" + String(health.jitter) + "ms", cJ);
    
    // BOX 3: Resp
    drawBox(0, 1, "RESP (Local/Ext)", "GW:" + String(net.pingGW), cGW, "EXT:" + String(net.pingInternet), cEXT);
    
    // BOX 4: Audit - Posición Badge FINAL
    bool toggle = (millis() % 10000 < 5000);
    if (toggle) {
        drawBox(1, 1, "AUDIT (Uptime)", uptime.substring(0,8), TFT_WHITE, "DR:" + String(disconnectRate, 1), TFT_MAGENTA);
    } else {
        String shortBssid = net.bssid.substring(9); 
        drawBox(1, 1, "AUDIT (BSSID)", shortBssid, TFT_YELLOW, "DR:" + String(disconnectRate, 1), TFT_MAGENTA);
    }

    // Badge FINAL
    _canvas.setTextColor(0x3186); // Gris oscuro
    _canvas.setTextSize(1);
    _canvas.setCursor(_canvas.width() - 55, _canvas.height() - 8);
    _canvas.print("V5.5-FINAL");
}

uint16_t DashboardRenderer::_getColorForState(QualityAnalyzer::HealthState state) {
    switch (state) {
        case QualityAnalyzer::EXCELLENT: return 0x07E0; // TFT_GREEN
        case QualityAnalyzer::GOOD:      return 0x07FF; // TFT_CYAN
        case QualityAnalyzer::DEGRADED:  return 0xFFE0; // TFT_YELLOW
        case QualityAnalyzer::CRITICAL:  return 0xF800; // TFT_RED
        default:                         return 0xFFFF; // TFT_WHITE
    }
}

void DashboardRenderer::drawDisconnected(String uptime, int reconnects, float disconnectRate) {
    _canvas.fillScreen(TFT_BLACK);
    _canvas.setTextColor(TFT_RED);
    _canvas.setTextSize(4);
    _canvas.setTextDatum(top_center);
    _canvas.drawString("OFFLINE", _canvas.width() / 2, 40);
    
    _canvas.setTextSize(2);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.drawString("ATTEMPTING LINK...", _canvas.width() / 2, 90);
    
    _canvas.pushSprite(&_tft, 0, 0);
}
