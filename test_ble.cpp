#include <Arduino.h>
#include <NimBLEDevice.h>
void setup() {
  NimBLEDevice::init("M5S3");
  NimBLEServer *pServer = NimBLEDevice::createServer();
  NimBLEService *pService = pServer->createService("ABCD");
  pService->start();
  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  pAdvertising->addServiceUUID("ABCD");
  pAdvertising->start();
}
void loop() {}
