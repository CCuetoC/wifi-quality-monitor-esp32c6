#include "NetworkService.h"
#include "DashboardRenderer.h"
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <time.h>
#include "esp_system.h"
#include "esp_wifi.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>

NetworkService::NetworkService() {
    _lastPingGW = 0;
    _lastPingInternet = 0;
    _reconnectCount = 0;
    _bootPhase = 0;
    _lastScore = 0;
    _lastJitter = 0;
    _lastPacketLoss = 0;
    _lastSNR = 0;
    _lastLinkEfficiency = 0.0f;
    _lastResourceAudit = 0;
}

// CSS V8.0 - MASTER CONSOLE WIDE
void NetworkService::_sendCommonCSS() {
    _server->sendContent(F("<style>"));
    _server->sendContent(F("body{background:#000;color:#fff;font-family:'Segoe UI',Roboto,sans-serif;margin:0;padding:0;overflow-x:hidden;}"));
    _server->sendContent(F(".lcd-container{max-width:900px;margin:20px auto;background:#000;border:8px solid #222;border-radius:15px;box-shadow:0 0 40px rgba(0,255,255,0.05);overflow:hidden;}"));
    _server->sendContent(F(".nav-tabs{display:flex;background:#1a1a1a;border-bottom:1px solid #333;position:sticky;top:0;z-index:100;}"));
    _server->sendContent(F(".nav-tab{flex:1;padding:15px;text-align:center;color:#666;text-decoration:none;font-weight:bold;font-size:13px;letter-spacing:1px;transition:0.3s;border-right:1px solid #222;}"));
    _server->sendContent(F(".nav-tab:hover{background:#222;color:#0ff;} .nav-tab.active{background:#000;color:#0ff;box-shadow:inset 0 -3px 0 #0ff;}"));
    _server->sendContent(F(".dashboard{padding:20px;} .bento-header{display:flex;justify-content:space-between;align-items:center;margin-bottom:20px;padding:10px;background:rgba(255,255,255,0.02);border-radius:8px;}"));
    _server->sendContent(F(".chart-container{height:120px;background:rgba(0,255,255,0.02);border:1px solid #111;border-radius:8px;margin-bottom:20px;display:flex;align-items:flex-end;padding:10px;gap:4px;}"));
    _server->sendContent(F(".bar-web{flex:1;background:#0ff;min-height:2px;border-radius:2px 2px 0 0;transition:all 0.4s cubic-bezier(0.175,0.885,0.32,1.275);}"));
    _server->sendContent(F(".grid{display:grid;grid-template-columns:1fr 1fr;gap:15px;} .card{background:#050505;border:1px solid #111;padding:20px;border-radius:10px;display:flex;flex-direction:column;}"));
    _server->sendContent(F(".label{color:#555;font-size:11px;font-weight:bold;text-transform:uppercase;margin-bottom:8px;} .value{font-size:24px;font-weight:200;font-family:'Courier New',monospace;color:#fff;}"));
    _server->sendContent(F(".health-bar-bg{height:8px;background:#111;border-radius:4px;overflow:hidden;margin:20px 0;} .health-bar-fill{height:100%;background:#0ff;transition:width 1s ease-in-out;}"));
    _server->sendContent(F("table{width:100%;border-collapse:collapse;margin-top:10px;font-size:13px;} th{text-align:left;color:#555;padding:12px;border-bottom:2px solid #222;}"));
    _server->sendContent(F("td{padding:12px;border-bottom:1px solid #111;color:#bbb;} .tag{padding:4px 8px;border-radius:4px;font-size:10px;font-weight:bold;}"));
    _server->sendContent(F(".CRITICAL{background:#a22;color:#fff;} .INFO{background:#22a;color:#fff;} .STATE_CHANGE{background:#0fc;color:#000;}"));
    _server->sendContent(F("</style>"));
}

void NetworkService::begin(const char* ssid, const char* pass) {
    _prefs.begin("net_stats", true);
    String s = _prefs.getString("w_ssid", ssid), p = _prefs.getString("w_pass", pass);
    _prefs.end();
    WiFi.mode(WIFI_STA); 
    WiFi.setSleep(false);
    
    // V6.3: Migración forzada para evitar colisión con el "fantasma" .36
    IPAddress local_IP(192, 168, 1, 40);
    IPAddress gateway(192, 168, 1, 1);
    IPAddress subnet(255, 255, 255, 0);
    IPAddress primaryDNS(8, 8, 8, 8);
    
    if (!WiFi.config(local_IP, gateway, subnet, primaryDNS)) {
        Serial.println("[!] V6.3: FAILED TO CONFIGURE STATIC IP");
    }

    WiFi.begin(s.c_str(), p.c_str());
    _startTime = millis();
}

void NetworkService::update(FileLogger& logger, DashboardRenderer& renderer) {
    unsigned long u = millis() - _startTime;
    if (_bootPhase == 0 && u > 8000) {
        logger.begin(); _prefs.begin("net_stats", false);
        _reconnectCount = _prefs.getInt("recon", 0); _gmtOffset = _prefs.getInt("gmt", -5);
        _historicalUptime = _prefs.getULong("t_uptime", 0); _prefs.end();
        Serial.printf("[NET] IP: %s | GW: %s | MASK: %s | DNS: %s\n", 
                      WiFi.localIP().toString().c_str(), WiFi.gatewayIP().toString().c_str(), 
                      WiFi.subnetMask().toString().c_str(), WiFi.dnsIP().toString().c_str());
        _bootPhase = 1; 
    }
    if (_bootPhase == 1 && u > 12000) { if (!_server) _server = new WebServer(80); if (!_dnsServer) _dnsServer = new DNSServer(); _setupWebServer(logger, renderer); _bootPhase = 2; }
    
    // Huso horario Lima (TZ Native)
    if (_bootPhase == 2 && u > 18000) { configTime(0, 0, "pool.ntp.org", "time.google.com"); setenv("TZ", "<-05>5", 1); tzset(); _bootPhase = 3; }

    if (_server) _server->handleClient();
    if (_dnsServer && _isConfigMode) _dnsServer->processNextRequest();

    time_t now; time(&now);
    bool hasTime = (now > 1000000);

    if (hasTime && _bootPhase >= 3) {
        logger.checkStartupReason();
        logger.estimateLastPowerOff();
        
        // Auditoría de Recursos v7.6.2
        if (millis() - _lastResourceAudit >= 5000) {
            uint32_t freeH = ESP.getFreeHeap();
            uint32_t maxH = ESP.getMaxAllocHeap();
            uint32_t minH = ESP.getMinFreeHeap();
            Serial.printf("[AUDIT] HEAP: %u KB | MAX_BLOCK: %u KB | MIN_EVER: %u KB | STACK: %u\n", 
                          freeH/1024, maxH/1024, minH/1024, uxTaskGetStackHighWaterMark(NULL));
            _lastResourceAudit = millis();
        }

        // Acelerador de Logs v7.5.0:HEARTBEAT cada 10 minutos (600,000 ms)
        if (millis() - _lastHeartbeat >= 600000) {
            String msg = "ALIVE | RSSI: " + String(WiFi.RSSI()) + " | IP: " + WiFi.localIP().toString();
            logger.logEvent("HEARTBEAT", msg.c_str());
            _lastHeartbeat = millis();

            _prefs.begin("net_stats", false);
            _prefs.putULong("t_uptime", _historicalUptime + (millis() - _startTime));
            _prefs.end();
        }

        // Telemetría Cloud v9.0: Push cada 5 segundos si hay configuración
        if (millis() - _lastCloudPush >= 5000 && !_supabaseUrl.isEmpty()) {
            _pushToCloud();
            _lastCloudPush = millis();
        }
    }

    bool c = (WiFi.status() == WL_CONNECTED);
    if (c) {
        if (!_lastConnectedTime || (_lastConnectedTime == 0 && c)) _connectionTrigger = true;
        _lastConnectedTime = millis();
        if (_isConfigMode) { WiFi.softAPdisconnect(true); if (_dnsServer) _dnsServer->stop(); _isConfigMode = false; }
        
        // V5.1: Captura de Auditoría (BSSID y Protocolo) cada 10s
        if (millis() - _lastExtraUpdate >= 10000) {
            _lastExtraUpdate = millis();
            wifi_ap_record_t ap;
            if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
                char b[20];
                sprintf(b, "%02X:%02X:%02X:%02X:%02X:%02X", 
                        ap.bssid[0], ap.bssid[1], ap.bssid[2], 
                        ap.bssid[3], ap.bssid[4], ap.bssid[5]);
                _lastBSSID = String(b);
                if (ap.phy_11ax) _lastPhyMode = "AX";
                else if (ap.phy_11n) _lastPhyMode = "N";
                else if (ap.phy_11g) _lastPhyMode = "G";
                else _lastPhyMode = "LEG";
            }
        }
    } else {
        if (u > 30000 && !c && !_isConfigMode && _bootPhase >= 2) { _isConfigMode = true; WiFi.softAP("WiFi-Monitor-C6"); if (_dnsServer) _dnsServer->start(53, "*", WiFi.softAPIP()); }
        if (millis() - _lastReconnectAttempt > 10000) { _lastReconnectAttempt = millis(); WiFi.reconnect(); _reconnectCount++; }
    }
}

// V4.8: Enfoque Maestro - Solo Google Hostname para veracidad absoluta

void NetworkService::setHistory(const int* data, int size, int index) {
    for (int i = 0; i < 24; i++) {
        int pos = (index + i) % size;
        _webHistory[i] = data[pos];
    }
}

void NetworkService::_setupWebServer(FileLogger& logger, DashboardRenderer& renderer) {
    _server->on("/", [this, &logger]() { _handleRoot(logger); });
    _server->on("/logs", [this, &logger]() { _handleLogs(logger); });
    _server->on("/config", [this]() { _handleConfig(); });
    _server->on("/capture.bmp", [this, &renderer]() { 
        if (_resMutex && xSemaphoreTake(_resMutex, pdMS_TO_TICKS(1000))) {
            renderer.serveScreenshot(*_server); 
            xSemaphoreGive(_resMutex);
        } else {
            _server->send(503, "text/plain", "Busy");
        }
    });

    _server->on("/status", [this]() {
        NetworkData d = getData();
        static char jsonBuffer[1024]; 
        
        // Regla #3: Construcción atómica del buffer del historial
        char hBuf[128] = "[";
        for (int i = 0; i < 24; i++) {
            char val[8];
            itoa(_webHistory[i], val, 10);
            strcat(hBuf, val);
            if (i < 23) strcat(hBuf, ",");
        }
        strcat(hBuf, "]");

        snprintf(jsonBuffer, sizeof(jsonBuffer), 
            "{\"u\":\"%s\",\"rssi\":%d,\"snr\":%d,\"ch\":%d,\"bs\":\"%s\",\"gw\":%d,\"ex\":%d,\"ls\":%d,\"dr\":%d,\"sc\":%d,\"h\":%s,\"m\":%lu}",
            getUptimeString().c_str(), d.rssi, d.snr, d.channel, d.bssid.c_str(),
            d.pingGW, d.pingInternet, d.packetLoss, _reconnectCount, d.score, hBuf, (unsigned long)(ESP.getFreeHeap()/1024));
            
        _server->send(200, "application/json", jsonBuffer);
    });
    _server->begin();
    Serial.println("[BOOT] PHASE 2: WebServer Active");
}

void NetworkService::_handleRoot(FileLogger& logger) {
    if (_resMutex && xSemaphoreTake(_resMutex, pdMS_TO_TICKS(1000))) {
        _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        _server->send(200, "text/html", "");

        // Bloque 1: Cabecera, CSS y Script de Control
        String h = F("<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>");
        _server->sendContent(h);
        _sendCommonCSS(); // v8.5: Estilos desde Flash
        
        h = F("<script>async function update(){try{const r=await fetch('/status');const d=await r.json();");
        h += F("document.getElementById('clk').innerText=new Date().toLocaleTimeString();");
        h += F("document.getElementById('val_sig').innerText=d.rssi+'d/'+d.snr+'dB';");
        h += F("document.getElementById('val_ch').innerText=d.ch+'(AX)';");
        h += F("document.getElementById('val_bs').innerText=d.bs.substring(6);");
        h += F("document.getElementById('val_re').innerText=d.u;");
        h += F("document.getElementById('h_bar').style.width=d.sc+'%';");
        h += F("const chart=document.getElementById('chart');chart.innerHTML='';");
        h += F("d.h.forEach(v=>{const b=document.createElement('div');b.className='bar-web';b.style.height=((v*100)/250)+'%';chart.appendChild(b);});");
        h += F("}catch(e){}} setTimeout(() => setInterval(update, 5000), 3000);</script></head><body>");
        _server->sendContent(h);

        // Bloque 2: Estructura Visual y Datos
        String b = F("<div class='lcd-container'><div class='nav-tabs'><a href='/' class='nav-tab active'>DASHBOARD</a>");
        b += F("<a href='/logs' class='nav-tab'>EVENT LOGS</a><a href='/config' class='nav-tab'>SETTINGS</a></div>");
        b += F("<div class='dashboard'><div class='bento-header'><div style='color:#0ff;font-weight:bold;'>SSID: ");
        b += WiFi.SSID();
        b += F("</div><div style='color:#555;'>IP: 192.168.1.40</div><div id='clk'>--</div></div>");
        b += F("<div id='chart' class='chart-container'></div><div class='health-bar-bg'><div id='h_bar' class='health-bar-fill'></div></div>");
        b += F("<div class='grid'><div class='card'><div class='label'>SIGNAL QUALITY</div><div class='value' id='val_sig'>--</div></div>");
        b += F("<div class='card'><div class='label'>PHYSICAL LAYER</div><div class='value' id='val_ch'>--</div></div>");
        b += F("<div class='card'><div class='label'>ACCESS POINT BSSID</div><div class='value' id='val_bs'>--</div></div>");
        b += F("<div class='card'><div class='label'>SYSTEM UPTIME</div><div class='value' id='val_re'>--</div></div>");
        b += F("</div></div></div></body></html>");
        _server->sendContent(b);

        _server->sendContent("");
        xSemaphoreGive(_resMutex);
    } else {
        _server->send(503, "text/plain", "Busy");
    }
}

void NetworkService::_handleLogs(FileLogger& logger) {
    if (_resMutex && xSemaphoreTake(_resMutex, pdMS_TO_TICKS(2000))) {
        _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        _server->send(200, "text/html", "");

        _server->sendContent(F("<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"));
        _sendCommonCSS();
        _server->sendContent(F("</head><body><div class='lcd-container'><div class='nav-tabs'>"));
        _server->sendContent(F("<a href='/' class='nav-tab'>DASHBOARD</a>"));
        _server->sendContent(F("<a href='/logs' class='nav-tab active'>EVENT LOGS</a>"));
        _server->sendContent(F("<a href='/config' class='nav-tab'>SETTINGS</a></div>"));
        _server->sendContent(F("<div class='dashboard'><div class='bento-header'><div>EVENT LOGS (Last 20)</div><div style='color:#555;'>v8.5 ATOMIC</div></div>"));
        _server->sendContent(F("<table><thead><tr><th>DATE</th><th>TIME</th><th>TYPE</th><th>MESSAGE</th></tr></thead><tbody>"));

        File f = LittleFS.open("/log.txt", "r");
        if (f) {
            if (f.size() > 2048) f.seek(f.size() - 2048);
            String rows[20];
            int count = 0;
            while (f.available() && count < 20) {
                yield(); 
                esp_task_wdt_reset(); // v8.6: Feed Watchdog in loops
                String l = f.readStringUntil('\n');
                int s1 = l.indexOf('|'), s2 = l.indexOf('|', s1 + 1), s3 = l.indexOf('|', s2 + 1);
                if (s1 > 0 && s3 > 0) {
                    String tp = l.substring(s2 + 1, s3);
                    if (tp == "HEARTBEAT") continue;
                    String row = "<tr><td>" + l.substring(0, s1) + "</td><td>" + l.substring(s1 + 1, s2) + "</td>";
                    row += "<td><span class='tag " + tp + "'>" + tp + "</span></td><td>" + l.substring(s3 + 1) + "</td></tr>";
                    rows[count++] = row;
                }
            }
            f.close();
            for(int i=count-1; i>=0; i--) _server->sendContent(rows[i]);
        }
        
        _server->sendContent(F("</tbody></table></div></div><script>setTimeout(()=>location.reload(),10000);</script></body></html>"));
        _server->sendContent("");
        xSemaphoreGive(_resMutex);
    } else {
        _server->send(503, "text/plain", "Hardware Busy");
    }
}

void NetworkService::_handleConfig() {
    if (_resMutex && xSemaphoreTake(_resMutex, pdMS_TO_TICKS(1000))) {
        _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
        _server->send(200, "text/html", "");

        _server->sendContent(F("<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"));
        _sendCommonCSS();
        _server->sendContent(F("</head><body><div class='lcd-container'><div class='nav-tabs'>"));
        _server->sendContent(F("<a href='/' class='nav-tab'>DASHBOARD</a>"));
        _server->sendContent(F("<a href='/logs' class='nav-tab'>EVENT LOGS</a>"));
        _server->sendContent(F("<a href='/config' class='nav-tab active'>SETTINGS</a></div>"));
        _server->sendContent(F("<div class='dashboard'><div class='bento-header'><div>SYSTEM CONFIGURATION</div><div style='color:#555;'>v8.5 MASTER</div></div>"));
        
        _server->sendContent(F("<form action='/save' method='POST' style='max-width:400px;margin:20px 0;'>"));
        _server->sendContent(F("<div class='label'>WIFI SSID</div><input type='text' name='ssid' style='width:100%;background:#111;color:#fff;border:1px solid #333;margin-bottom:20px;padding:12px;border-radius:4px;'>"));
        _server->sendContent(F("<div class='label'>WIFI PASSWORD</div><input type='password' name='pass' style='width:100%;background:#111;color:#fff;border:1px solid #333;margin-bottom:20px;padding:12px;border-radius:4px;'>"));
        _server->sendContent(F("<div class='label'>GMT OFFSET (Lima: -5)</div><input type='number' name='gmt' value='-5' style='width:100%;background:#111;color:#fff;border:1px solid #333;margin-bottom:30px;padding:12px;border-radius:4px;'>"));
        _server->sendContent(F("<input type='submit' value='APPLY & REBOOT SYSTEM' style='background:#0fc;color:#000;padding:15px;border:none;border-radius:4px;font-weight:bold;width:100%;cursor:pointer;'></form>"));
        
        _server->sendContent(F("</div></div></body></html>"));
        _server->sendContent("");
        xSemaphoreGive(_resMutex);
    } else {
        _server->send(503, "text/plain", "Busy");
    }
}

String NetworkService::getUptimeString() { 
    unsigned long s = (_historicalUptime + (millis() - _startTime)) / 1000; 
    char b[16]; sprintf(b, "%02luh %02lum %02lus", s/3600, (s%3600)/60, s%60); 
    return String(b); 
}

NetworkData NetworkService::getData() { 
    NetworkData d; d.connected = (WiFi.status() == WL_CONNECTED); 
    if (d.connected) { 
        d.rssi = WiFi.RSSI(); d.ip = WiFi.localIP().toString(); d.channel = WiFi.channel(); 
        d.pingGW = _lastPingGW; d.pingInternet = _lastPingInternet; d.score = _lastScore; 
        d.jitter = _lastJitter; d.packetLoss = _lastPacketLoss; 
        
        // SNR Estimado (V7.4.0 Legacy Mode para C6)
        d.snr = 25; // Valor estático por defecto hasta integración PHY
        
        d.linkEfficiency = _lastLinkEfficiency;
        d.bssid = _lastBSSID; d.phyMode = _lastPhyMode;
    } else { 
        d.rssi = -100; d.channel = 0; d.pingGW = -1; d.pingInternet = -1; d.score = 0; 
        d.jitter = 0; d.packetLoss = 0; d.snr = 0; d.linkEfficiency = 0;
        d.bssid = "00:00:00:00:00:00"; d.phyMode = "OFF";
    } 
    return d; 
}

void NetworkService::setSupabaseConfig(String url, String key) {
    _supabaseUrl = url;
    _supabaseKey = key;
    Serial.println("[CLOUD] Supabase Config Received");
}

int NetworkService::getReconnectCount() { return _reconnectCount; }
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
void NetworkService::setQuality(int score, int jitter, int loss, int snr, float efficiency) { 
    _lastScore = score; 
    _lastJitter = jitter; 
    _lastPacketLoss = loss;
    _lastSNR = snr;
    _lastLinkEfficiency = efficiency;
}

void NetworkService::_pushToCloud() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    WiFiClientSecure client;
    client.setInsecure(); // Simplificado para Supabase HTTPS
    HTTPClient http;
    
    // Endpoint Digital Twin: Fila id=1
    String url = _supabaseUrl + "/rest/v1/device_state?id=eq.1";
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("apikey", _supabaseKey);
    http.addHeader("Authorization", "Bearer " + _supabaseKey);
    http.addHeader("Prefer", "resolution=merge-duplicates");

    NetworkData d = getData();
    char json[512];
    snprintf(json, sizeof(json), 
        "{\"payload\":{\"rssi\":%d,\"snr\":%d,\"score\":%d,\"uptime\":\"%s\",\"packetLoss\":%d,\"channel\":%d,\"ip\":\"%s\",\"bssid\":\"%s\",\"status\":\"ONLINE\"}}",
        d.rssi, d.snr, d.score, getUptimeString().c_str(), d.packetLoss, d.channel, d.ip.c_str(), d.bssid.c_str());

    int httpCode = http.sendRequest("PATCH", json);
    
    if (httpCode > 0) {
        Serial.printf("[CLOUD] Digital Twin Updated (Code: %d)\n", httpCode);
    } else {
        Serial.printf("[CLOUD] Push Error: %s\n", http.errorToString(httpCode).c_str());
    }
    
    http.end();
}
