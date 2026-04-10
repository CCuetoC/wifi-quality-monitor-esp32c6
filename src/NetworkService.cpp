#include "NetworkService.h"
#include <time.h>

NetworkService::NetworkService() {}

// Helper para CSS compartido (V2.1 - Estética Premium con soporte para Scanner)
String getCommonCSS() {
    String c = "<style>body{background:#0a0a0a;color:#eee;font-family:sans-serif;text-align:center;padding:0;margin:0;}";
    c += ".nav{background:#111;padding:15px;border-bottom:1px solid #00ffcc;margin-bottom:20px;display:flex;justify-content:center;gap:12px;flex-wrap:wrap;}";
    c += ".nav a{color:#00ffcc;text-decoration:none;font-weight:bold;font-size:0.85em;padding:8px 12px;border:1px solid #222;border-radius:6px;transition:0.2s;}";
    c += ".nav a:hover{background:#00ffcc;color:#000;} .card{background:#111;padding:20px;border-radius:12px;border:1px solid #222;margin:8px;display:inline-block;min-width:160px;}";
    c += "h1,h2{color:#00ffcc;text-transform:uppercase;letter-spacing:1px;margin:15px 0;} .val{font-size:1.6em;font-weight:bold;color:#fff;}";
    c += "table{width:95%;max-width:900px;margin:15px auto;border-collapse:collapse;background:#111;font-family:monospace;font-size:0.85em;}";
    c += "th{background:#00ffcc;color:#000;padding:10px;} td{padding:8px;border-bottom:1px solid #222;}";
    c += ".tag{padding:2px 6px;border-radius:3px;font-size:0.75em;font-weight:bold;}";
    c += ".CRITICAL{background:#ff4444;} .STATE_CHANGE{background:#00ffcc;color:#000;} .SYS_STATUS{background:#4488ff;}";
    c += ".btn{background:#00ffcc;color:#000;padding:12px 25px;border:none;border-radius:5px;font-weight:bold;cursor:pointer;text-decoration:none;display:inline-block;margin:15px 0;}";
    c += "input,select{background:#111;color:#fff;border:1px solid #333;padding:10px;border-radius:5px;width:280px;margin:5px 0;}";
    c += ".scan-item{padding:10px;border-bottom:1px solid #222;cursor:pointer;text-align:left;} .scan-item:hover{background:#222;}";
    c += "</style>";
    return c;
}

String getNav() {
    return "<div class='nav'><a href='/'>DASHBOARD</a><a href='/graph'>LIVE TREND</a><a href='/logs'>SYSTEM LOGS</a><a href='/config'>SETTINGS</a></div>";
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
        // Trigger de reconexión exitosa para limpiar buffers
        if (!_lastConnectedTime || !connected) _connectionTrigger = true; 
        
        _lastConnectedTime = millis();
        if (_isConfigMode) { 
            WiFi.softAPdisconnect(true); 
            if (_dnsServer) _dnsServer->stop();
            _isConfigMode = false; 
            _connectionTrigger = true; // Forzar flush tras salir de modo config
        }
        if (_bootPhase >= 1 && (millis() - _lastPingTime > 5000)) { 
            _lastPingTime = millis(); _performPing(); 
        }
        if (_bootPhase >= 1 && (millis() - _lastSaveTime > _saveInterval)) {
            _historicalUptime += (millis() - _lastSaveTime);
            _lastSaveTime = millis();
            _prefs.begin("net_stats", false); _prefs.putULong("t_uptime", _historicalUptime); _prefs.end();
        }
    } else {
        if (uptime > 30000 && !connected && !_isConfigMode && _bootPhase >= 2) {
            _isConfigMode = true;
            WiFi.softAP("WiFi-Monitor-C6");
            if (_dnsServer) _dnsServer->start(53, "*", WiFi.softAPIP());
        }
        if (millis() - _lastReconnectAttempt > _reconnectInterval) {
            _lastReconnectAttempt = millis();
            static int failedAttempts = 0;
            if (failedAttempts < 2) WiFi.reconnect(); else WiFi.begin();
            failedAttempts++;
            _reconnectCount++;
            if (_bootPhase >= 1) {
                _prefs.begin("net_stats", false); _prefs.putInt("recon", _reconnectCount); _prefs.end();
            }
        }
        if (millis() - _lastConnectedTime > _maxDisconnectTime) {
            logEvent("CRITICAL", "Watchdog Reboot Triggered"); delay(1000); ESP.restart();
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
    Serial.println(msg);
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
    if (size > 30000) { 
        LittleFS.rename("/log.txt", "/log_old.txt");
        LittleFS.remove("/log_old.txt"); 
    }
}

void NetworkService::setQuality(int score, int jitter) {
    _lastScore = score; _lastJitter = jitter;
}

bool NetworkService::saveTrend(const int* history, int size, int index) {
    if (!_fsReady || !history) return false;
    File f = LittleFS.open("/trend.bin", FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)&index, sizeof(int));
    f.write((uint8_t*)history, size * sizeof(int));
    f.close();
    return true;
}

bool NetworkService::loadTrend(int* history, int size, int* index) {
    if (!_fsReady || !history || !index) return false;
    if (!LittleFS.exists("/trend.bin")) return false;
    File f = LittleFS.open("/trend.bin", FILE_READ);
    if (!f) return false;
    f.read((uint8_t*)index, sizeof(int));
    f.read((uint8_t*)history, size * sizeof(int));
    f.close();
    return true;
}

void NetworkService::_setupWebServer() {
    if (!_server) return;
    
    // Captive Portal Redirect
    _server->onNotFound([this]() {
        if (_isConfigMode) {
            _server->sendHeader("Location", "/config", true);
            _server->send(302, "text/plain", "");
        } else {
            _server->send(404, "text/plain", "Not Found");
        }
    });

    _server->on("/", [this]() { _handleRoot(); });
    _server->on("/logs", [this]() { _handleLogs(); });
    _server->on("/config", [this]() { _handleConfig(); });
    
    _server->on("/scan", [this]() {
        int n = WiFi.scanNetworks();
        String j = "[";
        for (int i=0; i<n; i++) {
            j += "{\"s\":\"" + WiFi.SSID(i) + "\",\"r\":" + String(WiFi.RSSI(i)) + "}";
            if (i < n-1) j += ",";
        }
        j += "]";
        _server->send(200, "application/json", j);
    });

    _server->on("/status", [this]() {
        NetworkData d = getData();
        String j = "{\"uptime\":\"" + getUptimeString() + "\",\"rssi\":" + String(d.rssi) + ",\"pingGW\":" + String(d.pingGW) + ",\"pingNet\":" + String(d.pingInternet) + ",\"score\":" + String(_lastScore) + ",\"jitter\":" + String(_lastJitter) + ",\"recon\":" + String(_reconnectCount) + "}";
        _server->send(200, "application/json", j);
    });
    
    _server->on("/graph", [this]() {
        String h = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='5'>" + getCommonCSS();
        h += "</head><body>" + getNav() + "<h2>Quality Trend</h2><div style='background:#111;padding:15px;border-radius:10px;display:inline-block;border:1px solid #333;'>";
        h += "<svg width='600' height='200' viewBox='0 0 600 200' style='background:#000;'>";
        for(int y=50; y<=150; y+=50) h += "<line x1='0' y1='"+String(y)+"' x2='600' y2='"+String(y)+"' stroke='#222'/>";
        int hi[50], id;
        if(loadTrend(hi, 50, &id)) {
            h += "<polyline points='";
            for(int i=0; i<50; i++) h += String(i*12) + "," + String(200-(hi[(id+i)%50]*2)) + " ";
            h += "' fill='none' stroke='#00ffcc' stroke-width='3'/>";
        }
        h += "</svg></div></body></html>";
        _server->send(200, "text/html", h);
    });

    _server->on("/save", HTTP_POST, [this]() {
        if (_server->arg("token") != "admin") { _server->send(403, "text/plain", "ACCESS DENIED"); return; }
        String s = _server->arg("ssid"), p = _server->arg("pass"), g = _server->arg("gmt");
        if (s.length() > 0) {
            _prefs.begin("net_stats", false);
            _prefs.putString("w_ssid", s); _prefs.putString("w_pass", p);
            _prefs.putInt("gmt", g.toInt()); _prefs.end();
            _server->send(200, "text/html", "<b>Settings Applied</b>. Restarting...");
            delay(1000); ESP.restart();
        }
    });
    _server->begin();
}

void NetworkService::_handleRoot() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getCommonCSS();
    h += "<script>function up(){fetch('/status').then(r=>r.json()).then(d=>{";
    h += "const map={'u':d.uptime,'r':d.rssi+' dBm','pg':d.pingGW+' ms','pi':d.pingNet+' ms','sc':d.score+'%','re':d.recon};";
    h += "for(let k in map)document.getElementById(k).innerText=map[k];});} setInterval(up,2500);</script></head><body>";
    h += getNav() + "<h1>Industrial Monitor</h1><div class='card'><div>UPTIME</div><div class='val' id='u'>--</div></div>";
    h += "<div class='card'><div>SIGNAL</div><div class='val' id='r'>--</div></div><div class='card'><div>SCORE</div><div class='val' id='sc'>--</div></div><br>";
    h += "<div class='card'><div>PING GW</div><div class='val' id='pg'>--</div></div><div class='card'><div>PING NET</div><div class='val' id='pi'>--</div></div><div class='card'><div>RECONS</div><div class='val' id='re'>--</div></div><br>";
    h += "<a href='/graph' class='btn'>VIEW LIVE TREND</a></body></html>";
    _server->send(200, "text/html", h);
}

void NetworkService::_handleLogs() {
    if (!_fsReady) { _server->send(500, "text/plain", "FS Error"); return; }
    File f = LittleFS.open("/log.txt", FILE_READ);
    String h = "<html><head><meta charset='UTF-8'>" + getCommonCSS() + "</head><body>" + getNav() + "<h2>System Audit Logs</h2><table><tr><th>DATE</th><th>TIME</th><th>EVENT</th><th>DATA</th></tr>";
    while (f.available()) {
        String l = f.readStringUntil('\n'); int s1=l.indexOf('|'), s2=l.indexOf('|', s1+1), s3=l.indexOf('|', s2+1);
        if (s1>0 && s2>0 && s3>0) {
            String dt=l.substring(0,s1), tm=l.substring(s1+1,s2), tp=l.substring(s2+1,s3), msg=l.substring(s3+1);
            h += "<tr><td>"+dt+"</td><td>"+tm+"</td><td><span class='tag "+tp+"'>"+tp+"</span></td><td>"+msg+"</td></tr>";
        }
    }
    f.close(); h += "</table></body></html>";
    _server->send(200, "text/html", h);
}

void NetworkService::_handleConfig() {
    String h = "<html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>" + getCommonCSS();
    h += "<script>function scan(){const b=document.getElementById('sb');b.innerText='Scanning...';fetch('/scan').then(r=>r.json()).then(d=>{";
    h += "let h='<div style=\"max-height:200px;overflow-y:auto;border:1px solid #333;margin-top:10px;\">';";
    h += "d.forEach(n=>{h+='<div class=\"scan-item\" onclick=\"sel(\\''+n.s+'\\')\">'+n.s+' ('+n.r+' dBm)</div>';});";
    h += "h+='</div>';document.getElementById('sl').innerHTML=h;b.innerText='Rescan';});}";
    h += "function sel(s){document.getElementById('ssid').value=s;}";
    h += "function tp(){const x=document.getElementById('pass');x.type=(x.type==='password')?'text':'password';}</script></head><body>";
    h += getNav() + "<h1>Configuration</h1><form action='/save' method='POST' style='display:inline-block;text-align:left;'>";
    h += "WiFi SSID:<br><input type='text' id='ssid' name='ssid' placeholder='Select or type SSID'><button type='button' id='sb' onclick='scan()' style='padding:10px;margin-left:5px;cursor:pointer;background:#222;color:#fff;border:1px solid #333;border-radius:5px;'>Scan</button>";
    h += "<div id='sl'></div><br>WiFi Pass:<br><input type='password' id='pass' name='pass'><br><input type='checkbox' onclick='tp()' style='width:auto;'> Show Password<br><br>";
    h += "GMT Offset:<br><input type='number' name='gmt' value='"+String(_gmtOffset)+"'><br><br>";
    h += "Admin Token:<br><input type='password' name='token' style='border-color:#00ffcc;'><br><br>";
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
