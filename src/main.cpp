#include <Arduino.h>
#include <esp_task_wdt.h>
#include <LittleFS.h>
#include "config.h"
#include "NetworkService.h"
#include "QualityAnalyzer.h"
#include "DashboardRenderer.h"
#include "FileLogger.h"
#include "PingManager.h"

#define WDT_TIMEOUT_SECONDS 20
#define UI_REFRESH_MS 1000
#define LED_PIN 5

// Instancias Globales
NetworkService network;
QualityAnalyzer analyzer;
DashboardRenderer renderer;
FileLogger logger;
PingManager pingMgr;

// Sincronización RTOS
SemaphoreHandle_t dataMutex;
SemaphoreHandle_t resourceMutex; // v8.4 Atomic Lock
NetworkData sharedNetData;
HealthMetrics sharedHealth;

// Prototipos de Tareas
void taskNetwork(void* pvParameters);
void taskUI(void* pvParameters);

void printResetReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    Serial.print("\n[DIAG] LAST RESET REASON: ");
    switch (reason) {
        case ESP_RST_POWERON: Serial.println("POWER ON"); break;
        case ESP_RST_EXT:     Serial.println("EXTERNAL PIN"); break;
        case ESP_RST_SW:      Serial.println("SOFTWARE REBOOT"); break;
        case ESP_RST_PANIC:   Serial.println("EXCEPTION / PANIC"); break;
        case ESP_RST_INT_WDT: Serial.println("INTERRUPT WATCHDOG"); break;
        case ESP_RST_TASK_WDT: Serial.println("TASK WATCHDOG"); break;
        case ESP_RST_WDT:     Serial.println("OTHER WATCHDOG"); break;
        case ESP_RST_DEEPSLEEP: Serial.println("DEEP SLEEP"); break;
        case ESP_RST_BROWNOUT: Serial.println("BROWNOUT (VOLTAGE DROP)"); break;
        case ESP_RST_SDIO:    Serial.println("SDIO REBOOT"); break;
        default:              Serial.println("UNKNOWN"); break;
    }
}

void setup() {
    #if !defined(ARDUINO_USB_CDC_ON_BOOT) || ARDUINO_USB_CDC_ON_BOOT != 1
        Serial.begin(115200);
    #endif
    for(int i=0; i<10 && !Serial; i++) delay(100); 

    setCpuFrequencyMhz(160); // v8.8.2: CPU Power Restoration (Baseline)
    
    Serial.println("\n\n######################################");
    Serial.println(">>>   V9.0.0 DIGITAL TWIN CLOUD    <<<");
    Serial.println("######################################");
    printResetReason();
    Serial.println("######################################\n");

    dataMutex = xSemaphoreCreateMutex();
    resourceMutex = xSemaphoreCreateMutex();
    pinMode(LED_PIN, OUTPUT);
    
    renderer.begin();
    renderer.drawBootScreen("V8.4-RTOS ATOMIC...");
    
    network.setMutex(resourceMutex);
    network.begin(WIFI_SSID, WIFI_PASS);
    
    // v9.0: Configuración Supabase (PROD)
    network.setSupabaseConfig("https://vbqrtwfsgpgbdfuebdge.supabase.co", "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6InZicXJ0d2ZzZ3BnYmRmdWViZGdlIiwicm9sZSI6ImFub24iLCJpYXQiOjE3NzYzODYyOTUsImV4cCI6MjA5MTk2MjI5NX0.fZEXVHNXRCIg1d7qF-1cX2qEO16FmaeH6dq_9V1URWg");

    // Watchdog Configuration (Base)
    #if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
        esp_task_wdt_config_t twdt_config = {
            .timeout_ms = WDT_TIMEOUT_SECONDS * 1000,
            .idle_core_mask = (1 << 0),
            .trigger_panic = true
        };
        esp_task_wdt_reconfigure(&twdt_config);
    #endif
    
    // Creación de Tareas (Single-Core C6)
    xTaskCreate(taskNetwork, "TaskNet", 8192, NULL, 5, NULL);
    xTaskCreate(taskUI, "TaskUI", 8192, NULL, 2, NULL);
}

void taskNetwork(void* pvParameters) {
    esp_task_wdt_add(NULL); // Registrar esta tarea en el WDT
    while(1) {
        esp_task_wdt_reset();
        network.update(logger, renderer);
        
        if (network.isConnected()) {
            pingMgr.performPings();
            PingResult p = pingMgr.getResults();
            network.setPingResults(p.lastPingGW, p.lastPingInternet, (int)p.lossPercentage);
            
            if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100))) {
                sharedNetData = network.getData();
                network.setHistory(analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex());
                xSemaphoreGive(dataMutex);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void taskUI(void* pvParameters) {
    unsigned long lastHistorySample = 0;
    unsigned long lastRamSample = 0;
    unsigned long lastTrendSave = 0;
    bool historyLoaded = false;
    HealthState lastState = CRITICAL;

    while(1) {
        if (!historyLoaded) {
            if (network.getBootPhase() >= 1) {
                if (xSemaphoreTake(resourceMutex, pdMS_TO_TICKS(500))) {
                    int hist[46], idx;
                    if (logger.loadTrend(hist, 46, &idx)) {
                        analyzer.loadHistory(hist, 46, idx);
                        logger.logEvent("SYS_STATUS", "Visual History Restored");
                    }
                    xSemaphoreGive(resourceMutex);
                    historyLoaded = true;
                }
            }
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        NetworkData localNet;
        if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(50))) {
            localNet = sharedNetData;
            xSemaphoreGive(dataMutex);
        }

        if (localNet.connected) {
            HealthMetrics health = analyzer.calculateHealth(localNet.rssi, localNet.pingInternet);
            network.setQuality(health.score, health.jitter, health.packetLoss, health.snr, health.linkEfficiency);

            // Samplings
            if (millis() - lastHistorySample >= 3000) {
                analyzer.addSample(localNet.pingInternet);
                lastHistorySample = millis();
            }
            if (millis() - lastRamSample >= 10000) {
                analyzer.addRamSample(ESP.getFreeHeap() / 1024);
                lastRamSample = millis();
            }
            if (millis() - lastTrendSave >= 60000) {
                logger.saveTrend(analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex());
                logger.saveRam(analyzer.getRamHistory(), analyzer.getRamIndex());
                lastTrendSave = millis();
            }

            // Auditoría de Cambio
            if (health.state != lastState) {
                char stateBuf[64];
                sprintf(stateBuf, "From %s to %s", QualityAnalyzer::getStateName(lastState), QualityAnalyzer::getStateName(health.state));
                logger.logEvent("STATE_CHANGE", stateBuf);
                lastState = health.state;
            }

            if (xSemaphoreTake(resourceMutex, pdMS_TO_TICKS(200))) {
                renderer.drawDashboard(localNet, health, analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex(), 
                                       network.getUptimeString(), network.getReconnectCount(), 0.0);
                xSemaphoreGive(resourceMutex);
            }
            digitalWrite(LED_PIN, (health.score > 70) ? HIGH : LOW);
        } else {
            if (xSemaphoreTake(resourceMutex, pdMS_TO_TICKS(200))) {
                renderer.drawDisconnected(network.getUptimeString(), network.getReconnectCount(), 0.0);
                xSemaphoreGive(resourceMutex);
            }
            digitalWrite(LED_PIN, (millis() % 500 < 250) ? HIGH : LOW);
        }

        vTaskDelay(pdMS_TO_TICKS(UI_REFRESH_MS));
    }
}

void loop() {
    // El ciclo se maneja en las tareas de FreeRTOS
    vTaskDelay(pdMS_TO_TICKS(1000));
}
