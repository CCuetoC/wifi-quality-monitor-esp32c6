#include <Arduino.h>
#include "config.h"
#include "NetworkService.h"
#include "QualityAnalyzer.h"
#include "DashboardRenderer.h"

// Instancias de Módulos
NetworkService network;
QualityAnalyzer analyzer;
DashboardRenderer renderer;

// LED de Status
#define LED_PIN 5

void setup() {
    Serial.begin(115200);
    Serial.println("\n--- WIFI QUALITY MONITOR M1 START ---");
    
    pinMode(LED_PIN, OUTPUT);
    
    // Inicializar Pantalla
    renderer.begin();
    renderer.drawBootScreen("INITIALIZING...");
    
    // Inicializar Red
    network.begin(WIFI_SSID, WIFI_PASS);
    
    Serial.println("System Ready.");
}

void loop() {
    // Actualizar Servicios
    network.update();
    
    // Obtener Datos y Analizar
    NetworkService::NetworkData netData = network.getData();
    
    if (netData.connected) {
        // Cálculo de Puntaje Industrial
        QualityAnalyzer::HealthMetrics health = analyzer.calculateHealth(netData.rssi, netData.pingMs);
        
        // Registrar en historial
        analyzer.addSample(health.score);
        
        // Renderizar Dashboard con Historial Cronológico
        renderer.drawDashboard(netData, health, analyzer.getHistory(), analyzer.getHistorySize(), analyzer.getHistoryIndex());
        
        // Control de LED según RSSI
        digitalWrite(LED_PIN, (netData.rssi > -70) ? HIGH : LOW);
        
    } else {
        // Estado Desconectado
        renderer.drawDisconnected();
        
        // Blink de alerta en LED
        digitalWrite(LED_PIN, (millis() % 500 < 250) ? HIGH : LOW);
    }
    
    delay(200); // Pequeño respiro para el procesador
}
