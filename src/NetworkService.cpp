#include "NetworkService.h"
#include <time.h>
#include "esp_system.h"
#include "esp_wifi.h"

NetworkService::NetworkService() {}

// CSS V4.0 - MASTERPIECE INDUSTRIAL
String getCommonCSS() {
    String c = "<style>body{background:#080808;color:#ccc;font-family:'Inter','Segoe UI',sans-serif;margin:0;padding-bottom:50px;}";
    c += ".top-bar{background:#111;padding:12px 25px;border-bottom:2px solid #00ffcc;display:flex;justify-content:space-between;align-items:center;max-width:1100px;margin:0 auto 20px;box-sizing:border-box;}";
    c += ".title{color:#00ffcc;font-weight:600;font-size:0.95em;letter-spacing:1px;} .clock{color:#777;font-family:monospace;font-size:0.85em;}";
    c += ".nav{display:flex;gap:12px;} .nav a{color:#00ffcc;font-weight:600;font-size:0.7em;padding:6px 14px;border:1px solid #333;border-radius:4px;text-decoration:none;background:#1a1a1a;transition:0.2s;} .nav a:hover{background:#00ffcc;color:#000;}";
    
    // Homologación de anchos 1100px
    c += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;padding:0;max-width:1100px;margin:0 auto 25px;}";
    c += ".card{background:#111;padding:12px;border-radius:8px;border:1px solid #222;box-shadow:0 4px 10px rgba(0,0,0,0.5);} .card div:first-child{font-size:0.62em;color:#555;margin-bottom:4px;text-transform:uppercase;letter-spacing:0.5px;} .val{font-size:0.95em;font-weight:bold;color:#eee;}";
    
    c += "h2{font-size:0.85em;color:#00ffcc;margin:30px auto 10px;text-align:left;max-width:1100px;padding:0 5px;text-transform:uppercase;letter-spacing:1.5px;font-weight:500;}";
    
    // Chart Precision Refinement
    c += ".chart-box{background:#111;padding:15px;border-radius:10px;border:1px solid #222;max-width:1100px;margin:10px auto;display:flex;height:240px;box-sizing:border-box;}";
    c += ".canvas{flex-grow:1;height:200px;background:#050505;border:1px solid #222;margin:0 12px;position:relative;padding-top:10px;box-sizing:border-box;}";
    
    // Axis Alignment (Centered with grid lines)
    c += ".axis-l, .axis-r{height:200px;display:flex;flex-direction:column;justify-content:space-between;font-size:10px;color:#444;padding-top:10px;box-sizing:border-box;}";
    c += ".axis-r{width:80px;color:#0fc;text-align:left;padding-left:10px;}";
    c += ".axis-l{width:40px;text-align:right;padding-right:8px;}";
    
    c += ".log-container{max-width:1100px;margin:20px auto;border:1px solid #333;border-radius:8px;overflow:hidden;background:#0c0c0c;}";
    c += ".log-grid{display:grid;grid-template-columns:110px 110px 160px 1fr;text-align:left;font-family:monospace;font-size:0.95em;}";
    c += ".log-h{background:#00ffcc;color:#000;font-weight:bold;padding:12px;font-size:0.85em;}";
    c += ".log-scroll{height:500px;overflow-y:auto;} .log-row{padding:10px 12px;border-bottom:1px solid #1a1a1a;}";
    c += ".tag{padding:2px 5px;border-radius:3px;font-size:0.65em;font-weight:bold;} .CRITICAL{background:#e55;color:#fff;} .STATE_CHANGE{background:#0fc;color:#000;} .HEARTBEAT{color:#333;}";
    c += "</style>";
    return c;
}

void NetworkService::begin(const char* ssid, const char* pass) {
    _prefs.begin("net_stats", true);
    String s = _prefs.getString("w_ssid", ssid), p = _prefs.getString("w_pass", pass);
    _prefs.end();
    WiFi.mode(WIFI_STA); WiFi.setSleep(false); // V4.6: Máxima alerta (No Sleep)
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
        logger.sendHeartbeat(WiFi.RSSI(), WiFi.localIP().toString());
    }

    bool c = (WiFi.status() == WL_CONNECTED);
    if (c) {
        if (!_lastConnectedTime || (_lastConnectedTime == 0 && c)) _connectionTrigger = true;
        _lastConnectedTime = millis();
        if (_isConfigMode) { WiFi.softAPdisconnect(true); if (_dnsServer) _dnsServer->stop(); _isConfigMode = false; }
        if (_bootPhase >= 1 && (millis() - _lastPingTime > 3000)) { _lastPingTime = millis(); _performPing(); }
        
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

void NetworkService::_performPing() {
    IPAddress gw = WiFi.gatewayIP();
    
    // V4.8: Enfoque Maestro - Solo Google Hostname para veracidad absoluta
    bool gwOk = Ping.ping(gw, 2);
    _lastPingGW = gwOk ? Ping.averageTime() : -1;
    
    delay(100); yield(); // Limpieza de aire
    
    bool extOk = Ping.ping("www.google.com", 2);
    _lastPingInternet = extOk ? Ping.averageTime() : -1;
    
    Serial.printf("[DIAG] GW:%s (%dms) | GOOGLE:%s (%dms)\n", 
                  gwOk?"OK":"FAIL", _lastPingGW, extOk?"OK":"FAIL", _lastPingInternet);
}

void NetworkService::_setupWebServer(FileLogger& logger, DashboardRenderer& renderer) {
    _server->on("/", [this, &logger]() { _handleRoot(logger); });
    _server->on("/logs", [this, &logger]() { _handleLogs(logger); });
    _server->on("/config", [this]() { _handleConfig(); });
    _server->on("/capture.bmp", [this, &renderer]() { renderer.serveScreenshot(*_server); });
    _server->on("/status", [this]() {
        uint32_t fh = ESP.getFreeHeap();
        String j = "{\"u\":\"" + getUptimeString() + "\",\"r\":" + String(WiFi.RSSI()) + ",\"pg\":" + String(_lastPingGW) + ",\"pn\":" + String(_lastPingInternet) + ",\"qs\":" + String(_lastScore) + ",\"re\":" + String(_reconnectCount) + ",\"h\":" + String(fh/1024) + "}";
        _server->send(200, "application/json", j);
    });
    _server->begin();
}

void NetworkService::_handleRoot(FileLogger& logger) {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getCommonCSS();
    h += "<script>function ck(){let n=new Date();document.getElementById('clk').innerText=n.toISOString().split('T')[0]+' '+n.toTimeString().split(' ')[0];} setInterval(ck,1000);</script></head><body>";
    h += "<div class='top-bar'><div class='title'>WIFI QUALITY MONITOR</div><div class='nav'><a href='/'>DASHBOARD</a><a href='/logs'>LOGGER</a><a href='/config'>SETTINGS</a></div><div class='clock' id='clk'>--</div></div>";
    
    // ZONA 0: V-MIRROR (Espejo Visual Industrial)
    h += "<h2>LIVE V-DISPLAY (LCD MIRROR)</h2>";
    h += "<div style='max-width:1100px;margin:10px auto;background:#111;padding:15px;border-radius:10px;border:1px solid #0fc;text-align:center;'>";
    h += "<img id='vdisplay' src='/capture.bmp' style='width:100%;max-width:640px;border:2px solid #333;border-radius:4px;image-rendering:pixelated;'>";
    h += "<div style='margin-top:10px;'><button onclick=\"document.getElementById('vdisplay').src='/capture.bmp?'+new Date().getTime()\" style='background:#1a1a1a;color:#0fc;border:1px solid #0fc;padding:8px 20px;cursor:pointer;border-radius:4px;font-size:0.8em;font-weight:bold;'>REFRESH LCD VIEW</button></div></div>";

    h += "<h2>LIVE METRICS</h2><div class='grid'>";
    h += "<div class='card'><div>UPTIME</div><div class='val'>"+getUptimeString()+"</div></div>";
    h += "<div class='card'><div>RSSI</div><div class='val'>"+String(WiFi.RSSI())+" dBm</div></div><div class='card'><div>SCORE</div><div class='val'>"+String(_lastScore)+"%</div></div>";
    h += "<div class='card'><div>GATEWAY</div><div class='val'>"+String(_lastPingGW)+" ms</div></div><div class='card'><div>LATENCY (ms)</div><div class='val'>"+String(_lastPingInternet)+" ms</div></div>";
    h += "<div class='card'><div>RAM (FREE)</div><div class='val'>"+String(ESP.getFreeHeap()/1024)+" KB</div></div><div class='card'><div>RECONS</div><div class='val'>"+String(_reconnectCount)+"</div></div></div>";
     h += "<h2>WIFI QUALITY</h2><div class='chart-box'><div class='axis-l'><div>100%</div><div>75%</div><div>50%</div><div>25%</div><div>0%</div></div><div class='canvas'><svg width='100%' height='100%' viewBox='0 0 600 110' preserveAspectRatio='none'>";
    for(int y=10; y<=110; y+=25) h += "<line x1='0' y1='"+String(y)+"' x2='600' y2='"+String(y)+"' stroke='#222'/>";
    int hi[46], id; if(logger.loadTrend(hi, 46, &id)) { h += "<polyline points='"; for(int i=0; i<46; i++) h += String(i*13) + "," + String(110-(hi[(id+i)%46]/10)) + " "; h += "' fill='none' stroke='#0fc' stroke-width='1.5'/></svg></div>"; h += "<div class='axis-r'><div>EXCELLENT</div><div>GOOD</div><div>DEGRADED</div><div>CRITICAL</div></div></div>"; }
    
    h += "<h2>SYSTEM RAM</h2><div class='chart-box'><div class='axis-l'><div>100%</div><div>75%</div><div>50%</div><div>25%</div><div>0%</div></div><div class='canvas'><svg width='100%' height='100%' viewBox='0 0 600 110' preserveAspectRatio='none'>";
    for(int y=10; y<=110; y+=25) h += "<line x1='0' y1='"+String(y)+"' x2='600' y2='"+String(y)+"' stroke='#222'/>";
    int rh[46], rid; if(logger.loadRam(rh, &rid)) { h += "<polyline points='"; for(int i=0; i<46; i++) h += String(i*13) + "," + String(110-((rh[(rid+i)%46]*100)/512)) + " "; h += "' fill='none' stroke='#48f' stroke-width='1.5'/></svg></div>"; h += "<div class='axis-r'><div>512 KB</div><div>384 KB</div><div>256 KB</div><div>128 KB</div><div>0 KB</div></div></div>"; }
    h += "</body></html>"; _server->send(200, "text/html", h);
}

void NetworkService::_handleLogs(FileLogger& logger) {
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _server->send(200, "text/html", "");
    _server->sendContent("<html><head><meta charset='UTF-8'>" + getCommonCSS() + "</head><body>" );
    _server->sendContent("<div class='top-bar'><div class='title'>WIFI QUALITY MONITOR</div><div class='nav'><a href='/'>DASHBOARD</a><a href='/logs'>LOGGER</a><a href='/config'>SETTINGS</a></div><div class='clock'>LOGGER ACTIVE</div></div>");
    _server->sendContent("<h2>EVENT LOGGER</h2><div class='log-container'><div class='log-grid log-h'><div>DATE</div><div>TIME</div><div>EVENT</div><div>DESCRIPTION</div></div><div class='log-scroll'>");
    File f = LittleFS.open("/log.txt", FILE_READ); if (f.size() > 5000) f.seek(f.size() - 5000); String data = f.readString(); f.close();
    int lineStart = data.indexOf('\n') + 1; String buf[46]; int count = 0;
    while(lineStart < data.length() && count < 46) { 
        int next = data.indexOf('\n', lineStart); if (next == -1) next = data.length(); 
        buf[count++] = data.substring(lineStart, next); lineStart = next + 1; 
    }
    for(int i=count-1; i>=0; i--) { String l = buf[i]; int s1=l.indexOf('|'), s2=l.indexOf('|', s1+1), s3=l.indexOf('|', s2+1); if(s1>0 && s3>0) { String dt=l.substring(0,s1), tm=l.substring(s1+1,s2), tp=l.substring(s2+1,s3), msg=l.substring(s3+1); if (tp == "HEARTBEAT") continue; _server->sendContent("<div class='log-grid log-row'><div>"+dt+"</div><div>"+tm+"</div><div><span class='tag "+tp+"'>"+tp+"</span></div><div>"+msg+"</div></div>"); } }
    _server->sendContent("</div></div><script>setTimeout(()=>location.reload(),5000);</script></body></html>"); _server->client().stop();
}

void NetworkService::_handleConfig() {
    String h = "<html><head><meta charset='UTF-8'>" + getCommonCSS() + "</head><body>" ;
    h += "<div class='top-bar'><div class='title'>WIFI QUALITY MONITOR</div><div class='nav'><a href='/'>DASHBOARD</a><a href='/logs'>LOGGER</a><a href='/config'>SETTINGS</a></div><div class='clock'>CONFIG MODE</div></div>";
    h += "<h2>Settings</h2><form action='/save' method='POST' style='display:inline-block;max-width:1100px;margin:20px auto;width:100%;text-align:left;padding-left:10px;'>";
    h += "SSID:<br><input type='text' name='ssid' style='width:300px;background:#111;color:#fff;border:1px solid #333;margin:8px 0;padding:8px;'><br>Pass:<br><input type='password' name='pass' style='width:300px;background:#111;color:#fff;border:1px solid #333;margin:8px 0;padding:8px;'><br>GMT Offset:<br><input type='number' name='gmt' value='"+String(_gmtOffset)+"' style='width:80px;background:#111;color:#fff;border:1px solid #333;margin:8px 0;padding:8px;'><br><br><input type='submit' value='SAVE & REBOOT' style='background:#0fc;color:#000;padding:10px 20px;border:none;border-radius:4px;cursor:pointer;'></form></body></html>";
    _server->send(200, "text/html", h);
}

String NetworkService::getUptimeString() { 
    unsigned long s = (_historicalUptime + (millis() - _startTime)) / 1000; 
    char b[16]; sprintf(b, "%02luh %02lum %02lus", s/3600, (s%3600)/60, s%60); 
    return String(b); 
}

NetworkService::NetworkData NetworkService::getData() { 
    NetworkData d; d.connected = (WiFi.status() == WL_CONNECTED); 
    if (d.connected) { 
        d.rssi = WiFi.RSSI(); d.ip = WiFi.localIP().toString(); d.channel = WiFi.channel(); 
        d.pingGW = _lastPingGW; d.pingInternet = _lastPingInternet; d.score = _lastScore; 
        d.jitter = _lastJitter; d.packetLoss = _lastPacketLoss; 
        d.snr = _lastSNR; d.linkEfficiency = _lastLinkEfficiency;
        d.bssid = _lastBSSID; d.phyMode = _lastPhyMode;
    } else { 
        d.rssi = -100; d.channel = 0; d.pingGW = -1; d.pingInternet = -1; d.score = 0; 
        d.jitter = 0; d.packetLoss = 0; d.snr = 0; d.linkEfficiency = 0;
        d.bssid = "00:00:00:00:00:00"; d.phyMode = "OFF";
    } 
    return d; 
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
