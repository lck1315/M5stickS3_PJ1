#ifndef WEB_SERVER_MANAGER_H
#define WEB_SERVER_MANAGER_H

#include <Arduino.h>
#include <Preferences.h>
void ensureNVS();
// Safe Preferences begin wrapper: ensures NVS is initialized and attempts
// recovery (erase+reinit) on failure. Returns true if prefs.begin succeeded.
bool prefsBegin(Preferences &prefs, const char *ns, bool readonly);
void setupWiFiWebManager();
void setupWiFiHotspot();  // [NEW] 직접 WiFi 핫스팟 시작 (AP 모드)
void handleWiFiWebManager();
void stopWiFiWebManager();
String getWiFiIP();
String getWiFiSSID();
int getWiFiAPClients();
bool isWiFiAPMode();
String getFirebaseURL(); // [NEW] Firebase URL 반환

extern String lastWebAction;

#endif
