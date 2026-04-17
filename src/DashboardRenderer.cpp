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
    _canvas.setColorDepth(8);
    
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

    // 1. ZONAS PROPORCIONALES v7.1.4 (Surgical Master)
    int h_total = _canvas.height();
    int z1_h = (h_total * 12) / 100; // Header (SSID) ~21px
    int z2_h = (h_total * 14) / 100; // Telemetry Row (LAT + IP) ~24px
    int z3_h = (h_total * 26) / 100; // Chart Area ~45px
    int z4_h = (h_total * 14) / 100; // Quality Row ~24px
    int z5_h = h_total - z1_h - z2_h - z3_h - z4_h; // Metrics Grid ~58px

    // 2. Ejecución Quirúrgica por Zonas
    _drawHeader(WiFi.SSID().substring(0,18), 0, z1_h);
    _drawTelemetryRow(net, "192.168.1.40", z1_h, z2_h);
    _drawLagChart(history, historySize, circularIndex, z1_h + z2_h, z3_h);
    _drawHealthBar(health.score, health.state, z1_h + z2_h + z3_h, z4_h);
    _drawMetricsGrid(net, health, uptime, reconnects, disconnectRate, z1_h + z2_h + z3_h + z4_h, z5_h);
    
    _canvas.pushSprite(&_tft, 0, 0);
}

void DashboardRenderer::_drawHeader(String ssid, int y, int h) {
    _canvas.fillRect(0, y, 320, h, TFT_BLACK); // Limpiar zona
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(1.5);
    _canvas.setTextDatum(middle_center);
    
    String label = "SSID: ";
    int totalW = _canvas.textWidth(label + ssid);
    int startX = 160 - (totalW / 2);
    
    _canvas.setTextDatum(middle_left);
    _canvas.setTextColor(0x5E58);
    _canvas.drawString(label, startX, y + (h/2));
    
    _canvas.setTextColor(TFT_WHITE);
    _canvas.drawString(ssid, startX + _canvas.textWidth(label), y + (h/2));
}

void DashboardRenderer::_drawTelemetryRow(const NetworkData& net, String ip, int y, int h) {
    _canvas.fillRect(0, y, 320, h, TFT_BLACK); // Limpiar zona
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(1.5);
    
    // Identidad de Latencia (Restaurado palabra completa)
    _canvas.setTextDatum(middle_left);
    _canvas.setTextColor(0x5E58);
    _canvas.drawString("LATENCY:", 14, y + (h/2));
    _canvas.setTextColor(TFT_WHITE);
    int latencyX = 14 + _canvas.textWidth("LATENCY:") + 4;
    _canvas.drawString(String((int)net.pingInternet) + "ms", latencyX, y + (h/2));

    // IP Identificada
    _canvas.setTextDatum(middle_right);
    _canvas.setTextColor(TFT_WHITE);
    _canvas.drawString(ip, 306, y + (h/2));
    
    _canvas.setTextColor(0x5E58);
    _canvas.drawString("IP:", 306 - _canvas.textWidth(ip) - 4, y + (h/2));
}

void DashboardRenderer::_drawLagChart(const int* history, int size, int circularIndex, int y, int h) {
    _canvas.fillRect(0, y, 320, h, TFT_BLACK); // Limpiar zona
    int chartX = 10, chartW = 300; // Simetría con Bento Grid
    int chartH = h - 6; 
    
    _canvas.drawRect(chartX, y + 2, chartW, chartH, 0x1082); 
    
    const int n_bars = 24; // Ajustado para 300px
    int barW = 10, gap = 2;
    for (int i = 0; i < n_bars; i++) {
        int idx = (circularIndex + i) % size;
        int val = (idx < size) ? history[idx] : 0;
        if (val <= 0) continue; 

        // Calibración v7.4.4: Escala 0-120ms para visibilidad de latencia estándar
        int barH = map(val, 0, 120, 2, chartH - 4);
        if (barH > chartH - 4) barH = chartH - 4;
        
        uint16_t color = 0x07FF; // Cyan Industrial (Máximo contraste)
        
        // BX = chartX(10) + padding(4) = 14px (ALINEACIÓN QUIRÚRGICA)
        int bx = chartX + 4 + (i * (barW + gap));
        if (bx + barW > chartX + chartW - 4) break;

        _canvas.fillRect(bx, y + 2 + chartH - barH - 1, barW, barH, color);
    }

    _canvas.setTextDatum(BR_DATUM);
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(1);
    _canvas.setTextColor(0x4208);
    _canvas.drawString("v7.7.0-OPTIMIZED", 320 - 4, _canvas.height() - 4);
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

void DashboardRenderer::_drawHealthBar(int score, HealthState lastState, int y, int h) {
    _canvas.fillRect(0, y, 320, h, TFT_BLACK); // Limpiar zona
    int bx = 110, bw = 196, bh = 14; 
    int by = y + (h - bh) / 2;

    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(1.5);
    _canvas.setTextDatum(middle_left);
    _canvas.setTextColor(0x5E58);
    _canvas.drawString("QUAL:", 14, y + (h/2)); // QUAL abreviado
    _canvas.setTextColor(TFT_WHITE);
    _canvas.drawString(String(score) + "%", 68, y + (h/2));

    int n_segments = 12; // 12 Segmentos Multinivel
    int segW = (bw / n_segments) - 1;
    int active_segs = (score * n_segments) / 100;

    for (int i = 0; i < n_segments; i++) {
        int sx = bx + (i * (segW + 1));
        uint16_t segColor;
        
        // Rectificación Cromática v7.4.2 (Paleta Industrial Certificada)
        if (i < 3)      segColor = 0xF800; // Rojo Puro
        else if (i < 6) segColor = 0xFD20; // Naranja/Ámbar
        else if (i < 9) segColor = 0xFFE0; // Amarillo Tráfico
        else            segColor = 0x07E0; // Verde Brillante

        uint16_t drawColor = (i < active_segs) ? segColor : 0x2104; // Gris oscuro inactivo
        _canvas.fillRect(sx, by, segW, bh, drawColor);
    }
}

void DashboardRenderer::_drawMetricsGrid(const NetworkData& net, const HealthMetrics& health, String uptime, int reconnects, float disconnectRate, int y, int h) {
    _canvas.fillRect(0, y, 320, h, TFT_BLACK); // Limpiar zona
    int startX = 10, startY = y + 2; // Alineado al px 10 para que el texto sea 14
    int boxW = 148, boxH = (h / 2) - 4; // Recalculado para márgenes de 10px

    auto drawMetric = [&](int col, int row, const char* label, String val, uint16_t color, float tSize = 1.5) {
        int x = startX + (col * (boxW + 4)); // Gap de 4px entre cajas
        int dy = startY + (row * (boxH + 2));
        _canvas.drawRect(x, dy, boxW, boxH, 0x1082); 
        
        _canvas.setFont(&fonts::Font0);
        _canvas.setTextSize(1.5); // Etiqueta siempre estable en 1.5
        _canvas.setTextDatum(middle_left);
        _canvas.setTextColor(0x5E58);
        _canvas.drawString(label, x + 4, dy + (boxH / 2)); // 10 + 4 = 14! (Alineación quirúrgica)
        
        _canvas.setTextSize(tSize); // Valor con escala variable para densidad
        _canvas.setTextDatum(middle_right);
        _canvas.setTextColor(color);
        _canvas.drawString(val, x + boxW - 4, dy + (boxH / 2));
    };

    bool showPageTwo = (millis() / 5000) % 2; 

    if (!showPageTwo) {
        // PÁGINA 1: RF y Latencia (Lo que está pasando ahora)
        String sigSNR = String(net.rssi) + "d/" + String(net.snr) + "dB";
        drawMetric(0, 0, "SIGNAL", sigSNR, (net.rssi < -67 ? 0xF800 : TFT_WHITE), 1.2); 
        drawMetric(1, 0, "CHAN", String(net.channel) + "(AX)", TFT_WHITE);
        drawMetric(0, 1, "BSSID", net.bssid.substring(6), TFT_WHITE, 1.3); // Identidad AP
        drawMetric(1, 1, "GW/EXT", String(net.pingGW) + "/" + String(net.pingInternet), TFT_WHITE, 1.3); // Latencia Inmediata
    } else {
        // PÁGINA 2: Auditoría y Resiliencia (Salud a largo plazo)
        drawMetric(0, 0, "LOSS %", String(net.packetLoss) + "%", (net.packetLoss > 2 ? 0xF800 : TFT_WHITE));
        drawMetric(1, 0, "DNS", "OK", TFT_WHITE); // Estado servicio DNS
        drawMetric(0, 1, "DISC %", String(disconnectRate * 100, 1) + "%", (disconnectRate > 0.05 ? 0xF800 : TFT_WHITE));
        drawMetric(1, 1, "UPTIME", uptime.substring(0,8), TFT_WHITE);
    }

    _canvas.setTextDatum(BR_DATUM);
    _canvas.setFont(&fonts::Font0);
    _canvas.setTextSize(1);
    _canvas.setTextColor(0x4208);
    _canvas.drawString("v7.7.0-OPTIMIZED", 320 - 4, _canvas.height() - 4);
}

uint16_t DashboardRenderer::_getColorForState(HealthState state) {
    switch (state) {
        case EXCELLENT: return 0x50C8; // Emerald
        case GOOD:      return 0x07FF; // Cyan
        case DEGRADED:  return 0xFFBF; // Amber
        case CRITICAL:  return 0xDC14; // Crimson
        default:        return 0xFFFF; // White
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
