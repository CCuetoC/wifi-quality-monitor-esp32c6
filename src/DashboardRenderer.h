#ifndef DASHBOARD_RENDERER_H
#define DASHBOARD_RENDERER_H

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include "NetworkService.h"
#include "QualityAnalyzer.h"

class DashboardRenderer {
public:
    void begin();
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
    void _drawHeader(int score, const char* label, uint16_t color);
    void _drawHistoryGraph(const int* history, int size, int circularIndex, uint16_t color);
    void _drawFooter(const NetworkService::NetworkData& net, const QualityAnalyzer::HealthMetrics& health, String uptime, int reconnects, float disconnectRate);
};

#endif
