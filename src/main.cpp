#include <Arduino.h>
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <ESP32Ping.h>
#include "config.h"

/**
 * PROYECTO: WIFI QUALITY PRO
 * DESCRIPCIÓN: Monitor de señal avanzado para ESP32-C6 (v4.2 - SPRITE)
 * ESTADO: DOBLE BUFFERING PARA CERO PARPADEOS
 */

class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789  _panel_instance;
    lgfx::Bus_SPI       _bus_instance;
    lgfx::Light_PWM     _light_instance;
public:
    LGFX(void) {
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
};

LGFX tft;
LGFX_Sprite canvas(&tft); // Nuestro lienzo de memoria

// Métricas de Red
int rssi = 0;
int signalPct = 0;
int pingLatency = -1;
unsigned long lastUpdate = 0;

// LED de Status
#define LED_PIN 5
unsigned long lastBlink = 0;
bool ledState = false;

// Función de color
uint16_t getSignalColor(int pct) {
    if (pct >= 80) return TFT_GREEN;
    if (pct >= 60) return TFT_GREENYELLOW;
    if (pct >= 40) return TFT_YELLOW;
    if (pct >= 20) return TFT_ORANGE;
    return TFT_RED;
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- WIFI QUALITY PRO v4.2 SPRITE START ---");
    pinMode(LED_PIN, OUTPUT);
    
    tft.init();
    tft.setRotation(1);
    
    // Crear el Sprite del tamaño de la pantalla
    if (!canvas.createSprite(tft.width(), tft.height())) {
        Serial.println("Error al crear Sprite! RAM insuficiente?");
    }
    
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(3);

    // SECUENCIA DE ARRANQUE (Calibrada con Sprite)
    canvas.fillScreen(TFT_RED); canvas.setTextColor(TFT_WHITE); canvas.drawString("ROJO", canvas.width()/2, canvas.height()/2); canvas.pushSprite(0,0); delay(1000);
    canvas.fillScreen(TFT_GREEN); canvas.setTextColor(TFT_BLACK); canvas.drawString("VERDE", canvas.width()/2, canvas.height()/2); canvas.pushSprite(0,0); delay(1000);
    canvas.fillScreen(TFT_BLUE); canvas.setTextColor(TFT_WHITE); canvas.drawString("AZUL", canvas.width()/2, canvas.height()/2); canvas.pushSprite(0,0); delay(1000);

    canvas.fillScreen(TFT_BLACK);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString("ESTABILIZANDO v4.2", canvas.width() / 2, canvas.height() / 2);
    canvas.pushSprite(0,0);

    WiFi.begin(WIFI_SSID, WIFI_PASS);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi Conectado!");
    canvas.fillScreen(TFT_BLACK);
    canvas.pushSprite(0,0);
}

void drawWiFiDashboard() {
    rssi = WiFi.RSSI();
    
    // Mapear RSSI a Porcentaje (-100 a -50 dBm -> 0 a 100%)
    signalPct = 2 * (rssi + 100);
    signalPct = constrain(signalPct, 0, 100);
    uint16_t color = getSignalColor(signalPct);

    // Limpiar el canvas invisible
    canvas.fillScreen(TFT_BLACK);

    // 1. PORCENTAJE (SUPERIOR)
    canvas.setTextSize(5);
    canvas.setTextColor(color, TFT_BLACK); 
    canvas.setTextDatum(top_center);
    char pctBuffer[10];
    sprintf(pctBuffer, "%3d%%", signalPct);
    canvas.drawString(pctBuffer, canvas.width() / 2, 15);

    // 2. NOMBRE Y VERSIÓN (CENTRO)
    canvas.setTextSize(2);
    canvas.setTextColor(TFT_WHITE, TFT_BLACK);
    canvas.setTextDatum(middle_center);
    canvas.drawString("WIFI MONITOR PRO v4.2", canvas.width() / 2, 85);

    // 3. BARRA DE SEÑAL (INFERIOR)
    int barY = 110;
    int barWidth = 280;
    int barHeight = 30;
    canvas.drawRect((canvas.width() - barWidth) / 2, barY, barWidth, barHeight, TFT_WHITE);
    canvas.fillRect((canvas.width() - barWidth) / 2 + 2, barY + 2, barWidth - 4, barHeight - 4, TFT_BLACK);
    canvas.fillRect((canvas.width() - barWidth) / 2 + 2, barY + 2, ((barWidth - 4) * signalPct) / 100, barHeight - 4, color);

    // 4. DETALLES TÉCNICOS (FOOTER)
    canvas.setTextSize(1);
    canvas.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    canvas.setTextDatum(bottom_left);
    canvas.setCursor(20, 150);
    canvas.printf("RSSI: %d dBm | IP: %s | CH: %d", rssi, WiFi.localIP().toString().c_str(), WiFi.channel());
    
    canvas.setCursor(20, 165);
    canvas.printf("PING: %d ms", pingLatency);

    // 5. ENVIAR TODO EL CUADRO A LA PANTALLA (INSTANTÁNEO)
    canvas.pushSprite(0, 0);
}

void loop() {
    if (millis() - lastUpdate > 5000 || lastUpdate == 0) {
        if (Ping.ping("8.8.8.8")) pingLatency = Ping.averageTime();
        else pingLatency = -1;
        lastUpdate = millis();
    }

    drawWiFiDashboard();

    if (rssi > -70) {
        digitalWrite(LED_PIN, HIGH);
    } else {
        if (millis() - lastBlink >= 250) {
            ledState = !ledState;
            digitalWrite(LED_PIN, ledState);
            lastBlink = millis();
        }
    }
    delay(250); 
}
