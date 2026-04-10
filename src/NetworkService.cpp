#include "NetworkService.h"
#include <time.h>
#include "esp_system.h"

NetworkService::NetworkService() {}

// Helper para CSS (V2.5 - Estética Grid Bloqueada)
String getCommonCSS() {
    String c = "<style>body{background:#080808;color:#ddd;font-family:'Segoe UI',sans-serif;text-align:center;margin:0;padding-bottom:30px;}";
    c += ".nav{background:#111;padding:12px;border-bottom:1px solid #00ffcc;margin-bottom:15px;display:flex;justify-content:center;gap:10px;}";
    c += ".nav a{color:#00ffcc;font-weight:bold;font-size:0.8em;padding:6px 12px;border:1px solid #222;border-radius:4px;text-decoration:none;}";
    c += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,1fr));gap:10px;padding:0 15px;max-width:1100px;margin:auto;}";
    c += ".card{background:#111;padding:12px;border-radius:8px;border:1px solid #222;} .card div:first-child{font-size:0.65em;color:#888;margin-bottom:4px;}";
    c += "h2{font-size:0.95em;color:#00ffcc;margin:25px auto 10px;text-align:left;max-width:1100px;padding-left:15px;}";
    c += ".chart-box{background:#111;padding:15px;border-radius:10px;border:1px solid #222;max-width:1100px;margin:10px auto;display:flex;align-items:center;}";
    c += ".canvas{flex-grow:1;height:180px;background:#000;border:1px solid #333;margin:0 10px;} .axis-l{width:40px;height:180px;display:flex;flex-direction:column;justify-content:space-between;font-size:0.7em;color:#666;}";
    c += ".axis-r{width:90px;height:180px;display:flex;flex-direction:column;justify-content:space-between;font-size:0.65em;color:#0fc;text-align:left;}";
    c += ".log-container{max-width:1100px;margin:20px auto;border:1px solid #333;border-radius:8px;overflow:hidden;background:#111;}";
    c += ".log-grid{display:grid;grid-template-columns:110px 110px 180px 1fr;text-align:left;font-family:monospace;font-size:1.05em;}";
    c += ".log-h{background:#00ffcc;color:#000;font-weight:bold;padding:12px;position:sticky;top:0;z-index:10;}";
    c += ".log-scroll{height:500px;overflow-y:auto;} .log-row{padding:12px 10px;border-bottom:1px solid #222;}";
    c += ".tag{padding:2px 4px;border-radius:3px;font-size:0.7em;font-weight:bold;} .CRITICAL{background:#f44;} .STATE_CHANGE{background:#0fc;color:#000;} .HEARTBEAT{color:#444;font-size:0.8em;}";
    c += "</style>";
    return c;
}

String getNav() { return "<div class='nav'><a href='/'>DASHBOARD</a><a href='/logs'>LOGGER</a><a href='/config'>SETTINGS</a></div>"; }

void NetworkService::begin(const char* ssid, const char* pass) {
    _prefs.begin("net_stats", true);
    String s = _prefs.getString("w_ssid", ssid), p = _prefs.getString("w_pass", pass);
    _prefs.end();
    // Calibración Nativa Lima (UTC-5)
    setenv("TZ", "<-05>5", 1); tzset();
    WiFi.mode(WIFI_STA); WiFi.begin(s.c_str(), p.c_str());
    _startTime = millis();
}

void NetworkService::update() {
    unsigned long u = millis() - _startTime;
    if (_bootPhase == 0 && u > 8000) {
        _fsReady = LittleFS.begin(true); _prefs.begin("net_stats", false);
        _historicalReconnects = _prefs.getInt("t_recon", 0); _historicalUptime = _prefs.getULong("t_uptime", 0);
        _reconnectCount = _prefs.getInt("recon", 0); _gmtOffset = _prefs.getInt("gmt", -5); _prefs.end();
        _bootPhase = 1; 
    }
    if (_bootPhase == 1 && u > 12000) { if (!_server) _server = new WebServer(80); if (!_dnsServer) _dnsServer = new DNSServer(); _setupWebServer(); _bootPhase = 2; }
    if (_bootPhase == 2 && u > 18000) { configTime(0, 0, "pool.ntp.org", "time.google.com"); _bootPhase = 3; }

    if (_server) _server->handleClient();
    if (_dnsServer && _isConfigMode) _dnsServer->processNextRequest();

    time_t now; time(&now);
    bool hasTime = (now > 1000000);

    // Auditoría de Booteo Única
    if (hasTime && !_bootReasonLogged) {
        checkStartupReason();
        estimateLastPowerOff();
        _bootReasonLogged = true;
    }

    // Heartbeat de 1 minuto (Caja Negra)
    if (_fsReady && hasTime && (millis() - _lastHeartbeatSent > 60000)) {
        logEvent("HEARTBEAT", "System Alive");
        _lastHeartbeatSent = millis();
    }

    bool c = (WiFi.status() == WL_CONNECTED);
    if (c) {
        if (!_lastConnectedTime || (_lastConnectedTime == 0 && c)) _connectionTrigger = true;
        _lastConnectedTime = millis();
        if (_isConfigMode) { WiFi.softAPdisconnect(true); if (_dnsServer) _dnsServer->stop(); _isConfigMode = false; _connectionTrigger = true; }
        if (_bootPhase >= 1 && (millis() - _lastPingTime > 5000)) { _lastPingTime = millis(); _performPing(); }
    } else {
        if (u > 30000 && !c && !_isConfigMode && _bootPhase >= 2) { _isConfigMode = true; WiFi.softAP("WiFi-Monitor-C6"); if (_dnsServer) _dnsServer->start(53, "*", WiFi.softAPIP()); }
        if (millis() - _lastReconnectAttempt > 10000) { _lastReconnectAttempt = millis(); WiFi.reconnect(); _reconnectCount++; }
    }
}

void NetworkService::checkStartupReason() {
    esp_reset_reason_t reason = esp_reset_reason();
    const char* r = "UNKNOWN";
    switch(reason) {
        case ESP_RST_POWERON: r = "POWER_ON"; break;
        case ESP_RST_EXT:     r = "PIN_BOOT"; break;
        case ESP_RST_SW:      r = "SOFTWARE_BOOT"; break;
        case ESP_RST_WDT:     r = "WATCHDOG_BOOT"; break;
        default:              r = "OTHER_BOOT"; break;
    }
    logEvent("RESTART_CAUSE", r);
}

void NetworkService::estimateLastPowerOff() {
    if (!_fsReady) return;
    File f = LittleFS.open("/log.txt", FILE_READ);
    if (!f) return;
    if (f.size() > 5000) f.seek(f.size() - 5000);
    String data = f.readString(); f.close();
    
    int lastH = data.lastIndexOf("HEARTBEAT");
    if (lastH > 20) {
        int lineStart = data.lastIndexOf('\n', lastH);
        if (lineStart == -1) lineStart = 0;
        else lineStart++;
        
        String line = data.substring(lineStart, data.indexOf('\n', lineStart));
        int s1 = line.indexOf('|'), s2 = line.indexOf('|', s1 + 1);
        if (s1 > 0 && s2 > 0) {
            String d = line.substring(0, s1), t = line.substring(s1 + 1, s2);
            // Inyectar evento de apagado estimado
            struct tm tm; strptime((d + " " + t).c_str(), "%Y-%m-%d %H:%M:%S", &tm);
            time_t offTime = mktime(&tm) + 60; // +1 min después del último latido
            logEventWithTime(offTime, "POWER_OFF_EST", "Power loss detected");
        }
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    time_t n; time(&n); logEventWithTime(n, type, data);
}

void NetworkService::logEventWithTime(time_t t, const char* type, const char* data) {
    char ds[16], ts[16]; struct tm ti; localtime_r(&t, &ti);
    if (t < 1000000) { strcpy(ds, "BOOT"); strcpy(ts, "BOOT"); }
    else { strftime(ds, 16, "%Y-%m-%d", &ti); strftime(ts, 16, "%H:%M:%S", &ti); }
    char msg[128]; sprintf(msg, "%s|%s|%s|%s", ds, ts, type, data);
    if (_fsReady) {
        _rotateLogs(); File f = LittleFS.open("/log.txt", FILE_APPEND);
        if (f) { f.println(msg); f.close(); }
    }
}

void NetworkService::_rotateLogs() {
    File f = LittleFS.open("/log.txt", FILE_READ); if (!f) return; size_t s = f.size(); f.close();
    if (s > 30000) { LittleFS.rename("/log.txt", "/log_old.txt"); LittleFS.remove("/log_old.txt"); }
}

void NetworkService::setQuality(int score, int jitter) { _lastScore = score; _lastJitter = jitter; }

bool NetworkService::saveTrend(const int* h, int s, int idx) {
    if (!_fsReady || !h) return false; File f = LittleFS.open("/trend.bin", FILE_WRITE); if (!f) return false;
    f.write((uint8_t*)&idx, sizeof(int)); f.write((uint8_t*)h, s * sizeof(int)); f.close(); return true;
}

bool NetworkService::loadTrend(int* h, int s, int* idx) {
    if (!_fsReady || !h || !idx) return false; if (!LittleFS.exists("/trend.bin")) return false;
    File f = LittleFS.open("/trend.bin", FILE_READ); if (!f) return false;
    f.read((uint8_t*)idx, sizeof(int)); f.read((uint8_t*)h, s * sizeof(int)); f.close(); return true;
}

bool saveRam(const int* h, int idx) {
    File f = LittleFS.open("/ram.bin", FILE_WRITE); if(!f) return false; f.write((uint8_t*)&idx, sizeof(int)); f.write((uint8_t*)h, 50*sizeof(int)); f.close(); return true;
}
bool loadRam(int* h, int* idx) {
    File f = LittleFS.open("/ram.bin", FILE_READ); if(!f) return false; f.read((uint8_t*)idx, sizeof(int)); f.read((uint8_t*)h, 50*sizeof(int)); f.close(); return true;
}

void NetworkService::_setupWebServer() {
    _server->on("/", [this]() { _handleRoot(); });
    _server->on("/logs", [this]() { _handleLogs(); });
    _server->on("/config", [this]() { _handleConfig(); });
    _server->on("/status", [this]() {
        NetworkData d = getData(); uint32_t fh = ESP.getFreeHeap();
        String j = "{\"u\":\"" + getUptimeString() + "\",\"r\":" + String(d.rssi) + ",\"pg\":" + String(d.pingGW) + ",\"pn\":" + String(d.pingInternet) + ",\"qs\":" + String(_lastScore) + ",\"re\":" + String(_reconnectCount) + ",\"h\":" + String(fh) + "}";
        _server->send(200, "application/json", j);
    });
    _server->begin();
}

void NetworkService::_handleRoot() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='10'>" + getCommonCSS();
    h += "<script>function up(){fetch('/status').then(r=>r.json()).then(d=>{";
    h += "const m={'u':d.u,'r':d.r+' dBm','pg':d.pg+' ms','pi':d.pn+' ms','qs':d.qs+'%','re':d.re,'h':(d.h/1024).toFixed(1)+' KB'};";
    h += "for(let k in m){let e=document.getElementById(k);if(e)e.innerText=m[k];}}); } setInterval(up,2500);</script></head><body>";
    h += getNav() + "<h2>LIVE METRICS</h2><div class='grid'>";
    h += "<div class='card'><div>UPTIME</div><div class='val' id='u'>--</div></div><div class='card'><div>RSSI</div><div class='val' id='r'>--</div></div><div class='card'><div>SCORE</div><div class='val' id='qs'>--</div></div>";
    h += "<div class='card'><div>GATEWAY</div><div class='val' id='pg'>--</div></div><div class='card'><div>INTERNET</div><div class='val' id='pi'>--</div></div><div class='card'><div>RAM</div><div class='val' id='h'>--</div></div><div class='card'><div>RECONS</div><div class='val' id='re'>--</div></div></div>";
    h += "<h2>WIFI QUALITY</h2><div class='chart-box'><div class='axis-l'><div>100%</div><div>50%</div><div>0%</div></div><div class='canvas'><svg width='100%' height='100%' viewBox='0 0 600 100' preserveAspectRatio='none'>";
    for(int y=25; y<=75; y+=25) h += "<line x1='0' y1='"+String(y)+"' x2='600' y2='"+String(y)+"' stroke='#222'/>";
    int hi[50], id; if(loadTrend(hi, 50, &id)) { h += "<polyline points='"; for(int i=0; i<50; i++) h += String(i*12) + "," + String(100-(hi[(id+i)%50])) + " "; h += "' fill='none' stroke='#0fc' stroke-width='3'/></svg></div>"; h += "<div class='axis-r'><div>EXCELLENT</div><div>GOOD</div><div>CRITICAL</div></div></div>"; }
    h += "<h2>SYSTEM RAM</h2><div class='chart-box'><div class='axis-l'><div>100%</div><div>50%</div><div>0%</div></div><div class='canvas'><svg width='100%' height='100%' viewBox='0 0 600 100' preserveAspectRatio='none'>";
    for(int y=25; y<=75; y+=25) h += "<line x1='0' y1='"+String(y)+"' x2='600' y2='"+String(y)+"' stroke='#222'/>";
    int rh[50], rid; if(loadRam(rh, &rid)) { h += "<polyline points='"; for(int i=0; i<50; i++) h += String(i*12) + "," + String(100-(rh[(rid+i)%50])) + " "; h += "' fill='none' stroke='#48f' stroke-width='2'/></svg></div>"; h += "<div class='axis-r'><div>STABLE</div><div>MID</div><div>LOW</div></div></div>"; }
    h += "</body></html>"; _server->send(200, "text/html", h);
}

void NetworkService::_handleLogs() {
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN); _server->send(200, "text/html", "");
    _server->sendContent("<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>" + getCommonCSS() + "</head><body>" + getNav());
    _server->sendContent("<h2>EVENT LOGGER</h2><div class='log-container'><div class='log-grid log-h'><div>DATE</div><div>TIME</div><div>EVENT</div><div>DATA</div></div><div class='log-scroll'>");
    
    File f = LittleFS.open("/log.txt", FILE_READ);
    if (f.size() > 4000) f.seek(f.size() - 4000);
    String data = f.readString(); f.close();
    
    int lineStart = data.indexOf('\n'); if (lineStart != -1) lineStart++; else lineStart = 0;
    String buf[60]; int count = 0;
    while(lineStart < data.length() && count < 60) {
        int next = data.indexOf('\n', lineStart); if (next == -1) next = data.length();
        buf[count++] = data.substring(lineStart, next); lineStart = next + 1;
    }
    for(int i=count-1; i>=0; i--) {
        String l = buf[i]; int s1=l.indexOf('|'), s2=l.indexOf('|', s1+1), s3=l.indexOf('|', s2+1);
        if(s1>0 && s2>0 && s3>0) {
            String dt=l.substring(0,s1), tm=l.substring(s1+1,s2), tp=l.substring(s2+1,s3), msg=l.substring(s3+1);
            if (tp == "HEARTBEAT") continue; // FILTRO: Ocultar ruido interno en web
            _server->sendContent("<div class='log-grid log-row'><div>"+dt+"</div><div>"+tm+"</div><div><span class='tag "+tp+"'>"+tp+"</span></div><div>"+msg+"</div></div>");
        }
    }
    _server->sendContent("</div></div></body></html>"); _server->client().stop();
}

void NetworkService::_handleConfig() {
    String h = "<html><head><meta charset='UTF-8'>" + getCommonCSS() + "</head><body>" + getNav() + "<h2>Settings</h2><form action='/save' method='POST' style='display:inline-block;'>";
    h += "SSID:<br><input type='text' name='ssid'><br>Pass:<br><input type='password' name='pass'><br>Token:<br><input type='password' name='token'><br><input type='submit' value='REBOOT' class='btn'></form></body></html>";
    _server->send(200, "text/html", h);
}

String NetworkService::getUptimeString() {
    unsigned long s = (_historicalUptime + (millis() - _startTime)) / 1000;
    char b[16]; sprintf(b, "%02luh %02lum %02lus", s/3600, (s%3600)/60, s%60); return String(b);
}

void NetworkService::_performPing() {
    static bool t = false; IPAddress g = WiFi.gatewayIP();
    if (!t) _lastPingGW = Ping.ping(g) ? Ping.averageTime() : -1;
    else _lastPingInternet = Ping.ping("8.8.8.8") ? Ping.averageTime() : -1;
    t = !t;
}

NetworkService::NetworkData NetworkService::getData() {
    NetworkData d; d.connected = (WiFi.status() == WL_CONNECTED);
    if (d.connected) { d.rssi = WiFi.RSSI(); d.ip = WiFi.localIP().toString(); d.pingGW = _lastPingGW; d.pingInternet = _lastPingInternet; d.score = _lastScore; d.jitter = _lastJitter; }
    else { d.rssi = -100; d.pingGW = -1; d.pingInternet = -1; d.score = 0; d.jitter = 0; }
    return d;
}

int NetworkService::getReconnectCount() { return _reconnectCount; }
float NetworkService::getDisconnectRate() {
    float h = (_historicalUptime + (millis() - _startTime)) / 3600000.0; return (h < 0.01) ? 0.0 : (float)(_historicalReconnects + _reconnectCount) / h;
}
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
