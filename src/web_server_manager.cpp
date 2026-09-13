#include "web_server_manager.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include "wordbook.h"
#include "web_page_html.h"
#include "wifi_setup_html.h"

#include <WiFiMulti.h>
#include <nvs_flash.h>

static WebServer server(80);
static const char* AP_SSID = "M5STICK_SETUP";
String current_ssid = "";
String current_pass = "";
WiFiMulti wifiMulti;

void handleRoot() {
    server.send(200, "text/html", INDEX_HTML);
}

// Ensure NVS is initialized; if not, attempt erase+init to recover.
void ensureNVS() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_OK) return;
    Serial.printf("[NVS] nvs_flash_init() returned 0x%X\n", err);
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND || err == ESP_ERR_NVS_NOT_INITIALIZED) {
        Serial.println("[NVS] Erasing NVS and retrying init...");
        esp_err_t e2 = nvs_flash_erase();
        Serial.printf("[NVS] nvs_flash_erase() -> 0x%X\n", e2);
        esp_err_t e3 = nvs_flash_init();
        Serial.printf("[NVS] Retry nvs_flash_init() -> 0x%X\n", e3);
    }
}

static bool beginPreferences(Preferences &prefs, const char *ns, bool readonly) {
    bool ok = prefs.begin(ns, readonly);
    if (!ok) {
        Serial.printf("[PREF] begin(%s, %s) failed\n", ns, readonly ? "true" : "false");
    }
    return ok;
}

static bool recoverNVSAndBegin(Preferences &prefs, const char *ns, bool readonly) {
    if (beginPreferences(prefs, ns, readonly)) {
        return true;
    }
    if (readonly) {
        // Do not erase NVS on readonly access when namespace is simply missing.
        return false;
    }
    Serial.println("[PREF] Attempting NVS erase and re-init due to Preferences begin failure...");
    esp_err_t e2 = nvs_flash_erase();
    Serial.printf("[NVS] nvs_flash_erase() -> 0x%X\n", e2);
    esp_err_t e3 = nvs_flash_init();
    Serial.printf("[NVS] Retry nvs_flash_init() -> 0x%X\n", e3);
    return beginPreferences(prefs, ns, readonly);
}

// Public wrapper to safely call Preferences::begin with automatic NVS
// initialization and recovery attempts.
bool prefsBegin(Preferences &prefs, const char *ns, bool readonly) {
    ensureNVS();
    if (beginPreferences(prefs, ns, readonly)) return true;
    if (readonly) {
        // For readonly, do not attempt to erase NVS — caller should handle
        return false;
    }

    Serial.printf("[PREF] prefsBegin(%s) initial begin failed, attempting erase+reinit...\n", ns);
    esp_err_t e2 = nvs_flash_erase();
    Serial.printf("[NVS] nvs_flash_erase() -> 0x%X\n", e2);
    esp_err_t e3 = nvs_flash_init();
    Serial.printf("[NVS] Retry nvs_flash_init() -> 0x%X\n", e3);

    return beginPreferences(prefs, ns, readonly);
}

String lastWebAction = "대기 중...";

void handleGetWords() {
    lastWebAction = "단어 목록 조회";
    // Generate JSON string
    String json = "{\"ssid\":\"" + current_ssid + "\",\"count\":" + String(wordbook.getWordCount()) + ",\"words\":[";
    for (int i = 0; i < wordbook.getWordCount(); i++) {
        String wordEscaped = wordbook.getWord(i);
        // Basic escaping for JSON (e.g., quotes)
        wordEscaped.replace("\"", "\\\"");
        
        json += "{\"index\":" + String(i) + ",\"word\":\"" + wordEscaped + "\"}";
        if (i < wordbook.getWordCount() - 1) {
            json += ",";
        }
    }
    json += "]}";
    
    // WebServer needs CORS headers just in case, though not strictly required for same-origin
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

void handleAddWord() {
    if (server.hasArg("word")) {
        String word = server.arg("word");
        if (word.length() > 0 && wordbook.getWordCount() < wordbook.getMaxWordCount()) {
            wordbook.addWord(word);
            lastWebAction = "단어 추가됨: " + word;
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(200, "text/plain", "OK");
            return;
        }
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "text/plain", "Bad Request or Full");
}

void handleDeleteWord() {
    if (server.hasArg("id")) {
        int index = server.arg("id").toInt();
        if (index >= 0 && index < wordbook.getWordCount()) {
            wordbook.removeWord(index);
            lastWebAction = "단어 삭제됨 (ID: " + String(index) + ")";
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(200, "text/plain", "OK");
            return;
        }
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(400, "text/plain", "Bad Request");
}

void handleWiFiScan() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; ++i) {
        if (i > 0) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

void handleSaveFirebase() {
    if (server.hasArg("firebase_url")) {
        ensureNVS();
        Preferences prefs;
        if (!recoverNVSAndBegin(prefs, "wifi", false)) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(500, "text/plain", "Preferences unavailable");
            return;
        }
        prefs.putString("firebase_url", server.arg("firebase_url"));
        prefs.end();
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "OK");
    } else {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(400, "text/plain", "Bad Request");
    }
}

void handleGetFirebase() {
    ensureNVS();
    Preferences prefs;
    String url = "https://dodo-family-space-lck-default-rtdb.firebaseio.com/";
    if (beginPreferences(prefs, "wifi", true)) {
        url = prefs.getString("firebase_url", url);
        prefs.end();
    }
    String json = "{\"url\":\"" + url + "\"}";
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

void handleWiFiConnect() {
    if (server.hasArg("ssid") && server.arg("ssid").length() > 0) {
        String new_ssid = server.arg("ssid");
        String new_pass = server.hasArg("password") ? server.arg("password") : "";
        Serial.printf("[WIFI WEB] Requested save SSID='%s' PASS='%s'\n", new_ssid.c_str(), new_pass.c_str());

        // Ensure NVS is initialized/recovered before touching Preferences
        ensureNVS();
        Preferences prefs;
        if (!recoverNVSAndBegin(prefs, "wifi", false)) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(500, "text/plain", "Preferences unavailable");
            return;
        }

        int count = prefs.getInt("count", 0);
        int existingIndex = -1;
        for (int i = 0; i < count; i++) {
            String s = prefs.getString(("ssid_" + String(i)).c_str(), "");
            if (s == new_ssid) {
                existingIndex = i;
                break;
            }
        }

        if (existingIndex >= 0) {
            // Update password
            prefs.putString(("pass_" + String(existingIndex)).c_str(), new_pass);
        } else {
            // Add new if limit not reached
            if (count < 30) {
                prefs.putString(("ssid_" + String(count)).c_str(), new_ssid);
                prefs.putString(("pass_" + String(count)).c_str(), new_pass);
                prefs.putInt("count", count + 1);
            } else {
                // Shift everything left to remove index 0, then add to index 29
                for (int i = 0; i < 29; i++) {
                    String nextSsid = prefs.getString(("ssid_" + String(i+1)).c_str(), "");
                    String nextPass = prefs.getString(("pass_" + String(i+1)).c_str(), "");
                    prefs.putString(("ssid_" + String(i)).c_str(), nextSsid);
                    prefs.putString(("pass_" + String(i)).c_str(), nextPass);
                }
                prefs.putString("ssid_29", new_ssid);
                prefs.putString("pass_29", new_pass);
            }
        }
        prefs.end();

        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "OK");
        delay(1000);
        ESP.restart(); // 설정 후 재시작하여 STA 모드로 접속
    } else {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(400, "text/plain", "Bad Request");
    }
}

void handleGetSavedWiFi() {
    ensureNVS();
    Preferences prefs;
    if (!recoverNVSAndBegin(prefs, "wifi", true)) {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/json", "[]");
        return;
    }

    String json = "[";
    bool first = true;
    int count = prefs.getInt("count", 0);
    for (int i = 0; i < count; i++) {
        String s = prefs.getString(("ssid_" + String(i)).c_str(), "");
        if (s.length() > 0) {
            if (!first) json += ",";
            json += "{\"index\":" + String(i) + ",\"ssid\":\"" + s + "\"}";
            first = false;
        }
    }
    prefs.end();
    json += "]";
    
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", json);
}

void handleDeleteSavedWiFi() {
    if (server.hasArg("index")) {
        int idx = server.arg("index").toInt();
        Preferences prefs;
        if (!recoverNVSAndBegin(prefs, "wifi", false)) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(500, "text/plain", "Preferences unavailable");
            return;
        }
        
        int count = prefs.getInt("count", 0);
        if (idx >= 0 && idx < count) {
            // Shift left
            for (int i = idx; i < count - 1; i++) {
                String nextSsid = prefs.getString(("ssid_" + String(i+1)).c_str(), "");
                String nextPass = prefs.getString(("pass_" + String(i+1)).c_str(), "");
                prefs.putString(("ssid_" + String(i)).c_str(), nextSsid);
                prefs.putString(("pass_" + String(i)).c_str(), nextPass);
            }
            // Remove last
            prefs.remove(("ssid_" + String(count-1)).c_str());
            prefs.remove(("pass_" + String(count-1)).c_str());
            prefs.putInt("count", count - 1);
        }
        prefs.end();
        
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "text/plain", "OK");
    } else {
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(400, "text/plain", "Bad Request");
    }
}

void setupWiFiWebManager() {
    // Ensure NVS is usable before accessing Preferences
    ensureNVS();
    Preferences prefs;
    bool prefsOpen = recoverNVSAndBegin(prefs, "wifi", false); // Need false for migration

    bool hasWifi = false;
    if (prefsOpen) {
        // Migration of legacy "ssid"
        String legacySsid = prefs.getString("ssid", "");
        String legacyPass = prefs.getString("pass", "");
        if (legacySsid.length() > 0) {
            int currentCount = prefs.getInt("count", 0);
            if (currentCount == 0) {
                prefs.putString("ssid_0", legacySsid);
                prefs.putString("pass_0", legacyPass);
                prefs.putInt("count", 1);
            }
            prefs.remove("ssid");
            prefs.remove("pass");
        }

        int wifiCount = prefs.getInt("count", 0);

        for (int i = 0; i < wifiCount; i++) {
            String si = prefs.getString(("ssid_" + String(i)).c_str(), "");
            String pi = prefs.getString(("pass_" + String(i)).c_str(), "");
            if (si.length() > 0) {
                wifiMulti.addAP(si.c_str(), pi.c_str());
                hasWifi = true;
                Serial.printf("[WIFI WEB] Loaded saved AP: %s\n", si.c_str());
            }
        }
        prefs.end();
    } else {
        Serial.println("[WIFI WEB] Preferences unavailable; starting without saved WiFi.");
    }

    if (hasWifi) {
        // 1. 와이파이 연결 시도 (STA 모드)
        WiFi.mode(WIFI_STA);
        
        Serial.println("[WIFI WEB] Connecting via WiFiMulti...");
        
        // 최대 10초 대기 (WiFiMulti는 등록된 AP들을 스캔하고 가장 강한 곳에 연결)
        int timeout = 20;
        while (wifiMulti.run() != WL_CONNECTED && timeout > 0) {
            delay(500);
            Serial.print(".");
            timeout--;
        }
        Serial.println();
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("[WIFI WEB] Connected! IP: " + WiFi.localIP().toString());
        lastWebAction = "WiFi 연결됨";
    } else {
        Serial.println("[WIFI WEB] Failed to connect or no saved WiFi. Starting AP Mode.");
        // 연결 실패 또는 저장된 와이파이가 없으면 AP 모드로 전환 (Captive Portal)
        lastWebAction = "AP 준비 중...";
        
        WiFi.disconnect(true, true);
        delay(200);
        WiFi.mode(WIFI_OFF);
        delay(200);
        
        WiFi.mode(WIFI_AP_STA);
        delay(200);
        
        IPAddress local_ip(192,168,4,1);
        IPAddress gateway(192,168,4,1);
        IPAddress subnet(255,255,255,0);
        WiFi.softAPConfig(local_ip, gateway, subnet);
        
        bool apStatus = WiFi.softAP(AP_SSID, NULL, 1, 0, 4); // SSID, Pass, Channel, Hidden, Max_conn
        Serial.println(String("[WIFI WEB] Fallback to AP Mode. SSID: ") + AP_SSID + " (Status: " + (apStatus ? "OK" : "FAIL") + ")");
        
        if (apStatus) {
            lastWebAction = "AP 정상 작동";
        } else {
            lastWebAction = "AP 에러발생";
        }
    }
    
    // 항상 모든 API 라우트 등록 (STA 모드에서도 웹에서 와이파이 변경 가능하도록)
    server.on("/", HTTP_GET, []() {
        if (WiFi.getMode() & WIFI_AP) {
            server.send_P(200, "text/html", WIFI_SETUP_HTML);
        } else {
            handleRoot();
        }
    });
    server.on("/api/words", HTTP_GET, handleGetWords);
    server.on("/api/words", HTTP_POST, handleAddWord);
    server.on("/api/words", HTTP_DELETE, handleDeleteWord);
    server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
    server.on("/api/wifi/connect", HTTP_POST, handleWiFiConnect);
    server.on("/api/wifi/saved", HTTP_GET, handleGetSavedWiFi);
    server.on("/api/wifi/saved", HTTP_DELETE, handleDeleteSavedWiFi);
    server.on("/api/firebase/save", HTTP_POST, handleSaveFirebase);
    server.on("/api/firebase/get", HTTP_GET, handleGetFirebase);
    
    // Handle Preflight OPTIONS requests for CORS (개인 홈페이지 등 다른 출처에서의 접근 허용)
    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
            server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
            server.send(204);
        } else {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(404, "text/plain", "Not Found");
        }
    });

    server.begin();
    Serial.println("[WIFI WEB] Server started.");
}

void handleWiFiWebManager() {
    server.handleClient();
}

void stopWiFiWebManager() {
    server.close();
    if (WiFi.getMode() & WIFI_AP) {
        WiFi.softAPdisconnect(true);
    }
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF); // 전력 소비 절감을 위해 WiFi 완전 OFF
    Serial.println("[WIFI WEB] Server stopped.");
}

// [NEW] 비밀기능 11번: 직접 WiFi 핫스팟 시작 (AP 모드, 저장된 WiFi 무시)
void setupWiFiHotspot() {
    Serial.println("\n========== [WIFI HOTSPOT] INIT START ==========");
    
    lastWebAction = "Hotspot Init...";

    // Ensure NVS available
    ensureNVS();
    
    // Step 1: 기존 WiFi 완전 종료
    Serial.println("[STEP 1] Disabling existing WiFi...");
    // Avoid writing WiFi config to NVS while toggling modes
    WiFi.persistent(false);
    WiFi.disconnect(true);  // disconnect and turn off radio
    delay(500);
    WiFi.mode(WIFI_OFF);
    delay(500);
    
    // Step 2: AP+STA 모드 설정 (스캔을 위해 STA 필요)
    Serial.println("[STEP 2] Setting WiFi mode to AP_STA...");
    // Keep persistence disabled to prevent NVS writes during AP setup
    WiFi.persistent(false);
    // Use AP-only mode for hotspot to avoid STA-related conflicts
    bool modeOK = WiFi.mode(WIFI_AP);
    Serial.printf("[STEP 2] Mode set: %s\n", modeOK ? "OK" : "FAIL");
    delay(500);
    
    // Step 3: 최소한의 파라미터로 softAP 시작
    Serial.println("[STEP 3] Starting AP...");
    const char* hotspot_ssid = "M5STICK_HS";

    // Ensure previous AP state cleared
    WiFi.softAPdisconnect(true);
    delay(200);

    // Step 3a: IP 설정 (pre-config)
    IPAddress local_ip(192, 168, 4, 1);
    IPAddress gateway(192, 168, 4, 1);
    IPAddress subnet(255, 255, 255, 0);
    bool configOK = WiFi.softAPConfig(local_ip, gateway, subnet);
    Serial.printf("[STEP 3] Pre softAP IP config: %s\n", configOK ? "OK" : "FAIL");

    // Try sequence: secure pass -> empty pass -> default call
    bool apStatus = false;
    const char* tryPass = "12345678"; // try a valid WPA2 pass first
    Serial.println("[STEP 3] Attempting softAP with WPA2 pass...");
    apStatus = WiFi.softAP(hotspot_ssid, tryPass, 1, 0, 4);
    Serial.printf("[STEP 3] try with pass result: %s\n", apStatus ? "OK" : "FAIL");
    delay(500);

    if (!apStatus) {
        Serial.println("[STEP 3] Retry: attempting softAP with empty password (open AP)...");
        WiFi.softAPdisconnect(true);
        delay(200);
        apStatus = WiFi.softAP(hotspot_ssid, "", 1, 0, 4);
        Serial.printf("[STEP 3] try empty pass result: %s\n", apStatus ? "OK" : "FAIL");
        delay(500);
    }

    if (!apStatus) {
        Serial.println("[STEP 3] Final retry: calling softAP(ssid) default... (last attempt)");
        WiFi.softAPdisconnect(true);
        delay(200);
        apStatus = WiFi.softAP(hotspot_ssid);
        Serial.printf("[STEP 3] try default softAP result: %s\n", apStatus ? "OK" : "FAIL");
        delay(500);
    }

    Serial.printf("[STEP 3] WiFi mode: %d, WiFi status: %d\n", WiFi.getMode(), WiFi.status());
    
    Serial.printf("[STEP 3] softAP result: %s\n", apStatus ? "OK" : "FAIL");
    Serial.printf("[STEP 3] AP SSID: %s\n", hotspot_ssid);
    Serial.printf("[STEP 3] AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    
    if (!apStatus) {
        lastWebAction = "Hotspot Error";
        Serial.println("[STEP 3] ERROR: Failed to start AP after retries!");
        Serial.println("========== [WIFI HOTSPOT] INIT FAILED ==========");
        return;
    }
    
    Serial.println("[STEP 3] AP successfully started!");
    
    Serial.printf("[STEP 4] Final AP IP: %s\n", WiFi.softAPIP().toString().c_str());
    delay(300);
    
    // Step 5: 라우트 등록
    Serial.println("[STEP 5] Registering routes...");
    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", WIFI_SETUP_HTML);
    });
    server.on("/api/words", HTTP_GET, handleGetWords);
    server.on("/api/words", HTTP_POST, handleAddWord);
    server.on("/api/words", HTTP_DELETE, handleDeleteWord);
    server.on("/api/wifi/scan", HTTP_GET, handleWiFiScan);
    server.on("/api/wifi/connect", HTTP_POST, handleWiFiConnect);
    server.on("/api/wifi/saved", HTTP_GET, handleGetSavedWiFi);
    server.on("/api/wifi/saved", HTTP_DELETE, handleDeleteSavedWiFi);
    server.on("/api/firebase/save", HTTP_POST, handleSaveFirebase);
    server.on("/api/firebase/get", HTTP_GET, handleGetFirebase);
    
    server.onNotFound([]() {
        if (server.method() == HTTP_OPTIONS) {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.sendHeader("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
            server.sendHeader("Access-Control-Allow-Headers", "Content-Type");
            server.send(204);
        } else {
            server.sendHeader("Access-Control-Allow-Origin", "*");
            server.send(404, "text/plain", "Not Found");
        }
    });
    
    // Step 6: 웹 서버 시작
    Serial.println("[STEP 6] Starting web server...");
    server.begin();
    delay(500);
    
    Serial.println("[STEP 6] Web server started!");
    Serial.println("========== [WIFI HOTSPOT] INIT SUCCESS ==========");
    
    lastWebAction = "Ready!";
}

String getWiFiIP() {
    if (WiFi.getMode() & WIFI_AP) {
        return WiFi.softAPIP().toString();
    }
    return WiFi.localIP().toString();
}

String getWiFiSSID() {
    if (WiFi.getMode() & WIFI_AP) {
        return AP_SSID;
    }
    return WiFi.SSID(); // 현재 접속된 와이파이 이름 반환
}

int getWiFiAPClients() {
    if (WiFi.getMode() & WIFI_AP) {
        return WiFi.softAPgetStationNum();
    }
    return 0;
}

bool isWiFiAPMode() {
    return (WiFi.getMode() & WIFI_AP) != 0;
}

String getFirebaseURL() {
    ensureNVS();
    Preferences prefs;
    String url = "https://dodo-family-space-lck-default-rtdb.firebaseio.com/";
    if (beginPreferences(prefs, "wifi", true)) {
        url = prefs.getString("firebase_url", url);
        prefs.end();
    }
    return url;
}
