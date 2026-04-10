#include <Arduino.h>
#include <esp_task_wdt.h>
#include "config.h"
#include "NetworkService.h"
#include "QualityAnalyzer.h"
#include "DashboardRenderer.h"

// Configuración de Watchdog (Industrial Robustness)
#define WDT_TIMEOUT_SECONDS 15

// Instancias de Módulos
NetworkService network;
QualityAnalyzer analyzer;
DashboardRenderer renderer;

// LED de Status
#define LED_PIN 5

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- WIFI QUALITY MONITOR M1 START ---");
    
    // Inicializar Watchdog (Versión compatible)
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
    
    // Inicializar Pantalla
    renderer.begin();
    renderer.drawBootScreen("INITIALIZING...");
    
    // Inicializar Red
    network.begin(WIFI_SSID, WIFI_PASS);
    
    Serial.println("System Ready.");
}

void loop() {
    // Alimentar al Perro Guardián (Keep-alive)
    esp_task_wdt_reset();
    
    // Actualizar Servicios
    network.update();
    
    // Obtener Datos y Analizar (Diagnóstico Dual)
    NetworkService::NetworkData netData = network.getData();
    
    if (netData.connected) {
        // QoS Logic: Usamos la latencia externa (WAN) para calcular la salud real del enlace
        QualityAnalyzer::HealthMetrics health = analyzer.calculateHealth(netData.rssi, netData.pingInternet);
        
        // Circular Buffer Storage: Registro en el historial para series temporales
        analyzer.addSample(health.score);
        
        // Rasterization & Display Swap: Renderizar Dashboard con métricas industriales y persistencia
        renderer.drawDashboard(netData, health, analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex(),
                               network.getUptimeString(), network.getReconnectCount(), network.getDisconnectRate());
        
        // Visual Handshake: Retroalimentación física vía LED según RSSI
        digitalWrite(LED_PIN, (netData.rssi > -70) ? HIGH : LOW);
        
    } else {
        // Estado Desconectado
        renderer.drawDisconnected();
        
        // Blink de alerta en LED
        digitalWrite(LED_PIN, (millis() % 500 < 250) ? HIGH : LOW);
    }
    
    delay(200); 
}
