#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "NetworkService.h"
#include "QualityAnalyzer.h"
#include "DashboardRenderer.h"

#define WDT_TIMEOUT_SECONDS 15
#define UI_REFRESH_MS 250

NetworkService network;
QualityAnalyzer analyzer;
DashboardRenderer renderer;

#define LED_PIN 5
unsigned long lastUIUpdate = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- WIFI QUALITY MONITOR START ---");
    
    // Watchdog Configuration
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_task_wdt_config_t twdt_config = {
            .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
            .idle_core_mask = (1 << 0),
            .trigger_panic = true
        };
        esp_task_wdt_reconfigure(&twdt_config);
    #else
        esp_task_wdt_init(WDT_TIMEOUT_SECONDS, true);
    #endif
    esp_task_wdt_add(NULL); 
    
    pinMode(LED_PIN, OUTPUT);
    renderer.begin();
    renderer.drawBootScreen("INITIALIZING...");
    
    network.begin(WIFI_SSID, WIFI_PASS);
    Serial.println("System Ready.");
}

void loop() {
    esp_task_wdt_reset();
    network.update();
    
    // Non-blocking UI Refresh Loop
    if (millis() - lastUIUpdate >= UI_REFRESH_MS) {
        lastUIUpdate = millis();
        
        NetworkService::NetworkData netData = network.getData();
        
        if (netData.connected) {
            // Análisis de salud basado en latencia WAN (Internet)
            QualityAnalyzer::HealthMetrics health = analyzer.calculateHealth(netData.rssi, netData.pingInternet);
            analyzer.addSample(health.score);
            
            renderer.drawDashboard(netData, health, analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex(),
                                   network.getUptimeString(), network.getReconnectCount(), network.getDisconnectRate());
            
            digitalWrite(LED_PIN, (netData.rssi > -70) ? HIGH : LOW);
        } else {
            // Telemetría básica persistente incluso sin enlace WiFi
            renderer.drawDisconnected(network.getUptimeString(), network.getReconnectCount(), network.getDisconnectRate());
            digitalWrite(LED_PIN, (millis() % 500 < 250) ? HIGH : LOW);
        }
    }
}
