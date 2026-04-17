#ifndef DASHBOARD_RENDERER_H
#define DASHBOARD_RENDERER_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "CommonTypes.h"

#include <WebServer.h>
#include <WiFi.h>

class DashboardRenderer {
public:
    DashboardRenderer() : _tft(), _canvas(&_tft) {
        _canvas.setColorDepth(16);
        _canvas.createSprite(320, 172);
    }
    void begin();
    void serveScreenshot(WebServer& server); 
    void drawBootScreen(const char* state);
    void drawDashboard(const NetworkData& net, 
                       const HealthMetrics& health, 
                       const int* history, int historySize, int circularIndex,
                       String uptime, int reconnects, float disconnectRate);
    void drawDisconnected(String uptime, int reconnects, float disconnectRate);

private:
    class LGFX_C6 : public lgfx::LGFX_Device {
        lgfx::Panel_ST7789  _panel_instance;
        lgfx::Bus_SPI       _bus_instance;
        lgfx::Light_PWM     _light_instance;
    public:
        LGFX_C6(void);
    };

    LGFX_C6 _tft;
    LGFX_Sprite _canvas;
    
    uint16_t _getColorForState(HealthState state);
    void _drawHeader(String ssid, int y, int h);
    void _drawTelemetryRow(const NetworkData& net, String ip, int y, int h);
    void _drawLagChart(const int* history, int size, int circularIndex, int y, int h);
    void _drawHealthBar(int score, HealthState state, int y, int h);
    void _drawMetricsGrid(const NetworkData& net, 
                          const HealthMetrics& health, 
                          String uptime, int reconnects, float disconnectRate,
                          int y, int h);
    int _mapLatencyToY(int ms, int maxHeight);
};

#endif
