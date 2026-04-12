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
        _canvas.createSprite(240, 240);
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
    void _drawLagChart(const NetworkData& net, const int* history, int size, int circularIndex);
    void _drawHealthBar(int score, HealthState state);
    void _drawMetricsGrid(const NetworkData& net, 
                         const HealthMetrics& health, 
                         String uptime, float disconnectRate);
    int _mapLatencyToY(int ms, int maxHeight);
};

#endif
