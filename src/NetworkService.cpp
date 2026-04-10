#include "NetworkService.h"
#include <time.h>

NetworkService::NetworkService() {}

// Helper para CSS (V2.3 - Consola Industrial Unificada)
String getCommonCSS() {
    String c = "<style>body{background:#080808;color:#ddd;font-family:'Segoe UI',sans-serif;text-align:center;margin:0;padding-bottom:30px;}";
    c += ".nav{background:#111;padding:12px;border-bottom:1px solid #00ffcc;margin-bottom:15px;display:flex;justify-content:center;gap:10px;}";
    c += ".nav a{color:#00ffcc;text-decoration:none;font-weight:bold;font-size:0.8em;padding:6px 12px;border:1px solid #222;border-radius:4px;}";
    c += ".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(140px,1fr));gap:10px;padding:0 15px;max-width:900px;margin:auto;}";
    c += ".card{background:#111;padding:12px;border-radius:8px;border:1px solid #222;} .card div:first-child{font-size:0.7em;color:#888;margin-bottom:4px;}";
    c += ".val{font-size:1.3em;font-weight:bold;color:#fff;} h2{font-size:1em;color:#00ffcc;margin:20px 0 10px;text-align:left;max-width:900px;margin-left:auto;margin-right:auto;padding-left:15px;}";
    c += ".chart-container{background:#000;padding:10px;border-radius:8px;border:1px solid #333;display:inline-block;margin:5px 0;width:95%;max-width:900px;}";
    c += "input,select{background:#111;color:#fff;border:1px solid #333;padding:10px;width:280px;}";
    c += ".log-header{max-width:900px;margin:15px auto 0;background:#00ffcc;color:#000;font-weight:bold;display:grid;grid-template-columns:100px 100px 120px 1fr;padding:10px;border-radius:8px 8px 0 0;}";
    c += ".log-scroll{max-width:900px;margin:0 auto;height:450px;overflow-y:auto;background:#111;border:1px solid #333;border-radius:0 0 8px 8px;}";
    c += ".log-row{display:grid;grid-template-columns:100px 100px 120px 1fr;padding:8px;border-bottom:1px solid #222;text-align:left;font-family:monospace;font-size:0.85em;}";
    c += ".tag{padding:2px 4px;border-radius:3px;font-size:0.7em;font-weight:bold;} .CRITICAL{background:#f44;} .STATE_CHANGE{background:#0fc;color:#000;}";
    c += "</style>";
    return c;
}

String getNav() {
    return "<div class='nav'><a href='/'>DASHBOARD (LIVE)</a><a href='/logs'>SYSTEM LOGS</a><a href='/config'>SETTINGS</a></div>";
}

void NetworkService::begin(const char* ssid, const char* pass) {
    _prefs.begin("net_stats", true);
    String s = _prefs.getString("w_ssid", ssid), p = _prefs.getString("w_pass", pass);
    _prefs.end();
    WiFi.mode(WIFI_STA); WiFi.begin(s.c_str(), p.c_str());
    _startTime = millis(); _lastConnectedTime = millis(); _bootPhase = 0;
}

void NetworkService::update() {
    unsigned long uptime = millis() - _startTime;

    if (_bootPhase == 0 && uptime > 10000) {
        _fsReady = LittleFS.begin(true);
        _prefs.begin("net_stats", false);
        _historicalReconnects = _prefs.getInt("t_recon", 0);
        _historicalUptime = _prefs.getULong("t_uptime", 0);
        _reconnectCount = _prefs.getInt("recon", 0);
        _gmtOffset = _prefs.getInt("gmt", -5);
        _prefs.end();
        _bootPhase = 1;
        logEvent("SYS_STATUS", "FS & NVS Initialized");
    }

    if (_bootPhase == 1 && uptime > 15000) {
        if (!_server) _server = new WebServer(80);
        if (!_dnsServer) _dnsServer = new DNSServer();
        _setupWebServer();
        _bootPhase = 2;
    }

    if (_bootPhase == 2 && uptime > 20000) {
        configTime(_gmtOffset * 3600, 0, "pool.ntp.org", "time.google.com");
        _bootPhase = 3;
    }

    if (_server) _server->handleClient();
    if (_dnsServer && _isConfigMode) _dnsServer->processNextRequest();

    bool connected = (WiFi.status() == WL_CONNECTED);

    if (connected) {
        if (!_lastConnectedTime || (_lastConnectedTime == 0 && connected)) _connectionTrigger = true; 
        _lastConnectedTime = millis();
        if (_isConfigMode) { 
            WiFi.softAPdisconnect(true); if (_dnsServer) _dnsServer->stop(); _isConfigMode = false; _connectionTrigger = true; 
        }
        if (_bootPhase >= 1 && (millis() - _lastPingTime > 5000)) { _lastPingTime = millis(); _performPing(); }
        if (_bootPhase >= 1 && (millis() - _lastSaveTime > _saveInterval)) {
            _historicalUptime += (millis() - _lastSaveTime);
            _lastSaveTime = millis();
            _prefs.begin("net_stats", false); _prefs.putULong("t_uptime", _historicalUptime); _prefs.end();
        }
    } else {
        if (uptime > 30000 && !connected && !_isConfigMode && _bootPhase >= 2) {
            _isConfigMode = true; WiFi.softAP("WiFi-Monitor-C6");
            if (_dnsServer) _dnsServer->start(53, "*", WiFi.softAPIP());
        }
        if (millis() - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = millis();
            static int f = 0; if (f < 2) WiFi.reconnect(); else WiFi.begin();
            f++; _reconnectCount++;
            if (_bootPhase >= 1) { _prefs.begin("net_stats", false); _prefs.putInt("recon", _reconnectCount); _prefs.end(); }
        }
    }
}

void NetworkService::logEvent(const char* type, const char* data) {
    char dateStr[16]="BOOT", timeStr[16]="BOOT";
    time_t now; time(&now);
    if (now > 1000000) {
        struct tm ti; localtime_r(&now, &ti);
        strftime(dateStr, 16, "%Y-%m-%d", &ti); strftime(timeStr, 16, "%H:%M:%S", &ti);
    }
    char msg[128]; sprintf(msg, "%s|%s|%s|%s", dateStr, timeStr, type, data);
    if (_fsReady) {
        _rotateLogs();
        File f = LittleFS.open("/log.txt", FILE_APPEND);
        if (f) { f.println(msg); f.close(); }
    }
}

void NetworkService::_rotateLogs() {
    File f = LittleFS.open("/log.txt", FILE_READ);
    if (!f) return;
    size_t size = f.size(); f.close();
    if (size > 30000) { LittleFS.rename("/log.txt", "/log_old.txt"); LittleFS.remove("/log_old.txt"); }
}

void NetworkService::setQuality(int score, int jitter) { _lastScore = score; _lastJitter = jitter; }

bool NetworkService::saveTrend(const int* history, int size, int index) {
    if (!_fsReady || !history) return false;
    File f = LittleFS.open("/trend.bin", FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)&index, sizeof(int)); f.write((uint8_t*)history, size * sizeof(int)); f.close();
    return true;
}

bool NetworkService::loadTrend(int* history, int size, int* index) {
    if (!_fsReady || !history || !index) return false;
    if (!LittleFS.exists("/trend.bin")) return false;
    File f = LittleFS.open("/trend.bin", FILE_READ);
    if (!f) return false;
    f.read((uint8_t*)index, sizeof(int)); f.read((uint8_t*)history, size * sizeof(int)); f.close();
    return true;
}

// NUEVO: Historial de RAM persistente
bool saveRam(const int* h, int idx) {
    File f = LittleFS.open("/ram.bin", FILE_WRITE);
    if(!f) return false; f.write((uint8_t*)&idx, sizeof(int)); f.write((uint8_t*)h, 50*sizeof(int)); f.close(); return true;
}
bool loadRam(int* h, int* idx) {
    File f = LittleFS.open("/ram.bin", FILE_READ);
    if(!f) return false; f.read((uint8_t*)idx, sizeof(int)); f.read((uint8_t*)h, 50*sizeof(int)); f.close(); return true;
}

void NetworkService::_setupWebServer() {
    _server->onNotFound([this]() {
        if (_isConfigMode) { _server->sendHeader("Location", "/config", true); _server->send(302, "text/plain", ""); }
        else _server->send(404, "text/plain", "Not Found");
    });

    _server->on("/", [this]() { _handleRoot(); });
    _server->on("/logs", [this]() { _handleLogs(); });
    _server->on("/config", [this]() { _handleConfig(); });
    _server->on("/scan", [this]() {
        int n = WiFi.scanNetworks(); String j = "[";
        for (int i=0; i<n; i++) { j += "{\"s\":\"" + WiFi.SSID(i) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}"; if (i < n-1) j += ","; }
        j += "]"; _server->send(200, "application/json", j);
    });

    _server->on("/status", [this]() {
        NetworkData d = getData(); uint32_t fh = ESP.getFreeHeap();
        int hp = (fh * 100) / ESP.getHeapSize();
        String j = "{\"u\":\"" + getUptimeString() + "\",\"r\":" + String(d.rssi) + ",\"pg\":" + String(d.pingGW) + ",\"pn\":" + String(d.pingInternet) + ",\"qs\":" + String(_lastScore) + ",\"re\":" + String(_reconnectCount) + ",\"h\":" + String(fh) + ",\"hp\":" + String(hp) + "}";
        _server->send(200, "application/json", j);
    });

    _server->on("/save", HTTP_POST, [this]() {
        if (_server->arg("token") != "admin") { _server->send(403, "text/plain", "ACCESS DENIED"); return; }
        String s = _server->arg("ssid"), p = _server->arg("pass"), g = _server->arg("gmt");
        if (s.length() > 0) {
            _prefs.begin("net_stats", false); _prefs.putString("w_ssid", s); _prefs.putString("w_pass", p); _prefs.putInt("gmt", g.toInt()); _prefs.end();
            _server->send(200, "text/html", "<b>Settings Applied</b>. Restarting..."); delay(1000); ESP.restart();
        }
    });
    _server->begin();
}

void NetworkService::_handleRoot() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'><meta http-equiv='refresh' content='10'>" + getCommonCSS();
    h += "<script>function up(){fetch('/status').then(r=>r.json()).then(d=>{";
    h += "const m={'u':d.u,'r':d.r+' dBm','pg':d.pg+' ms','pi':d.pn+' ms','qs':d.qs+'%','re':d.re,'h':(d.h/1024).toFixed(1)+' KB'};";
    h += "for(let k in m)document.getElementById(k).innerText=m[k];});} setInterval(up,2500);</script></head><body>";
    h += getNav() + "<h1>Command Center</h1>";
    h += "<div class='grid'><div class='card'><div>APP UPTIME</div><div class='val' id='u'>--</div></div>";
    h += "<div class='card'><div>RSSI SIGNAL</div><div class='val' id='r'>--</div></div>";
    h += "<div class='card'><div>QUALITY SCORE</div><div class='val' id='qs'>--</div></div>";
    h += "<div class='card'><div>PING INTERNET</div><div class='val' id='pi'>--</div></div>";
    h += "<div class='card'><div>FREE RAM</div><div class='val' id='h'>--</div></div>";
    h += "<div class='card'><div>RECONNECTS</div><div class='val' id='re'>--</div></div></div>";
    
    // Gráfica de Calidad
    h += "<h2>WIFI QUALITY TREND</h2><div class='chart-container'><svg width='100%' height='160' viewBox='0 0 600 160' preserveAspectRatio='none'>";
    for(int y=40; y<=120; y+=40) h += "<line x1='0' y1='"+String(y)+"' x2='600' y2='"+String(y)+"' stroke='#222'/>";
    int hi[50], id; if(loadTrend(hi, 50, &id)) {
        h += "<text x='5' y='20' fill='#888' font-size='10'>100%</text><text x='5' y='155' fill='#888' font-size='10'>0%</text>";
        h += "<polyline points='"; for(int i=0; i<50; i++) h += String(i*12) + "," + String(160-(hi[(id+i)%50]*1.6)) + " ";
        h += "' fill='none' stroke='#0fc' stroke-width='3'/></svg></div>";
    }

    // Gráfica de RAM
    h += "<h2>SYSTEM RAM TREND</h2><div class='chart-container'><svg width='100%' height='100' viewBox='0 0 600 100' preserveAspectRatio='none'>";
    for(int y=25; y<=75; y+=25) h += "<line x1='0' y1='"+String(y)+"' x2='600' y2='"+String(y)+"' stroke='#222'/>";
    int rh[50], rid; if(loadRam(rh, &rid)) {
        h += "<text x='5' y='15' fill='#888' font-size='10'>STABLE</text>";
        h += "<polyline points='"; for(int i=0; i<50; i++) h += String(i*12) + "," + String(100-(rh[(rid+i)%50])) + " ";
        h += "' fill='none' stroke='#48f' stroke-width='2'/></svg></div>";
    }
    h += "</body></html>"; _server->send(200, "text/html", h);
}

void NetworkService::_handleLogs() {
    if (!_fsReady) { _server->send(500, "text/plain", "FS Error"); return; }
    _server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    _server->send(200, "text/html", "");
    _server->sendContent("<html><head><meta charset='UTF-8'>" + getCommonCSS() + "</head><body>" + getNav());
    _server->sendContent("<h2>System Audit Logs</h2><div class='log-header'><div>DATE</div><div>TIME</div><div>EVENT</div><div>DATA</div></div>");
    _server->sendContent("<div class='log-scroll'>");
    
    File f = LittleFS.open("/log.txt", FILE_READ);
    String lines[50]; int count = 0;
    while (f.available() && count < 50) { lines[count++] = f.readStringUntil('\n'); }
    f.close();
    // Reversa: Recientes primero
    for (int i=count-1; i>=0; i--) {
        String l = lines[i]; int s1=l.indexOf('|'), s2=l.indexOf('|', s1+1), s3=l.indexOf('|', s2+1);
        if (s1>0 && s2>0 && s3>0) {
            String dt=l.substring(0,s1), tm=l.substring(s1+1,s2), tp=l.substring(s2+1,s3), msg=l.substring(s3+1);
            _server->sendContent("<div class='log-row'><div>"+dt+"</div><div>"+tm+"</div><div><span class='tag "+tp+"'>"+tp+"</span></div><div>"+msg+"</div></div>");
        }
    }
    _server->sendContent("</div></body></html>"); _server->client().stop();
}

void NetworkService::_handleConfig() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getCommonCSS();
    h += "<script>function scan(){const b=document.getElementById('sb');b.innerText='Scanning...';fetch('/scan').then(r=>r.json()).then(d=>{";
    h += "let h='<div style=\"max-height:200px;overflow-y:auto;border:1px solid #333;margin-top:10px;\">';";
    h += "d.forEach(n=>{h+='<div class=\"scan-item\" onclick=\"sel(\\''+n.s+'\\')\">'+n.s+' ('+n.r+' dBm)</div>';});";
    h += "h+='</div>';document.getElementById('sl').innerHTML=h;b.innerText='Rescan';});}";
    h += "function sel(s){document.getElementById('ssid').value=s;}";
    h += "function tp(){const x=document.getElementById('pass');x.type=(x.type==='password')?'text':'password';}</script></head><body>";
    h += getNav() + "<h1>System Settings</h1><form action='/save' method='POST' style='display:inline-block;text-align:left;'>";
    h += "WiFi SSID:<br><input type='text' id='ssid' name='ssid' placeholder='SSID'><button type='button' id='sb' onclick='scan()' style='padding:10px;margin-left:5px;cursor:pointer;background:#222;color:#eee;border:1px solid #333;'>Scan</button>";
    h += "<div id='sl'></div><br>WiFi Pass:<br><input type='password' id='pass' name='pass'><br><input type='checkbox' onclick='tp()'> Show password<br><br>";
    h += "GMT Offset:<br><input type='number' name='gmt' value='"+String(_gmtOffset)+"'><br><br>";
    h += "Admin Token:<br><input type='password' name='token' style='border:2px solid #0fc;'><br><br>";
    h += "<input type='submit' value='APPLY & REBOOT' class='btn' style='width:100%;'></form></body></html>";
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
    if (d.connected) {
        d.rssi = WiFi.RSSI(); d.ip = WiFi.localIP().toString();
        d.pingGW = _lastPingGW; d.pingInternet = _lastPingInternet;
        d.score = _lastScore; d.jitter = _lastJitter;
    } else { d.rssi = -100; d.pingGW = -1; d.pingInternet = -1; d.score = 0; d.jitter = 0; }
    return d;
}

int NetworkService::getReconnectCount() { return _reconnectCount; }
float NetworkService::getDisconnectRate() {
    float h = (_historicalUptime + (millis() - _startTime)) / 3600000.0;
    return (h < 0.01) ? 0.0 : (float)(_historicalReconnects + _reconnectCount) / h;
}
bool NetworkService::isConnected() { return WiFi.status() == WL_CONNECTED; }
