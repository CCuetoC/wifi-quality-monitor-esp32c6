#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "NetworkService.h"
#include "QualityAnalyzer.h"
#include "DashboardRenderer.h"

#define WDT_TIMEOUT_SECONDS 15
#define UI_REFRESH_MS 1000

NetworkService network;
QualityAnalyzer analyzer;
DashboardRenderer renderer;

#define LED_PIN 5
unsigned long lastUIUpdate = 0;
unsigned long lastHistorySample = 0;
QualityAnalyzer::HealthState lastState = QualityAnalyzer::CRITICAL;

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- WIFI QUALITY MONITOR START ---");
    
    // Watchdog Configuration (Diferido para estabilidad)
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_task_wdt_config_t twdt_config = {
            .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
            .idle_core_mask = (1 << 0),
            .trigger_panic = true
        };
        esp_task_wdt_reconfigure(&twdt_config);
    #endif
    
    pinMode(LED_PIN, OUTPUT);
    renderer.begin();
    renderer.drawBootScreen("INITIALIZING...");
    
    network.begin(WIFI_SSID, WIFI_PASS);
    
    // Activar el Watchdog SOLO cuando estemos listos para el loop
    esp_task_wdt_add(NULL); 
    Serial.println("System Ready.");
}

void loop() {
    esp_task_wdt_reset();
    network.update();
    
    // Sincronización de Buffers tras reconexión (V2.1 - Sinceridad Industrial)
    if (network.consumeConnectionTrigger()) {
        analyzer.resetBuffers();
        network.logEvent("SYS_STATUS", "Link Restored - Buffers Flushed");
    }

    // Non-blocking UI Refresh Loop (Refresco de 1 segundo)
    if (millis() - lastUIUpdate >= UI_REFRESH_MS) {
        lastUIUpdate = millis();
        
        NetworkService::NetworkData netData = network.getData();
        
        // Recuperación de Historial (Solo una vez tras el arranque de archivos)
        static bool historyLoaded = false;
        if (!historyLoaded) {
            if (network.getBootPhase() >= 1) {
                int hist[50], idx;
                if (network.loadTrend(hist, 50, &idx)) {
                    analyzer.loadHistory(hist, 50, idx);
                    network.logEvent("SYS_STATUS", "Visual History Restored");
                }
                historyLoaded = true;
            } else {
                // Mantener pantalla de carga mientras LittleFS despierta
                renderer.drawBootScreen("RESTORING HISTORY...");
                return;
            }
        }

        if (netData.connected) {
            QualityAnalyzer::HealthMetrics health = analyzer.calculateHealth(netData.rssi, netData.pingInternet);
            // Inyectar métricas para API /status
            network.setQuality(health.score, health.jitter);

            // CAPTURA: Seguimos muestreando cada 2s para suavidad local (v2.4)
            if (millis() - lastHistorySample >= 2000) {
                analyzer.addSample(health.score);
                lastHistorySample = millis();
            }

            // MUESTREO DE RAM (V2.3): Cada 10 segundos para analítica de estabilidad
            static unsigned long lastRamSample = 0;
            if (millis() - lastRamSample >= 10000) {
                analyzer.addRamSample((ESP.getFreeHeap() * 100) / ESP.getHeapSize());
                lastRamSample = millis();
            }

            // PERSISTENCIA: Solo cada 60s para proteger la vida útil de la Flash
            static unsigned long lastTrendSave = 0;
            if (millis() - lastTrendSave >= 60000) {
                network.saveTrend(analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex());
                lastTrendSave = millis();
            }
            
            // Detección y registro de cambio de estado Semántico
            if (health.state != lastState) {
                char stateBuf[64];
                sprintf(stateBuf, "From %s to %s", 
                        QualityAnalyzer::getStateName(lastState), 
                        QualityAnalyzer::getStateName(health.state));
                network.logEvent("STATE_CHANGE", stateBuf);
                lastState = health.state;
            }

            renderer.drawDashboard(netData, health, analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex(),
                                   network.getUptimeString(), network.getReconnectCount(), network.getDisconnectRate());
            
            digitalWrite(LED_PIN, (netData.rssi > -70) ? HIGH : LOW);
        } else {
            renderer.drawDisconnected(network.getUptimeString(), network.getReconnectCount(), network.getDisconnectRate());
            digitalWrite(LED_PIN, (millis() % 500 < 250) ? HIGH : LOW);
        }
    }
}
