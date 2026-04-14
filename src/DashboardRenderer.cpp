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

void DashboardRenderer::serveScreenshot(WebServer& server) {
    int w = _canvas.width();
    int h = _canvas.height();
    int rowSize = (w * 3 + 3) & ~3;
    uint32_t fileSize = 54 + (rowSize * h);

    // 1. Headers HTTP
    server.setContentLength(fileSize);
    server.send(200, "image/bmp", "");
    WiFiClient client = server.client();

    // 2. BMP Header (54 bytes)
    uint8_t header[54] = {
        'B', 'M', 
        (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0, 
        54, 0, 0, 0, 
        40, 0, 0, 0, 
        (uint8_t)(w), (uint8_t)(w >> 8), (uint8_t)(w >> 16), (uint8_t)(w >> 24),
        (uint8_t)(h), (uint8_t)(h >> 8), (uint8_t)(h >> 16), (uint8_t)(h >> 24),
        1, 0, 24, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    client.write(header, 54);

    // 3. Streaming de Pixeles (BMP es Bottom-to-Top)
    uint8_t rgb[w * 3];
    for (int y = h - 1; y >= 0; y--) {
        for (int x = 0; x < w; x++) {
            uint16_t c = _canvas.readPixel(x, y);
            // RGB565 to BGR888
            rgb[x * 3 + 0] = (uint8_t)((c & 0x001F) << 3);  // Blue
            rgb[x * 3 + 1] = (uint8_t)((c & 0x07E0) >> 3);  // Green
            rgb[x * 3 + 2] = (uint8_t)((c & 0xF800) >> 8);  // Red
        }
        client.write(rgb, w * 3);
        // Padding if needed (w=320 doesn't need it)
        if (rowSize > w * 3) {
            uint8_t pad[4] = {0,0,0,0};
            client.write(pad, rowSize - w * 3);
        }
        yield(); // Feed Watchdog during heavy I/O
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

void DashboardRenderer::drawDashboard(const NetworkData& net, 
                               const HealthMetrics& health, 
                               const int* history, int historySize, int circularIndex,
                               String uptime, int reconnects, float disconnectRate) {
    _canvas.fillScreen(TFT_BLACK);
    
    uint16_t stateColor = _getColorForState(health.state);
    
    const int margin = 10;
    const int safe_w = 320 - (2 * margin);
    const int safe_h = 172 - (2 * margin);

    // 1. Borde de Alerta Glow (Marco de Referencia Interno)
    _canvas.drawRect(margin, margin, safe_w, safe_h, stateColor);
    _canvas.drawRect(margin + 1, margin + 1, safe_w - 2, safe_h - 2, stateColor);

    // 2. Cabecera (Sincronizada con Mockup)
    _canvas.setTextSize(1);
    _canvas.setTextColor(TFT_CYAN, TFT_BLACK);
    _canvas.setTextDatum(TL_DATUM);
    _canvas.setCursor(margin + 6, margin + 5); 
    _canvas.print("SSID: "); _canvas.setTextColor(TFT_WHITE); _canvas.print(WiFi.SSID());
    
    _canvas.setTextColor(0x7BEF); // Gris tenue
    _canvas.setTextDatum(TR_DATUM);
    _canvas.drawString("IP: 192.168.1.40", margin + safe_w - 6, margin + 5);

    // 3. Componentes Visuales
    _drawLagChart(net, history, historySize, circularIndex);
    _drawHealthBar(health.score, health.state);
    _drawMetricsGrid(net, health, uptime, disconnectRate);
    
    _canvas.pushSprite(&_tft, 0, 0);
}

void DashboardRenderer::_drawLagChart(const NetworkData& net, const int* history, int size, int circularIndex) {
    // Coordenadas fijas para evitar solapamientos (dentro de safe_w)
    int chartX = 18, chartY = 32, chartW = 284, chartH = 50; 
    
    _canvas.setTextDatum(TL_DATUM);
    _canvas.setTextColor(0x7BEF);
    _canvas.drawString("LATENCY (ms): ", chartX + 4, chartY - 9);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.drawString(String((int)net.pingInternet), chartX + 85, chartY - 9);
    
    _canvas.drawRect(chartX, chartY, chartW, chartH, 0x2104); 
    
    const int n_bars = 46;
    int barW = 4;
    int gap = 2;
    for (int i = 0; i < n_bars; i++) {
        int idx = (circularIndex + i) % size;
        int val = (idx < size) ? history[idx] : 0;
        if (val <= 0) continue; 

        int h = map(val, 0, 500, 2, chartH - 4);
        if (h > chartH - 4) h = chartH - 4;
        
        uint16_t color = (val > 100) ? ((val > 500) ? TFT_RED : TFT_YELLOW) : 0x07FF;
        int bx = chartX + 3 + (i * (barW + gap));
        if (bx + barW > chartX + chartW - 2) break;

        _canvas.fillRect(bx, chartY + chartH - h - 1, barW, h, color);
        _canvas.drawFastHLine(bx, chartY + chartH - h - 1, barW, TFT_WHITE);
    }
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

void DashboardRenderer::_drawHealthBar(int score, HealthState lastState) {
    int bx = 18, by = 90, bw = 250, bh = 8;
    _canvas.drawRect(bx, by, bw, bh, 0x2104);
    
    int barW = (score * (bw - 4)) / 100;
    _canvas.fillRect(bx + 2, by + 2, barW, bh - 4, _getColorForState(lastState));
    
    _canvas.setTextDatum(TR_DATUM);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.drawString(String(score) + "%", 304, by - 2);
}

void DashboardRenderer::_drawMetricsGrid(const NetworkData& net, const HealthMetrics& health, String uptime, float disconnectRate) {
    int startX = 18, startY = 105;
    int boxW = 141, boxH = 24;

    auto drawMetric = [&](int col, int row, const char* label, String val, uint16_t color) {
        int x = startX + (col * (boxW + 2));
        int y = startY + (row * (boxH + 2));
        _canvas.drawRect(x, y, boxW, boxH, 0x1082);
        
        _canvas.setTextDatum(TL_DATUM);
        _canvas.setTextColor(0x7BEF);
        _canvas.drawString(label, x + 4, y + 8);
        
        _canvas.setTextDatum(TR_DATUM);
        _canvas.setTextColor(color);
        _canvas.drawString(val, x + boxW - 4, y + 8);
    };

    drawMetric(0, 0, "SIGNAL", String(net.rssi) + " dBm", (net.rssi < -67 ? TFT_ORANGE : TFT_GREEN));
    drawMetric(1, 0, "CHAN", String(net.channel) + " (AX)", TFT_CYAN);
    drawMetric(0, 1, "GW/EXT", String(net.pingGW) + "/" + String(net.pingInternet), TFT_WHITE);
    
    bool toggle = (millis() % 10000 < 5000);
    if (toggle) drawMetric(1, 1, "UPTIME", uptime.substring(0,8), TFT_WHITE);
    else drawMetric(1, 1, "BSSID", net.bssid.substring(9), TFT_YELLOW);

    _canvas.setTextDatum(BR_DATUM);
    _canvas.setTextColor(0x4208);
    _canvas.drawString("v6.6.5", 320 - 12, 172 - 12);
}

uint16_t DashboardRenderer::_getColorForState(HealthState state) {
    switch (state) {
        case EXCELLENT: return 0x07E0; // TFT_GREEN
        case GOOD:      return 0x07FF; // TFT_CYAN
        case DEGRADED:  return 0xFFE0; // TFT_YELLOW
        case CRITICAL:  return 0xF800; // TFT_RED
        default:        return 0xFFFF; // TFT_WHITE
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
