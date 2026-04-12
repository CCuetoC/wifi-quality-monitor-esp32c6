#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "NetworkService.h"
#include "QualityAnalyzer.h"
#include "DashboardRenderer.h"
#include "FileLogger.h"

#define WDT_TIMEOUT_SECONDS 15
#define UI_REFRESH_MS 1000

NetworkService network;
QualityAnalyzer analyzer;
DashboardRenderer renderer;
FileLogger logger;

#define LED_PIN 5
unsigned long lastUIUpdate = 0;
unsigned long lastHistorySample = 0;
QualityAnalyzer::HealthState lastState = QualityAnalyzer::CRITICAL;

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- WIFI QUALITY MONITOR v4.0 START ---");
    
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
    renderer.drawBootScreen("INDUSTRIAL V4.0...");
    
    network.begin(WIFI_SSID, WIFI_PASS);
    
    // El Watchdog se activa al final del setup
    esp_task_wdt_add(NULL); 
}

void loop() {
    esp_task_wdt_reset();
    network.update(logger);
    
    // Sincronización de Buffers tras reconexión
    if (network.consumeConnectionTrigger()) {
        analyzer.resetBuffers();
        logger.logEvent("SYS_STATUS", "Link Restored - Buffers Flushed");
    }

    // Loop de Control Local (1s)
    if (millis() - lastUIUpdate >= UI_REFRESH_MS) {
        lastUIUpdate = millis();
        NetworkService::NetworkData netData = network.getData();
        
        // Restauración de Historial Forense
        static bool historyLoaded = false;
        if (!historyLoaded) {
            if (network.getBootPhase() >= 1) {
                int hist[100], idx;
                if (logger.loadTrend(hist, 100, &idx)) {
                    analyzer.loadHistory(hist, 100, idx);
                    logger.logEvent("SYS_STATUS", "Visual History Restored");
                }
                historyLoaded = true;
            } else {
                renderer.drawBootScreen("ACCESSING FILES...");
                return;
            }
        }

        if (netData.connected) {
            // QoS Analysis
            QualityAnalyzer::HealthMetrics health = analyzer.calculateHealth(netData.rssi, netData.pingInternet);
            // V5.0: Propagar todas las métricas industriales
            network.setQuality(health.score, health.jitter, health.packetLoss, health.snr, health.linkEfficiency);

            // Muestreo de Latencia (V5.3: 3s para coincidir con red)
            if (millis() - lastHistorySample >= 3000) {
                analyzer.addSample(netData.pingInternet); 
                lastHistorySample = millis();
            }

            // Muestreo de RAM (cada 10s) - Escala 512 KB
            static unsigned long lastRamSample = 0;
            if (millis() - lastRamSample >= 10000) {
                // Guardamos el valor absoluto en KB para que el logger aplique la escala
                analyzer.addRamSample(ESP.getFreeHeap() / 1024);
                lastRamSample = millis();
            }

            // Persistencia Industrial (cada 60s)
            static unsigned long lastTrendSave = 0;
            if (millis() - lastTrendSave >= 60000) {
                logger.saveTrend(analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex());
                logger.saveRam(analyzer.getRamHistory(), analyzer.getRamIndex());
                lastTrendSave = millis();
            }
            
            // Auditoría de Cambio de Estado
            if (health.state != lastState) {
                char stateBuf[64];
                sprintf(stateBuf, "From %s to %s", 
                        QualityAnalyzer::getStateName(lastState), 
                        QualityAnalyzer::getStateName(health.state));
                logger.logEvent("STATE_CHANGE", stateBuf);
                lastState = health.state;
            }

            // Renderizado Local (LCD)
            renderer.drawDashboard(netData, health, analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex(),
                                   network.getUptimeString(), network.getReconnectCount(), 0.0);
            
            digitalWrite(LED_PIN, (netData.rssi > -70) ? HIGH : LOW);
        } else {
            renderer.drawDisconnected(network.getUptimeString(), network.getReconnectCount(), 0.0);
            digitalWrite(LED_PIN, (millis() % 500 < 250) ? HIGH : LOW);
        }
    }
}
