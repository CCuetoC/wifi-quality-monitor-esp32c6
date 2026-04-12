#include <Arduino.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>
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
HealthState lastState = CRITICAL;

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n\n######################################");
    Serial.println(">>>   V6.3-MIGRATED BOOT SUCCESS   <<<");
    Serial.println("######################################\n");
    
    // Watchdog Configuration
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
    renderer.drawBootScreen("V6.3-MIGRATING...");
    
    network.begin(WIFI_SSID, WIFI_PASS);

    // V6.3: Limpieza forzada tras fix de arquitectura
    Preferences pver;
    pver.begin("fver", false);
    if (pver.getInt("v", 0) != 63) {
        Serial.println("[!] V6.3: VERSION CHANGE DETECTED - WIPING FS...");
        LittleFS.begin(); 
        LittleFS.remove("/trend.bin");
        LittleFS.remove("/ram.bin");
        pver.putInt("v", 63);
    }
    pver.end();
    
    // El Watchdog se activa al final del setup
    esp_task_wdt_add(NULL); 
}

void loop() {
    esp_task_wdt_reset();
    network.update(logger, renderer);
    
    // Sincronización de Buffers tras reconexión
    if (network.consumeConnectionTrigger()) {
        analyzer.resetBuffers();
        logger.logEvent("SYS_STATUS", "Link Restored - Buffers Flushed");
    }

    // Loop de Control Local (1s)
    if (millis() - lastUIUpdate >= UI_REFRESH_MS) {
        lastUIUpdate = millis();
        NetworkData netData = network.getData();
        
        // Restauración de Historial Forense (46 muestras)
        static bool historyLoaded = false;
        if (!historyLoaded) {
            if (network.getBootPhase() >= 1) {
                int hist[46], idx;
                if (logger.loadTrend(hist, 46, &idx)) {
                    analyzer.loadHistory(hist, 46, idx);
                    logger.logEvent("SYS_STATUS", "Visual History Restored");
                }
                historyLoaded = true;
            } else {
                renderer.drawBootScreen("SYTEM BOOTING...");
                return;
            }
        }

        if (netData.connected) {
            // QoS Analysis
            HealthMetrics health = analyzer.calculateHealth(netData.rssi, netData.pingInternet);
            // Sincronizar métricas con el servicio de red
            network.setQuality(health.score, health.jitter, health.packetLoss, health.snr, health.linkEfficiency);

            // Muestreo de Latencia (3s)
            if (millis() - lastHistorySample >= 3000) {
                analyzer.addSample(netData.pingInternet); 
                lastHistorySample = millis();
            }

            // Muestreo de RAM (10s)
            static unsigned long lastRamSample = 0;
            if (millis() - lastRamSample >= 10000) {
                analyzer.addRamSample(ESP.getFreeHeap() / 1024);
                lastRamSample = millis();
            }

            // Persistencia (60s)
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

            // Renderizado LCD Directo
            renderer.drawDashboard(netData, health, analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex(),
                                   network.getUptimeString(), network.getReconnectCount(), 0.0);
            
            digitalWrite(LED_PIN, (netData.score > 70) ? HIGH : LOW);
        } else {
            renderer.drawDisconnected(network.getUptimeString(), network.getReconnectCount(), 0.0);
            digitalWrite(LED_PIN, (millis() % 500 < 250) ? HIGH : LOW);
        }
    }
}
