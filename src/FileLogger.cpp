#include "FileLogger.h"
#include <esp_system.h>

FileLogger::FileLogger() {}

bool FileLogger::begin() {
    _fsReady = LittleFS.begin(true);
    return _fsReady;
}

void FileLogger::logEvent(const char* type, const char* data) {
    time_t n; time(&n);
    logEventWithTime(n, type, data);
}

void FileLogger::logEventWithTime(time_t t, const char* type, const char* data) {
    char ds[16], ts[16]; struct tm ti = {0};
    localtime_r(&t, &ti);
    
    if (t < 1000000) { strcpy(ds, "BOOT"); strcpy(ts, "BOOT"); }
    else { 
        strftime(ds, 16, "%Y-%m-%d", &ti); 
        strftime(ts, 16, "%H:%M:%S", &ti); 
    }
    
    char msg[128];
    sprintf(msg, "%s|%s|%s|%s", ds, ts, type, data);
    Serial.printf("[LOG] %s\n", msg);
    
    if (_fsReady) {
        _rotateLogs();
        File f = LittleFS.open("/log.txt", FILE_APPEND);
        if (f) { f.println(msg); f.close(); }
    }
}

void FileLogger::checkStartupReason() {
    if (_bootReasonLogged) return;
    esp_reset_reason_t reason = esp_reset_reason();
    const char* r = "UNKNOWN";
    switch(reason) {
        case ESP_RST_POWERON: r = "POWER_ON"; break;
        case ESP_RST_EXT:     r = "PIN_BOOT"; break;
        case ESP_RST_SW:      r = "SOFTWARE_BOOT"; break;
        case ESP_RST_WDT:     r = "WATCHDOG_BOOT"; break;
        case ESP_RST_BROWNOUT: r = "BROWNOUT_VOLT"; break;
        case ESP_RST_PANIC:   r = "CPU_PANIC"; break;
        default:              r = "OTHER_BOOT"; break;
    }
    logEvent("RESTART_CAUSE", r);
    _bootReasonLogged = true;
}

void FileLogger::estimateLastPowerOff() {
    if (!_fsReady || _powerOffLogged) return;
    _powerOffLogged = true;
    File f = LittleFS.open("/log.txt", FILE_READ);
    if (!f) return;
    
    size_t sz = f.size();
    if (sz > 2048) f.seek(sz - 2048);
    String data = f.readString(); f.close();
    
    int lastH = data.lastIndexOf("HEARTBEAT");
    if (lastH > 20) {
        int lineStart = data.lastIndexOf('\n', lastH);
        if (lineStart == -1) lineStart = 0; else lineStart++;
        String line = data.substring(lineStart, data.indexOf('\n', lineStart));
        
        int yy, mm, dd, h, m, s;
        if (sscanf(line.c_str(), "%d-%d-%d|%d:%d:%d", &yy, &mm, &dd, &h, &m, &s) == 6) {
            struct tm tm = {0};
            tm.tm_year = yy - 1900; tm.tm_mon = mm-1; tm.tm_mday = dd;
            tm.tm_hour = h; tm.tm_min = m; tm.tm_sec = s;
            time_t offTime = mktime(&tm) + 30; // Estimación: 30s después del último latido
            logEventWithTime(offTime, "POWER_OFF_EST", "Power loss detected");
        }
    }
}

void FileLogger::sendHeartbeat(int rssi, String ip) {
    time_t now; time(&now);
    if (now < 1000000) return; // Esperar sync NTP

    if (millis() - _lastHeartbeatSent > 30000) {
        char buf[64];
        sprintf(buf, "ALIVE | RSSI: %d | IP: %s", rssi, ip.c_str());
        logEvent("HEARTBEAT", buf);
        _lastHeartbeatSent = millis();
    }
}

void FileLogger::_rotateLogs() {
    File f = LittleFS.open("/log.txt", FILE_READ);
    if (!f) return;
    size_t s = f.size();
    if (s < 51200) { // Límite de 50KB
        f.close();
        return;
    }

    // Proceso de Truncamiento Circular: Preservar últimos 25KB
    Serial.println("[LOG] Truncating log file (Circular Buffering)...");
    f.seek(s - 25600); // Ir a la mitad (aprox)
    f.readStringUntil('\n'); // Alinear a inicio de línea
    
    String trailingData = f.readString();
    f.close();

    f = LittleFS.open("/log.txt", FILE_WRITE);
    if (f) {
        f.print(trailingData);
        f.close();
        logEvent("SYSTEM", "Log truncated - 25KB preserved");
    }
}

bool FileLogger::saveTrend(const int* h, int s, int idx) {
    if (!_fsReady || !h) return false;
    File f = LittleFS.open("/trend.bin", FILE_WRITE);
    if (!f) return false;
    f.write((uint8_t*)&idx, sizeof(int));
    f.write((uint8_t*)h, s * sizeof(int));
    f.close();
    return true;
}

bool FileLogger::loadTrend(int* h, int s, int* idx) {
    if (!_fsReady || !h || !idx) return false;
    if (!LittleFS.exists("/trend.bin")) return false;
    File f = LittleFS.open("/trend.bin", FILE_READ);
    if (!f) return false;
    f.read((uint8_t*)idx, sizeof(int));
    f.read((uint8_t*)h, s * sizeof(int));
    f.close();
    return true;
}

bool FileLogger::saveRam(const int* h, int idx) {
    if (!_fsReady) return false;
    File f = LittleFS.open("/ram.bin", FILE_WRITE);
    if(!f) return false;
    f.write((uint8_t*)&idx, sizeof(int));
    f.write((uint8_t*)h, 46*sizeof(int));
    f.close();
    return true;
}

bool FileLogger::loadRam(int* h, int* idx) {
    if (!_fsReady) return false;
    File f = LittleFS.open("/ram.bin", FILE_READ);
    if(!f) return false;
    f.read((uint8_t*)idx, sizeof(int));
    f.read((uint8_t*)h, 46*sizeof(int));
    f.close();
    return true;
}
