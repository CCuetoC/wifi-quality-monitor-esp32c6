#include "NetworkService.h"

void NetworkService::begin(const char* ssid, const char* pass) {
    // 0. Limpieza total de logs para asegurar booteo
    Serial.println("\n[SAFEBOOT] Starting basic network services...");
    Serial.flush();

    // 1. WiFi Local (Sin NTP, sin WebServer pesado)
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, pass);
    Serial.println("[SAFEBOOT] WiFi command sent.");
    Serial.flush();

    _startTime = millis();
    _lastSaveTime = millis();
    _lastConnectedTime = millis();
    
    // El resto de servicios (Web, Logs) se activarán en el update() 
    // diferidos por tiempo para evitar el crash de inicio.
}

void NetworkService::update() {
    // Solo manejamos el cliente SI el servidor ha sido inicializado
    // (Añadiremos el inicio progresivo aquí pronto)
}

void NetworkService::logEvent(const char* type, const char* data) {
    Serial.print("LOG: [");
    Serial.print(type);
    Serial.print("] ");
    Serial.println(data);
    Serial.flush();
}

void NetworkService::_setupWebServer() {}
void NetworkService::_handleRoot() {}
void NetworkService::_handleLogs() {}
void NetworkService::_handleConfig() {}
String NetworkService::getUptimeString() { return "00h 00m 00s"; }
void NetworkService::_performPing() {}
NetworkService::NetworkData NetworkService::getData() {
    NetworkData d; d.connected = (WiFi.status() == WL_CONNECTED); d.rssi = -100; d.ip = "0.0.0.0";
    return d;
}
int NetworkService::getReconnectCount() { return 0; }
float NetworkService::getDisconnectRate() { return 0.0; }
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
