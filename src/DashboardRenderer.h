#ifndef DASHBOARD_RENDERER_H
#define DASHBOARD_RENDERER_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "NetworkService.h"
#include "QualityAnalyzer.h"

#include <WebServer.h>

class DashboardRenderer {
public:
    DashboardRenderer();
    void begin();
    void serveScreenshot(WebServer& server); 
    void drawBootScreen(const char* state);
    void drawDashboard(const NetworkService::NetworkData& net, 
                       const QualityAnalyzer::HealthMetrics& health, 
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
    
    uint16_t _getColorForState(QualityAnalyzer::HealthState state);
    void _drawLagChart(const int* history, int size, int circularIndex);
    void _drawHealthBar(int score, QualityAnalyzer::HealthState state);
    void _drawMetricsGrid(const NetworkService::NetworkData& net, 
                         const QualityAnalyzer::HealthMetrics& health, 
                         String uptime, float disconnectRate);
    int _mapLatencyToY(int ms, int maxHeight);
};

#endif
