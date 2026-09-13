#include <lvgl.h>
#include <ui.h>

// ui_Screen1.h는 ui.h 내부에서 이미 포함됨

#include "web_server_manager.h"
#include "wordbook.h"
#include <IRrecv.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRutils.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <EEPROM.h>
#include <SPIFFS.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <esp_pm.h> // [NEW] 자동 전원 관리용
#include <esp_wifi.h>
#include <HTTPClient.h> // [NEW] 구글 캘린더 등 웹 API 통신용
#include <WiFiClientSecure.h> // [NEW] Firebase HTTPS 통신용
#include <vector>

#define IR_TX_PIN 46 // [CONFIRMED] 내장 IR LED 핀
#define IR_RX_PIN 42 // [CONFIRMED] 회로도 확인 결과 42번이 IR_RX 핀임
IRsend irsend(IR_TX_PIN, false); // 신호 반전 해제 (정상 오프 상태 유지)
IRrecv irrecv(IR_RX_PIN);
decode_results results;

// Forward declarations
void saveSettings();
void updateSoundStatusUI();
void setAmplifier(bool on);
void updateBluetoothStatusLabel();
void updateWordDisplay(const String &word);

enum AppMode {
  MODE_WORDBOOK,
  MODE_BT_CONFIG, // [NEW] 블루투스 및 시스템 설정 모드
  MODE_IR_REMOTE,
  MODE_IR_RECEIVE, // [NEW] IR 수신 모드
  MODE_IR_CLONE,   // [NEW] IR 복제 모드
  MODE_SECRET,
  MODE_BT_WALKIE,   // [NEW] 블루투스 무전기 모드
  MODE_GAME,        // [NEW] 캐치 게임 모드
  MODE_RUNNER_GAME, // [NEW] 길러너 친구들 게임 모드
  MODE_SECRET_7,    // [NEW] 비밀기능 7번 모드
  MODE_SECRET_8,    // [NEW] 비밀기능 8번 모드
  MODE_SECRET_9,    // [NEW] 비밀기능 9번 모드
  MODE_SECRET_10,   // [NEW] 비밀기능 10번 모드
  MODE_WIFI_WEB,    // [NEW] WiFi 웹 관리 모드
  MODE_WIFI_HOTSPOT,// [NEW] WiFi 핫스팟 모드 (비밀기능 11번)
  MODE_CALENDAR,    // [NEW] 달력 모드 (구글 캘린더 연동, 비밀기능 12번)
  MODE_CLOUD_WEB,   // [NEW] 클라우드(Firebase) 모드 (비밀기능 13번)
  MODE_WIFI_LIST    // [NEW] 저장된 WiFi 목록 모드 (비밀기능 14번)
};

AppMode currentAppMode = MODE_WORDBOOK;
int secretMenuIndex = 0; // [NEW] 0~14 (1번~15번 항목)

unsigned long lastCloudPollTime = 0;
std::vector<String> cloudWordsList; // [NEW] Firebase 단어 목록 캐시
int cloudWordIndex = 0;             // [NEW] 현재 단어 인덱스
portMUX_TYPE cloudMutex = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t cloudTaskHandle = NULL;
bool cloudNeedsInitialDisplay = true; // [NEW] 클라우드 모드 진입 시 자동 표시 플래그
bool cloudFirstShow = false; // [NEW] 처음 진입 시 버튼 누를 때 첫 단어 보여주기 플래그
volatile int cloudButtonPressed = 0;  // [NEW] 0: None, 1: A, 2: B (Firebase 전송용)
bool cloudTaskStarted = false; // [NEW] 클라우드 태스크 실행 여부

#define ENABLE_BLUETOOTH
char BT_DEVICE_NAME[32] = "M5S3_00";
int deviceNameIndex = 0;

#ifdef ENABLE_BLUETOOTH
#include <NimBLEDevice.h>

class NimBLESerial : public Stream {
public:
  NimBLESerial()
      : deviceConnected(false), pServer(nullptr), pClient(nullptr),
        pService(nullptr), pRemoteTxChar(nullptr), pRemoteRxChar(nullptr) {}

  // [NEW] 슬레이브(대기자)로 시작
  void beginSlave(const char *name) { begin(name); }

  // [NEW] 마스터(검색자)로 시작
  void beginMaster(const char *name) {
    Serial.println("\n======== [BLE MASTER] Search Start ========");
    NimBLEDevice::init(name);
    NimBLEScan *pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(this));
    pScan->setInterval(45); // [OPTIMIZE] 더 빠른 스캔
    pScan->setWindow(15);
    pScan->setActiveScan(true);
    pScan->start(0, nullptr, false); // 무제한 스캔
  }

  void begin(const char *name) {
    Serial.println("\n======== [BLE] Initialize Start ========");

    try {
      // Device 초기화
      NimBLEDevice::init(name);
      NimBLEDevice::setMTU(512); // MTU를 512바이트로 확장 (20바이트 제한 해제)
      Serial.printf("[BLE] Device name set: %s (MTU=512)\n", name);

      // Server 생성
      pServer = NimBLEDevice::createServer();
      if (!pServer) {
        Serial.println("[BLE ERROR] Server creation failed!");
        return;
      }
      pServer->setCallbacks(new ServerCallbacks(this));
      Serial.println("[BLE] Server created");

      // Service 생성
      pService = pServer->createService("6E400001-B5A3-F393-E0A9-E50E24DCCA9E");
      if (!pService) {
        Serial.println("[BLE ERROR] Service creation failed!");
        return;
      }
      Serial.println("[BLE] Service created (6E400001...)");

      // RX Characteristic (Write from client)
      NimBLECharacteristic *pRxChar = pService->createCharacteristic(
          "6E400002-B5A3-F393-E0A9-E50E24DCCA9E",
          NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
      if (!pRxChar) {
        Serial.println("[BLE ERROR] RX Characteristic creation failed!");
        return;
      }
      pRxChar->setCallbacks(new CharCallbacks(this));
      Serial.println("[BLE] RX Characteristic created (6E400002)");

      // TX Characteristic (Notify to client)
      pTxCharacteristic = pService->createCharacteristic(
          "6E400003-B5A3-F393-E0A9-E50E24DCCA9E", NIMBLE_PROPERTY::NOTIFY);
      if (!pTxCharacteristic) {
        Serial.println("[BLE ERROR] TX Characteristic creation failed!");
        return;
      }
      Serial.println("[BLE] TX Characteristic created (6E400003)");

      // Service 시작
      pService->start();
      Serial.println("[BLE] Service started");

      // Advertising 설정
      NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
      pAdvertising->addServiceUUID(pService->getUUID());
      pAdvertising->setScanResponse(true);
      pAdvertising->setMinInterval(0x0020); // [OPTIMIZE] 더 자주 광고 (20ms)
      pAdvertising->setMaxInterval(0x0040); // 40ms
      pAdvertising->start();

      Serial.println("======== [BLE] Initialize Complete! ========\n");
    } catch (...) {
      Serial.println("[BLE ERROR] Exception during initialization!");
    }
  }

  void end() { NimBLEDevice::deinit(); }
  bool connected() { return deviceConnected; }
  int available() override { return rxBuffer.length(); }
  int read() override {
    if (rxBuffer.length() == 0)
      return -1;
    char c = rxBuffer[0];
    rxBuffer.remove(0, 1);
    return c;
  }
  int peek() override {
    if (rxBuffer.length() == 0)
      return -1;
    return rxBuffer[0];
  }
  void flush() override {}
  size_t write(uint8_t c) override {
    if (!deviceConnected)
      return 0;

    if (pTxCharacteristic) { // Server mode
      uint8_t data = c;
      pTxCharacteristic->setValue(&data, 1);
      pTxCharacteristic->notify();
    } else if (pRemoteTxChar) { // Client mode
      pRemoteTxChar->writeValue(&c, 1, false);
    }
    return 1;
  }

  size_t write(const uint8_t *data, size_t size) override {
    if (!deviceConnected || !data || size == 0)
      return 0;

    if (pTxCharacteristic) { // Server mode
      size_t sent = 0;
      while (sent < size) {
        size_t chunk = (size - sent > 20) ? 20 : (size - sent);
        pTxCharacteristic->setValue(data + sent, chunk);
        pTxCharacteristic->notify();
        sent += chunk;
      }
    } else if (pRemoteTxChar) { // Client mode
      size_t sent = 0;
      while (sent < size) {
        size_t chunk = (size - sent > 20) ? 20 : (size - sent);
        pRemoteTxChar->writeValue(data + sent, chunk, false);
        sent += chunk;
      }
    }
    return size;
  }

public:
  bool lastDataReceived =
      false; // [NEW] 데이터 수신 플래그 - loop()에서 접근 가능해야 함

private:
  NimBLEServer *pServer = nullptr;
  NimBLEClient *pClient = nullptr;
  NimBLEService *pService = nullptr;
  NimBLECharacteristic *pTxCharacteristic = nullptr;
  NimBLERemoteCharacteristic *pRemoteTxChar = nullptr;
  NimBLERemoteCharacteristic *pRemoteRxChar = nullptr;
  bool deviceConnected;
  String rxBuffer;

  // [NEW] 마스터용 스캔 콜백
  class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    NimBLESerial *parent;

  public:
    AdvertisedDeviceCallbacks(NimBLESerial *p) : parent(p) {}
    void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
      Serial.printf("[BLE SCAN] Device found: %s\n",
                    advertisedDevice->toString().c_str());

      if (advertisedDevice->haveServiceUUID() &&
          advertisedDevice->isAdvertisingService(
              NimBLEUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"))) {

        Serial.println(
            "[BLE MASTER] Target Walkie-Talkie found! Connecting...");
        NimBLEDevice::getScan()->stop();

        if (parent->pClient) {
          NimBLEDevice::deleteClient(parent->pClient);
        }

        parent->pClient = NimBLEDevice::createClient();
        parent->pClient->setClientCallbacks(new ClientCallbacks(parent));

        if (parent->pClient->connect(advertisedDevice)) {
          Serial.println(
              "[BLE MASTER] Connected to Server. Discovering services...");
          NimBLERemoteService *pRemoteService = parent->pClient->getService(
              "6E400001-B5A3-F393-E0A9-E50E24DCCA9E");

          if (pRemoteService) {
            parent->pRemoteRxChar = pRemoteService->getCharacteristic(
                "6E400003-B5A3-F393-E0A9-E50E24DCCA9E");
            parent->pRemoteTxChar = pRemoteService->getCharacteristic(
                "6E400002-B5A3-F393-E0A9-E50E24DCCA9E");

            if (parent->pRemoteRxChar && parent->pRemoteRxChar->canNotify()) {
              NimBLESerial *pSerial = parent;
              if (parent->pRemoteRxChar->subscribe(
                      true,
                      [pSerial](NimBLERemoteCharacteristic *pChar,
                                uint8_t *pData, size_t length, bool isNotify) {
                        pSerial->rxBuffer.concat((const char *)pData, length);
                        pSerial->lastDataReceived = true;
                      })) {
                parent->deviceConnected = true;
                Serial.println(
                    "[BLE MASTER] Successfully Subscribed to Notifications!");
              } else {
                Serial.println(
                    "[BLE ERROR] Failed to subscribe to notifications.");
              }
            } else {
              Serial.println("[BLE ERROR] Remote characteristic not found or "
                             "not notify-able.");
            }
          } else {
            Serial.println("[BLE ERROR] Remote service not found.");
          }
        } else {
          Serial.println("[BLE ERROR] Connection failed. Restarting scan...");
          NimBLEDevice::getScan()->start(0, nullptr, false);
        }
      }
    }
  };

  class ClientCallbacks : public NimBLEClientCallbacks {
    NimBLESerial *parent;

  public:
    ClientCallbacks(NimBLESerial *p) : parent(p) {}
    void onConnect(NimBLEClient *pClient) override {}
    void onDisconnect(NimBLEClient *pClient) override {
      parent->deviceConnected = false;
      Serial.println("[BLE MASTER] Disconnected. Restarting Scan...");
      NimBLEDevice::getScan()->start(0, nullptr, false);
    }
  };

  class ServerCallbacks : public NimBLEServerCallbacks {
    NimBLESerial *parent;

  public:
    ServerCallbacks(NimBLESerial *p) : parent(p) {}
    void onConnect(NimBLEServer *pServer) override {
      parent->deviceConnected = true;
      Serial.println("\n[BLE CONNECT] Client connected!");
    }
    void onDisconnect(NimBLEServer *pServer) override {
      parent->deviceConnected = false;
      Serial.println("[BLE DISCONNECT] Client disconnected");
      NimBLEDevice::startAdvertising();
    }
  };

  class CharCallbacks : public NimBLECharacteristicCallbacks {
    NimBLESerial *parent;

  public:
    CharCallbacks(NimBLESerial *p) : parent(p) {}

    void onWrite(NimBLECharacteristic *pCharacteristic) override {
      std::string value = pCharacteristic->getValue();
      Serial.printf("[BLE RX] %d bytes: ", (int)value.length());
      for (size_t i = 0; i < value.length(); i++) {
        Serial.printf("%02X ", (uint8_t)value[i]);
      }
      Serial.println();

      if (value.length() > 0) {
        // Use concat with explicit length to avoid truncation at null bytes
        parent->rxBuffer.concat(value.data(), value.length());
        parent->lastDataReceived = true; // [NEW] 데이터 수신 플래그 설정
      }
    }
  };
};

NimBLESerial SerialBT;
#endif

static const uint16_t screenWidth = 135;
static const uint16_t screenHeight = 240;

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf[screenWidth * screenHeight / 10];

// [NEW] 사운드 및 상태 표시용 전역 UI 객체 (SquareLine에 없는 커스텀 객체)
lv_obj_t *ui_SoundLabel = nullptr;
lv_obj_t *ui_SecretContainer = nullptr;     // [NEW] 비밀 메뉴 전용 컨테이너
lv_obj_t *ui_SecretListContainer = nullptr; // [NEW] 리스트 스크롤용 컨테이너
lv_obj_t *ui_SecretLabel = nullptr;         // [NEW] 비밀 메뉴 전용 라벨
lv_obj_t *ui_SecretTitle = nullptr;         // [NEW] 비밀 메뉴 전용 제목

lv_obj_t *ui_SecretSubContainer =
    nullptr; // [NEW] 비밀 서브 메뉴 전용 컨테이너 (7~10번 공유)
lv_obj_t *ui_SecretSubTitle = nullptr; // [NEW] 비밀 서브 메뉴 제목
lv_obj_t *ui_SecretSubLabel = nullptr; // [NEW] 비밀 서브 메뉴 내용 라벨

lv_obj_t *ui_IRContainer = nullptr;     // [NEW] 리모컨 모드 전용 컨테이너
lv_obj_t *ui_IRListContainer = nullptr; // [NEW] 리모컨 리스트 컨테이너
lv_obj_t *ui_IRLabel = nullptr;         // [NEW] 리모컨 모드 전용 라벨
lv_obj_t *ui_IRTitle = nullptr;         // [NEW] 리모컨 모드 제목
int irRemoteIndex = 0;                  // [NEW] 리모컨 선택 인덱스

lv_obj_t *ui_IRRecvContainer = nullptr;     // [NEW] IR 수신 모드 전용 컨테이너
lv_obj_t *ui_IRRecvListContainer = nullptr; // [NEW] IR 수신 리스트 컨테이너
lv_obj_t *ui_IRRecvLabel = nullptr;         // [NEW] IR 수신 모드 전용 라벨
lv_obj_t *ui_IRRecvTitle = nullptr;         // [NEW] IR 수신 모드 제목

lv_obj_t *ui_BTWalkieContainer = nullptr; // [NEW] 블루투스 무전기 전용 컨테이너
lv_obj_t *ui_BTWalkieListContainer =
    nullptr;                            // [NEW] 블루투스 무전기 리스트 컨테이너
lv_obj_t *ui_BTWalkieLabel = nullptr;   // [NEW] 블루투스 무전기 전용 라벨
lv_obj_t *ui_BTWalkieTitle = nullptr;   // [NEW] 블루투스 무전기 제목
bool isBTWalkieTalking = false;         // [NEW] 송신 중 여부
bool isBTWalkieReceiving = false;       // [NEW] 수신 중 여부
unsigned long lastBTWalkieRecvTime = 0; // [NEW] 마지막 수신 시간

int btWalkieRole = 0; // [NEW] 0:선택안함, 1:마스터(찾기), 2:슬레이브(대기)

// [NEW] IR 복제 모드 전역 변수
#define MAX_IR_CLONE_ITEMS                                                     \
  20 // [CRITICAL] 메모리 누수 방지를 위한 최대 복제 항목 제한

struct IRCloneItem {
  decode_type_t type;
  uint64_t value;
  uint16_t bits;
  String name;
};
std::vector<IRCloneItem> irCloneList;
int irCloneIndex = 0;
bool isCloningWait = false;
lv_obj_t *ui_IRCloneContainer = nullptr;
lv_obj_t *ui_IRCloneListContainer = nullptr;
lv_obj_t *ui_IRCloneLabel = nullptr;
lv_obj_t *ui_IRCloneTitle = nullptr;

const int BT_CONFIG_COUNT = 15;
lv_obj_t *ui_BTConfigContainer = nullptr;     // [NEW] BT 설정 모드 컨테이너
lv_obj_t *ui_BTConfigListContainer = nullptr; // [NEW] 리스트 스크롤용 컨테이너
struct BTConfigItemRow {
  lv_obj_t *numLabel = nullptr;
  lv_obj_t *nameLabel = nullptr;
  lv_obj_t *valLabel = nullptr;
};
BTConfigItemRow ui_BTConfigRows[BT_CONFIG_COUNT]; // [NEW] 각 항목별 번호/이름/값 분리 라벨
lv_obj_t *ui_BTConfigTitle = nullptr;         // [NEW] 제목용 라벨
lv_obj_t *ui_BTConfigDescContainer = nullptr; // [NEW] 하단 설명 컨테이너
lv_obj_t *ui_BTConfigDescLabel = nullptr;     // [NEW] 하단 실시간 한글 설명 라벨

int btConfigIndex = 0; // [NEW] BT 설정 항목 인덱스 (0~5)

// [CRITICAL] IR 복제 목록 초기화 함수
void clearIRCloneList() {
  irCloneList.clear();
  irCloneList.shrink_to_fit(); // 메모리 반환
  irCloneIndex = 0;
  isCloningWait = false;
  Serial.println("[IR CLONE] List cleared, memory freed");
}

extern lv_obj_t *ui_Label8; // 블루투스 텍스트 라벨
extern lv_obj_t *ui_Label5; // 블루투스 상태 지시기 (파란 점)
lv_obj_t *ui_BtnIndicator = nullptr; // [NEW] 버튼 누름 피드백 지시기 (Spinner2 중앙)
lv_obj_t *ui_WiFiSSIDLabel = nullptr; // [NEW] 메인 화면 WiFi SSID 표시 라벨

// [NEW] 선으로 떨어지는 목표물 잡는 게임 (위에서 아래로 떨어짐)
void updateGameDisplay();
void resetGame();

// 게임 UI 요소
lv_obj_t *ui_GameContainer = nullptr;
lv_obj_t *ui_GameBar = nullptr;         // 플레이어 선
lv_obj_t *ui_GameScoreLabel = nullptr;  // 스코어
lv_obj_t *ui_GameEnergyLabel = nullptr; // 에너지 표시
lv_obj_t *ui_GameInfoLabel = nullptr;   // 게임 정보

// 게임 변수 - 선의 위치 및 컨트롤
int barY = 200;                       // 선의 Y 좌표 (하단 고정)
const int barX = 5;                   // 선의 X 좌표
int barWidth = 125;                   // 선의 너비 (거의 전체 화면)
const int barHeight = 8;              // 선의 높이 (더 두껍게)
lv_obj_t *ui_GameEnergyBar = nullptr; // 시각적 에너지 바

// 떨어지는 목표물 구조
struct FallingObject {
  float y;
  int x;
  int size;
  float speed;   // [NEW] 개별 속도
  lv_obj_t *obj; // LVGL 객체 포인터
};
std::vector<FallingObject> fallingObjects;

// 게임 상태
int gameEnergy = 100; // 현재 에너지 (0~100)
const int maxEnergy = 100;
int gameCaught = 0; // 잡은 목표물 개수
int gameMissed = 0; // 놓친 목표물 개수
int gameCombo = 0;  // [NEW] 콤보 카운트
int maxCombo = 0;   // [NEW] 최대 콤보
bool isGameOver = false;
String lastJudgement = ""; // [NEW] 마지막 판정 (PERFECT, GREAT, MISS)

// UI 객체 포인터 추가
lv_obj_t *ui_GameComboLabel = nullptr;
lv_obj_t *ui_GameJudgeLabel = nullptr;

// 게임 난이도
unsigned long lastObjectSpawnTime = 0;
int objectSpawnInterval = 1200; // 목표물 생성 간격 (ms)
float objectFallSpeed = 4.0f;   // 목표물 떨어지는 속도 (상향)
bool btnAPressed = false;       // A 버튼 상태

// ─────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────
// [NEW] 길러너 친구들 게임 (Crossy Road 스타일)
// ─────────────────────────────────────────────────────────
// 장애물 구조체 (좌우로 움직임)
struct RunnerObstacle {
  float x;       // X 위치 (좌우)
  int y;         // Y 위치 (고정 행)
  float vx;      // X 방향 속도
  int width;     // 너비
  int height;    // 높이
  lv_obj_t *obj; // LVGL 객체 포인터
};

struct RunnerLane {
  int y;       // 현재 Y 위치 (0 ~ 240)
  int type;    // 0: Grass, 1: Road
  int dir;     // 방향: -1 (좌), 1 (우)
  float speed; // 속도
  unsigned long lastSpawnTime;
  int spawnInterval;
  lv_obj_t *bgObj; // 레인 배경 LVGL 객체
};

// 길러너 게임 상태
lv_obj_t *ui_RunnerGameContainer = nullptr;
lv_obj_t *ui_RunnerPlayerBar = nullptr;         // 플레이어
lv_obj_t *ui_RunnerScoreLabel = nullptr;        // 스코어/거리
lv_obj_t *ui_RunnerGameOverLabel = nullptr;     // 게임 오버 라벨
lv_obj_t *ui_RunnerTutorialContainer = nullptr; // [NEW] 튜토리얼 컨테이너
std::vector<RunnerObstacle> runnerObstacles;
std::vector<RunnerLane> runnerLanes;

// 길러너 게임 변수
int runnerPlayerY = 220; // 플레이어 Y 위치 (한 칸 = 20px)
int runnerScore = 0;     // 현재 거리/점수
bool isRunnerGameOver = false;
bool isRunnerReady = true; // [NEW] 준비 상태 플래그

void updateRunnerGameDisplay();
void resetRunnerGame();

// ── [NEW] 플래피 버드 게임 데이터 구조체 및 전역 변수 정의 ──
struct FlappyPipe {
  float x;
  float gapY;      // 갭(통과할 구멍)의 중앙 Y 좌표
  float gapHeight; // 갭의 세로 크기
  lv_obj_t *topObj;
  lv_obj_t *bottomObj;
  bool passed;
};

// UI 컴포넌트
lv_obj_t *ui_FlappyContainer = nullptr;
lv_obj_t *ui_FlappyBird = nullptr;
lv_obj_t *ui_FlappyScoreLabel = nullptr;
lv_obj_t *ui_FlappyGameOverLabel = nullptr;
lv_obj_t *ui_FlappyTutorialContainer = nullptr; // [NEW] 튜토리얼 컨테이너

// 게임 상태 변수
std::vector<FlappyPipe> flappyPipes;
float flappyBirdY = 120.0f;
float flappyBirdYSpeed = 0.0f;
int flappyScore = 0;
int flappyHighScore = 0;
bool isFlappyGameOver = false;
bool isFlappyReady = true; // 게임 시작 전 준비 상태
unsigned long lastFlappyPipeSpawnTime = 0;
unsigned long flappyPressStartTime = 0;
bool flappyHoldActive = false;

void updateFlappyDisplay();
void resetFlappyGame();

// ─────────────────────────────────────────────────────────
// [NEW] Vector Classic 게임 구조체 및 상태 변수
// ─────────────────────────────────────────────────────────
struct VectorBuilding {
  float x;
  float width;
  float height;
  lv_obj_t *obj;
};

struct VectorObstacle {
  float x;
  float width;
  float height;
  bool isHigh; // true면 슬라이딩으로 피해야 함, false면 점프로 피해야 함
  lv_obj_t *obj;
  bool passed;
};

enum VectorState {
  VSTATE_RUNNING,
  VSTATE_JUMPING,
  VSTATE_FLIPPING,
  VSTATE_SLIDING,
  VSTATE_STUMBLING,
  VSTATE_TASERED
};


// 선언
lv_obj_t *ui_VectorContainer = nullptr;
lv_obj_t *ui_VectorPlayerCanvas = nullptr;
lv_obj_t *ui_VectorHunterCanvas = nullptr;
lv_obj_t *ui_VectorScoreLabel = nullptr;
lv_obj_t *ui_VectorGameOverLabel = nullptr;
lv_obj_t *ui_VectorTutorialContainer = nullptr;

std::vector<VectorBuilding> vectorBuildings;
std::vector<VectorObstacle> vectorObstacles;

float vectorPlayerY = 160.0f;
float vectorPlayerYSpeed = 0.0f;
VectorState vectorPlayerState = VSTATE_RUNNING;
int vectorPlayerAnimFrame = 0;

float vectorHunterX = -20.0f; // 헌터의 상대 X 위치 (Player X = 35)
float vectorHunterY = 160.0f;
float vectorHunterYSpeed = 0.0f;
VectorState vectorHunterState = VSTATE_RUNNING;
int vectorHunterAnimFrame = 0;

int vectorScore = 0;
int vectorHighScore = 0;
bool isVectorGameOver = false;
bool isVectorReady = true;

unsigned long lastVectorBuildingSpawnTime = 0;
unsigned long vectorStateTimer = 0;

// ─────────────────────────────────────────────────────────
// [NEW] Stack 블록 쌓기 게임 구조체 및 전역 변수
// ─────────────────────────────────────────────────────────
struct StackBlock {
  float x;
  float width;
  int y;
  lv_obj_t *obj;
  uint32_t colorHex;
};

struct StackShard {
  float x;
  float width;
  float y;
  float ySpeed;
  lv_obj_t *obj;
  uint32_t colorHex;
};



lv_obj_t *ui_StackContainer = nullptr;
lv_obj_t *ui_StackScoreLabel = nullptr;
lv_obj_t *ui_StackGameOverLabel = nullptr;
lv_obj_t *ui_StackTutorialContainer = nullptr;

std::vector<StackBlock> stackBlocks;
std::vector<StackShard> stackShards;

float stackActiveX = 0.0f;
float stackActiveWidth = 80.0f;
float stackActiveSpeed = 2.5f;
int stackActiveDir = 1;
int stackActiveY = 180;
lv_obj_t *ui_StackActiveObj = nullptr;

int stackScore = 0;
int stackHighScore = 0;
int stackPerfectCombo = 0;
bool isStackGameOver = false;
bool isStackReady = true;

void updateVectorDisplay();
void resetVectorGame();
void updateStackDisplay();
void resetStackGame();

// ─────────────────────────────────────────────────────────
// [NEW] Space Shooter (비밀기능 10번) 구조체 및 전역 변수
// ─────────────────────────────────────────────────────────
struct ShooterLaser {
  float x;
  float y;
  bool isEnemy;
  lv_obj_t *obj;
};

struct ShooterEnemy {
  float x;
  float y;
  float speedX;
  int hp;
  lv_obj_t *obj;
};

struct ShooterStar {
  float x;
  float y;
  float speed;
  lv_obj_t *obj;
};

lv_obj_t *ui_ShooterContainer = nullptr;
lv_obj_t *ui_ShooterPlayer = nullptr;
lv_obj_t *ui_ShooterScoreLabel = nullptr;
lv_obj_t *ui_ShooterHPBar = nullptr;
lv_obj_t *ui_ShooterGameOverLabel = nullptr;
lv_obj_t *ui_ShooterTutorialContainer = nullptr;

std::vector<ShooterLaser> shooterLasers;
std::vector<ShooterEnemy> shooterEnemies;
std::vector<ShooterStar> starfield;

float shooterPlayerX = 60.0f;
const float shooterPlayerY = 205.0f;
float shooterPlayerDir = 1.0f;
float shooterPlayerSpeed = 2.0f;

int shooterScore = 0;
int shooterHighScore = 0;
int shooterHP = 100;
bool isShooterGameOver = false;
bool isShooterReady = true;

unsigned long lastEnemySpawnTime = 0;
unsigned long lastEnemyFireTime = 0;
unsigned long lastPlayerFireTime = 0;

void updateShooterDisplay();
void resetShooterGame();

// [NEW] WiFi Web UI 전역 변수
lv_obj_t *ui_WiFiWebContainer = nullptr;
lv_obj_t *ui_WiFiWebTitle = nullptr;
lv_obj_t *ui_WiFiWebLabel = nullptr;

// --- Calendar UI ---
lv_obj_t *ui_CalendarContainer = nullptr;
lv_obj_t *ui_Calendar = nullptr;
lv_obj_t *ui_CalendarEventLabel = nullptr;
void updateWiFiWebDisplay();
void updateWiFiListDisplay();
int wifiListScrollIndex = 0;
lv_obj_t *ui_WiFiListContainer = nullptr;
lv_obj_t *ui_WiFiListTitle = nullptr;
lv_obj_t *ui_WiFiListLabel = nullptr;

void playTone(uint32_t freq, uint32_t dur);
void soundSuccess();
void soundError();
void soundNext();
void soundPrev();
void soundBTOn();
void soundBTOff();
void soundAutoStart();
void soundAutoStop();
void soundDelete();
void soundWake();
void soundBeep();
void soundBoot(); // [NEW] 시작 웅장음

// Function to convert hex values to EUC-KR encoded Korean string
String hexToKorean(String hexStr) {
  String result = "";
  hexStr.replace(" ", "");

  for (int i = 0; i < (int)hexStr.length(); i += 4) {
    if (i + 3 < (int)hexStr.length()) {
      String hexByte1 = hexStr.substring(i, i + 2);
      String hexByte2 = hexStr.substring(i + 2, i + 4);
      uint8_t byte1 = (uint8_t)strtol(hexByte1.c_str(), NULL, 16);
      uint8_t byte2 = (uint8_t)strtol(hexByte2.c_str(), NULL, 16);
      char koreanChar[3] = {0};
      koreanChar[0] = (char)byte1;
      koreanChar[1] = (char)byte2;
      koreanChar[2] = '\0';
      result += String(koreanChar);
    }
  }
  return result;
}

// [FIX] 기본값 5분으로 변경 (사용자 요청)
int slimModeTime = 5;

// Auto word transition
bool autoTransitionEnabled = false;
unsigned long lastTransitionTime = 0;
int autoTransitionInterval = 2000; // 기본 2초
// Button A direction
bool btnADirectionForward = true;
// Assign auto-skip to A when true
bool aAssignedAuto = false;
// Slim mode
unsigned long lastActivityTime = 0;
bool screenOn = true;

// Hold times (seconds)
int btHoldTime = 5;     // BtnA -> BT toggle
int secretHoldTime = 1; // BtnB -> Secret menu
int aBtnDirTime = 1;    // A Button <<>> toggle time
int aBtnAutoTime = 2;   // A Button G/A toggle time

// Bluetooth
#ifdef ENABLE_BLUETOOTH
bool bluetoothEnabled = false; // [FIX] 시작 시 기본적으로 꺼짐
bool soundEnabled = false;     // [FIX] 시작 시 기본 사운드 OFF

void btPrintln(const String &message) {
  if (bluetoothEnabled) {
    if (SerialBT.connected()) {
      Serial.print("[BT TX] ");
      Serial.println(message);
      SerialBT.println(message);
    } else {
      Serial.println("[BT TX SKIP] Not connected");
    }
  }
}
void btPrint(const String &message) {
  if (bluetoothEnabled) {
    if (SerialBT.connected()) {
      Serial.print("[BT TX] ");
      Serial.print(message);
      SerialBT.print(message);
    }
  }
}

void updateBluetoothStatusLabel() {
  if (ui_Label8) {
    if (bluetoothEnabled) {
      lv_label_set_text(ui_Label8, "B");
      lv_obj_set_style_text_color(ui_Label8, lv_color_hex(0x0000FF),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
      lv_label_set_text(ui_Label8, "b");
      lv_obj_set_style_text_color(ui_Label8, lv_color_hex(0x999999),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }
}
#endif

void updateSoundStatusUI() {
  if (ui_SoundLabel) {
    if (soundEnabled) {
      lv_label_set_text(ui_SoundLabel, "S");
      // 블루투스(B)와 동일한 파란색
      lv_obj_set_style_text_color(ui_SoundLabel, lv_color_hex(0x0000FF),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
      lv_label_set_text(ui_SoundLabel, "S");
      // 블루투스(b)와 동일한 회색
      lv_obj_set_style_text_color(ui_SoundLabel, lv_color_hex(0x999999),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
  }
  // ui_Label1은 원래대로 유지
  if (ui_Label1) {
    lv_label_set_text(ui_Label1, "project");
    lv_obj_set_style_text_color(ui_Label1, lv_color_hex(0xB0B0B0),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  }
}

// Function prototypes
void set_backlight(bool on);
void set_backlight_brightness(uint8_t brightness);
void display_commands();
int getBatteryPercentage(float voltage);
void navigateButtonADirection();
void updateWordDisplay(const String &word);
void updateSecretMenuDisplay();                 // [NEW] 비밀 메뉴 UI 갱신
void updateIRRemoteDisplay();                   // [NEW] 리모컨 모드 UI 갱신
void updateIRRecvDisplay(String info = "");     // [NEW] IR 수신 모드 UI 갱신
void updateIRCloneDisplay();                    // [NEW] IR 복제 모드 UI 갱신
void updateBTWalkieDisplay(String status = ""); // [NEW] 블루투스 무전기 UI 갱신
void updateBTConfigDisplay();           // [NEW] BT 및 시스템 설정 UI 갱신
void updateSecretSubDisplay(int index); // [NEW] 비밀 서브 메뉴 UI 갱신 (7~10번)
void processCommand(String &buf);

// Backlight — M5.Display.setBrightness() 사용
static uint8_t current_brightness = 25; // 최대 / 평상시 밝기 (10~255)
static uint8_t normal_brightness = 25;  // 최대 밝기 토글 전 밝기 저장용
static uint8_t min_brightness = 5;      // [NEW] 최소 / 디밍 밝기 (0~30)
static int dimTimeSec = 15;             // [NEW] 최소 밝기로 전환되는 무활동 시간 (초단위, 0=OFF)
static bool isDimmed = false;          // 화면 디밍(어두워짐) 상태 플래그
static bool isManualMinBright = false;  // [NEW] A+B로 수동 최소 밝기 모드 고정 여부
static bool savedBrightStateBeforeConfig = false; // [NEW] 설정 메뉴 진입 전 밝기 상태 백업
static uint8_t preDimBrightness = 25;  // 디밍 진입 전의 원래 밝기 백업용
static unsigned long lastBothBtnsAction = 0; // A+B 토글 직후 릴리즈 시 디밍 해제 방지용
uint32_t currentCpuFreq = 240;         // 현재 CPU 목표 주파수

void setSystemClock(uint32_t mhz) {
  if (getCpuFrequencyMhz() != mhz) {
    setCpuFrequencyMhz(mhz);
    currentCpuFreq = mhz;
    Serial.printf("[POWER] CPU Frequency changed to %d MHz\n", mhz);
  }
}

// [NEW] 단어장 모드용 저전력 모드 활성화
void enablePowerSavingMode() {
  Serial.println("\n========== [POWER SAVING] ENABLE ==========");
  
  // BLE 종료
  #ifdef ENABLE_BLUETOOTH
  if (bluetoothEnabled) {
    Serial.println("[POWER SAVING] Disabling BLE...");
    SerialBT.end();
    esp_bt_controller_disable();
    bluetoothEnabled = false;
    updateBluetoothStatusLabel();
  }
  #endif
  
  // WiFi 종료
  Serial.println("[POWER SAVING] Disabling WiFi...");
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  
  // CPU 클럭 낮추기 (단어장은 계산 부하 적음)
  Serial.println("[POWER SAVING] Reducing CPU frequency to 80MHz...");
  setSystemClock(80);
  
  // IR 수신 비활성화
  Serial.println("[POWER SAVING] Disabling IR receiver...");
  irrecv.disableIRIn();
  
  Serial.println("========== [POWER SAVING] COMPLETE ==========\n");
}

// [NEW] 단어장 모드용 저전력 모드 해제
void disablePowerSavingMode() {
  Serial.println("\n========== [POWER SAVING] DISABLE ==========");
  
  // CPU 클럭 복구
  Serial.println("[POWER SAVING] Restoring CPU frequency to 240MHz...");
  setSystemClock(240);
  
  Serial.println("========== [POWER SAVING] DISABLED ==========\n");
}

void set_backlight_brightness(uint8_t brightness) {
  current_brightness = brightness;
  normal_brightness = brightness;
  if (!isDimmed) {
    M5.Display.setBrightness(brightness);
  }
}

void set_backlight(bool on) {
  uint8_t target_bright =
      on ? (current_brightness > 0 ? current_brightness : 25) : 0;
  if (isDimmed && on) {
    target_bright = min_brightness; // 디밍 상태에서 켜질 때는 설정된 최소 밝기로 켬
  }
  M5.Display.setBrightness(target_bright);
}

void display_commands() {
  const char *help = "=== Commands ===\r\n"
                     "w [word1] [word2] : Add words\r\n"
                     "h [hex]           : Add Korean word (EUC-KR hex)\r\n"
                     "r [index]         : Remove word by index\r\n"
                     "first             : Go to first word\r\n"
                     "clear/deleteall   : Delete all words\r\n"
                     "bl [0-255]        : Backlight brightness\r\n"
                     "pf [1-10000]      : PWM frequency (Hz)\r\n"
                     "auto [sec]        : Auto-transition interval\r\n"
                     "time [min]        : Slim mode timeout\r\n"
                     "sound [on/off]    : Toggle sound\r\n"
                     "vol [0-255]       : Set volume\r\n"
                     "================\r\n";
  Serial.print(help);
#ifdef ENABLE_BLUETOOTH
  btPrint(String(help));
#endif
}

// [FIX] M5StickS3 Li-Po 배터리 비선형 방전 커브 기반 (4.2V=100%, 3.3V=0%)
int getBatteryPercentage(float voltage) {
  // Li-Po 배터리 실측 기반 비선형 룩업 테이블
  // M5StickS3는 충전 완료 시 약 4.2V, 컷오프 약 3.3V
  static const float voltTable[] = {3.30, 3.50, 3.60, 3.70, 3.75, 3.80, 3.85, 3.90, 3.95, 4.00, 4.10, 4.20};
  static const int   pctTable[]  = {   0,    5,   10,   20,   30,   40,   50,   60,   70,   80,   90,  100};
  static const int tableSize = sizeof(voltTable) / sizeof(voltTable[0]);

  if (voltage >= 4.20f) return 100;
  if (voltage <= 3.30f) return 0;

  // 룩업 테이블에서 선형 보간
  for (int i = 1; i < tableSize; i++) {
    if (voltage <= voltTable[i]) {
      float ratio = (voltage - voltTable[i - 1]) / (voltTable[i] - voltTable[i - 1]);
      return pctTable[i - 1] + (int)(ratio * (pctTable[i] - pctTable[i - 1]));
    }
  }
  return 100;
}

// ─────────────────────────────────────────────────────────
// [NEW] 통합 화면 업데이트 헬퍼
// 단어 텍스트, 그룹 카운터(Label2), 아크(Arc1)를 한 번에 갱신
// ─────────────────────────────────────────────────────────
void speakWordTTS(const String& word);

void updateWordDisplay(const String &word) {
  if (wordbook.getWordCount() == 0) {
    lv_label_set_text(ui_MainText1, "No Words Found\nAdd words via Serial/BT");
    // 아크 및 기타 정보 초기화
    if (ui_Arc1)
      lv_arc_set_value(ui_Arc1, 0);
    if (ui_Label2)
      lv_label_set_text(ui_Label2, "0/0");
    if (ui_Bar1)
      lv_bar_set_value(ui_Bar1, 0, LV_ANIM_OFF);
    return;
  }

  Serial.printf("[UPDATE WORD] Called with: %s\n", word.c_str());

  lv_label_set_text(ui_MainText1, word.c_str());

  // ── [NEW] 배경색 및 UI 복구 (비밀/리모컨 모드에서 돌아올 때) ──
  lv_obj_set_style_bg_color(lv_scr_act(), lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(lv_scr_act(), LV_OPA_COVER, 0);
  lv_obj_set_style_text_color(ui_MainText1, lv_color_hex(0xFFFFFF), 0);

  // ── [NEW] 전용 컨테이너들 숨기기 ──
  if (ui_SecretContainer)
    lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
  if (ui_IRContainer)
    lv_obj_add_flag(ui_IRContainer, LV_OBJ_FLAG_HIDDEN);
  if (ui_IRRecvContainer)
    lv_obj_add_flag(ui_IRRecvContainer, LV_OBJ_FLAG_HIDDEN);
  if (ui_IRCloneContainer)
    lv_obj_add_flag(ui_IRCloneContainer, LV_OBJ_FLAG_HIDDEN);
  if (ui_BTWalkieContainer)
    lv_obj_add_flag(ui_BTWalkieContainer, LV_OBJ_FLAG_HIDDEN);
  if (ui_SecretSubContainer)
    lv_obj_add_flag(ui_SecretSubContainer, LV_OBJ_FLAG_HIDDEN);
  if (ui_FlappyContainer)
    lv_obj_add_flag(ui_FlappyContainer, LV_OBJ_FLAG_HIDDEN);
  if (ui_VectorContainer)
    lv_obj_add_flag(ui_VectorContainer, LV_OBJ_FLAG_HIDDEN);

  // 숨겨진 요소들 다시 표시
  if (ui_Arc1)
    lv_obj_clear_flag(ui_Arc1, LV_OBJ_FLAG_HIDDEN);
  if (ui_Label2)
    lv_obj_clear_flag(ui_Label2, LV_OBJ_FLAG_HIDDEN);
  if (ui_Bar1)
    lv_obj_clear_flag(ui_Bar1, LV_OBJ_FLAG_HIDDEN);
  if (ui_Label4)
    lv_obj_clear_flag(ui_Label4, LV_OBJ_FLAG_HIDDEN);
  if (ui_Label3)
    lv_obj_clear_flag(ui_Label3, LV_OBJ_FLAG_HIDDEN);
  if (ui_Label7)
    lv_obj_clear_flag(ui_Label7, LV_OBJ_FLAG_HIDDEN);
  if (ui_Label6)
    lv_obj_clear_flag(ui_Label6, LV_OBJ_FLAG_HIDDEN);
  if (ui_Label1)
    lv_obj_clear_flag(ui_Label1, LV_OBJ_FLAG_HIDDEN);

  lv_obj_set_style_pad_left(ui_MainText1, 3, LV_PART_MAIN);
  lv_obj_set_style_pad_right(ui_MainText1, 2, LV_PART_MAIN);
  lv_obj_set_style_pad_top(ui_MainText1, 5, LV_PART_MAIN);
  lv_obj_set_style_pad_bottom(ui_MainText1, 5, LV_PART_MAIN);

  // ── 텍스트 왼쪽 정렬 ──
  lv_obj_set_style_text_align(ui_MainText1, LV_TEXT_ALIGN_LEFT,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_align(ui_MainText1, LV_ALIGN_LEFT_MID);
  lv_obj_set_x(ui_MainText1, 0);
  lv_obj_set_y(ui_MainText1, -52);

  // ── 자동 폰트 크기 조절: 2줄 초과 시 폰트 축소 ──
  // 화면 전체 너비 135px을 알뜰하게 활용 (가용 너비 = 135 - 3 - 2 = 130px, 기존 120px 대비 +10px 확보)
  const int textAreaWidth = 135;

  // 한글 포함 여부 감지 (UTF-8: 한글은 0xEA~0xED로 시작하는 3바이트)
  bool hasKorean = false;
  const char *p = word.c_str();
  while (*p) {
    uint8_t c = (uint8_t)*p;
    if (c >= 0xEA && c <= 0xED) {
      hasKorean = true;
      break;
    }
    p++;
  }

  const lv_font_t *selectedFont = &ui_font_Font22; // 기본 폰트
  const lv_font_t *fontCandidates[] = {
      &ui_font_Font22, // 22px
      &ui_font_Font16   // 16px (완전한 한글 풀버전 폰트)
  };
  const int fontCount = 2;
  selectedFont = fontCandidates[fontCount - 1];

  for (int i = 0; i < fontCount; i++) {
    const lv_font_t *font = fontCandidates[i];
    int lineHeight = lv_font_get_line_height(font);
    int maxHeight = lineHeight * 2 + 2;

    lv_obj_set_style_text_font(ui_MainText1, font,
                               LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_width(ui_MainText1, textAreaWidth);
    lv_obj_update_layout(ui_MainText1);
    lv_coord_t actualHeight = lv_obj_get_self_height(ui_MainText1);

    if (actualHeight <= maxHeight) {
      selectedFont = font;
      break;
    }
  }

  lv_obj_set_style_text_font(ui_MainText1, selectedFont,
                             LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_width(ui_MainText1, textAreaWidth);



  int currentGroupPos = wordbook.getCurrentGroupIndex();
  int totalGroupCount = wordbook.getGroupCount();
  lv_label_set_text(
      ui_Label2,
      (String(currentGroupPos) + "/" + String(totalGroupCount)).c_str());

  int totalWords = wordbook.getWordCount();
  int arcValue = (totalWords > 0)
                     ? ((wordbook.getCurrentIndex() + 1) * 100 / totalWords)
                     : 0;
  lv_arc_set_value(ui_Arc1, arcValue);

#ifdef ENABLE_BLUETOOTH
  // [DEBUG] 단어 갱신 확인 로그 (블루투스 전송은 버튼 핸들러에서 상세히 수행함)
  if (bluetoothEnabled && SerialBT.connected()) {
    Serial.printf("[WORD UPDATE] Screen updated with: %s\n", word.c_str());
  }
#endif
}

// ─────────────────────────────────────────────────────────
// [NEW] 비밀 메뉴 화면 업데이트
// 전용 컨테이너를 사용하여 기존 화면과 완전히 분리된 디자인을 제공합니다.
// ─────────────────────────────────────────────────────────
void updateSecretMenuDisplay() {
  if (!ui_SecretContainer) {
    ui_SecretContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_SecretContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_SecretContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_SecretContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_SecretContainer, 0, 0);
    lv_obj_set_style_radius(ui_SecretContainer, 0, 0);
    lv_obj_set_align(ui_SecretContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_SecretContainer, LV_SCROLLBAR_MODE_OFF);

    ui_SecretTitle = lv_label_create(ui_SecretContainer);
    lv_obj_set_width(ui_SecretTitle, 135);
    lv_obj_set_style_text_color(ui_SecretTitle, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_SecretTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_SecretTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_SecretTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_style_pad_top(ui_SecretTitle, 10, 0);
    lv_obj_set_style_bg_color(ui_SecretTitle, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_SecretTitle, LV_OPA_COVER, 0);
    lv_label_set_text(ui_SecretTitle, "SECRET MENU");

    // 리스트 전용 컨테이너 (제목 아래 배치)
    ui_SecretListContainer = lv_obj_create(ui_SecretContainer);
    lv_obj_set_size(ui_SecretListContainer, 135, 210);
    lv_obj_set_align(ui_SecretListContainer, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_SecretListContainer, 28);
    lv_obj_set_style_bg_opa(ui_SecretListContainer, 0, 0);
    lv_obj_set_style_border_width(ui_SecretListContainer, 0, 0);
    lv_obj_set_scrollbar_mode(ui_SecretListContainer, LV_SCROLLBAR_MODE_OFF);

    ui_SecretLabel = lv_label_create(ui_SecretListContainer);
    lv_obj_set_width(ui_SecretLabel, 135);
    lv_obj_set_style_text_color(ui_SecretLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_SecretLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_SecretLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_align(ui_SecretLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_pad_left(ui_SecretLabel, 0, 0); // 왼쪽으로 한 칸 이동 (5 -> 0)
  }

  // 2. 비밀 메뉴 표시 및 텍스트 갱신
  lv_obj_clear_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_SecretContainer, -1); // 항상 최상단에 표시

  String menuText = "";
  const int secretMenuCount = 9; // [MOD] Added Stack Game
  for (int i = 0; i < secretMenuCount; i++) {
    menuText += (i == secretMenuIndex) ? ">" : " ";

    if (i == 0)
      menuText += "0. Cloud Mode";
    else if (i == 1)
      menuText += "1. WiFi Web";
    else if (i == 2)
      menuText += "2. Saved WiFi";
    else if (i == 3)
      menuText += "3. BT Config";
    else if (i == 4)
      menuText += "4. IR Remote";
    else if (i == 5)
      menuText += "5. IR Receiver";
    else if (i == 6)
      menuText += "6. IR Clone";
    else if (i == 7)
      menuText += "7. BT Walkie";
    else if (i == 8)
      menuText += "8. Stack Game"; // [NEW] 

    menuText += "\n";
  }

  menuText += "\nB : Next \nA : Enter";
  lv_label_set_text(ui_SecretLabel, menuText.c_str());
  lv_obj_set_style_text_line_space(
      ui_SecretLabel, 3,
      0); // 가독성을 위해 줄 간격을 3으로 복구 (BT Config와 동일)

  // 0번 항목(BT Config)과 마찬가지로 메뉴가 많아질 때 선택 위치에 따라 스크롤
  // 처리
  int scrollY = 0;
  if (secretMenuIndex > 4) {
    scrollY = (secretMenuIndex - 4) * 17;
  }
  lv_obj_set_y(ui_SecretLabel, 5 - scrollY);
  Serial.printf("[SECRET MENU] UI Updated selection: %d (scrollY: %d)\n",
                secretMenuIndex + 1, scrollY);
}

// ─────────────────────────────────────────────────────────
// [NEW] WiFi Web UI 갱신 함수
// ─────────────────────────────────────────────────────────
void updateWiFiListDisplay() {
  if (!ui_WiFiListContainer) {
    ui_WiFiListContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_WiFiListContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_WiFiListContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_WiFiListContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_WiFiListContainer, 0, 0);
    lv_obj_set_style_radius(ui_WiFiListContainer, 0, 0);
    lv_obj_set_align(ui_WiFiListContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_WiFiListContainer, LV_SCROLLBAR_MODE_OFF);

    ui_WiFiListTitle = lv_label_create(ui_WiFiListContainer);
    lv_obj_set_width(ui_WiFiListTitle, 135);
    lv_obj_set_style_text_color(ui_WiFiListTitle, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_text_font(ui_WiFiListTitle, &ui_font_Font16, 0);
    lv_obj_set_style_text_align(ui_WiFiListTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_WiFiListTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_WiFiListTitle, 10);
    lv_label_set_text(ui_WiFiListTitle, "SAVED WIFI");

    ui_WiFiListLabel = lv_label_create(ui_WiFiListContainer);
    lv_obj_set_width(ui_WiFiListLabel, 125);
    lv_obj_set_style_text_color(ui_WiFiListLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_WiFiListLabel, &ui_font_Font16, 0);
    lv_obj_set_style_text_align(ui_WiFiListLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_align(ui_WiFiListLabel, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_WiFiListLabel, 35);
  }

  lv_obj_clear_flag(ui_WiFiListContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_WiFiListContainer, -1);

  Preferences prefs;
  prefsBegin(prefs, "wifi", true);
  int count = prefs.getInt("count", 0);
  if (count == 0 && prefs.getString("ssid", "").length() > 0) {
    count = 1;
  }

  std::vector<String> savedSsids;
  for (int i = 0; i < count; i++) {
    String s = prefs.getString(("ssid_" + String(i)).c_str(), "");
    if (s.length() > 0) {
      savedSsids.push_back(s);
    }
  }
  prefs.end();

  String listStr = "";
  
  if (WiFi.status() == WL_CONNECTED) {
    listStr += "[연결됨] " + WiFi.SSID() + "\n----------------\n";
  } else {
    listStr += "[연결 안됨]\n----------------\n";
  }

  if (savedSsids.empty()) {
    listStr += "No Saved WiFi.\n\nB: 나가기";
    lv_label_set_text(ui_WiFiListLabel, listStr.c_str());
    return;
  }

  if (wifiListScrollIndex >= savedSsids.size()) {
    wifiListScrollIndex = 0;
  }

  // 연결 상태가 위에 표시되므로 3개씩만 보여줌
  int endIndex = min((int)savedSsids.size(), wifiListScrollIndex + 3);
  for (int i = wifiListScrollIndex; i < endIndex; i++) {
    listStr += "- " + savedSsids[i] + "\n";
  }

  listStr += "\n[" + String(wifiListScrollIndex + 1) + "~" + String(endIndex) + " / " + String(savedSsids.size()) + "]\n";
  listStr += "A: 스크롤\nB: 나가기";
  lv_label_set_text(ui_WiFiListLabel, listStr.c_str());
}

void updateWiFiWebDisplay() {
  if (!ui_WiFiWebContainer) {
    ui_WiFiWebContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_WiFiWebContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_WiFiWebContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_WiFiWebContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_WiFiWebContainer, 0, 0);
    lv_obj_set_style_radius(ui_WiFiWebContainer, 0, 0);
    lv_obj_set_align(ui_WiFiWebContainer, LV_ALIGN_CENTER);

    ui_WiFiWebTitle = lv_label_create(ui_WiFiWebContainer);
    lv_obj_set_width(ui_WiFiWebTitle, 135);
    lv_obj_set_style_text_color(ui_WiFiWebTitle, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_text_font(ui_WiFiWebTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_WiFiWebTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_WiFiWebTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_WiFiWebTitle, 10);

    ui_WiFiWebLabel = lv_label_create(ui_WiFiWebContainer);
    lv_obj_set_width(ui_WiFiWebLabel, 125);
    lv_obj_set_style_text_color(ui_WiFiWebLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_WiFiWebLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_WiFiWebLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_WiFiWebLabel, LV_ALIGN_CENTER, 0, 10);
  }

  lv_obj_clear_flag(ui_WiFiWebContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_WiFiWebContainer, -1);

  // [NEW] MODE_WIFI_HOTSPOT 여부 확인
  bool isHotspotMode = (currentAppMode == MODE_WIFI_HOTSPOT);
  
  // 제목 설정
  if (isHotspotMode) {
    lv_label_set_text(ui_WiFiWebTitle, "HOTSPOT");
  } else {
    lv_label_set_text(ui_WiFiWebTitle, "WiFi Web MGR");
  }

  String info = "";
  if (isWiFiAPMode()) {
    int clients = getWiFiAPClients();
    String clientStr = (clients > 0) ? String(clients) + " connected" : "waiting";
    
    if (isHotspotMode) {
      info = "[HOTSPOT MODE]\n";
      info += "SSID:\n";
      info += "M5STICK_HS\n";
      info += "IP: 192.168.4.1\n";
      info += "Clients: " + clientStr + "\n\n";
      info += lastWebAction + "\n";
      info += "Exit: BtnB";
    } else {
      info = "[AP CONFIG]\n";
      info += "SSID:\n" + String(getWiFiSSID()) + "\n";
      info += "IP: " + getWiFiIP() + "\n";
      info += "Clients: " + clientStr + "\n\n";
      info += lastWebAction + "\n";
      info += "Exit: BtnB";
    }
  } else {
    String statusStr =
        (WiFi.status() == WL_CONNECTED) ? "Connected" : "Failed";
    info = "[" + statusStr + "]\n";
    info += "WiFi: " + getWiFiSSID() + "\n";
    info += "IP: " + getWiFiIP() + "\n\n";
    info += lastWebAction + "\n";
    info += "Exit: BtnB";
  }
  lv_label_set_text(ui_WiFiWebLabel, info.c_str());
}

// ─────────────────────────────────────────────────────────
// [NEW] Calendar UI 갱신 및 이벤트 핸들러
// ─────────────────────────────────────────────────────────
static void calendar_event_handler(lv_event_t * e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * obj = lv_event_get_current_target(e);

    if(code == LV_EVENT_VALUE_CHANGED) {
        lv_calendar_date_t date;
        if(lv_calendar_get_pressed_date(obj, &date)) {
            Serial.printf("Clicked date: %02d.%02d.%d\n", date.day, date.month, date.year);
            String info = String(date.year) + "-" + String(date.month) + "-" + String(date.day) + "\n";
            info += "(No Event)";
            if(ui_CalendarEventLabel) {
                lv_label_set_text(ui_CalendarEventLabel, info.c_str());
            }
        }
    }
}

void updateCalendarDisplay() {
  if (!ui_CalendarContainer) {
    ui_CalendarContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_CalendarContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_CalendarContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_CalendarContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_CalendarContainer, 0, 0);
    lv_obj_set_style_radius(ui_CalendarContainer, 0, 0);
    lv_obj_set_align(ui_CalendarContainer, LV_ALIGN_CENTER);

    // Calendar Header
    lv_obj_t* header = lv_label_create(ui_CalendarContainer);
    lv_obj_set_width(header, 135);
    lv_obj_set_style_text_color(header, lv_color_hex(0x00FFFF), 0);
    lv_obj_set_style_text_align(header, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(header, LV_ALIGN_TOP_MID);
    lv_obj_set_y(header, 5);
    lv_label_set_text(header, "CALENDAR");

    // Calendar widget
    ui_Calendar = lv_calendar_create(ui_CalendarContainer);
    lv_obj_set_size(ui_Calendar, 135, 135);
    lv_obj_align(ui_Calendar, LV_ALIGN_TOP_MID, 0, 25);
    lv_obj_add_event_cb(ui_Calendar, calendar_event_handler, LV_EVENT_ALL, NULL);
    
    // Header Arrow for Year/Month Change
    lv_calendar_header_arrow_create(ui_Calendar);
    lv_calendar_set_showed_date(ui_Calendar, 2026, 5); // Default start
    lv_calendar_set_today_date(ui_Calendar, 2026, 5, 26);

    // Event Info Label
    ui_CalendarEventLabel = lv_label_create(ui_CalendarContainer);
    lv_obj_set_width(ui_CalendarEventLabel, 135);
    lv_obj_set_style_text_color(ui_CalendarEventLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_CalendarEventLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_CalendarEventLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(ui_CalendarEventLabel, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_label_set_text(ui_CalendarEventLabel, "Month: < A | B >\nYear: Hold A | B");
  }

  lv_obj_clear_flag(ui_CalendarContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_CalendarContainer, -1);
}
// ─────────────────────────────────────────────────────────
// [NEW] 비밀 서브 메뉴 화면 업데이트 (자리표시자 화면 공유)
// ─────────────────────────────────────────────────────────
void updateSecretSubDisplay(int index) {
  if (!ui_SecretSubContainer) {
    ui_SecretSubContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_SecretSubContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_SecretSubContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_SecretSubContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_SecretSubContainer, 0, 0);
    lv_obj_set_style_radius(ui_SecretSubContainer, 0, 0);
    lv_obj_set_align(ui_SecretSubContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_SecretSubContainer, LV_SCROLLBAR_MODE_OFF);

    ui_SecretSubTitle = lv_label_create(ui_SecretSubContainer);
    lv_obj_set_width(ui_SecretSubTitle, 135);
    lv_obj_set_style_text_color(ui_SecretSubTitle, lv_color_hex(0x00FFFF),
                                0); // Cyan title
    lv_obj_set_style_text_font(ui_SecretSubTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_SecretSubTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_SecretSubTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_style_pad_top(ui_SecretSubTitle, 15, 0);

    ui_SecretSubLabel = lv_label_create(ui_SecretSubContainer);
    lv_obj_set_width(ui_SecretSubLabel, 125);
    lv_obj_set_style_text_color(ui_SecretSubLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_SecretSubLabel, &ui_font_Font1,
                               0); // 한글 지원 폰트
    lv_obj_set_style_text_align(ui_SecretSubLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_SecretSubLabel, LV_ALIGN_CENTER);
  }

  String titleText = "SECRET FEATURE " + String(index);
  lv_label_set_text(ui_SecretSubTitle, titleText.c_str());

  String labelText = "Secret Feature " + String(index) +
                     "\n\n[ Coming Soon ]\n\nHold B to Exit";
  lv_label_set_text(ui_SecretSubLabel, labelText.c_str());

  lv_obj_clear_flag(ui_SecretSubContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_SecretSubContainer, -1); // 항상 최상단 표시
}

// ─────────────────────────────────────────────────────────
// [NEW] 리모컨 모드 화면 업데이트
// 전용 레드 컨테이너를 사용하여 단어장 내용과 완전히 분리합니다.
// ─────────────────────────────────────────────────────────
void updateIRRemoteDisplay() {
  if (!ui_IRContainer) {
    ui_IRContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_IRContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_IRContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_IRContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_IRContainer, 0, 0);
    lv_obj_set_style_radius(ui_IRContainer, 0, 0);
    lv_obj_set_align(ui_IRContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_IRContainer, LV_SCROLLBAR_MODE_OFF);

    ui_IRTitle = lv_label_create(ui_IRContainer);
    lv_obj_set_width(ui_IRTitle, 135);
    lv_obj_set_style_text_color(ui_IRTitle, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_IRTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_IRTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_IRTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_style_pad_top(ui_IRTitle, 10, 0);
    lv_obj_set_style_bg_color(ui_IRTitle, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_IRTitle, LV_OPA_COVER, 0);
    lv_label_set_text(ui_IRTitle, "IR REMOTE");

    ui_IRListContainer = lv_obj_create(ui_IRContainer);
    lv_obj_set_size(ui_IRListContainer, 135, 210);
    lv_obj_set_align(ui_IRListContainer, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_IRListContainer, 28);
    lv_obj_set_style_bg_opa(ui_IRListContainer, 0, 0);
    lv_obj_set_style_border_width(ui_IRListContainer, 0, 0);
    lv_obj_set_scrollbar_mode(ui_IRListContainer, LV_SCROLLBAR_MODE_OFF);

    ui_IRLabel = lv_label_create(ui_IRListContainer);
    lv_obj_set_width(ui_IRLabel, 130);
    lv_obj_set_style_text_color(ui_IRLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_IRLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_IRLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_align(ui_IRLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_pad_left(ui_IRLabel, 5, 0);
  }

  lv_obj_clear_flag(ui_IRContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_IRContainer, -1); // 최상단 배치

  String remoteText = "";
  const char *cmds[] = {"Power ON", "Power OFF", "CH Up",
                        "CH Down",  "Vol Up",    "Vol Down"};
  for (int i = 0; i < 6; i++) {
    remoteText += (i == irRemoteIndex) ? ">" : " ";
    remoteText += String(cmds[i]) + "\n";
  }

  remoteText += "\n(A: Send, B: Next)";
  lv_label_set_text(ui_IRLabel, remoteText.c_str());
  lv_obj_set_style_text_line_space(ui_IRLabel, 3, 0);

  int scrollY = 0;
  if (irRemoteIndex > 4)
    scrollY = (irRemoteIndex - 4) * 17;
  lv_obj_set_y(ui_IRLabel, 5 - scrollY);

  Serial.printf("[IR] Remote UI Updated (LG TV, Index: %d)\n", irRemoteIndex);
}

// ─────────────────────────────────────────────────────────
// [NEW] IR 수신 모드 화면 업데이트
// 전용 오렌지 컨테이너를 사용합니다.
// ─────────────────────────────────────────────────────────
void updateIRRecvDisplay(String info) {
  if (!ui_IRRecvContainer) {
    ui_IRRecvContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_IRRecvContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_IRRecvContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_IRRecvContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_IRRecvContainer, 0, 0);
    lv_obj_set_style_radius(ui_IRRecvContainer, 0, 0);
    lv_obj_set_align(ui_IRRecvContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_IRRecvContainer, LV_SCROLLBAR_MODE_OFF);

    ui_IRRecvTitle = lv_label_create(ui_IRRecvContainer);
    lv_obj_set_width(ui_IRRecvTitle, 135);
    lv_obj_set_style_text_color(ui_IRRecvTitle, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_IRRecvTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_IRRecvTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_IRRecvTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_style_pad_top(ui_IRRecvTitle, 10, 0);
    lv_obj_set_style_bg_color(ui_IRRecvTitle, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_IRRecvTitle, LV_OPA_COVER, 0);
    lv_label_set_text(ui_IRRecvTitle, "IR RECEIVER");

    ui_IRRecvListContainer = lv_obj_create(ui_IRRecvContainer);
    lv_obj_set_size(ui_IRRecvListContainer, 135, 200);
    lv_obj_set_align(ui_IRRecvListContainer, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_IRRecvListContainer, 35);
    lv_obj_set_style_bg_opa(ui_IRRecvListContainer, 0, 0);
    lv_obj_set_style_border_width(ui_IRRecvListContainer, 0, 0);
    lv_obj_set_scrollbar_mode(ui_IRRecvListContainer, LV_SCROLLBAR_MODE_OFF);

    ui_IRRecvLabel = lv_label_create(ui_IRRecvListContainer);
    lv_obj_set_width(ui_IRRecvLabel, 125);
    lv_obj_set_style_text_color(ui_IRRecvLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_IRRecvLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_IRRecvLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_align(ui_IRRecvLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_pad_left(ui_IRRecvLabel, 10, 0);
  }

  lv_obj_clear_flag(ui_IRRecvContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_IRRecvContainer, -1);

  // 스피커는 IR 수신 모드 진입 시 이미 end()로 종료됨 (하드웨어 충돌 방지)

  if (info == "") {
    lv_label_set_text(ui_IRRecvLabel,
                      "Waiting for signal...\n\n(Short press B: Back)");
  } else {
    lv_label_set_text(ui_IRRecvLabel, info.c_str());
  }
  lv_obj_set_y(ui_IRRecvLabel, 5);
}

// ─────────────────────────────────────────────────────────
// [NEW] 선으로 떨어지는 목표물 잡는 게임
// 위에서 아래로 떨어지는 노란색 목표물을 하단의 선으로 잡는 게임
// ─────────────────────────────────────────────────────────
void resetGame() {
  gameEnergy = maxEnergy;
  gameCaught = 0;
  gameMissed = 0;
  gameCombo = 0;
  isGameOver = false;
  lastJudgement = "";
  barY = 195;
  btnAPressed = false;

  // 떨어지는 목표물 리스트 초기화 및 LVGL 객체 삭제
  for (auto &obj : fallingObjects) {
    if (obj.obj) {
      lv_obj_del(obj.obj);
      obj.obj = nullptr;
    }
  }
  fallingObjects.clear();

  // 게임 파라미터 리셋
  objectSpawnInterval = 1000;
  objectFallSpeed = 5.0f;
  lastObjectSpawnTime = millis();

  updateGameDisplay();
  soundSuccess();
}

void updateGameDisplay() {
  if (!ui_GameContainer) {
    // 게임 컨테이너 생성
    ui_GameContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_GameContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_GameContainer, lv_color_hex(0x000000),
                              0); // 검정 배경
    lv_obj_set_style_bg_opa(ui_GameContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_GameContainer, 0, 0);
    lv_obj_set_style_radius(ui_GameContainer, 0, 0);
    lv_obj_set_style_pad_all(ui_GameContainer, 0, 0);
    lv_obj_set_align(ui_GameContainer, LV_ALIGN_CENTER);

    // 타겟 바 (옆으로 긴 네모)
    ui_GameBar = lv_obj_create(ui_GameContainer);
    lv_obj_set_size(ui_GameBar, 100, 10); // 가로로 길고 세로로 좁음
    lv_obj_set_pos(ui_GameBar, 17, barY);
    lv_obj_set_style_bg_color(ui_GameBar, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_color(ui_GameBar, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(ui_GameBar, 2, 0);
    lv_obj_set_style_radius(ui_GameBar, 2, 0);

    // 콤보 표시
    ui_GameComboLabel = lv_label_create(ui_GameContainer);
    lv_obj_set_style_text_color(ui_GameComboLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_GameComboLabel, &ui_font_Font22, 0);
    lv_obj_set_align(ui_GameComboLabel, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_GameComboLabel, -40);

    // 판정 표시
    ui_GameJudgeLabel = lv_label_create(ui_GameContainer);
    lv_obj_set_style_text_font(ui_GameJudgeLabel, &ui_font_Font1, 0);
    lv_obj_set_align(ui_GameJudgeLabel, LV_ALIGN_CENTER);
    lv_obj_set_y(ui_GameJudgeLabel, 20);

    // 스코어/정보 라벨
    ui_GameScoreLabel = lv_label_create(ui_GameContainer);
    lv_obj_set_style_text_color(ui_GameScoreLabel, lv_color_hex(0x00FF00), 0);
    lv_obj_set_style_text_font(ui_GameScoreLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_align(ui_GameScoreLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_GameScoreLabel, 2, 2);

    // 시각적 에너지 바
    ui_GameEnergyBar = lv_bar_create(ui_GameContainer);
    lv_obj_set_size(ui_GameEnergyBar, 60, 8);
    lv_obj_set_align(ui_GameEnergyBar, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(ui_GameEnergyBar, -2, 2);
    lv_obj_set_style_bg_color(ui_GameEnergyBar, lv_color_hex(0x400000),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(ui_GameEnergyBar, lv_color_hex(0xFF0000),
                              LV_PART_INDICATOR);

    // 게임 정보 라벨
    ui_GameInfoLabel = lv_label_create(ui_GameContainer);
    lv_obj_set_width(ui_GameInfoLabel, 125);
    lv_obj_set_style_text_color(ui_GameInfoLabel, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_GameInfoLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_GameInfoLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_GameInfoLabel, LV_ALIGN_CENTER);
  }

  lv_obj_clear_flag(ui_GameContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_GameContainer, -1);

  // 스코어 업데이트
  lv_label_set_text_fmt(ui_GameScoreLabel, "S:%d M:%d", gameCaught, gameMissed);
  lv_bar_set_value(ui_GameEnergyBar, gameEnergy, LV_ANIM_OFF);

  // 콤보 업데이트
  if (gameCombo > 1) {
    lv_label_set_text_fmt(ui_GameComboLabel, "%d\nCOMBO", gameCombo);
    lv_obj_clear_flag(ui_GameComboLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui_GameComboLabel, LV_OBJ_FLAG_HIDDEN);
  }

  // 판정 업데이트
  if (lastJudgement != "") {
    lv_label_set_text(ui_GameJudgeLabel, lastJudgement.c_str());
    if (lastJudgement == "PERFECT")
      lv_obj_set_style_text_color(ui_GameJudgeLabel, lv_color_hex(0x00FFFF), 0);
    else if (lastJudgement == "GREAT")
      lv_obj_set_style_text_color(ui_GameJudgeLabel, lv_color_hex(0xFFFF00), 0);
    else
      lv_obj_set_style_text_color(ui_GameJudgeLabel, lv_color_hex(0xFF0000), 0);
    lv_obj_clear_flag(ui_GameJudgeLabel, LV_OBJ_FLAG_HIDDEN);
  } else {
    lv_obj_add_flag(ui_GameJudgeLabel, LV_OBJ_FLAG_HIDDEN);
  }

  if (isGameOver) {
    lv_label_set_text(ui_GameInfoLabel, "GAME OVER!\nPress A to Retry");
    lv_obj_clear_flag(ui_GameInfoLabel, LV_OBJ_FLAG_HIDDEN);
    if (gameCombo > maxCombo)
      maxCombo = gameCombo;
  } else {
    lv_obj_add_flag(ui_GameInfoLabel, LV_OBJ_FLAG_HIDDEN);
  }
}

// ─────────────────────────────────────────────────────────
// [NEW] 길러너 친구들 게임 (Crossy Road 스타일)
// ─────────────────────────────────────────────────────────
void resetRunnerGame() {
  runnerPlayerY = 220; // 플레이어 초기 위치 (맨 아래)
  runnerScore = 0;
  isRunnerGameOver = false;
  isRunnerReady = true; // [NEW] 튜토리얼 준비상태 재설정
  btnAPressed = false;

  // 장애물 리스트 초기화 및 LVGL 객체 삭제
  for (auto &obj : runnerObstacles) {
    if (obj.obj) {
      lv_obj_del(obj.obj);
      obj.obj = nullptr;
    }
  }
  runnerObstacles.clear();

  // 레인 리스트 초기화 및 LVGL 객체 삭제
  for (auto &lane : runnerLanes) {
    if (lane.bgObj) {
      lv_obj_del(lane.bgObj);
      lane.bgObj = nullptr;
    }
  }
  runnerLanes.clear();

  // 컨테이너가 생성된 후에 레인을 만들어야 함
  updateRunnerGameDisplay();

  // 초기 12개 레인 생성 (Y = 0, 20, 40, 60, 80, 100, 120, 140, 160, 180, 200,
  // 220)
  for (int i = 0; i < 12; i++) {
    RunnerLane lane;
    lane.y = i * 20;

    // Y=220, 200은 무조건 잔디밭(0)으로 시작해서 플레이어 안전 보장
    if (lane.y == 220 || lane.y == 200 || lane.y == 140 || lane.y == 80 ||
        lane.y == 20 || lane.y == 0) {
      lane.type = 0; // Grass
    } else {
      lane.type = 1; // Road
    }

    lane.bgObj = lv_obj_create(ui_RunnerGameContainer);
    lv_obj_set_size(lane.bgObj, 135, 20);
    lv_obj_set_pos(lane.bgObj, 0, lane.y);
    lv_obj_set_style_border_width(lane.bgObj, 0, 0);
    lv_obj_set_style_radius(lane.bgObj, 0, 0);
    lv_obj_set_style_pad_all(lane.bgObj, 0, 0);

    if (lane.type == 0) {
      lv_obj_set_style_bg_color(lane.bgObj, lv_color_hex(0x1f3c25), 0); // 숲색
    } else {
      lv_obj_set_style_bg_color(lane.bgObj, lv_color_hex(0x2b2b2b),
                                0); // 아스팔트 회색

      // 도로 위아래 얇은 구분선 추가
      lv_obj_set_style_border_color(lane.bgObj, lv_color_hex(0x3e3e3e), 0);
      lv_obj_set_style_border_width(lane.bgObj, 1, 0);
    }

    // 레이어를 맨 뒤로 보냄
    lv_obj_move_to_index(lane.bgObj, 0);

    if (lane.type == 1) {
      lane.dir = (random(0, 2) == 0) ? -1 : 1;
      lane.speed = (random(10, 18) * 0.1f);    // 쉬운 속도 (1.0 ~ 1.8 px/frame)
      lane.spawnInterval = random(2000, 4500); // 여유로운 스폰 간격
      lane.lastSpawnTime = millis() - random(0, 1500); // 스폰 분산

      // 시작 시점에 화면에 차가 배치되어 있도록 사전 스폰 (70% 확률)
      if (random(0, 10) < 7) {
        RunnerObstacle newObs;
        newObs.y = lane.y + 2;
        newObs.width = random(20, 30);
        newObs.height = 14;
        newObs.vx = lane.speed * lane.dir;

        // 화면 안쪽 임의의 X 위치에 배치
        newObs.x = random(10, 110);

        newObs.obj = lv_obj_create(ui_RunnerGameContainer);
        lv_obj_set_size(newObs.obj, newObs.width, newObs.height);
        lv_obj_set_pos(newObs.obj, (int)newObs.x, newObs.y);

        uint32_t carColor;
        int colSel = random(0, 4);
        if (colSel == 0)
          carColor = 0xE74C3C; // 빨강
        else if (colSel == 1)
          carColor = 0x3498DB; // 파랑
        else if (colSel == 2)
          carColor = 0xF1C40F; // 노랑
        else
          carColor = 0x9B59B6; // 보라

        lv_obj_set_style_bg_color(newObs.obj, lv_color_hex(carColor), 0);
        lv_obj_set_style_border_width(newObs.obj, 0, 0);
        lv_obj_set_style_radius(newObs.obj, 2, 0);

        // 헤드라이트 추가
        lv_obj_t *lightL = lv_obj_create(newObs.obj);
        lv_obj_t *lightR = lv_obj_create(newObs.obj);
        lv_obj_set_size(lightL, 2, 2);
        lv_obj_set_size(lightR, 2, 2);
        lv_obj_set_style_bg_color(lightL, lv_color_hex(0xFFFF00), 0);
        lv_obj_set_style_bg_color(lightR, lv_color_hex(0xFFFF00), 0);
        lv_obj_set_style_border_width(lightL, 0, 0);
        lv_obj_set_style_border_width(lightR, 0, 0);

        if (lane.dir == 1) {
          lv_obj_set_pos(lightL, newObs.width - 3, 2);
          lv_obj_set_pos(lightR, newObs.width - 3, newObs.height - 4);
        } else {
          lv_obj_set_pos(lightL, 1, 2);
          lv_obj_set_pos(lightR, 1, newObs.height - 4);
        }

        runnerObstacles.push_back(newObs);
      }
    } else {
      lane.dir = 0;
      lane.speed = 0.0f;
      lane.spawnInterval = 0;
      lane.lastSpawnTime = 0;
    }

    runnerLanes.push_back(lane);
  }

  // 게임 오버 라벨 숨기기
  if (ui_RunnerGameOverLabel) {
    lv_obj_add_flag(ui_RunnerGameOverLabel, LV_OBJ_FLAG_HIDDEN);
  }

  soundSuccess();
  Serial.println("[RUNNER GAME] Reset");
}

void updateRunnerGameDisplay() {
  if (!ui_RunnerGameContainer) {
    // 게임 컨테이너 생성
    ui_RunnerGameContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_RunnerGameContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_RunnerGameContainer, lv_color_hex(0x1a1a2e),
                              0); // 짙은 파란 배경
    lv_obj_set_style_bg_opa(ui_RunnerGameContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_RunnerGameContainer, 0, 0);
    lv_obj_set_style_radius(ui_RunnerGameContainer, 0, 0);
    lv_obj_set_style_pad_all(ui_RunnerGameContainer, 0, 0);
    lv_obj_set_align(ui_RunnerGameContainer, LV_ALIGN_CENTER);

    // 플레이어 바 (귀여운 닭 캐릭터)
    ui_RunnerPlayerBar = lv_obj_create(ui_RunnerGameContainer);
    lv_obj_set_size(ui_RunnerPlayerBar, 20, 15); // 플레이어 크기
    lv_obj_set_pos(ui_RunnerPlayerBar, 57, runnerPlayerY);
    lv_obj_set_style_bg_color(ui_RunnerPlayerBar, lv_color_hex(0xFFFFFF),
                              0); // 하얀 몸통
    lv_obj_set_style_border_width(ui_RunnerPlayerBar, 0, 0);
    lv_obj_set_style_radius(ui_RunnerPlayerBar, 4, 0); // 둥글게

    // 빨간 볏
    lv_obj_t *comb = lv_obj_create(ui_RunnerPlayerBar);
    lv_obj_set_size(comb, 6, 4);
    lv_obj_set_pos(comb, 7, -3);
    lv_obj_set_style_bg_color(comb, lv_color_hex(0xE74C3C), 0); // 빨간색
    lv_obj_set_style_border_width(comb, 0, 0);
    lv_obj_set_style_radius(comb, 1, 0);

    // 노란 부리
    lv_obj_t *beak = lv_obj_create(ui_RunnerPlayerBar);
    lv_obj_set_size(beak, 4, 3);
    lv_obj_set_pos(beak, 8, 1);
    lv_obj_set_style_bg_color(beak, lv_color_hex(0xF39C12), 0); // 주황/노랑
    lv_obj_set_style_border_width(beak, 0, 0);

    // 스코어 라벨
    ui_RunnerScoreLabel = lv_label_create(ui_RunnerGameContainer);
    lv_obj_set_style_text_color(ui_RunnerScoreLabel, lv_color_hex(0xFFFF00),
                                0); // 노란색 스코어
    lv_obj_set_style_text_font(ui_RunnerScoreLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_align(ui_RunnerScoreLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_RunnerScoreLabel, 4, 4);

    // 튜토리얼 컨테이너
    ui_RunnerTutorialContainer = lv_obj_create(ui_RunnerGameContainer);
    lv_obj_set_size(ui_RunnerTutorialContainer, 133, 222);
    lv_obj_set_align(ui_RunnerTutorialContainer, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_RunnerTutorialContainer,
                              lv_color_hex(0x064e3b), 0); // 다크 그린 테마
    lv_obj_set_style_bg_opa(ui_RunnerTutorialContainer, LV_OPA_90, 0);
    lv_obj_set_style_border_color(ui_RunnerTutorialContainer,
                                  lv_color_hex(0x22c55e), 0); // 연그린 경계선
    lv_obj_set_style_border_width(ui_RunnerTutorialContainer, 2, 0);
    lv_obj_set_style_radius(ui_RunnerTutorialContainer, 8, 0);
    lv_obj_set_style_pad_all(ui_RunnerTutorialContainer, 4, 0);
    lv_obj_set_scrollbar_mode(ui_RunnerTutorialContainer,
                              LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tut_title = lv_label_create(ui_RunnerTutorialContainer);
    lv_label_set_text(tut_title, "길러너 친구들");
    lv_obj_set_style_text_color(tut_title, lv_color_hex(0x22c55e), 0);
    lv_obj_set_style_text_font(tut_title, &ui_font_Font1, 0);
    lv_obj_set_align(tut_title, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(tut_title, 0, 0);

    lv_obj_t *tut_body = lv_label_create(ui_RunnerTutorialContainer);
    lv_label_set_text(tut_body, "  [조작법]\n"
                                "단타A: 앞으로 전진\n"
                                "B홀드: 게임종료\n"
                                "  [규칙]\n"
                                "좌우로 질주하는\n"
                                "자동차들을 피해\n"
                                "앞으로 무사히\n"
                                "나아가세요!\n\n"
                                "시작: A 누름");
    lv_obj_set_style_text_color(tut_body, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(tut_body, &ui_font_Font1, 0);
    lv_obj_set_align(tut_body, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(tut_body, 0, 18);
    lv_obj_set_style_text_line_space(tut_body, 0, 0);
  }

  lv_obj_clear_flag(ui_RunnerGameContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_RunnerGameContainer, -1);

  // 스코어 업데이트
  lv_label_set_text_fmt(ui_RunnerScoreLabel, "Score:%d", runnerScore);

  // 플레이어 위치 업데이트 (X는 57 고정, Y축 업데이트)
  if (ui_RunnerPlayerBar) {
    lv_obj_set_pos(ui_RunnerPlayerBar, 57, runnerPlayerY);
    // 닭 캐릭터가 다른 객체들보다 앞에 보이게 정렬
    lv_obj_move_to_index(ui_RunnerPlayerBar, -1);
  }

  // 튜토리얼 화면 상태 처리
  if (isRunnerReady) {
    if (ui_RunnerTutorialContainer) {
      lv_obj_clear_flag(ui_RunnerTutorialContainer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_to_index(ui_RunnerTutorialContainer, -1);
    }
  } else {
    if (ui_RunnerTutorialContainer) {
      lv_obj_add_flag(ui_RunnerTutorialContainer, LV_OBJ_FLAG_HIDDEN);
    }
  }

  if (isRunnerGameOver) {
    // 게임 오버 표시
    if (!ui_RunnerGameOverLabel) {
      ui_RunnerGameOverLabel = lv_label_create(ui_RunnerGameContainer);
      lv_obj_set_width(ui_RunnerGameOverLabel, 125);
      lv_obj_set_style_text_color(ui_RunnerGameOverLabel,
                                  lv_color_hex(0xFF3333), 0);
      lv_obj_set_style_text_font(ui_RunnerGameOverLabel, &ui_font_Font22, 0);
      lv_obj_set_style_text_align(ui_RunnerGameOverLabel, LV_TEXT_ALIGN_CENTER,
                                  0);
      lv_obj_set_align(ui_RunnerGameOverLabel, LV_ALIGN_CENTER);

      // 반투명 어두운 배경 스타일 추가해서 글씨 가독성 향상
      lv_obj_set_style_bg_color(ui_RunnerGameOverLabel, lv_color_hex(0x000000),
                                0);
      lv_obj_set_style_bg_opa(ui_RunnerGameOverLabel, LV_OPA_70, 0);
      lv_obj_set_style_radius(ui_RunnerGameOverLabel, 8, 0);
      lv_obj_set_style_pad_all(ui_RunnerGameOverLabel, 8, 0);
    }
    lv_label_set_text_fmt(ui_RunnerGameOverLabel,
                          "GAME OVER!\nScore:%d\nPress A", runnerScore);
    lv_obj_clear_flag(ui_RunnerGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(ui_RunnerGameOverLabel, -1); // 맨 위에 띄우기
  }
}

// ─────────────────────────────────────────────────────────
// [NEW] 플래피 버드 게임 초기화 및 화면 업데이트
// ─────────────────────────────────────────────────────────
void resetFlappyGame() {
  Preferences pref;
  prefsBegin(pref, "flappy", true);
  flappyHighScore = pref.getInt("hiscore", 0);
  pref.end();

  flappyBirdY = 120.0f;
  flappyBirdYSpeed = 0.0f;
  flappyScore = 0;
  isFlappyGameOver = false;
  isFlappyReady = true;
  lastFlappyPipeSpawnTime = millis();
  flappyPressStartTime = 0;
  flappyHoldActive = false;

  // 기존 파이프 삭제
  for (auto &pipe : flappyPipes) {
    if (pipe.topObj) {
      lv_obj_del(pipe.topObj);
      pipe.topObj = nullptr;
    }
    if (pipe.bottomObj) {
      lv_obj_del(pipe.bottomObj);
      pipe.bottomObj = nullptr;
    }
  }
  flappyPipes.clear();

  updateFlappyDisplay();
  soundSuccess();
  Serial.println("[FLAPPY GAME] Reset / Initialized");
}

void updateFlappyDisplay() {
  if (!ui_FlappyContainer) {
    // 1. 게임 배경 컨테이너
    ui_FlappyContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_FlappyContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_FlappyContainer, lv_color_hex(0x0a192f),
                              0); // 다크 네이비 테마
    lv_obj_set_style_bg_opa(ui_FlappyContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_FlappyContainer, 0, 0);
    lv_obj_set_style_radius(ui_FlappyContainer, 0, 0);
    lv_obj_set_style_pad_all(ui_FlappyContainer, 0, 0);
    lv_obj_set_align(ui_FlappyContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_FlappyContainer, LV_SCROLLBAR_MODE_OFF);

    // 2. 플레이어 새 (버드 - 주황색)
    ui_FlappyBird = lv_obj_create(ui_FlappyContainer);
    lv_obj_set_size(ui_FlappyBird, 12, 12);
    lv_obj_set_style_bg_color(ui_FlappyBird, lv_color_hex(0xffaa00), 0);
    lv_obj_set_style_border_width(ui_FlappyBird, 0, 0);
    lv_obj_set_style_radius(ui_FlappyBird, 3, 0); // 살짝 둥글게

    // 버드 눈 추가 (포인트 디자인)
    lv_obj_t *eye = lv_obj_create(ui_FlappyBird);
    lv_obj_set_size(eye, 3, 3);
    lv_obj_set_pos(eye, 6, 2);
    lv_obj_set_style_bg_color(eye, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_border_width(eye, 0, 0);

    // 3. 실시간 점수 라벨
    ui_FlappyScoreLabel = lv_label_create(ui_FlappyContainer);
    lv_obj_set_style_text_color(ui_FlappyScoreLabel, lv_color_hex(0xffff00),
                                0); // 밝은 노란색으로 변경
    lv_obj_set_style_text_font(ui_FlappyScoreLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_align(ui_FlappyScoreLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_FlappyScoreLabel, 6, 6);

    // 파이프와 겹치더라도 가독성을 확보할 수 있도록 반투명 검정 배경 설정
    lv_obj_set_style_bg_color(ui_FlappyScoreLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_FlappyScoreLabel, LV_OPA_60, 0);
    lv_obj_set_style_radius(ui_FlappyScoreLabel, 3, 0);
    lv_obj_set_style_pad_all(ui_FlappyScoreLabel, 4, 0);

    // 4. 한국어 조작 설명 컨테이너 (시작 대기 화면용)
    ui_FlappyTutorialContainer = lv_obj_create(ui_FlappyContainer);
    lv_obj_set_size(ui_FlappyTutorialContainer, 133, 222);
    lv_obj_set_align(ui_FlappyTutorialContainer, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_FlappyTutorialContainer,
                              lv_color_hex(0x0f172a), 0); // 슬레이트 배경색
    lv_obj_set_style_bg_opa(ui_FlappyTutorialContainer, LV_OPA_90, 0);
    lv_obj_set_style_border_color(ui_FlappyTutorialContainer,
                                  lv_color_hex(0xffaa00), 0); // 주황색 경계선
    lv_obj_set_style_border_width(ui_FlappyTutorialContainer, 2, 0);
    lv_obj_set_style_radius(ui_FlappyTutorialContainer, 8, 0);
    lv_obj_set_style_pad_all(ui_FlappyTutorialContainer, 4, 0);
    lv_obj_set_scrollbar_mode(ui_FlappyTutorialContainer,
                              LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tut_title = lv_label_create(ui_FlappyTutorialContainer);
    lv_label_set_text(tut_title, "플래피 버드");
    lv_obj_set_style_text_color(tut_title, lv_color_hex(0xffaa00), 0);
    lv_obj_set_style_text_font(tut_title, &ui_font_Font1, 0);
    lv_obj_set_align(tut_title, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(tut_title, 0, 0);

    lv_obj_t *tut_body = lv_label_create(ui_FlappyTutorialContainer);
    lv_label_set_text(tut_body, "  [조작법]\n"
                                "단타A: 플랩 (상승)\n"
                                "B홀드: 게임종료\n"
                                "  [규칙]\n"
                                "기둥 사이의 빈틈을\n"
                                "타이밍 맞춰\n"
                                "통과하세요!\n\n"
                                "시작: A 누름");
    lv_obj_set_style_text_color(tut_body, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(tut_body, &ui_font_Font1, 0);
    lv_obj_set_align(tut_body, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(tut_body, 0, 18);
    lv_obj_set_style_text_line_space(tut_body, 0, 0);
  }

  // 컨테이너 보이기
  lv_obj_clear_flag(ui_FlappyContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_FlappyContainer, -1);

  // 점수 텍스트 갱신
  if (isFlappyReady) {
    if (ui_FlappyTutorialContainer) {
      lv_obj_clear_flag(ui_FlappyTutorialContainer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_to_index(ui_FlappyTutorialContainer, -1);
    }
    lv_label_set_text_fmt(ui_FlappyScoreLabel, "Hi:%d", flappyHighScore);
  } else {
    if (ui_FlappyTutorialContainer) {
      lv_obj_add_flag(ui_FlappyTutorialContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(ui_FlappyScoreLabel, "Score: %d\nHigh: %d",
                          flappyScore, flappyHighScore);
  }

  // 버드 Y 위치 업데이트
  if (ui_FlappyBird) {
    lv_obj_set_pos(ui_FlappyBird, 30, (int)flappyBirdY);
    lv_obj_move_to_index(ui_FlappyBird, -1); // 버드를 항상 맨 앞으로
  }

  // 게임 오버 상태 UI 처리
  if (isFlappyGameOver) {
    if (!ui_FlappyGameOverLabel) {
      ui_FlappyGameOverLabel = lv_label_create(ui_FlappyContainer);
      lv_obj_set_width(ui_FlappyGameOverLabel, 125);
      lv_obj_set_style_text_color(ui_FlappyGameOverLabel,
                                  lv_color_hex(0xff3b30), 0); // Red
      lv_obj_set_style_text_font(ui_FlappyGameOverLabel, &ui_font_Font1,
                                 0); // 한글 지원
      lv_obj_set_style_text_align(ui_FlappyGameOverLabel, LV_TEXT_ALIGN_CENTER,
                                  0);
      lv_obj_set_align(ui_FlappyGameOverLabel, LV_ALIGN_CENTER);

      // 가독성용 반투명 배경
      lv_obj_set_style_bg_color(ui_FlappyGameOverLabel, lv_color_hex(0x000000),
                                0);
      lv_obj_set_style_bg_opa(ui_FlappyGameOverLabel, LV_OPA_70, 0);
      lv_obj_set_style_radius(ui_FlappyGameOverLabel, 6, 0);
      lv_obj_set_style_pad_all(ui_FlappyGameOverLabel, 10, 0);
    }

    lv_label_set_text_fmt(ui_FlappyGameOverLabel,
                          "GAME OVER!\nScore: %d\nPress A to play",
                          flappyScore);
    lv_obj_clear_flag(ui_FlappyGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(ui_FlappyGameOverLabel, -1);
  } else {
    if (ui_FlappyGameOverLabel) {
      lv_obj_add_flag(ui_FlappyGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

// ─────────────────────────────────────────────────────────
// [NEW] Vector Classic 파쿠르 뼈대 관절 그리기 및 UI 관리
// ─────────────────────────────────────────────────────────
static uint8_t vector_player_buf[32 * 40 * 3]; // RGB565 + Alpha
static uint8_t vector_hunter_buf[32 * 40 * 3]; // RGB565 + Alpha

void drawVectorLine(lv_obj_t *canvas, int x1, int y1, int x2, int y2,
                    lv_color_t color, int width = 2) {
  lv_draw_line_dsc_t line_dsc;
  lv_draw_line_dsc_init(&line_dsc);
  line_dsc.color = color;
  line_dsc.width = width;
  line_dsc.round_start = true;
  line_dsc.round_end = true;

  lv_point_t pts[2];
  pts[0].x = x1;
  pts[0].y = y1;
  pts[1].x = x2;
  pts[1].y = y2;
  lv_canvas_draw_line(canvas, pts, 2, &line_dsc);
}

void drawStickFigure(lv_obj_t *canvas, VectorState state, int frame,
                     lv_color_t color, bool hasGlowingEyes) {
  // 1. 캔버스 배경 채우기 (투명)
  lv_canvas_fill_bg(canvas, lv_color_hex(0x000000), LV_OPA_TRANSP);

  // 2. 관절 데이터 정의 (Hip 기준 상대 좌표)
  lv_point_t hip = {0, 0};
  lv_point_t neck = {0, -12};
  lv_point_t head = {0, -16};
  lv_point_t l_knee = {0, 6};
  lv_point_t l_foot = {0, 13};
  lv_point_t r_knee = {0, 6};
  lv_point_t r_foot = {0, 13};
  lv_point_t l_elbow = {0, -6};
  lv_point_t l_hand = {0, 0};
  lv_point_t r_elbow = {0, -6};
  lv_point_t r_hand = {0, 0};

  // 상태별 관절 셋업 (진짜 사람이 파쿠르 질주하는 것처럼 상체 각도 및 관절
  // 가동성 향상)
  if (state == VSTATE_RUNNING) {
    float swing = sin(frame * 0.785f);        // 8프레임 주기
    float bounce = sin(frame * 1.57f) * 1.5f; // 위아래로 더 탄력 있게 바운스

    hip.y = (lv_coord_t)bounce;
    neck.x = 5;
    neck.y = (lv_coord_t)(-11 + (int)bounce); // 앞으로 힘차게 기울인 상체
    head.x = 7;
    head.y = (lv_coord_t)(-15 + (int)bounce);

    // 역동적인 달리기 다리 동작
    l_knee.x = (lv_coord_t)(3 + swing * 5.0f);
    l_knee.y = (lv_coord_t)(6 - abs(cos(frame * 0.785f)) *
                                    3.0f); // 무릎 들어올리기 Y축 연동
    l_foot.x = (lv_coord_t)(l_knee.x + (swing > 0 ? 3 : -3));
    l_foot.y = (lv_coord_t)(l_knee.y + 7 + (swing < 0 ? -2 : 0));

    r_knee.x = (lv_coord_t)(3 - swing * 5.0f);
    r_knee.y = (lv_coord_t)(6 - abs(cos(frame * 0.785f)) * 3.0f);
    r_foot.x = (lv_coord_t)(r_knee.x + (swing < 0 ? 3 : -3));
    r_foot.y = (lv_coord_t)(r_knee.y + 7 + (swing > 0 ? -2 : 0));

    // 팔 저어주기 (다리 동작과 반대 위상)
    l_elbow.x = (lv_coord_t)(2 - swing * 4.0f);
    l_elbow.y = (lv_coord_t)(-6 + (int)bounce);
    l_hand.x = (lv_coord_t)(l_elbow.x + 3 - swing * 2.0f);
    l_hand.y = (lv_coord_t)(l_elbow.y + 5);

    r_elbow.x = (lv_coord_t)(2 + swing * 4.0f);
    r_elbow.y = (lv_coord_t)(-6 + (int)bounce);
    r_hand.x = (lv_coord_t)(r_elbow.x + 3 + swing * 2.0f);
    r_hand.y = (lv_coord_t)(r_elbow.y + 5);
  } else if (state == VSTATE_JUMPING) {
    // 공중 도약 시 상체 젖힘과 역동성
    neck.x = 4;
    neck.y = -11;
    head.x = 6;
    head.y = -15;

    // 앞다리 굽힘, 뒷다리 뻗음
    l_knee.x = 2;
    l_knee.y = 5;
    l_foot.x = 0;
    l_foot.y = 10;
    r_knee.x = -3;
    r_knee.y = 5;
    r_foot.x = -5;
    r_foot.y = 11;

    // 두 팔을 든 포즈
    l_elbow.x = 2;
    l_elbow.y = -12;
    l_hand.x = 5;
    l_hand.y = -16;
    r_elbow.x = -3;
    r_elbow.y = -8;
    r_hand.x = -1;
    r_hand.y = -12;
  } else if (state == VSTATE_FLIPPING) {
    // 공중제비 시 컴팩트한 몸 말기 (회전 변환 시 훨씬 부드러워 보임)
    neck.x = 0;
    neck.y = -6;
    head.x = 0;
    head.y = -10;

    l_knee.x = -4;
    l_knee.y = 2;
    l_foot.x = -2;
    l_foot.y = 6;
    r_knee.x = 4;
    r_knee.y = 2;
    r_foot.x = 2;
    r_foot.y = 6;

    l_elbow.x = -5;
    l_elbow.y = -3;
    l_hand.x = -2;
    l_hand.y = 0;
    r_elbow.x = 5;
    r_elbow.y = -3;
    r_hand.x = 2;
    r_hand.y = 0;
  } else if (state == VSTATE_SLIDING) {
    // 슬라이딩은 지면에 닿은 저중심 포즈
    hip.x = 4;
    hip.y = 7;
    neck.x = -8;
    neck.y = 6;
    head.x = -12;
    head.y = 5;

    // 앞다리 뻗기
    l_knee.x = 12;
    l_knee.y = 6;
    l_foot.x = 18;
    l_foot.y = 5;

    // 뒷다리 접기
    r_knee.x = 10;
    r_knee.y = 8;
    r_foot.x = 16;
    r_foot.y = 7;

    l_elbow.x = -3;
    l_elbow.y = 8;
    l_hand.x = 2;
    l_hand.y = 8;
    r_elbow.x = -3;
    r_elbow.y = 5;
    r_hand.x = 1;
    r_hand.y = 5;
  } else if (state == VSTATE_STUMBLING) {
    // 비틀거리며 앞으로 쏠리는 포즈
    neck.x = 7;
    neck.y = -7;
    head.x = 10;
    head.y = -9;

    l_knee.x = 2;
    l_knee.y = 6;
    l_foot.x = 1;
    l_foot.y = 13;
    r_knee.x = -4;
    r_knee.y = 5;
    r_foot.x = -5;
    r_foot.y = 12;

    l_elbow.x = 8;
    l_elbow.y = -8;
    l_hand.x = 12;
    l_hand.y = -6;
    r_elbow.x = -4;
    r_elbow.y = -6;
    r_hand.x = -8;
    r_hand.y = -4;
  } else if (state == VSTATE_TASERED) {
    int jx = random(-2, 3);
    int jy = random(-2, 3);
    neck.x = (lv_coord_t)(0 + jx);
    neck.y = (lv_coord_t)(-11 + jy);
    head.x = (lv_coord_t)(0 + jx);
    head.y = (lv_coord_t)(-15 + jy);
    l_knee.x = (lv_coord_t)(-4 + jx);
    l_knee.y = (lv_coord_t)(6 + jy);
    l_foot.x = (lv_coord_t)(-6 + jx);
    l_foot.y = (lv_coord_t)(13 + jy);
    r_knee.x = (lv_coord_t)(4 + jx);
    r_knee.y = (lv_coord_t)(6 + jy);
    r_foot.x = (lv_coord_t)(6 + jx);
    r_foot.y = (lv_coord_t)(13 + jy);
    l_elbow.x = (lv_coord_t)(-6 + jx);
    l_elbow.y = (lv_coord_t)(-6 + jy);
    l_hand.x = (lv_coord_t)(-9 + jx);
    l_hand.y = (lv_coord_t)(-2 + jy);
    r_elbow.x = (lv_coord_t)(6 + jx);
    r_elbow.y = (lv_coord_t)(-6 + jy);
    r_hand.x = (lv_coord_t)(9 + jx);
    r_hand.y = (lv_coord_t)(-2 + jy);
  }

  // 3. 회전 적용 (공중제비 일 때만)
  float rotationAngle = 0.0f;
  if (state == VSTATE_FLIPPING) {
    rotationAngle = (millis() - vectorStateTimer) * 0.009f; // 회전 속도 설정
  }

  auto rot = [rotationAngle](lv_point_t p) -> lv_point_t {
    if (rotationAngle == 0.0f)
      return p;
    float c = cos(rotationAngle);
    float s = sin(rotationAngle);
    return {(lv_coord_t)(p.x * c - p.y * s), (lv_coord_t)(p.x * s + p.y * c)};
  };

  hip = rot(hip);
  neck = rot(neck);
  head = rot(head);
  l_knee = rot(l_knee);
  l_foot = rot(l_foot);
  r_knee = rot(r_knee);
  r_foot = rot(r_foot);
  l_elbow = rot(l_elbow);
  l_hand = rot(l_hand);
  r_elbow = rot(r_elbow);
  r_hand = rot(r_hand);

  // 4. 캔버스 원점(16, 22)에 투사하여 그리기
  int cx = 16, cy = 22;

  // 몸통
  drawVectorLine(canvas, cx + hip.x, cy + hip.y, cx + neck.x, cy + neck.y,
                 color, 3);

  // 머리
  lv_draw_rect_dsc_t head_dsc;
  lv_draw_rect_dsc_init(&head_dsc);
  head_dsc.bg_color = color;
  head_dsc.bg_opa = LV_OPA_COVER;
  head_dsc.radius = 2;
  head_dsc.border_width = 0;
  lv_canvas_draw_rect(canvas, cx + head.x - 3, cy + head.y - 3, 6, 6,
                      &head_dsc);

  // 붉은 광채 눈 (헌터 특전)
  if (hasGlowingEyes) {
    lv_draw_rect_dsc_t eye_dsc;
    lv_draw_rect_dsc_init(&eye_dsc);
    eye_dsc.bg_color = lv_color_hex(0xff0000); // 붉은 레이저 눈
    eye_dsc.bg_opa = LV_OPA_COVER;
    eye_dsc.border_width = 0;

    lv_point_t eyePos;
    eyePos.x = (lv_coord_t)(head.x + 2);
    eyePos.y = (lv_coord_t)(head.y - 1);
    eyePos = rot(eyePos);
    lv_canvas_draw_rect(canvas, cx + eyePos.x, cy + eyePos.y, 2, 2, &eye_dsc);
  }

  // 왼다리
  drawVectorLine(canvas, cx + hip.x, cy + hip.y, cx + l_knee.x, cy + l_knee.y,
                 color, 2);
  drawVectorLine(canvas, cx + l_knee.x, cy + l_knee.y, cx + l_foot.x,
                 cy + l_foot.y, color, 2);

  // 오른다리
  drawVectorLine(canvas, cx + hip.x, cy + hip.y, cx + r_knee.x, cy + r_knee.y,
                 color, 2);
  drawVectorLine(canvas, cx + r_knee.x, cy + r_knee.y, cx + r_foot.x,
                 cy + r_foot.y, color, 2);

  // 왼팔
  drawVectorLine(canvas, cx + neck.x, cy + neck.y, cx + l_elbow.x,
                 cy + l_elbow.y, color, 2);
  drawVectorLine(canvas, cx + l_elbow.x, cy + l_elbow.y, cx + l_hand.x,
                 cy + l_hand.y, color, 2);

  // 오른팔
  drawVectorLine(canvas, cx + neck.x, cy + neck.y, cx + r_elbow.x,
                 cy + r_elbow.y, color, 2);
  drawVectorLine(canvas, cx + r_elbow.x, cy + r_elbow.y, cx + r_hand.x,
                 cy + r_hand.y, color, 2);
}

void resetVectorGame() {
  Preferences pref;
  prefsBegin(pref, "vector", true);
  vectorHighScore = pref.getInt("hiscore", 0);
  pref.end();

  vectorPlayerY = 160.0f;
  vectorPlayerYSpeed = 0.0f;
  vectorPlayerState = VSTATE_RUNNING;
  vectorPlayerAnimFrame = 0;

  vectorHunterX = -70.0f; // 헌터 시작 위치 (Player X = 35 대비 상대 거리)
  vectorHunterY = 160.0f;
  vectorHunterYSpeed = 0.0f;
  vectorHunterState = VSTATE_RUNNING;
  vectorHunterAnimFrame = 0;

  vectorScore = 0;
  isVectorGameOver = false;
  isVectorReady = true;
  lastVectorBuildingSpawnTime = millis();
  vectorStateTimer = millis();

  // 기존 객체들 제거
  for (auto &b : vectorBuildings) {
    if (b.obj)
      lv_obj_del(b.obj);
  }
  vectorBuildings.clear();

  for (auto &o : vectorObstacles) {
    if (o.obj)
      lv_obj_del(o.obj);
  }
  vectorObstacles.clear();

  // 초기 빌딩 3개로 시작 구간 다지기
  // 빌딩 1: 플레이어 시작 지붕
  VectorBuilding b1;
  b1.x = 0.0f;
  b1.width = 150.0f;
  b1.height = 80.0f;
  b1.obj =
      lv_obj_create(ui_VectorContainer ? ui_VectorContainer : lv_scr_act());
  lv_obj_set_size(b1.obj, (int)b1.width, (int)b1.height);
  lv_obj_set_pos(b1.obj, (int)b1.x, 240 - (int)b1.height);
  lv_obj_set_style_bg_color(b1.obj, lv_color_hex(0x0f172a), 0);
  lv_obj_set_style_border_width(b1.obj, 0, 0);
  lv_obj_set_style_radius(b1.obj, 0, 0);
  vectorBuildings.push_back(b1);

  // 빌딩 2: 적절한 갭 뒤 배치
  VectorBuilding b2;
  b2.x = 180.0f;
  b2.width = 130.0f;
  b2.height = 80.0f;
  b2.obj =
      lv_obj_create(ui_VectorContainer ? ui_VectorContainer : lv_scr_act());
  lv_obj_set_size(b2.obj, (int)b2.width, (int)b2.height);
  lv_obj_set_pos(b2.obj, (int)b2.x, 240 - (int)b2.height);
  lv_obj_set_style_bg_color(b2.obj, lv_color_hex(0x0f172a), 0);
  lv_obj_set_style_border_width(b2.obj, 0, 0);
  lv_obj_set_style_radius(b2.obj, 0, 0);
  vectorBuildings.push_back(b2);

  updateVectorDisplay();
  soundSuccess();
  Serial.println("[VECTOR GAME] Reset / Initialized");
}

void updateVectorDisplay() {
  if (!ui_VectorContainer) {
    // 1. 게임 배경 컨테이너 (남보라 밤하늘)
    ui_VectorContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_VectorContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_VectorContainer, lv_color_hex(0x1e1b4b), 0);
    lv_obj_set_style_bg_opa(ui_VectorContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_VectorContainer, 0, 0);
    lv_obj_set_style_radius(ui_VectorContainer, 0, 0);
    lv_obj_set_style_pad_all(ui_VectorContainer, 0, 0);
    lv_obj_set_align(ui_VectorContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_VectorContainer, LV_SCROLLBAR_MODE_OFF);

    // 2. 뒤 배경 원경 빌딩 2채 (장식용)
    lv_obj_t *bg_building = lv_obj_create(ui_VectorContainer);
    lv_obj_set_size(bg_building, 80, 120);
    lv_obj_set_pos(bg_building, 10, 140);
    lv_obj_set_style_bg_color(bg_building, lv_color_hex(0x312e81), 0);
    lv_obj_set_style_border_width(bg_building, 0, 0);
    lv_obj_set_style_radius(bg_building, 0, 0);

    lv_obj_t *bg_building2 = lv_obj_create(ui_VectorContainer);
    lv_obj_set_size(bg_building2, 60, 150);
    lv_obj_set_pos(bg_building2, 85, 120);
    lv_obj_set_style_bg_color(bg_building2, lv_color_hex(0x312e81), 0);
    lv_obj_set_style_border_width(bg_building2, 0, 0);
    lv_obj_set_style_radius(bg_building2, 0, 0);

    // 3. 플레이어 캐릭터 캔버스
    ui_VectorPlayerCanvas = lv_canvas_create(ui_VectorContainer);
    lv_canvas_set_buffer(ui_VectorPlayerCanvas, vector_player_buf, 32, 40,
                         LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_set_pos(ui_VectorPlayerCanvas, 35, 160 - 35);

    // 4. 헌터 캐릭터 캔버스
    ui_VectorHunterCanvas = lv_canvas_create(ui_VectorContainer);
    lv_canvas_set_buffer(ui_VectorHunterCanvas, vector_hunter_buf, 32, 40,
                         LV_IMG_CF_TRUE_COLOR_ALPHA);
    lv_obj_set_pos(ui_VectorHunterCanvas, 10, 160 - 35);

    // 5. 점수 라벨
    ui_VectorScoreLabel = lv_label_create(ui_VectorContainer);
    lv_obj_set_style_text_color(ui_VectorScoreLabel, lv_color_hex(0x38bdf8),
                                0); // 라이트블루
    lv_obj_set_style_text_font(ui_VectorScoreLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_align(ui_VectorScoreLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_VectorScoreLabel, 6, 6);

    lv_obj_set_style_bg_color(ui_VectorScoreLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_VectorScoreLabel, LV_OPA_60, 0);
    lv_obj_set_style_radius(ui_VectorScoreLabel, 3, 0);
    lv_obj_set_style_pad_all(ui_VectorScoreLabel, 4, 0);

    // 6. 한국어 조작 설명 컨테이너 (시작 대기 화면용)
    ui_VectorTutorialContainer = lv_obj_create(ui_VectorContainer);
    lv_obj_set_size(ui_VectorTutorialContainer, 133, 222);
    lv_obj_set_align(ui_VectorTutorialContainer, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_VectorTutorialContainer,
                              lv_color_hex(0x0f172a), 0); // 슬레이트 배경색
    lv_obj_set_style_bg_opa(ui_VectorTutorialContainer, LV_OPA_90, 0);
    lv_obj_set_style_border_color(ui_VectorTutorialContainer,
                                  lv_color_hex(0xf59e0b), 0); // 골드 경계선
    lv_obj_set_style_border_width(ui_VectorTutorialContainer, 2, 0);
    lv_obj_set_style_radius(ui_VectorTutorialContainer, 8, 0);
    lv_obj_set_style_pad_all(ui_VectorTutorialContainer, 4, 0);
    lv_obj_set_scrollbar_mode(ui_VectorTutorialContainer,
                              LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tut_title = lv_label_create(ui_VectorTutorialContainer);
    lv_label_set_text(tut_title, "파쿠르 러너");
    lv_obj_set_style_text_color(tut_title, lv_color_hex(0xf59e0b), 0);
    lv_obj_set_style_text_font(tut_title, &ui_font_Font1, 0);
    lv_obj_set_align(tut_title, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(tut_title, 0, 0);

    lv_obj_t *tut_body = lv_label_create(ui_VectorTutorialContainer);
    lv_label_set_text(tut_body, "  [조작법]\n"
                                "지상A: 점프\n"
                                "공중A: 공중제비\n"
                                "지상B: 슬라이딩\n"
                                "B홀드: 게임종료\n"
                                "  [규칙]\n"
                                "장애물 뛰어넘고\n"
                                "헌터를 따돌리세요!\n\n"
                                "시작: A 누름");
    lv_obj_set_style_text_color(tut_body, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(tut_body, &ui_font_Font1, 0);
    lv_obj_set_align(tut_body, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(tut_body, 0, 18);
    lv_obj_set_style_text_line_space(tut_body, 0, 0);
  }

  // 컨테이너 표시
  lv_obj_clear_flag(ui_VectorContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_VectorContainer, -1);

  // 점수 텍스트 및 조작 설명 표시 갱신
  if (isVectorReady) {
    if (ui_VectorTutorialContainer) {
      lv_obj_clear_flag(ui_VectorTutorialContainer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_to_index(ui_VectorTutorialContainer, -1); // 항시 최상단 배치
    }
    lv_label_set_text_fmt(ui_VectorScoreLabel, "Hi:%d m", vectorHighScore);
  } else {
    if (ui_VectorTutorialContainer) {
      lv_obj_add_flag(ui_VectorTutorialContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(ui_VectorScoreLabel, "Score: %d m\nHigh: %d m",
                          vectorScore, vectorHighScore);
  }

  // 캐릭터 애니메이션 프레임 그리기
  // 플레이어: 검은색 실루엣 (0x000000)
  drawStickFigure(ui_VectorPlayerCanvas, vectorPlayerState,
                  vectorPlayerAnimFrame, lv_color_hex(0x000000), false);

  // 헌터: 짙은 남색 실루엣 (0x0f172a) + 붉은 레이저 눈
  drawStickFigure(ui_VectorHunterCanvas, vectorHunterState,
                  vectorHunterAnimFrame, lv_color_hex(0x0f172a), true);

  // 캔버스 위치 실시간 동적 갱신
  if (ui_VectorPlayerCanvas) {
    lv_obj_set_pos(ui_VectorPlayerCanvas, 35, (int)vectorPlayerY - 35);
  }
  if (ui_VectorHunterCanvas) {
    lv_obj_set_pos(ui_VectorHunterCanvas, 35 + (int)vectorHunterX,
                   (int)vectorPlayerY -
                       35); // 헌터도 플레이어의 발판 높이를 똑같이 추적
  }

  // 게임 오버 라벨 처리
  if (isVectorGameOver) {
    if (!ui_VectorGameOverLabel) {
      ui_VectorGameOverLabel = lv_label_create(ui_VectorContainer);
      lv_obj_set_width(ui_VectorGameOverLabel, 125);
      lv_obj_set_style_text_color(ui_VectorGameOverLabel,
                                  lv_color_hex(0xff3b30), 0); // Red
      lv_obj_set_style_text_font(ui_VectorGameOverLabel, &ui_font_Font1, 0);
      lv_obj_set_style_text_align(ui_VectorGameOverLabel, LV_TEXT_ALIGN_CENTER,
                                  0);
      lv_obj_set_align(ui_VectorGameOverLabel, LV_ALIGN_CENTER);

      lv_obj_set_style_bg_color(ui_VectorGameOverLabel, lv_color_hex(0x000000),
                                0);
      lv_obj_set_style_bg_opa(ui_VectorGameOverLabel, LV_OPA_70, 0);
      lv_obj_set_style_radius(ui_VectorGameOverLabel, 6, 0);
      lv_obj_set_style_pad_all(ui_VectorGameOverLabel, 10, 0);
    }

    if (vectorPlayerState == VSTATE_TASERED) {
      lv_label_set_text_fmt(ui_VectorGameOverLabel,
                            "BUSTED!\nScore: %d m\nPress A to restart",
                            vectorScore);
    } else {
      lv_label_set_text_fmt(ui_VectorGameOverLabel,
                            "FALLEN!\nScore: %d m\nPress A to restart",
                            vectorScore);
    }
    lv_obj_clear_flag(ui_VectorGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(ui_VectorGameOverLabel, -1);
  }
}

// ─────────────────────────────────────────────────────────
// [NEW] Stack 블록 쌓기 게임 헬퍼 및 메인 로직
// ─────────────────────────────────────────────────────────
uint32_t getHSVColorHex(float h, float s, float v) {
  float r = 0, g = 0, b = 0;
  if (s == 0) {
    r = g = b = v;
  } else {
    h /= 60.0f;
    int i = (int)floor(h);
    float f = h - i;
    float p = v * (1.0f - s);
    float q = v * (1.0f - s * f);
    float t = v * (1.0f - s * (1.0f - f));
    switch (i) {
    case 0:
      r = v;
      g = t;
      b = p;
      break;
    case 1:
      r = q;
      g = v;
      b = p;
      break;
    case 2:
      r = p;
      g = v;
      b = t;
      break;
    case 3:
      r = p;
      g = q;
      b = v;
      break;
    case 4:
      r = t;
      g = p;
      b = v;
      break;
    default:
      r = v;
      g = p;
      b = q;
      break;
    }
  }
  int ri = (int)(r * 255.0f);
  int gi = (int)(g * 255.0f);
  int bi = (int)(b * 255.0f);
  return (ri << 16) | (gi << 8) | bi;
}

uint32_t getStackColorHex(int index) {
  float hue = (float)((index * 15) % 360);
  return getHSVColorHex(hue, 0.8f, 0.9f);
}

void updateStackDisplay() {
  if (!ui_StackContainer) {
    // 1. 메인 게임 컨테이너 (어두운 보라빛 밤하늘 느낌)
    ui_StackContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_StackContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_StackContainer, lv_color_hex(0x0f172a),
                              0); // 슬레이트 색상
    lv_obj_set_style_bg_opa(ui_StackContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_StackContainer, 0, 0);
    lv_obj_set_style_radius(ui_StackContainer, 0, 0);
    lv_obj_set_style_pad_all(ui_StackContainer, 0, 0);
    lv_obj_set_align(ui_StackContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_StackContainer, LV_SCROLLBAR_MODE_OFF);

    // 2. 점수판 라벨
    ui_StackScoreLabel = lv_label_create(ui_StackContainer);
    lv_obj_set_style_text_color(ui_StackScoreLabel, lv_color_hex(0xf59e0b),
                                0); // 오렌지/골드 색상
    lv_obj_set_style_text_font(ui_StackScoreLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_align(ui_StackScoreLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_StackScoreLabel, 6, 6);
    lv_obj_set_style_bg_color(ui_StackScoreLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_StackScoreLabel, LV_OPA_60, 0);
    lv_obj_set_style_radius(ui_StackScoreLabel, 3, 0);
    lv_obj_set_style_pad_all(ui_StackScoreLabel, 4, 0);

    // 3. 한글 조작 설명 카드 (시작 대기 화면용)
    ui_StackTutorialContainer = lv_obj_create(ui_StackContainer);
    lv_obj_set_size(ui_StackTutorialContainer, 133, 222);
    lv_obj_set_align(ui_StackTutorialContainer, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_StackTutorialContainer, lv_color_hex(0x1e1b4b),
                              0); // 다크 보라
    lv_obj_set_style_bg_opa(ui_StackTutorialContainer, LV_OPA_90, 0);
    lv_obj_set_style_border_color(ui_StackTutorialContainer,
                                  lv_color_hex(0x38bdf8),
                                  0); // 스카이블루 테두리
    lv_obj_set_style_border_width(ui_StackTutorialContainer, 2, 0);
    lv_obj_set_style_radius(ui_StackTutorialContainer, 8, 0);
    lv_obj_set_style_pad_all(ui_StackTutorialContainer, 4, 0);
    lv_obj_set_scrollbar_mode(ui_StackTutorialContainer, LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tut_title = lv_label_create(ui_StackTutorialContainer);
    lv_label_set_text(tut_title, "스택 (Stack)");
    lv_obj_set_style_text_color(tut_title, lv_color_hex(0x38bdf8), 0);
    lv_obj_set_style_text_font(tut_title, &ui_font_Font1, 0);
    lv_obj_set_align(tut_title, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(tut_title, 0, 0);

    lv_obj_t *tut_body = lv_label_create(ui_StackTutorialContainer);
    lv_label_set_text(tut_body, "  [조작법]\n"
                                "단층A: 블록 쌓기\n"
                                "B홀드: 게임종료\n"
                                "  [규칙]\n"
                                "좌우 블록을\n"
                                "밑에 정밀하게\n"
                                "쌓으세요!\n\n"
                                "시작: A 누름");
    lv_obj_set_style_text_color(tut_body, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(tut_body, &ui_font_Font1, 0);
    lv_obj_set_align(tut_body, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(tut_body, 0, 18);
    lv_obj_set_style_text_line_space(tut_body, 0, 0);
  }

  // 표시 활성화
  lv_obj_clear_flag(ui_StackContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_StackContainer, -1);

  // 점수판 및 튜토리얼 갱신
  if (isStackReady) {
    if (ui_StackTutorialContainer) {
      lv_obj_clear_flag(ui_StackTutorialContainer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_to_index(ui_StackTutorialContainer, -1);
    }
    lv_label_set_text_fmt(ui_StackScoreLabel, "Hi:%d", stackHighScore);
  } else {
    if (ui_StackTutorialContainer) {
      lv_obj_add_flag(ui_StackTutorialContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(ui_StackScoreLabel, "Score: %d\nHigh: %d", stackScore,
                          stackHighScore);
  }

  // 게임오버 UI 갱신
  if (isStackGameOver) {
    if (!ui_StackGameOverLabel) {
      ui_StackGameOverLabel = lv_label_create(ui_StackContainer);
      lv_obj_set_width(ui_StackGameOverLabel, 120);
      lv_obj_set_style_text_color(ui_StackGameOverLabel, lv_color_hex(0xff3b30),
                                  0);
      lv_obj_set_style_text_font(ui_StackGameOverLabel, &ui_font_Font1, 0);
      lv_obj_set_style_text_align(ui_StackGameOverLabel, LV_TEXT_ALIGN_CENTER,
                                  0);
      lv_obj_set_align(ui_StackGameOverLabel, LV_ALIGN_CENTER);

      lv_obj_set_style_bg_color(ui_StackGameOverLabel, lv_color_hex(0x000000),
                                0);
      lv_obj_set_style_bg_opa(ui_StackGameOverLabel, LV_OPA_70, 0);
      lv_obj_set_style_radius(ui_StackGameOverLabel, 6, 0);
      lv_obj_set_style_pad_all(ui_StackGameOverLabel, 8, 0);
    }
    lv_label_set_text_fmt(ui_StackGameOverLabel,
                          "게임 오버!\nScore: %d\n\n재시작: A 누름",
                          stackScore);
    lv_obj_clear_flag(ui_StackGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(ui_StackGameOverLabel, -1);
  } else {
    if (ui_StackGameOverLabel) {
      lv_obj_add_flag(ui_StackGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void resetStackGame() {
  // 최고 점수 복원
  Preferences prefs;
  prefsBegin(prefs, "stack", true);
  stackHighScore = prefs.getInt("hiscore", 0);
  prefs.end();

  // 기존 블록 리스트 삭제
  for (auto &b : stackBlocks) {
    if (b.obj) {
      lv_obj_del(b.obj);
    }
  }
  stackBlocks.clear();

  // 기존 파편 제거
  for (auto &s : stackShards) {
    if (s.obj) {
      lv_obj_del(s.obj);
    }
  }
  stackShards.clear();

  if (ui_StackActiveObj) {
    lv_obj_del(ui_StackActiveObj);
    ui_StackActiveObj = nullptr;
  }

  // 상태 리셋
  stackScore = 0;
  stackPerfectCombo = 0;
  isStackGameOver = false;
  isStackReady = true;

  stackActiveWidth = 80.0f;
  stackActiveX = 27.5f; // (135 - 80) / 2
  stackActiveSpeed = 2.0f;
  stackActiveDir = 1;
  stackActiveY = 200; // 하단 시작

  updateStackDisplay();

  // 베이스 블록 배치
  uint32_t color = getStackColorHex(0);
  lv_obj_t *baseObj = lv_obj_create(ui_StackContainer);
  lv_obj_set_size(baseObj, (int)stackActiveWidth, 12);
  lv_obj_set_pos(baseObj, (int)stackActiveX, stackActiveY);
  lv_obj_set_style_bg_color(baseObj, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(baseObj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(baseObj, 0, 0);
  lv_obj_set_style_radius(baseObj, 2, 0);

  StackBlock baseBlock = {stackActiveX, stackActiveWidth, stackActiveY, baseObj,
                          color};
  stackBlocks.push_back(baseBlock);

  // 첫 번째 움직이는 활성 블록 생성
  stackActiveY = 188;
  stackActiveX = 0;
  stackActiveSpeed = 2.0f;
  stackActiveDir = 1;

  ui_StackActiveObj = lv_obj_create(ui_StackContainer);
  uint32_t activeColor = getStackColorHex(1);
  lv_obj_set_size(ui_StackActiveObj, (int)stackActiveWidth, 12);
  lv_obj_set_pos(ui_StackActiveObj, (int)stackActiveX, stackActiveY);
  lv_obj_set_style_bg_color(ui_StackActiveObj, lv_color_hex(activeColor), 0);
  lv_obj_set_style_bg_opa(ui_StackActiveObj, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ui_StackActiveObj, 0, 0);
  lv_obj_set_style_radius(ui_StackActiveObj, 2, 0);
}

// ─────────────────────────────────────────────────────────
// [NEW] 스페이스 슈터 (Space Shooter) 게임 제어 및 화면 갱신
// ─────────────────────────────────────────────────────────
void updateShooterDisplay() {
  if (!ui_ShooterContainer) {
    // 1. 메인 게임 컨테이너 (칠흑 같은 우주 공간 배경)
    ui_ShooterContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_ShooterContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_ShooterContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_ShooterContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_ShooterContainer, 0, 0);
    lv_obj_set_style_radius(ui_ShooterContainer, 0, 0);
    lv_obj_set_style_pad_all(ui_ShooterContainer, 0, 0);
    lv_obj_set_align(ui_ShooterContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_ShooterContainer, LV_SCROLLBAR_MODE_OFF);

    // 2. 점수판 라벨 (네온 블루 컬러)
    ui_ShooterScoreLabel = lv_label_create(ui_ShooterContainer);
    lv_obj_set_style_text_color(ui_ShooterScoreLabel, lv_color_hex(0x00ffff),
                                0);
    lv_obj_set_style_text_font(ui_ShooterScoreLabel, &lv_font_montserrat_12, 0);
    lv_obj_set_align(ui_ShooterScoreLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(ui_ShooterScoreLabel, 6, 6);
    lv_obj_set_style_bg_color(ui_ShooterScoreLabel, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_ShooterScoreLabel, LV_OPA_60, 0);
    lv_obj_set_style_radius(ui_ShooterScoreLabel, 3, 0);
    lv_obj_set_style_pad_all(ui_ShooterScoreLabel, 4, 0);

    // 3. 네온 체력 바 (HP Bar)
    ui_ShooterHPBar = lv_bar_create(ui_ShooterContainer);
    lv_obj_set_size(ui_ShooterHPBar, 48, 6);
    lv_obj_set_align(ui_ShooterHPBar, LV_ALIGN_TOP_RIGHT);
    lv_obj_set_pos(ui_ShooterHPBar, -6, 10);
    lv_obj_set_style_bg_color(ui_ShooterHPBar, lv_color_hex(0x333333),
                              LV_PART_MAIN); // 미충전 슬롯 회색
    lv_obj_set_style_bg_color(ui_ShooterHPBar, lv_color_hex(0x00ff00),
                              LV_PART_INDICATOR); // 디폴트 초록
    lv_bar_set_range(ui_ShooterHPBar, 0, 100);

    // 4. 한글 조작법 안내 카드
    ui_ShooterTutorialContainer = lv_obj_create(ui_ShooterContainer);
    lv_obj_set_size(ui_ShooterTutorialContainer, 133, 222);
    lv_obj_set_align(ui_ShooterTutorialContainer, LV_ALIGN_CENTER);
    lv_obj_set_style_bg_color(ui_ShooterTutorialContainer,
                              lv_color_hex(0x000022), 0); // 은하색 다크 블루
    lv_obj_set_style_bg_opa(ui_ShooterTutorialContainer, LV_OPA_90, 0);
    lv_obj_set_style_border_color(ui_ShooterTutorialContainer,
                                  lv_color_hex(0x00ffff), 0); // 사이언 테두리
    lv_obj_set_style_border_width(ui_ShooterTutorialContainer, 2, 0);
    lv_obj_set_style_radius(ui_ShooterTutorialContainer, 8, 0);
    lv_obj_set_style_pad_all(ui_ShooterTutorialContainer, 4, 0);
    lv_obj_set_scrollbar_mode(ui_ShooterTutorialContainer,
                              LV_SCROLLBAR_MODE_OFF);

    lv_obj_t *tut_title = lv_label_create(ui_ShooterTutorialContainer);
    lv_label_set_text(tut_title, "스페이스 슈터");
    lv_obj_set_style_text_color(tut_title, lv_color_hex(0x00ffff), 0);
    lv_obj_set_style_text_font(tut_title, &ui_font_Font1, 0);
    lv_obj_set_align(tut_title, LV_ALIGN_TOP_MID);
    lv_obj_set_pos(tut_title, 0, 2);

    lv_obj_t *tut_body = lv_label_create(ui_ShooterTutorialContainer);
    lv_label_set_text(tut_body, "  [조작법]\n"
                                "버튼A: 레이저 발사\n"
                                "버튼B: 비행선 턴\n"
                                "B홀드: 게임 종료\n"
                                "  [규칙]\n"
                                "자동으로 좌우를\n"
                                "왕복 정찰합니다.\n"
                                "외계 비행선을\n"
                                "격추하세요!\n\n"
                                "시작: A 누름");
    lv_obj_set_style_text_color(tut_body, lv_color_hex(0xffffff), 0);
    lv_obj_set_style_text_font(tut_body, &ui_font_Font1, 0);
    lv_obj_set_align(tut_body, LV_ALIGN_TOP_LEFT);
    lv_obj_set_pos(tut_body, 0, 20);
    lv_obj_set_style_text_line_space(tut_body, 0, 0);
  }

  // 화면 표시 및 무대 갱신
  lv_obj_clear_flag(ui_ShooterContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_ShooterContainer, -1);

  if (isShooterReady) {
    if (ui_ShooterTutorialContainer) {
      lv_obj_clear_flag(ui_ShooterTutorialContainer, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_to_index(ui_ShooterTutorialContainer, -1);
    }
    lv_label_set_text_fmt(ui_ShooterScoreLabel, "Hi:%d", shooterHighScore);
  } else {
    if (ui_ShooterTutorialContainer) {
      lv_obj_add_flag(ui_ShooterTutorialContainer, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text_fmt(ui_ShooterScoreLabel, "Score: %d\nHigh: %d",
                          shooterScore, shooterHighScore);
  }

  // 실시간 체력 바 업데이트 및 위험도별 인디케이터 색상 변환
  if (ui_ShooterHPBar) {
    lv_bar_set_value(ui_ShooterHPBar, shooterHP, LV_ANIM_OFF);
    if (shooterHP > 50) {
      lv_obj_set_style_bg_color(ui_ShooterHPBar, lv_color_hex(0x00ff00),
                                LV_PART_INDICATOR); // 안정: 초록
    } else if (shooterHP > 25) {
      lv_obj_set_style_bg_color(ui_ShooterHPBar, lv_color_hex(0xffff00),
                                LV_PART_INDICATOR); // 경고: 노랑
    } else {
      lv_obj_set_style_bg_color(ui_ShooterHPBar, lv_color_hex(0xff0000),
                                LV_PART_INDICATOR); // 위험: 빨강
    }
  }

  // 게임오버 오버레이 처리
  if (isShooterGameOver) {
    if (!ui_ShooterGameOverLabel) {
      ui_ShooterGameOverLabel = lv_label_create(ui_ShooterContainer);
      lv_obj_set_width(ui_ShooterGameOverLabel, 120);
      lv_obj_set_style_text_color(ui_ShooterGameOverLabel,
                                  lv_color_hex(0xff3b30), 0);
      lv_obj_set_style_text_font(ui_ShooterGameOverLabel, &ui_font_Font1, 0);
      lv_obj_set_style_text_align(ui_ShooterGameOverLabel, LV_TEXT_ALIGN_CENTER,
                                  0);
      lv_obj_set_align(ui_ShooterGameOverLabel, LV_ALIGN_CENTER);

      lv_obj_set_style_bg_color(ui_ShooterGameOverLabel, lv_color_hex(0x000000),
                                0);
      lv_obj_set_style_bg_opa(ui_ShooterGameOverLabel, LV_OPA_70, 0);
      lv_obj_set_style_radius(ui_ShooterGameOverLabel, 6, 0);
      lv_obj_set_style_pad_all(ui_ShooterGameOverLabel, 8, 0);
    }
    lv_label_set_text_fmt(ui_ShooterGameOverLabel,
                          "지구 멸망!\nScore: %d\n\n재시작: A 누름",
                          shooterScore);
    lv_obj_clear_flag(ui_ShooterGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_to_index(ui_ShooterGameOverLabel, -1);
  } else {
    if (ui_ShooterGameOverLabel) {
      lv_obj_add_flag(ui_ShooterGameOverLabel, LV_OBJ_FLAG_HIDDEN);
    }
  }
}

void resetShooterGame() {
  // 최고 점수 복원 (NVS 불러오기)
  Preferences prefs;
  prefsBegin(prefs, "shooter", true);
  shooterHighScore = prefs.getInt("hiscore", 0);
  prefs.end();

  // 기존 레이저(아군/적군 전체) 리소스 소거
  for (auto &l : shooterLasers) {
    if (l.obj) {
      lv_obj_del(l.obj);
    }
  }
  shooterLasers.clear();

  // 기존 외계인 비행선 리소스 소거
  for (auto &e : shooterEnemies) {
    if (e.obj) {
      lv_obj_del(e.obj);
    }
  }
  shooterEnemies.clear();

  // 게임 상태 초기화
  shooterScore = 0;
  shooterHP = 100;
  isShooterGameOver = false;
  isShooterReady = true;

  shooterPlayerX = 60.0f;
  shooterPlayerDir = 1.0f;
  shooterPlayerSpeed = 1.8f;

  lastEnemySpawnTime = millis();
  lastEnemyFireTime = millis();
  lastPlayerFireTime = 0;

  updateShooterDisplay();

  // 배경 스타필드 별 리소스 클리어 및 리스폰
  for (auto &s : starfield) {
    if (s.obj) {
      lv_obj_del(s.obj);
    }
  }
  starfield.clear();

  // 7개의 별 오브젝트를 스폰하여 깊이 있는 화면 구성
  for (int i = 0; i < 7; i++) {
    lv_obj_t *starObj = lv_obj_create(ui_ShooterContainer);
    lv_obj_set_size(starObj, 2, 2);
    float sx = random(0, 135);
    float sy = random(0, 240);
    lv_obj_set_pos(starObj, (int)sx, (int)sy);
    lv_obj_set_style_bg_color(starObj,
                              lv_color_hex(0x666666 + random(0, 4) * 0x222222),
                              0); // 미세한 별 밝기 조절
    lv_obj_set_style_bg_opa(starObj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(starObj, 0, 0);
    lv_obj_set_style_radius(starObj, 0, 0);

    ShooterStar newStar = {sx, sy, 0.4f + random(0, 100) * 0.007f, starObj};
    starfield.push_back(newStar);
  }

  // 플레이어 비행선 스폰
  if (ui_ShooterPlayer) {
    lv_obj_del(ui_ShooterPlayer);
    ui_ShooterPlayer = nullptr;
  }
  ui_ShooterPlayer = lv_obj_create(ui_ShooterContainer);
  lv_obj_set_size(ui_ShooterPlayer, 15, 10);
  lv_obj_set_pos(ui_ShooterPlayer, (int)shooterPlayerX, (int)shooterPlayerY);
  lv_obj_set_style_bg_color(ui_ShooterPlayer, lv_color_hex(0x00ffff),
                            0); // 네온 사이언 플레이어
  lv_obj_set_style_bg_opa(ui_ShooterPlayer, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(ui_ShooterPlayer, 0, 0);
  lv_obj_set_style_radius(ui_ShooterPlayer, 2, 0);
}

// ─────────────────────────────────────────────────────────
// [NEW] IR 복제 모드 화면 업데이트
// ─────────────────────────────────────────────────────────
void updateIRCloneDisplay() {
  if (!ui_IRCloneContainer) {
    ui_IRCloneContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_IRCloneContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_IRCloneContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_IRCloneContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_IRCloneContainer, 0, 0);
    lv_obj_set_style_radius(ui_IRCloneContainer, 0, 0);
    lv_obj_set_align(ui_IRCloneContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_IRCloneContainer, LV_SCROLLBAR_MODE_OFF);

    ui_IRCloneTitle = lv_label_create(ui_IRCloneContainer);
    lv_obj_set_width(ui_IRCloneTitle, 135);
    lv_obj_set_style_text_color(ui_IRCloneTitle, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_IRCloneTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_IRCloneTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_IRCloneTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_style_pad_top(ui_IRCloneTitle, 10, 0);
    lv_obj_set_style_bg_color(ui_IRCloneTitle, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_IRCloneTitle, LV_OPA_COVER, 0);
    lv_label_set_text(ui_IRCloneTitle, "IR CLONE");

    ui_IRCloneListContainer = lv_obj_create(ui_IRCloneContainer);
    lv_obj_set_size(ui_IRCloneListContainer, 135, 210);
    lv_obj_set_align(ui_IRCloneListContainer, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_IRCloneListContainer, 28);
    lv_obj_set_style_bg_opa(ui_IRCloneListContainer, 0, 0);
    lv_obj_set_style_border_width(ui_IRCloneListContainer, 0, 0);
    lv_obj_set_scrollbar_mode(ui_IRCloneListContainer, LV_SCROLLBAR_MODE_OFF);

    ui_IRCloneLabel = lv_label_create(ui_IRCloneListContainer);
    lv_obj_set_width(ui_IRCloneLabel, 130);
    lv_obj_set_style_text_color(ui_IRCloneLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_IRCloneLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_IRCloneLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_align(ui_IRCloneLabel, LV_ALIGN_TOP_LEFT);
    lv_obj_set_style_pad_left(ui_IRCloneLabel, 5, 0);
  }

  lv_obj_clear_flag(ui_IRCloneContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_IRCloneContainer, -1);

  String cloneText = "";
  if (irCloneIndex == 0) {
    cloneText += isCloningWait ? ">[WAITING...]\n" : ">1.[Clone Start]\n";
  } else {
    cloneText += " 1.[Clone Start]\n";
  }

  for (size_t i = 0; i < irCloneList.size(); i++) {
    int displayIdx = i + 2;
    cloneText += (irCloneIndex == (int)i + 1) ? ">" : " ";
    cloneText += String(displayIdx) + "." + irCloneList[i].name + "\n";
  }

  cloneText += "\n(A: Action, B: Next)";
  lv_label_set_text(ui_IRCloneLabel, cloneText.c_str());

  lv_obj_set_style_text_line_space(ui_IRCloneLabel, 3, 0);

  int scrollY = 0;
  if (irCloneIndex > 4)
    scrollY = (irCloneIndex - 4) * 17;
  lv_obj_set_y(ui_IRCloneLabel, 5 - scrollY);

  Serial.printf("[IR CLONE] UI Updated (Index: %d, Wait: %d)\n", irCloneIndex,
                isCloningWait);
}

// ─────────────────────────────────────────────────────────
// [NEW] 블루투스 무전기 화면 업데이트
// ─────────────────────────────────────────────────────────
void updateBTWalkieDisplay(String status) {
  if (!ui_BTWalkieContainer) {
    ui_BTWalkieContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_BTWalkieContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_BTWalkieContainer, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_BTWalkieContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_BTWalkieContainer, 0, 0);
    lv_obj_set_style_radius(ui_BTWalkieContainer, 0, 0);
    lv_obj_set_align(ui_BTWalkieContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_BTWalkieContainer, LV_SCROLLBAR_MODE_OFF);

    ui_BTWalkieTitle = lv_label_create(ui_BTWalkieContainer);
    lv_obj_set_width(ui_BTWalkieTitle, 135);
    lv_obj_set_style_text_color(ui_BTWalkieTitle, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_BTWalkieTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_BTWalkieTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_BTWalkieTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_style_pad_top(ui_BTWalkieTitle, 10, 0);
    lv_obj_set_style_bg_color(ui_BTWalkieTitle, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(ui_BTWalkieTitle, LV_OPA_COVER, 0);
    lv_label_set_text(ui_BTWalkieTitle, "BT WALKIE");

    ui_BTWalkieListContainer = lv_obj_create(ui_BTWalkieContainer);
    lv_obj_set_size(ui_BTWalkieListContainer, 135, 210);
    lv_obj_set_align(ui_BTWalkieListContainer, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_BTWalkieListContainer, 28);
    lv_obj_set_style_bg_opa(ui_BTWalkieListContainer, 0, 0);
    lv_obj_set_style_border_width(ui_BTWalkieListContainer, 0, 0);
    lv_obj_set_scrollbar_mode(ui_BTWalkieListContainer, LV_SCROLLBAR_MODE_OFF);

    ui_BTWalkieLabel = lv_label_create(ui_BTWalkieListContainer);
    lv_obj_set_width(ui_BTWalkieLabel, 125);
    lv_obj_set_style_text_color(ui_BTWalkieLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ui_BTWalkieLabel, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_BTWalkieLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_BTWalkieLabel, LV_ALIGN_TOP_MID);
  }

  lv_obj_clear_flag(ui_BTWalkieContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_BTWalkieContainer, -1);

  String walkieText = "";

  if (btWalkieRole == 0) {
    walkieText += "SELECT ROLE:\n\nA: MASTER (Search)\nB: SLAVE (Wait)";
  } else {
    walkieText +=
        (btWalkieRole == 1) ? "[ ROLE: MASTER ]\n" : "[ ROLE: SLAVE ]\n";

    if (status == "") {
      if (SerialBT.connected()) {
        walkieText += "CONNECTED\n\n";
        if (isBTWalkieTalking)
          walkieText += ">> TALKING... <<";
        else if (isBTWalkieReceiving)
          walkieText += "<< RECEIVING >>";
        else
          walkieText += "READY\n(Hold A: Talk)";
      } else {
        walkieText += (btWalkieRole == 1) ? "Searching..." : "Waiting...";
      }
    } else {
      walkieText += status;
    }
  }

  walkieText += "\n\n(Hold B 5s Exit)";
  lv_label_set_text(ui_BTWalkieLabel, walkieText.c_str());
  lv_obj_set_style_text_line_space(ui_BTWalkieLabel, 3, 0);
  lv_obj_set_y(ui_BTWalkieLabel, 5);

  Serial.printf("[BT WALKIE] UI Updated: %s\n", status.c_str());
}

// ─────────────────────────────────────────────────────────
// [NEW] BT 및 시스템 설정 화면 업데이트
// ─────────────────────────────────────────────────────────
void updateBTConfigDisplay() {
  if (!ui_BTConfigContainer) {
    ui_BTConfigContainer = lv_obj_create(lv_scr_act());
    lv_obj_set_size(ui_BTConfigContainer, 135, 240);
    lv_obj_set_style_bg_color(ui_BTConfigContainer, lv_color_hex(0x181818), 0);
    lv_obj_set_style_bg_opa(ui_BTConfigContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ui_BTConfigContainer, 0, 0);
    lv_obj_set_style_radius(ui_BTConfigContainer, 0, 0);
    lv_obj_set_align(ui_BTConfigContainer, LV_ALIGN_CENTER);
    lv_obj_set_scrollbar_mode(ui_BTConfigContainer, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_pad_all(ui_BTConfigContainer, 0, 0);

    // 상단 타이틀
    ui_BTConfigTitle = lv_label_create(ui_BTConfigContainer);
    lv_obj_set_width(ui_BTConfigTitle, 135);
    lv_obj_set_style_text_color(ui_BTConfigTitle, lv_color_hex(0xFFFF00), 0);
    lv_obj_set_style_text_font(ui_BTConfigTitle, &ui_font_Font1, 0);
    lv_obj_set_style_text_align(ui_BTConfigTitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_align(ui_BTConfigTitle, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_BTConfigTitle, 3);
    lv_label_set_text(ui_BTConfigTitle, "BT CONFIG");

    // 리스트 전용 컨테이너 (높이 168px, y: 24)
    ui_BTConfigListContainer = lv_obj_create(ui_BTConfigContainer);
    lv_obj_set_size(ui_BTConfigListContainer, 135, 168);
    lv_obj_set_align(ui_BTConfigListContainer, LV_ALIGN_TOP_MID);
    lv_obj_set_y(ui_BTConfigListContainer, 24);
    lv_obj_set_style_bg_opa(ui_BTConfigListContainer, 0, 0);
    lv_obj_set_style_border_width(ui_BTConfigListContainer, 0, 0);
    lv_obj_set_style_pad_all(ui_BTConfigListContainer, 0, 0);
    lv_obj_set_scrollbar_mode(ui_BTConfigListContainer, LV_SCROLLBAR_MODE_OFF);

    for (int i = 0; i < BT_CONFIG_COUNT; i++) {
      // 1. 번호 라벨 (x: 2, w: 18, 고정, 줄바꿈 방지)
      ui_BTConfigRows[i].numLabel = lv_label_create(ui_BTConfigListContainer);
      lv_obj_set_width(ui_BTConfigRows[i].numLabel, 18);
      lv_obj_set_style_text_font(ui_BTConfigRows[i].numLabel, &ui_font_Font1, 0);
      lv_obj_set_style_text_align(ui_BTConfigRows[i].numLabel, LV_TEXT_ALIGN_LEFT, 0);
      lv_label_set_long_mode(ui_BTConfigRows[i].numLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_x(ui_BTConfigRows[i].numLabel, 2);
      lv_obj_set_y(ui_BTConfigRows[i].numLabel, i * 18);

      // 2. 설정 이름 라벨 (x: 22, 비선택 시 44, 선택 시 110으로 확장)
      ui_BTConfigRows[i].nameLabel = lv_label_create(ui_BTConfigListContainer);
      lv_obj_set_width(ui_BTConfigRows[i].nameLabel, 44);
      lv_obj_set_style_text_font(ui_BTConfigRows[i].nameLabel, &ui_font_Font1, 0);
      lv_obj_set_style_text_align(ui_BTConfigRows[i].nameLabel, LV_TEXT_ALIGN_LEFT, 0);
      lv_label_set_long_mode(ui_BTConfigRows[i].nameLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_x(ui_BTConfigRows[i].nameLabel, 22);
      lv_obj_set_y(ui_BTConfigRows[i].nameLabel, i * 18);

      // 3. 설정 값 라벨 (x: 66, w: 67, 우측 정렬, M5S3_00의 M 잘림 완벽 방지)
      ui_BTConfigRows[i].valLabel = lv_label_create(ui_BTConfigListContainer);
      lv_obj_set_width(ui_BTConfigRows[i].valLabel, 67);
      lv_obj_set_style_text_font(ui_BTConfigRows[i].valLabel, &ui_font_Font1, 0);
      lv_obj_set_style_text_align(ui_BTConfigRows[i].valLabel, LV_TEXT_ALIGN_RIGHT, 0);
      lv_label_set_long_mode(ui_BTConfigRows[i].valLabel, LV_LABEL_LONG_CLIP);
      lv_obj_set_x(ui_BTConfigRows[i].valLabel, 66);
      lv_obj_set_y(ui_BTConfigRows[i].valLabel, i * 18);
    }

    // 하단 상세 설명 전용 패널 (y: -4, h: 36, 단독 전광판으로 시원하게 표시)
    ui_BTConfigDescContainer = lv_obj_create(ui_BTConfigContainer);
    lv_obj_set_size(ui_BTConfigDescContainer, 131, 36);
    lv_obj_set_align(ui_BTConfigDescContainer, LV_ALIGN_BOTTOM_MID);
    lv_obj_set_y(ui_BTConfigDescContainer, -4);
    lv_obj_set_style_bg_color(ui_BTConfigDescContainer, lv_color_hex(0x101626), 0);
    lv_obj_set_style_bg_opa(ui_BTConfigDescContainer, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(ui_BTConfigDescContainer, lv_color_hex(0x00AACC), 0);
    lv_obj_set_style_border_width(ui_BTConfigDescContainer, 1, 0);
    lv_obj_set_style_radius(ui_BTConfigDescContainer, 5, 0);
    lv_obj_set_style_pad_all(ui_BTConfigDescContainer, 2, 0);
    lv_obj_set_scrollbar_mode(ui_BTConfigDescContainer, LV_SCROLLBAR_MODE_OFF);

    // 하단 실시간 한글 티커 라벨 (수직 중앙 배치로 글자가 크고 선명하게 보임)
    ui_BTConfigDescLabel = lv_label_create(ui_BTConfigDescContainer);
    lv_obj_set_width(ui_BTConfigDescLabel, 125);
    lv_obj_set_style_text_color(ui_BTConfigDescLabel, lv_color_hex(0x00FFCC), 0);
    lv_obj_set_style_text_font(ui_BTConfigDescLabel, &ui_font_Font16, 0);
    lv_obj_set_style_text_align(ui_BTConfigDescLabel, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_align(ui_BTConfigDescLabel, LV_ALIGN_CENTER);
    lv_label_set_long_mode(ui_BTConfigDescLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
  }

  lv_obj_clear_flag(ui_BTConfigContainer, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_to_index(ui_BTConfigContainer, -1);

  struct ConfigItemData {
    String shortName;
    String fullName;
    String valText;
    String descText;
  };

  ConfigItemData items[BT_CONFIG_COUNT];

  // 1. Device Name
  items[0].shortName = "Dev";
  items[0].fullName  = "Device Name";
  items[0].valText   = String(BT_DEVICE_NAME);
  items[0].descText  = "[기기 이름: " + String(BT_DEVICE_NAME) + "] 블루투스 검색 시 표시되는 기기명";

  // 2. Bluetooth
  items[1].shortName = "BT";
  items[1].fullName  = "Bluetooth";
  items[1].valText   = (bluetoothEnabled) ? "ON" : "OFF";
  items[1].descText  = "[블루투스: " + String(bluetoothEnabled ? "ON" : "OFF") + "] 스마트폰 연결 및 무선 제어 ON/OFF";

  // 3. Sound
  items[2].shortName = "Sound";
  items[2].fullName  = "Sound Buzzer";
  items[2].valText   = (soundEnabled) ? "ON" : "OFF";
  items[2].descText  = "[효과음: " + String(soundEnabled ? "ON" : "OFF") + "] 버튼 및 동작 알림 부저음 ON/OFF";

  // 4. Volume
  {
    Preferences pref;
    prefsBegin(pref, "settings", true);
    int v = pref.getInt("vol", 30);
    pref.end();
    items[3].shortName = "Vol";
    items[3].fullName  = "Buzzer Volume";
    items[3].valText   = String(v);
    items[3].descText  = "[볼륨: " + String(v) + "] 효과음 부저 소리 크기 조절 (0~100)";
  }

  // 5. Max Brightness
  items[4].shortName = "MaxBri";
  items[4].fullName  = "Max Brightness";
  items[4].valText   = String(current_brightness);
  items[4].descText  = "[최대 밝기: " + String(current_brightness) + "] 평상시 사용하는 화면 밝기 (10~255)";

  // 6. Min Brightness
  items[5].shortName = "MinBri";
  items[5].fullName  = "Min Brightness";
  items[5].valText   = String(min_brightness);
  items[5].descText  = "[최소 밝기: " + String(min_brightness) + "] A+B 버튼 또는 절전 시 적용 밝기 (5~255)";

  // 7. Dim Time
  items[6].shortName = "DimTime";
  items[6].fullName  = "Auto Dimming";
  items[6].valText   = (dimTimeSec > 0) ? String(dimTimeSec) + "s" : "OFF";
  items[6].descText  = (dimTimeSec > 0) ? ("[자동 디밍: " + String(dimTimeSec) + "s] 미입력 시 최소밝기 전환 대기시간") : "[자동 디밍: OFF] 최소밝기 자동 전환 비활성화";

  // 8. Auto Time
  items[7].shortName = "Auto";
  items[7].fullName  = "Auto Word Flip";
  items[7].valText   = (autoTransitionEnabled) ? String(autoTransitionInterval / 1000) + "s" : "OFF";
  items[7].descText  = (autoTransitionEnabled) ? ("[단어 자동 넘김: " + String(autoTransitionInterval / 1000) + "s] 다음 단어 전환 간격") : "[단어 자동 넘김: OFF] 자동 전환 끔";

  // 9. Slim Time
  items[8].shortName = "Slim";
  items[8].fullName  = "Sleep Screen Off";
  items[8].valText   = (slimModeTime > 0) ? String(slimModeTime) + "m" : "OFF";
  items[8].descText  = (slimModeTime > 0) ? ("[화면 끄기 절전: " + String(slimModeTime) + "m] 화면 완전히 꺼지는 대기시간") : "[화면 끄기 절전: OFF] 화면 꺼짐 끔";

  // 10. LED
  bool ledOn = (digitalRead(10) == LOW);
  items[9].shortName = "LED";
  items[9].fullName  = "Front Red LED";
  items[9].valText   = ledOn ? "ON" : "OFF";
  items[9].descText  = ledOn ? "[전면 LED: ON] 앞면 빨간색 표시등 켜짐" : "[전면 LED: OFF] 앞면 빨간색 표시등 꺼짐";

  // 11. Auto Dir
  items[10].shortName = "AutoDir";
  items[10].fullName  = "Word Direction";
  items[10].valText   = (btnADirectionForward) ? "FWD" : "BWD";
  items[10].descText  = (btnADirectionForward) ? "[단어 넘김 방향: FWD] 순방향 단어 넘김" : "[단어 넘김 방향: BWD] 역방향 단어 넘김";

  // 12. BT Hold Time
  items[11].shortName = "BTHold";
  items[11].fullName  = "BT Menu Hold";
  items[11].valText   = String(btHoldTime) + "s";
  items[11].descText  = "[BT설정 진입: " + String(btHoldTime) + "s] B버튼 길게 눌러 메뉴 들어가는 시간";

  // 13. Secret Hold Time
  items[12].shortName = "SecHold";
  items[12].fullName  = "Secret Hold";
  items[12].valText   = String(secretHoldTime) + "s";
  items[12].descText  = "[시크릿 모드: " + String(secretHoldTime) + "s] B버튼 길게 눌러 진입하는 시간";

  // 14. A Dir Time
  items[13].shortName = "ADir";
  items[13].fullName  = "A Long: Flip Dir";
  items[13].valText   = String(aBtnDirTime) + "s";
  items[13].descText  = "[A버튼 방향반전: " + String(aBtnDirTime) + "s] A버튼 길게 눌러 넘김방향 반전 시간";

  // 15. A Auto Time
  items[14].shortName = "AAuto";
  items[14].fullName  = "A Long: Auto Run";
  items[14].valText   = String(aBtnAutoTime) + "s";
  items[14].descText  = "[A버튼 자동실행: " + String(aBtnAutoTime) + "s] A버튼 길게 눌러 단어 자동넘김 켤 때 시간";

  // 스크롤 오프셋 계산 (컨테이너 높이 168px, 각 항목 18px)
  int scrollY = 0;
  if (btConfigIndex > 4) {
    scrollY = (btConfigIndex - 4) * 18;
    int maxScroll = BT_CONFIG_COUNT * 18 - 168;
    if (scrollY > maxScroll) scrollY = maxScroll;
  }
  if (scrollY < 0) scrollY = 0;

  for (int i = 0; i < BT_CONFIG_COUNT; i++) {
    int yPos = i * 18 - scrollY;
    lv_obj_set_y(ui_BTConfigRows[i].numLabel, yPos);
    lv_obj_set_y(ui_BTConfigRows[i].nameLabel, yPos);
    lv_obj_set_y(ui_BTConfigRows[i].valLabel, yPos);

    char numBuf[8];
    snprintf(numBuf, sizeof(numBuf), "%d.", i + 1);
    lv_label_set_text(ui_BTConfigRows[i].numLabel, numBuf);

    if (i == btConfigIndex) {
      // 선택 항목:
      // 1) 번호는 밝은 노란색
      lv_obj_set_style_text_color(ui_BTConfigRows[i].numLabel, lv_color_hex(0xFFFF00), 0);

      // 2) 숫자는 사라짐 (값 라벨 빈 문자열)
      lv_label_set_text(ui_BTConfigRows[i].valLabel, "");

      // 3) 이름 라벨이 우측 숫자 영역까지 대폭 확장 (폭 110px) 되어 이름이 최대한 많이 보임!
      lv_obj_set_width(ui_BTConfigRows[i].nameLabel, 110);
      lv_obj_set_style_text_color(ui_BTConfigRows[i].nameLabel, lv_color_hex(0xFFFF00), 0);
      lv_label_set_long_mode(ui_BTConfigRows[i].nameLabel, LV_LABEL_LONG_SCROLL_CIRCULAR);
      lv_label_set_text(ui_BTConfigRows[i].nameLabel, items[i].fullName.c_str());
    } else {
      // 비선택 항목:
      // 1) 번호는 어두운 회색
      lv_obj_set_style_text_color(ui_BTConfigRows[i].numLabel, lv_color_hex(0x666666), 0);

      // 2) 이름 라벨은 기본 44px 폭에 축약 이름
      lv_obj_set_width(ui_BTConfigRows[i].nameLabel, 44);
      lv_obj_set_style_text_color(ui_BTConfigRows[i].nameLabel, lv_color_hex(0xAAAAAA), 0);
      lv_label_set_long_mode(ui_BTConfigRows[i].nameLabel, LV_LABEL_LONG_CLIP);
      lv_label_set_text(ui_BTConfigRows[i].nameLabel, items[i].shortName.c_str());

      // 3) 값 라벨 복원 (67px 너비로 M 글자 잘림 완벽 방지)
      lv_obj_set_style_text_color(ui_BTConfigRows[i].valLabel, lv_color_hex(0xAAAAAA), 0);
      lv_label_set_text(ui_BTConfigRows[i].valLabel, items[i].valText.c_str());
    }
  }

  // 하단 실시간 한글 설명 갱신
  if (btConfigIndex >= 0 && btConfigIndex < BT_CONFIG_COUNT) {
    lv_label_set_text(ui_BTConfigDescLabel, items[btConfigIndex].descText.c_str());
  }
}

// ─────────────────────────────────────────────────────────
// [NEW] 통합 명령어 파서

// Serial / Bluetooth 공통 사용 → 중복 코드 제거
// ─────────────────────────────────────────────────────────
void processCommand(String &inputBuffer) {
  Serial.printf("[CMD] Received %d bytes: '%s'\n", (int)inputBuffer.length(), inputBuffer.c_str());

  // ── w [word1] [word2] : 단어 추가 ───────────────────
  if (inputBuffer.startsWith("w ") && inputBuffer.length() > 2) {
    String wordsString = inputBuffer.substring(2);
    wordsString.trim();

    if (wordsString.length() > 0) {
      int wordCount = 0;
      String firstAddedWord = "";
      int firstWordIndex = -1;
      int groupId = wordbook.getGroupCount();

      int firstSlash = wordsString.indexOf('/');
      if (firstSlash == -1) {
        if (wordbook.addWordFast(wordsString, groupId)) {
          firstAddedWord = wordsString;
          firstWordIndex = wordbook.getWordCount() - 1;
          wordCount++;
          Serial.printf("[WORD ADD] %s (group:%d)\n", wordsString.c_str(),
                        groupId);
        } else {
          soundError();
#ifdef ENABLE_BLUETOOTH
          btPrintln("Storage full (max " + String(wordbook.getMaxWordCount()) +
                    " words)");
#endif
        }
      } else {
        String word1 = wordsString.substring(0, firstSlash);
        word1.trim();
        if (word1.length() > 0) {
          if (wordbook.addWordFast(word1, groupId)) {
            firstAddedWord = word1;
            firstWordIndex = wordbook.getWordCount() - 1;
            wordCount++;
            Serial.printf("[WORD ADD] %s (group:%d)\n", word1.c_str(), groupId);
          } else {
            soundError();
#ifdef ENABLE_BLUETOOTH
            btPrintln("Failed to add '" + word1 + "' - storage full");
#endif
          }
        }
        String remaining = wordsString.substring(firstSlash + 1);
        remaining.trim();
        if (remaining.length() > 0 && wordCount > 0) {
          if (wordbook.addWordFast(remaining, groupId)) {
            wordCount++;
            Serial.printf("[WORD ADD] %s (group:%d)\n", remaining.c_str(),
                          groupId);
          } else {
            soundError();
#ifdef ENABLE_BLUETOOTH
            btPrintln("Failed to add '" + remaining + "' - storage full");
#endif
          }
        }
      }

      if (wordCount > 0) {
        wordbook.saveWords();
        soundSuccess();
        if (firstWordIndex >= 0)
          wordbook.setCurrentIndex(firstWordIndex);
        updateWordDisplay(firstAddedWord);
        wordbook.saveCurrentIndex();
        Serial.printf("[WORD SHOW] %s (%d/%d)\n", firstAddedWord.c_str(),
                      wordbook.getCurrentIndex() + 1, wordbook.getWordCount());
#ifdef ENABLE_BLUETOOTH
        btPrintln("Added " + String(wordCount) + " words: " + wordsString);
#endif
      }
    }
    inputBuffer = "";
  }

  // ── h [hex] : HEX → 한국어 단어 추가 ───────────────
  else if (inputBuffer.startsWith("h ") && inputBuffer.length() > 2) {
    String hexStr = inputBuffer.substring(2);
    hexStr.trim();
    if (hexStr.length() > 0) {
      String koreanStr = hexToKorean(hexStr);
      if (wordbook.addWord(koreanStr)) {
        soundSuccess();
        updateWordDisplay(koreanStr);
        wordbook.saveCurrentIndex();
#ifdef ENABLE_BLUETOOTH
        btPrintln("Added Korean: " + koreanStr);
#endif
      } else {
        soundError();
#ifdef ENABLE_BLUETOOTH
        btPrintln("Failed - storage full (max " +
                  String(wordbook.getMaxWordCount()) + " words)");
#endif
      }
    }
    inputBuffer = "";
  }

  // ── bl [0-255] : 백라이트 밝기 ─────────────────────
  else if (inputBuffer.startsWith("bl ") && inputBuffer.length() > 3) {
    String brightnessStr = inputBuffer.substring(3);
    brightnessStr.trim();
    if (brightnessStr.length() > 0) {
      int brightness = brightnessStr.toInt();
      if (brightness >= 0 && brightness <= 255) {
        set_backlight_brightness(brightness);
        soundSuccess();
#ifdef ENABLE_BLUETOOTH
        btPrintln("Backlight: " + String(brightness));
#endif
      } else {
        soundError();
#ifdef ENABLE_BLUETOOTH
        btPrintln("Invalid brightness. Range: 0-255");
#endif
      }
    }
    inputBuffer = "";
  }

  // ── pf : PWM 주파수 (M5.Display 사용 시 미지원) ─────
  else if (inputBuffer.startsWith("pf ") && inputBuffer.length() > 3) {
    Serial.println("PWM freq not supported with M5.Display driver");
    soundError();
#ifdef ENABLE_BLUETOOTH
    btPrintln("PWM freq not supported on S3");
#endif
    inputBuffer = "";
  }

  // ── auto [seconds] : 자동 넘기기 간격 ──────────────
  else if (inputBuffer.startsWith("auto ") && inputBuffer.length() > 5) {
    String intervalStr = inputBuffer.substring(5);
    intervalStr.trim();
    if (intervalStr.length() > 0) {
      int seconds = intervalStr.toInt();
      if (seconds > 0) {
        autoTransitionInterval = seconds * 1000;
        Preferences autoPrefs;
        prefsBegin(autoPrefs, "autotransition", false);
        autoPrefs.putInt("interval", autoTransitionInterval);
        autoPrefs.end();
        soundSuccess();
        Serial.println("Auto interval: " + String(seconds) + "s");
#ifdef ENABLE_BLUETOOTH
        btPrintln("Auto interval: " + String(seconds) + " seconds");
#endif
      } else {
        soundError();
#ifdef ENABLE_BLUETOOTH
        btPrintln("Invalid. Use: auto [seconds]");
#endif
      }
    }
    inputBuffer = "";
  }

  // ── time [minutes] : 슬림 모드 타임아웃 ────────────
  else if (inputBuffer.startsWith("time ") && inputBuffer.length() > 5) {
    String timeStr = inputBuffer.substring(5);
    timeStr.trim();
    if (timeStr.length() > 0) {
      int newTime = timeStr.toInt();
      if (newTime > 0) {
        slimModeTime = newTime;
        Preferences tempPrefs;
        prefsBegin(tempPrefs, "slimmode", false);
        tempPrefs.putInt("time", slimModeTime);
        tempPrefs.end();
        soundSuccess();
        Serial.println("Slim mode: " + String(slimModeTime) + " min");
#ifdef ENABLE_BLUETOOTH
        btPrintln("Slim mode: " + String(slimModeTime) + " minutes");
#endif
      } else {
        soundError();
#ifdef ENABLE_BLUETOOTH
        btPrintln("Invalid. Use: time [minutes]");
#endif
      }
    }
    inputBuffer = "";
  }

  // ── first : 첫 번째 단어로 이동 ─────────────────────
  else if (inputBuffer.equalsIgnoreCase("first") ||
           inputBuffer.equalsIgnoreCase("first\r\n") ||
           inputBuffer.equalsIgnoreCase("first\n")) {
    lastActivityTime = millis();
    wordbook.setCurrentIndex(0);
    String firstWord = wordbook.getCurrentWord();
    if (firstWord != "") {
      updateWordDisplay(firstWord);
      wordbook.saveCurrentIndex();
      soundNext();
#ifdef ENABLE_BLUETOOTH
      btPrintln("First word: " + firstWord);
#endif
    } else {
      lv_label_set_text(ui_MainText1, "Vocabulary");
      lv_label_set_text(ui_Label2, "0/0");
    }
    inputBuffer = "";
  }
  // ── help : 도움말 출력 ────────────────────────────
  else if (inputBuffer.equalsIgnoreCase("help") ||
           inputBuffer.equalsIgnoreCase("?")) {
    display_commands();
    inputBuffer = "";
  }
  // ── r [index] : 단어 삭제 ───────────────────────────
  else if (inputBuffer.startsWith("r ") && inputBuffer.length() > 2) {
    String indexStr = inputBuffer.substring(2);
    indexStr.trim();
    if (indexStr.length() > 0) {
      int wordIndex = indexStr.toInt();
      if (wordIndex > 0 && wordIndex <= wordbook.getWordCount()) {
        int zeroIdx = wordIndex - 1;
        String wordToDelete = wordbook.getWord(zeroIdx);
        if (wordbook.removeWord(zeroIdx)) {
          soundPrev();
          if (wordbook.getWordCount() > 0) {
            if (wordbook.getCurrentIndex() >= wordbook.getWordCount())
              wordbook.setCurrentIndex(wordbook.getWordCount() - 1);
            updateWordDisplay(wordbook.getCurrentWord());
            wordbook.saveCurrentIndex();
          } else {
            lv_label_set_text(ui_MainText1, "Vocabulary");
            lv_obj_set_style_pad_left(ui_MainText1, 5, LV_PART_MAIN);
            lv_obj_set_style_pad_right(ui_MainText1, 5, LV_PART_MAIN);
            lv_obj_set_style_pad_top(ui_MainText1, 5, LV_PART_MAIN);
            lv_obj_set_style_pad_bottom(ui_MainText1, 5, LV_PART_MAIN);
            lv_label_set_text(ui_Label2, "0/0");
            lv_arc_set_value(ui_Arc1, 0);
            wordbook.saveCurrentIndex();
          }
#ifdef ENABLE_BLUETOOTH
          btPrintln("Deleted #" + String(wordIndex) + ": " + wordToDelete);
#endif
        } else {
          soundError();
        }
      } else {
        soundError();
      }
    }
    inputBuffer = "";
  }
  // ── clear / deleteall : 전체 삭제 ───────────────────
  else if (inputBuffer.equalsIgnoreCase("clear") ||
           inputBuffer.equalsIgnoreCase("deleteall")) {
    if (wordbook.clearAllWords()) {
      soundDelete();
      lv_label_set_text(ui_MainText1, "All words deleted");
      lv_label_set_text(ui_Label2, "0/0");
      lv_arc_set_value(ui_Arc1, 0);
      wordbook.saveCurrentIndex();
#ifdef ENABLE_BLUETOOTH
      btPrintln("All words deleted");
#endif
    } else {
#ifdef ENABLE_BLUETOOTH
      btPrintln("Failed to delete words");
#endif
    }
    inputBuffer = "";
  }

  // ── sound [on/off] : 사운드 토글 ─────────────────────
  else if (inputBuffer.startsWith("sound")) {
    String state = inputBuffer.substring(5);
    state.trim();

    bool oldState = soundEnabled;
    if (state.equalsIgnoreCase("on"))
      soundEnabled = true;
    else if (state.equalsIgnoreCase("off"))
      soundEnabled = false;
    else
      soundEnabled = !soundEnabled;

    Preferences pref;
    prefsBegin(pref, "settings", false);
    pref.putBool("sound", soundEnabled);
    pref.end();

    setAmplifier(false); // [OPTIMIZE] 평상시 앰프 OFF 유지
    updateSoundStatusUI();
    saveSettings();

    if (soundEnabled) {
      if (!oldState)
        soundSuccess();
      Serial.println("Sound: ON");
#ifdef ENABLE_BLUETOOTH
      btPrintln("Sound: ON");
#endif
    } else {
      Serial.println("Sound: OFF");
#ifdef ENABLE_BLUETOOTH
      btPrintln("Sound: OFF");
#endif
    }
    inputBuffer = "";
  }

  // ── vol [0-255] : 볼륨 조절 ───────────────────────
  else if (inputBuffer.startsWith("vol ") && inputBuffer.length() > 4) {
    String volStr = inputBuffer.substring(4);
    volStr.trim();
    if (volStr.length() > 0) {
      int v = volStr.toInt();
      if (v >= 0 && v <= 255) {
        M5.Speaker.setVolume(v);
        soundSuccess();
        Preferences pref;
        prefsBegin(pref, "settings", false);
        pref.putInt("vol", v);
        pref.end();
        Serial.println("Volume: " + String(v));
#ifdef ENABLE_BLUETOOTH
        btPrintln("Volume: " + String(v));
#endif
      } else {
        soundError();
      }
    }
    inputBuffer = "";
  }

  // 인식 불가 긴 입력 → 버퍼 클리어
  else if (inputBuffer.length() > 10) {
    inputBuffer = "";
  }
}

// ── [NEW] 단어 분리(/) 및 앞/뒤 이동 로직 ──────────────────────
int wordSubIndex = 0; // 0: 왼쪽(한글), 1: 오른쪽(영어)

String getWordPart(String rawWord, int subIdx) {
    int slashIdx = rawWord.indexOf('/');
    if (slashIdx == -1) return rawWord; // 구분자 없으면 전체 반환
    
    if (subIdx == 0) return rawWord.substring(0, slashIdx);
    return rawWord.substring(slashIdx + 1);
}

// 다음 단어 파트 가져오기 (>>)
String advanceWordForward(bool isCloud) {
    String currentRaw;
    if (isCloud) {
        portENTER_CRITICAL(&cloudMutex);
        if (cloudWordsList.size() == 0) { portEXIT_CRITICAL(&cloudMutex); return ""; }
        currentRaw = cloudWordsList[cloudWordIndex];
        portEXIT_CRITICAL(&cloudMutex);
    } else {
        if (wordbook.getWordCount() == 0) return "";
        currentRaw = wordbook.getCurrentWord();
    }
    
    // 현재 단어에 /가 있고 한글(0)을 보고 있었다면 -> 영어(1)로 변경
    if (currentRaw.indexOf('/') != -1 && wordSubIndex == 0) {
        wordSubIndex = 1;
        return getWordPart(currentRaw, 1);
    } 
    // 그 외 (영어 보고 있었거나 /가 없는 단어) -> 다음 단어의 한글(0)로 변경
    else {
        wordSubIndex = 0;
        if (isCloud) {
            portENTER_CRITICAL(&cloudMutex);
            int sz = cloudWordsList.size();
            if (sz > 0) cloudWordIndex = (cloudWordIndex + 1) % sz;
            currentRaw = cloudWordsList[cloudWordIndex];
            portEXIT_CRITICAL(&cloudMutex);
        } else {
            currentRaw = wordbook.getNextWord();
        }
        return getWordPart(currentRaw, 0);
    }
}

// 이전 단어 파트 가져오기 (<<)
String advanceWordBackward(bool isCloud) {
    String currentRaw;
    if (isCloud) {
        portENTER_CRITICAL(&cloudMutex);
        if (cloudWordsList.size() == 0) { portEXIT_CRITICAL(&cloudMutex); return ""; }
        currentRaw = cloudWordsList[cloudWordIndex];
        portEXIT_CRITICAL(&cloudMutex);
    } else {
        if (wordbook.getWordCount() == 0) return "";
        currentRaw = wordbook.getCurrentWord();
    }
    
    // 현재 단어에 /가 있고 영어(1)를 보고 있었다면 -> 한글(0)로 변경
    if (currentRaw.indexOf('/') != -1 && wordSubIndex == 1) {
        wordSubIndex = 0;
        return getWordPart(currentRaw, 0);
    } 
    // 그 외 (한글 보고 있었거나 /가 없는 단어) -> 이전 단어의 영어(1)로 변경
    else {
        if (isCloud) {
            portENTER_CRITICAL(&cloudMutex);
            int sz = cloudWordsList.size();
            if (sz > 0) cloudWordIndex = (cloudWordIndex - 1 + sz) % sz;
            currentRaw = cloudWordsList[cloudWordIndex];
            portEXIT_CRITICAL(&cloudMutex);
        } else {
            currentRaw = wordbook.getPrevWord();
        }
        
        if (currentRaw.indexOf('/') != -1) {
            wordSubIndex = 1;
            return getWordPart(currentRaw, 1);
        } else {
            wordSubIndex = 0;
            return currentRaw;
        }
    }
}

// 버튼 A/B 방향에 맞춰 다음 텍스트 가져오기
String getNextText(bool isCloud, bool forward) {
    if (isCloud && cloudFirstShow) {
        cloudFirstShow = false;
        String currentRaw = "";
        portENTER_CRITICAL(&cloudMutex);
        if (cloudWordsList.size() > 0) currentRaw = cloudWordsList[cloudWordIndex];
        portEXIT_CRITICAL(&cloudMutex);
        if (currentRaw != "") {
            wordSubIndex = 0;
            return getWordPart(currentRaw, 0);
        }
    }
    if (forward) return advanceWordForward(isCloud);
    else return advanceWordBackward(isCloud);
}

// 버튼A 현재 방향으로 단어 이동
void navigateButtonADirection() {
  if (wordbook.getWordCount() == 0) {
    Serial.println("[NAV] Wordbook is empty, ignoring button press");
    return;
  }

  String word = getNextText(false, btnADirectionForward);

  Serial.printf("[NAV] %s word=%s\n", btnADirectionForward ? ">>" : "<<",
                word.c_str());

  if (word != "") {
    updateWordDisplay(word);
    wordbook.saveCurrentIndex();
#ifdef ENABLE_BLUETOOTH
    btPrintln(btnADirectionForward ? "A >> Next: " + word
                                   : "A << Prev: " + word);
#endif
  }

  // [FIX] 화면(UI)을 먼저 즉시 갱신한 후 효과음을 재생하여, 소리 때문에 화면
  // 갱신이 멈추는 현상을 완벽히 해결합니다.
  if (btnADirectionForward)
    soundNext();
  else
    soundPrev();
}

// M5StickS3(M5StickC Plus2) PMIC 스피커 전원 직접 제어
// M5.Speaker.begin()/end()를 반복 호출하면 FreeRTOS 태스크 크래시가 발생하므로,
// I2C로 물리적 앰프 전원만 켬/끔.
void setAmplifier(bool enable) {
  if (enable) {
    M5.In_I2C.bitOn(0x6E, 0x11, 0b00001000, 100000); // 앰프 켜기
  } else {
    M5.In_I2C.bitOff(0x6E, 0x11, 0b00001000, 100000); // 앰프 끄기 (절전)
  }
}

/* Display flushing — M5.Display 사용 */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area,
                   lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  M5.Display.startWrite();
  M5.Display.setAddrWindow(area->x1, area->y1, w, h);
  // Byte Swap을 true로 설정하여 색상 깨짐(색빠짐) 현상 해결
  M5.Display.writePixels((uint16_t *)&color_p->full, w * h, true);
  M5.Display.endWrite();
  lv_disp_flush_ready(disp);
}

void setup() {
  Serial.begin(115200);
  // delay(100) 제거 (부팅 속도 향상)

  Serial.println("\n========================================");
  Serial.println("M5StickC Plus S3 — LVGL Wordbook (M5Unified)");
  Serial.println("========================================");

  // [NEW] 최대한 빠른 부팅을 위해 CPU 주파수를 최고치로 설정
  setCpuFrequencyMhz(240);

  // ── 0단계: NVS 초기화 및 저장된 설정 로드 (화면 켜지기 전에 먼저 로드) ──
  ensureNVS();
  Serial.println("[INIT] NVS initialized");
  {
    Preferences pref;
    prefsBegin(pref, "settings", true);
    current_brightness = pref.getInt("bright", 25);
    normal_brightness = current_brightness;
    min_brightness = pref.getInt("min_bright", 5);
    if (min_brightness == 0) min_brightness = 5;
    if (min_brightness > 255) min_brightness = 255;
    dimTimeSec = pref.getInt("dim_time", 15);
    isManualMinBright = pref.getBool("min_mode", true); // 최초 밝기 기본값: min 밝기 (true)
    isDimmed = isManualMinBright;
    pref.end();
  }

  // ── 1단계: M5Unified 초기화 ──
  auto cfg = M5.config();
  cfg.internal_imu = false; // [FIX] 42번 핀 간섭 방지
  cfg.internal_mic =
      false; // [CRITICAL] 42번 핀이 마이크 CLK와 공유되므로 반드시 꺼야 함
  cfg.internal_spk = true; // 스피커 엔진 활성화
  cfg.external_spk = false;
  M5.begin(cfg);

  // [NEW] 화면 밝기를 M5.begin 직후 최종 밝기 상태(최초 기본: min)로 가장 먼저 적용
  uint8_t bootBright = isManualMinBright ? min_brightness : current_brightness;
  M5.Display.setBrightness(bootBright);

  // [OPTIMIZE] 배터리 절약: 5V Boost(ExtOutput)는 평상시 OFF 유지 (IR 송신 시에만 켬)
  M5.Power.setExtOutput(false);
  irsend.begin(); // IR 송신기 시작

  // [NEW] 화면을 최대한 빨리 켭니다.
  M5.Display.setRotation(0);
  M5.Display.fillScreen(TFT_BLACK);

  M5.Power.setBatteryCharge(true); // 충전 기능 활성화 확인

  // [OPTIMIZE] 부팅 시 WiFi 모뎀 완전 OFF (STA 대기 전력 30~50mA 즉시 차단)
  WiFi.mode(WIFI_OFF);
  WiFi.disconnect(true);
  Serial.println("[INIT] WiFi completely OFF for maximum battery life");

  // ── 2단계: M5.Display 설정 (이미 위에서 수행됨) ──

  // ── 3단계: LVGL 초기화 ──
  lv_init();
  Serial.println("[INIT] LVGL initialized");

  lv_disp_draw_buf_init(&draw_buf, buf, NULL, screenWidth * screenHeight / 10);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = screenWidth;
  disp_drv.ver_res = screenHeight;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_drv_register(&disp_drv);

  // ── 4단계: SquareLine UI 초기화 (deferred to loop) ──
  // ui_init() will be called in loop() first frame
  Serial.println("[INIT] LVGL driver registered");
  delay(100); // Preferences 안정화 대기

  // [DISABLED] ui_SoundLabel 생성 - 메모리 접근 에러 방지
  // if (ui_Screen1) {
  //   ui_SoundLabel = lv_label_create(ui_Screen1);
  //   lv_label_set_text(ui_SoundLabel, "S");
  //   lv_obj_set_style_text_font(ui_SoundLabel, &lv_font_montserrat_12, 0);
  //   lv_obj_set_align(ui_SoundLabel, LV_ALIGN_CENTER);
  //   lv_obj_set_x(ui_SoundLabel, 40);
  //   lv_obj_set_y(ui_SoundLabel, -113);
  // }

  uint8_t bootBright2 = isManualMinBright ? min_brightness : current_brightness;
  M5.Display.setBrightness(bootBright2);
  delay(10);

  // Preferences 접근 전 충분한 초기화 시간 (LVGL 안정화)
  delay(200);

  // 단어장 초기화
  wordbook.begin();
  wordbook.loadCurrentIndex(); // [NEW] 이전 위치 복구

  // ── [OPTIMIZE] NVS 설정 및 부팅 초기화 통합 ──
  {
    Preferences pref;
    prefsBegin(pref, "settings", false);
    
    // 슬림 모드
    slimModeTime = pref.getInt("slim", 5);
    if (slimModeTime <= 0) {
      slimModeTime = 5;
      pref.putInt("slim", 5);
    }
    
    // 홀드 시간 및 밝기
    btHoldTime = pref.getInt("bthold", 5);
    secretHoldTime = 1; // [MOD] 사용자 요청으로 강제 1초 설정
    current_brightness = pref.getInt("bright", 25);
    normal_brightness = current_brightness;
    min_brightness = pref.getInt("min_bright", 5);
    if (min_brightness == 0) min_brightness = 5;
    if (min_brightness > 255) min_brightness = 255;
    dimTimeSec = pref.getInt("dim_time", 15);
    isManualMinBright = pref.getBool("min_mode", true);
    isDimmed = isManualMinBright;
    aBtnDirTime = pref.getInt("adirtime", 1);
    aBtnAutoTime = pref.getInt("aautotime", 2);

    // LED
    pinMode(10, OUTPUT);
    bool ledOn = pref.getBool("led", false);
    digitalWrite(10, ledOn ? LOW : HIGH);

    // 사운드
    soundEnabled = pref.getBool("sound", false);
    int savedVol = pref.getInt("vol", 30);
    if (savedVol > 160) savedVol = 160;
    M5.Speaker.setVolume(savedVol);
    setAmplifier(false); // [OPTIMIZE] 평상시 앰프 OFF 유지 (대기 전력 및 노이즈 차단)
    Serial.printf("[INIT] Sound: %s, Vol: %d (Amp Standby OFF)\n", soundEnabled ? "ON" : "OFF", savedVol);

#ifdef ENABLE_BLUETOOTH
    // [FIX] 단어장 모드에서는 배터리 절약을 위해 시작 시 무조건 꺼지도록 강제
    bluetoothEnabled = false;
#endif

    // 자동 넘기기
    autoTransitionEnabled = pref.getBool("auto", false);
    btnADirectionForward = pref.getBool("fwd", true);

    pref.end();

    // SPIFFS 장치 이름
    if (SPIFFS.begin(true)) {
      File f = SPIFFS.open("/btname.txt", "r");
      if (f) {
        String s = f.readString();
        deviceNameIndex = s.toInt();
        f.close();
      }
    }
    if (deviceNameIndex > 99 || deviceNameIndex < 0) deviceNameIndex = 0;
    sprintf(BT_DEVICE_NAME, "M5S3_%02d", deviceNameIndex);
    
    set_backlight_brightness(current_brightness);
  }


  // 마지막 활동 시간 초기화
  {
    unsigned long timeout_ms = (unsigned long)slimModeTime * 60 * 1000;
    unsigned long adjustment = 60000;
    unsigned long current_time = millis();
    lastActivityTime = (current_time > (timeout_ms - adjustment))
                           ? current_time - (timeout_ms - adjustment)
                           : 0;
  }

  // 자동 넘기기 초기화
  autoTransitionEnabled =
      false; // [FIX] 전원 껐다 켰을 때 자동 넘김이 꺼진 상태로 시작되도록 수정
  lastTransitionTime = millis();
  {
    Preferences autoPrefs;
    prefsBegin(autoPrefs, "autotransition", false);
    autoTransitionInterval = autoPrefs.getInt("interval", 2000);
    autoPrefs.end();
  }

  Serial.println("[INIT] Setup complete - UI updates deferred to loop()");

#ifdef ENABLE_BLUETOOTH
  // bluetoothEnabled = false; // [FIX] 위에서 불러온 설정 덮어쓰기 방지
  Serial.println("[INIT] Bluetooth is ready.");
#endif

  // [OPTIMIZE] 부팅 완료 즉시 80MHz 저전력 클럭으로 전환 (배터리 대폭 절약)
  setSystemClock(80);
  Serial.println("[INIT] Boot complete. System clock running at 80MHz (Low Power)");
}

// delay() 제거 (버튼 상태 꼬임 방지)
// 대신 표준 delay()를 사용합니다.

void playTone(uint32_t freq, uint32_t dur) {
  if (!soundEnabled || freq == 0 || dur == 0)
    return;

  // [FIX] IR 수신 및 무전기 모드에서는 스피커 출력을 전용 오디오용으로
  // 사용하므로 일반 톤 재생 건너뜀
  if (currentAppMode == MODE_IR_RECEIVE || currentAppMode == MODE_BT_WALKIE)
    return;

  uint32_t prevFreq = currentCpuFreq;
  setSystemClock(240); // 톤 재생 시 클럭 깨짐 방지를 위해 240MHz로 일시 승격
  setAmplifier(true);  // [OPTIMIZE] 소리 출력 시에만 앰프 ON

  M5.Speaker.tone(freq, dur);

  // [FIX] 백그라운드 화면(LVGL) 업데이트 유지
  unsigned long start = millis();
  while (millis() - start < dur) {
    lv_timer_handler();
    delay(1);
  }

  setAmplifier(false);      // [OPTIMIZE] 소리 출력 종료 즉시 앰프 OFF (대기 전력 차단)
  setSystemClock(prevFreq); // 재생 완료 후 원래 주파수로 복구
}

void soundSuccess() {
  playTone(1760, 100);
  playTone(2637, 150);
} // 맑은 성공음
void soundError() {
  playTone(440, 180);
  playTone(220, 230);
}
void soundNext() {
  playTone(2093, 50);
  playTone(2637, 70);
} // 맑고 높은 다음음
void soundPrev() {
  playTone(2637, 50);
  playTone(2093, 70);
} // 맑고 높은 이전음
void soundBTOn() {
  playTone(1046, 120);
  playTone(1318, 120);
  playTone(1568, 120);
  playTone(2093, 200);
}
void soundBTOff() {
  playTone(2093, 120);
  playTone(1568, 120);
  playTone(1318, 120);
  playTone(1046, 200);
}
void soundAutoStart() {
  playTone(1568, 100);
  playTone(2093, 100);
  playTone(3136, 150);
}
void soundAutoStop() {
  playTone(3136, 100);
  playTone(2093, 100);
  playTone(1568, 150);
}
void soundDelete() {
  playTone(1244, 100);
  playTone(932, 140);
}
void soundWake() {
  playTone(1046, 100);
  playTone(2093, 140);
}
void soundBeep() { playTone(2093, 40); } // 아주 짧고 맑은 빕소리

void soundBoot() {
  M5.Speaker.setVolume(255); // 부팅시에는 최대 볼륨으로 웅장하게!
  delay(10);

  // 1. 깊은 베이스에서 시작 (Deep Resonance)
  M5.Speaker.tone(131, 400);
  delay(450); // C3

  // 2. 파도처럼 휘몰아치는 상승 (Grand Swell)
  for (int f = 131; f < 523; f += 8) {
    M5.Speaker.tone(f, 40);
    delay(15);
  }

  // 3. 웅장한 메이저 코드 피날레 (C Major Chord Finale)
  M5.Speaker.tone(523, 150);
  delay(100); // C5
  M5.Speaker.tone(659, 150);
  delay(100); // E5
  M5.Speaker.tone(784, 150);
  delay(100);                  // G5
  M5.Speaker.tone(1046, 2000); // C6 (가장 맑고 웅장한 마무리)
  delay(2200);

  M5.Speaker.stop();

  // 저장된 볼륨으로 복구 (기본값 30)
  Preferences pref;
  pref.begin("settings", false);
  int savedVol = pref.getInt("vol", 30);
  M5.Speaker.setVolume(savedVol);
  pref.end();
}

void saveSettings() {
  Preferences pref;
  prefsBegin(pref, "settings", false);
  pref.putBool("sound", soundEnabled);
#ifdef ENABLE_BLUETOOTH
  pref.putBool("bt", bluetoothEnabled);
#endif
  pref.putBool("auto", autoTransitionEnabled);
  pref.putBool("fwd", btnADirectionForward);
  pref.putInt("slim", slimModeTime);
  pref.putInt("bthold", btHoldTime);
  pref.putInt("sechold", secretHoldTime);
  pref.putInt("bright", current_brightness);
  pref.putInt("min_bright", min_brightness);
  pref.putInt("dim_time", dimTimeSec);
  pref.putBool("min_mode", isManualMinBright); // [NEW] 최종 밝기 토글 상태 저장
  pref.putInt("adirtime", aBtnDirTime);
  pref.putInt("aautotime", aBtnAutoTime);
  pref.putInt("vol", M5.Speaker.getVolume());
  pref.putBool("led", digitalRead(10) == LOW); // [NEW] Save LED status
  pref.end();

  // [NEW] Save device name index to SPIFFS
  if (SPIFFS.begin(true)) {
    File f = SPIFFS.open("/btname.txt", "w");
    if (f) {
      f.print(deviceNameIndex);
      f.close();
    }
  }
}

static bool pendingNameSave = false; // [FIX] 글로벌로 이동
static bool pendingBTConfigSave = false; // [NEW] BT Config 연속 증가 후 지연 저장용

// ── [NEW] 버튼 이벤트 클라우드 전송 태스크 ──
void cloudButtonReportTask(void * pvParameters) {
  int btnId = (int)pvParameters;
  String btnType = (btnId == 1) ? "A" : "B";
  if (WiFi.status() == WL_CONNECTED) {
    String fbUrl = getFirebaseURL();
    if (fbUrl.length() > 0) {
      String baseUrl = fbUrl;
      if (!baseUrl.endsWith("/")) baseUrl += "/";
      
      WiFiClientSecure client;
      client.setInsecure();
      
      HTTPClient httpStatus;
      httpStatus.begin(client, baseUrl + "device_status/button_events.json");
      httpStatus.addHeader("Content-Type", "application/json");
      String payload = "{\"lastButton\": \"" + btnType + "\", \"timestamp\": {\".sv\": \"timestamp\"}}";
      if (httpStatus.PATCH(payload) > 0) {
        httpStatus.getString(); // 버퍼 비우기
      }
      httpStatus.end();
    }
  }
  vTaskDelete(NULL);
}

void reportButtonPress(const char* btnType) {
  if (WiFi.status() == WL_CONNECTED) {
    int btnId = (btnType[0] == 'A') ? 1 : 2;
    xTaskCreate(cloudButtonReportTask, "btn_report_task", 4096, (void*)btnId, 1, NULL);
  }
}

// ── [NEW] 클라우드 백그라운드 갱신 태스크 (버벅임 완벽 해결용 FreeRTOS 태스크) ──
void cloudFetchTask(void * pvParameters) {
  for(;;) {
    if (currentAppMode != MODE_CLOUD_WEB) {
      cloudTaskStarted = false;
      cloudTaskHandle = NULL;
      vTaskDelete(NULL);
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      String fbUrl = getFirebaseURL();
      if (fbUrl.length() > 0) {
        String baseUrl = fbUrl;
        if (!baseUrl.endsWith("/")) baseUrl += "/";
        
        WiFiClientSecure client;
        client.setInsecure();
        
        // 1. 단어 목록 백그라운드 갱신 (1~2초 소요되지만 메인 화면은 절대 멈추지 않음)
        HTTPClient httpWord;
        httpWord.begin(client, baseUrl + "device_wordbook.json");
        if (httpWord.GET() == HTTP_CODE_OK) {
           String payload = httpWord.getString();
           std::vector<String> tempWords;
           int idx = 0;
           while ((idx = payload.indexOf("\"word\"", idx)) != -1) {
              idx = payload.indexOf(":", idx);
              if (idx == -1) break;
              idx = payload.indexOf("\"", idx);
              if (idx == -1) break;
              idx++;
              int endIdx = payload.indexOf("\"", idx);
              if (endIdx != -1) {
                tempWords.push_back(payload.substring(idx, endIdx));
                idx = endIdx + 1;
              } else break;
           }
           
           if (tempWords.size() > 0) {
              portENTER_CRITICAL(&cloudMutex);
              cloudWordsList = tempWords;
              if (cloudWordIndex >= cloudWordsList.size()) cloudWordIndex = 0;
              portEXIT_CRITICAL(&cloudMutex);
           }
        }
        httpWord.end();

        // 2. 온라인 상태 보고
        HTTPClient httpStatus;
        httpStatus.begin(client, baseUrl + "device_status/m5stick.json");
        httpStatus.addHeader("Content-Type", "application/json");
        if (httpStatus.PATCH("{\"lastSeen\": {\".sv\": \"timestamp\"}}") > 0) {
          httpStatus.getString(); // 버퍼 비우기
        }
        httpStatus.end();
      }
    }
    
    // [NEW] 5초 대기하되, 버튼 입력이 있으면 즉시 Firebase로 전송 (메인 루프 멈춤 방지)
    for (int i = 0; i < 50; i++) {
      if (cloudButtonPressed > 0 && currentAppMode == MODE_CLOUD_WEB && WiFi.status() == WL_CONNECTED) {
        int btn = cloudButtonPressed;
        cloudButtonPressed = 0;
        
        String fbUrl = getFirebaseURL();
        if (fbUrl.length() > 0) {
          String baseUrl = fbUrl;
          if (!baseUrl.endsWith("/")) baseUrl += "/";
          WiFiClientSecure client;
          client.setInsecure();
          HTTPClient httpBtn;
          httpBtn.begin(client, baseUrl + "device_events/button.json");
          httpBtn.addHeader("Content-Type", "application/json");
          String btnStr = (btn == 1) ? "A" : "B";
          httpBtn.PUT("{\"pressed\":\"" + btnStr + "\", \"timestamp\": {\".sv\": \"timestamp\"}}");
          httpBtn.end();
          Serial.println("[CLOUD] Button " + btnStr + " event sent to Firebase!");
        }
      }
      vTaskDelay(pdMS_TO_TICKS(100)); // 100ms * 50 = 5000ms
    }
  }
}

void loop() {
  M5.update();

  // ── [NEW] 전원 버튼(BtnPWR) 하드웨어 제어 ──
  static bool forceSleepNow = false;
  if (M5.BtnPWR.wasHold()) {
    Serial.println("[POWER] Power button HELD! Powering off completely...");
    wordbook.saveCurrentIndex();
    set_backlight(false);
    M5.Display.sleep();
    M5.Power.setExtOutput(false);
    setAmplifier(false);
    WiFi.mode(WIFI_OFF);
    digitalWrite(10, HIGH); // LED 끄기
    delay(50);
    M5.Power.powerOff(); // 기기 완전 전원 OFF (배터리 보존)
  } else if (M5.BtnPWR.wasClicked()) {
    Serial.println("[POWER] Power button CLICKED! Instant sleep toggle.");
    forceSleepNow = true;
  }

  // ── [NEW] 버튼 조작 시 디밍 해제 및 타이머 갱신 ──
  bool isBothPressed = (M5.BtnA.isPressed() && M5.BtnB.isPressed());
  bool justBothHandled = (millis() - lastBothBtnsAction < 600);

  if (M5.BtnA.wasPressed() || M5.BtnB.wasPressed()) {
    lastActivityTime = millis();
    
    if (M5.BtnA.wasPressed()) reportButtonPress("A");
    if (M5.BtnB.wasPressed()) reportButtonPress("B");
    // [중요] wasPressed에서는 디밍을 풀지 않음 (A+B 동시 누름 시 찰나의 시간차로 인한 밝아짐 방지)
  } else if (M5.BtnA.wasReleased() || M5.BtnB.wasReleased()) {
    lastActivityTime = millis();
    // [중요] 사용자가 A+B로 수동 최소 밝기를 설정한 경우(!isManualMinBright)가 아닐 때만,
    // 즉 자동 디밍으로 어두워졌을 때만 단일 버튼 클릭으로 최대 밝기로 복원!
    if (isDimmed && !isManualMinBright && !isBothPressed && !justBothHandled) {
      isDimmed = false;
      uint8_t restoreBright = (current_brightness > 0) ? current_brightness : 25;
      M5.Display.setBrightness(restoreBright);
      Serial.printf("[POWER] Single button activity! Restored brightness to %d\n",
                    restoreBright);
    }
  }

  // ── [NEW] 각 모드별 지능형 동적 CPU 클럭(DFS) 제어 ──
  if (screenOn) {
    if (currentAppMode == MODE_BT_WALKIE) {
      setSystemClock(240); // 블루투스 무전기는 스트리밍 안정성을 위해 240MHz
                           // 고성능 모드 고정
    } else if (currentAppMode == MODE_GAME ||
               currentAppMode == MODE_RUNNER_GAME ||
               currentAppMode == MODE_SECRET_7 ||
               currentAppMode == MODE_SECRET_8 ||
               currentAppMode == MODE_SECRET_9 ||
               currentAppMode == MODE_SECRET_10 ||
               currentAppMode == MODE_IR_RECEIVE) {
      setSystemClock(
          160); // 게임 및 IR 실시간 수신은 160MHz로 원활한 속도와 절전 공존
    } else {
      setSystemClock(80); // 일반 단어장, 설정, 단순 메뉴, IR 송신 등은 80MHz
                          // 최소 전력 모드
    }
  } else {
    setSystemClock(80); // 화면이 꺼져있을 때는 무조건 80MHz로 전력 최소화
  }

  // ── [NEW] 클라우드(Firebase) 모드 (FreeRTOS 듀얼코어 0초 딜레이) ──
  if (currentAppMode == MODE_CLOUD_WEB) {
    if (!cloudTaskStarted) {
      // Core 0(백그라운드 전용)에 태스크를 띄워 메인 스레드 멈춤 완벽 차단
      xTaskCreatePinnedToCore(cloudFetchTask, "CloudTask", 8192, NULL, 1, &cloudTaskHandle, 0);
      cloudTaskStarted = true;
    }

    portENTER_CRITICAL(&cloudMutex);
    int currentSize = cloudWordsList.size();
    portEXIT_CRITICAL(&cloudMutex);

    // [NEW] 처음 진입하거나 인터넷 갱신이 완료된 직후, "대기중" 화면을 자동으로 첫 단어로 교체
    if (cloudNeedsInitialDisplay && currentSize > 0) {
      cloudNeedsInitialDisplay = false;
      
      // [NEW] 기기가 꺼져도 기억하도록 저장된 마지막 단어 인덱스 불러오기
      Preferences pref;
      prefsBegin(pref, "cloud", true);
      cloudWordIndex = pref.getInt("idx", 0);
      pref.end();
      
      portENTER_CRITICAL(&cloudMutex);
      if (cloudWordIndex >= currentSize) cloudWordIndex = 0;
      portEXIT_CRITICAL(&cloudMutex);
      
      cloudFirstShow = true; // 다음 번 버튼을 누르면 단어가 나오도록 플래그 설정
      
      // [NEW] Ck (ui_Label5) 텍스트를 검정색에서 흰색으로 변경 (클라우드 연결/버튼 누름 표시)
      if (ui_Label5) {
        lv_obj_set_style_text_color(ui_Label5, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
      }
      
      updateWordDisplay("서버 연동 완료\n버튼을 눌러주세요"); // 단어 대신 연결 알림만 표시
      soundSuccess();
    }
  }



  // ── [NEW] 블루투스 무전기 핵심 로직 ──
  if (currentAppMode == MODE_BT_WALKIE) {
    // 0. 역할 선택 (A: 마스터/찾기, B: 슬레이브/대기)
    if (btWalkieRole == 0) {
      if (M5.BtnA.wasPressed()) {
        btWalkieRole = 1; // MASTER
#ifdef ENABLE_BLUETOOTH
        SerialBT.beginMaster(BT_DEVICE_NAME);
        bluetoothEnabled = true;
        updateBluetoothStatusLabel();
#endif
        updateBTWalkieDisplay();
        soundSuccess();
        Serial.println("[BT WALKIE] Role selected: MASTER (Search)");
      } else if (M5.BtnB.wasPressed()) {
        btWalkieRole = 2; // SLAVE
#ifdef ENABLE_BLUETOOTH
        SerialBT.beginSlave(BT_DEVICE_NAME);
        bluetoothEnabled = true;
        updateBluetoothStatusLabel();
#endif
        updateBTWalkieDisplay();
        soundSuccess();
        Serial.println("[BT WALKIE] Role selected: SLAVE (Wait)");
      }

      // 역할 선택 전에는 무전 로직 건너뜀
      if (btWalkieRole == 0) {
        lv_timer_handler();
        delay(1);
        // B버튼 길게 누르기(탈출)를 위해 아래로 진행
      }
    }

    // 1. 송신 (PTT: A 버튼 누름)
    if (btWalkieRole != 0 && M5.BtnA.isPressed()) {
      if (!isBTWalkieTalking) {
        isBTWalkieTalking = true;
        M5.Mic.begin();
        updateBTWalkieDisplay();
        Serial.println("[BT WALKIE] Started Talking...");
      }

      int16_t mic_buf[128];
      if (M5.Mic.record(mic_buf, 128, 10)) {
        // 16bit -> 8bit 압축 (간단한 방식: 중심점 이동 및 축소)
        uint8_t send_buf[128];
        for (int i = 0; i < 128; i++) {
          // int16 (-32768~32767) -> uint8 (0~255)
          send_buf[i] = (uint8_t)((mic_buf[i] + 32768) >> 8);
        }
#ifdef ENABLE_BLUETOOTH
        if (bluetoothEnabled && SerialBT.connected()) {
          SerialBT.write(send_buf, 128);
        }
#endif
      }
    } else if (isBTWalkieTalking) {
      isBTWalkieTalking = false;
      M5.Mic.end();
      updateBTWalkieDisplay();
      Serial.println("[BT WALKIE] Stopped Talking.");
    }

// 2. 수신 (BLE -> Speaker)
#ifdef ENABLE_BLUETOOTH
    if (bluetoothEnabled && SerialBT.available() > 0) {
      uint8_t recv_buf[128];
      // 한 번에 최대 128바이트 읽기
      size_t len = SerialBT.readBytes(recv_buf, 128);
      if (len > 0) {
        if (!isBTWalkieReceiving) {
          isBTWalkieReceiving = true;
          updateBTWalkieDisplay();
        }
        lastBTWalkieRecvTime = millis();

        // 8bit -> 16bit 복원 및 재생
        int16_t play_buf[128];
        for (size_t i = 0; i < len; i++) {
          play_buf[i] = (int16_t)((recv_buf[i] << 8) - 32768);
        }
        M5.Speaker.playRaw(play_buf, len, 8000, false, 1, 0);
      }
    } else if (isBTWalkieReceiving && (millis() - lastBTWalkieRecvTime > 300)) {
      // 마지막 수신 후 300ms 지나면 수신 상태 해제
      isBTWalkieReceiving = false;
      updateBTWalkieDisplay();
    }
#endif

    // 무전기 모드에서는 LVGL 업데이트만 수행하고 아래의 일반 버튼 로직은 대부분
    // 건너뜀 (B버튼 길게 눌러 탈출하는 로직만 아래에서 수행됨)
    lv_timer_handler();
    delay(1);
  }

  static unsigned long btnALastPressTime = 0;
  static unsigned long btnBLastPressTime = 0;
  static bool btnALongPressHandled = false;
  static bool btnBLongPressHandled = false;
  static bool btnA1SecHandled = false;
  static bool btnA2SecHandled = false;
  static bool btnA3SecHandled = false;
  static bool btnB1SecHandled = false;
  static bool btnA10SecHandled = false;
  static bool btnB10SecHandled = false;

  // ── [NEW] 펌프 스타일 리듬 게임 핵심 로직 ──
  if (currentAppMode == MODE_GAME) {
    // 1. 버튼 타이머 및 활동 타이머 업데이트
    if (M5.BtnB.wasPressed()) {
      btnBLastPressTime = millis();
      lastActivityTime = millis();
      btnBLongPressHandled = false;
      btnB10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputA = M5.BtnA.wasPressed();
    if (inputA) {
      btnALastPressTime = millis();
      lastActivityTime = millis();
      btnALongPressHandled = false;
      btnA10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }

    // 2. 탈출 로직 (Button B 3초/설정된 시간 홀드)
    unsigned long secTarget = (unsigned long)secretHoldTime * 1000;
    if (M5.BtnB.isPressed() && (millis() - btnBLastPressTime > secTarget) &&
        !btnBLongPressHandled) {
      currentAppMode = MODE_SECRET;
      btnB10SecHandled = true;
      btnBLongPressHandled = true;
      lastActivityTime = millis();

      for (auto &obj : fallingObjects) {
        if (obj.obj)
          lv_obj_del(obj.obj);
      }
      fallingObjects.clear();
      if (ui_GameContainer)
        lv_obj_add_flag(ui_GameContainer, LV_OBJ_FLAG_HIDDEN);

      updateSecretMenuDisplay();
      soundSuccess();
      Serial.println("[MODE] Catch Game -> SECRET MENU (B Hold)");
      return;
    }

    if (!isGameOver) {
      if (inputA) {
        soundBeep();
        if (ui_GameBar)
          lv_obj_set_style_bg_color(ui_GameBar, lv_color_hex(0x00FFFF), 0);
      } else if (M5.BtnA.wasReleased()) {
        if (ui_GameBar)
          lv_obj_set_style_bg_color(ui_GameBar, lv_color_hex(0x333333), 0);
      }

      // === 목표물 생성 (중앙 고정 사각형, 랜덤 속도 및 간격) ===
      unsigned long currentTime = millis();
      if (currentTime - lastObjectSpawnTime > objectSpawnInterval) {
        FallingObject newObj;
        newObj.y = -30;
        newObj.x = 17;
        newObj.size = 100;

        // 난이도에 따른 속도 범위 계산 (최대 12.0)
        float baseSpeed = 4.0f + (gameCaught * 0.1f);
        if (baseSpeed > 12.0f)
          baseSpeed = 12.0f;
        newObj.speed = baseSpeed + (random(-10, 10) * 0.1f); // ±1.0 랜덤성

        newObj.obj = lv_obj_create(ui_GameContainer);
        lv_obj_set_size(newObj.obj, newObj.size, 10);
        lv_obj_set_pos(newObj.obj, newObj.x, (int)newObj.y);
        lv_obj_set_style_bg_color(newObj.obj, lv_color_hex(0xFFCC00), 0);
        lv_obj_set_style_border_width(newObj.obj, 0, 0);
        lv_obj_set_style_radius(newObj.obj, 2, 0);

        fallingObjects.push_back(newObj);
        lastObjectSpawnTime = currentTime;

        // 다음 생성 간격 랜덤 결정 (최소 300ms)
        int minInt = max(300, 1000 - (gameCaught * 10));
        int maxInt = max(500, 1500 - (gameCaught * 15));
        objectSpawnInterval = random(minInt, maxInt);
      }

      // === 업데이트 및 정밀 판정 ===
      for (int i = (int)fallingObjects.size() - 1; i >= 0; i--) {
        FallingObject &obj = fallingObjects[i];
        obj.y += obj.speed; // 개별 속도 적용
        if (obj.obj)
          lv_obj_set_pos(obj.obj, obj.x, (int)obj.y);

        if (inputA) {
          float dist = abs(obj.y - barY);
          if (dist < 8) { // PERFECT
            lastJudgement = "PERFECT";
            gameCaught++;
            gameCombo++;
            gameEnergy = min(maxEnergy, gameEnergy + 5);
            soundSuccess();
            if (obj.obj)
              lv_obj_del(obj.obj);
            fallingObjects.erase(fallingObjects.begin() + i);
            continue;
          } else if (dist < 15) { // GREAT
            lastJudgement = "GREAT";
            gameCaught++;
            gameCombo++;
            gameEnergy = min(maxEnergy, gameEnergy + 2);
            soundBeep();
            if (obj.obj)
              lv_obj_del(obj.obj);
            fallingObjects.erase(fallingObjects.begin() + i);
            continue;
          }
        }

        if (obj.y > 235) {
          lastJudgement = "MISS";
          gameMissed++;
          gameCombo = 0;
          gameEnergy -= 15; // 고난도 패널티
          soundError();
          if (obj.obj)
            lv_obj_del(obj.obj);
          fallingObjects.erase(fallingObjects.begin() + i);
          if (gameEnergy <= 0) {
            gameEnergy = 0;
            isGameOver = true;
          }
        }
      }
      updateGameDisplay();
    } else {
      if (inputA) {
        resetGame();
      }
    }

    lv_timer_handler();
    delay(10);
    return;
  }

  // ── [NEW] 길러너 친구들 게임 핵심 로직 (Crossy Road 스타일) ──
  if (currentAppMode == MODE_RUNNER_GAME) {
    // 1. 버튼 타이머 및 활동 타이머 업데이트
    if (M5.BtnB.wasPressed()) {
      btnBLastPressTime = millis();
      lastActivityTime = millis();
      btnBLongPressHandled = false;
      btnB10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputA = M5.BtnA.wasPressed();
    if (inputA) {
      btnALastPressTime = millis();
      lastActivityTime = millis();
      btnALongPressHandled = false;
      btnA10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }

    // 2. 탈출 로직 (Button B 3초/설정된 시간 홀드)
    unsigned long secTarget = (unsigned long)secretHoldTime * 1000;
    if (M5.BtnB.isPressed() && (millis() - btnBLastPressTime > secTarget) &&
        !btnBLongPressHandled) {
      currentAppMode = MODE_SECRET;
      btnB10SecHandled = true;
      btnBLongPressHandled = true;
      lastActivityTime = millis();

      for (auto &obj : runnerObstacles) {
        if (obj.obj)
          lv_obj_del(obj.obj);
      }
      runnerObstacles.clear();

      for (auto &lane : runnerLanes) {
        if (lane.bgObj)
          lv_obj_del(lane.bgObj);
      }
      runnerLanes.clear();

      if (ui_RunnerGameContainer)
        lv_obj_add_flag(ui_RunnerGameContainer, LV_OBJ_FLAG_HIDDEN);

      updateSecretMenuDisplay();
      soundSuccess();
      Serial.println("[MODE] Runner Game -> SECRET MENU (B Hold)");
      return;
    }

    if (!isRunnerGameOver) {
      if (isRunnerReady) {
        if (inputA) {
          isRunnerReady = false;
          soundBeep();
        }
      } else {
        // A 버튼: 앞으로 한 칸 이동 (플레이어는 고정 폭이고 Y만 감소)
        if (inputA && runnerPlayerY > 0) {
          runnerPlayerY -= 20; // 한 칸 앞으로 (20px)
          runnerScore += 10;
          soundBeep();
          Serial.printf("[RUNNER] Moved forward: Y=%d, Score=%d\n",
                        runnerPlayerY, runnerScore);

          // 무한 스크롤 감지 (플레이어 Y가 120 미만인 경우 전체 스크롤)
          if (runnerPlayerY < 120) {
            runnerPlayerY += 20; // 플레이어 위치 유지 (120으로)

            // 장애물들 Y 좌표 20px씩 아래로 스크롤
            for (auto &obs : runnerObstacles) {
              obs.y += 20;
              if (obs.obj)
                lv_obj_set_pos(obs.obj, (int)obs.x, obs.y);
            }

            // 레인들 Y 좌표 20px씩 아래로 스크롤
            for (auto &lane : runnerLanes) {
              lane.y += 20;
              if (lane.bgObj)
                lv_obj_set_y(lane.bgObj, lane.y);
            }

            // 화면 아래로 벗어난 레인(Y >= 240) 파괴
            for (int i = (int)runnerLanes.size() - 1; i >= 0; i--) {
              if (runnerLanes[i].y >= 240) {
                if (runnerLanes[i].bgObj)
                  lv_obj_del(runnerLanes[i].bgObj);
                runnerLanes.erase(runnerLanes.begin() + i);
              }
            }

            // 맨 위에 새로운 레인 생성 (Y = 0)
            RunnerLane newLane;
            newLane.y = 0;

            // 연속 도로 방지: 이전 레인 2개가 모두 도로였으면 이번에는 강제로
            // 잔디밭 생성
            bool prevIsRoad1 =
                (runnerLanes.size() > 0 && runnerLanes[0].type == 1);
            bool prevIsRoad2 =
                (runnerLanes.size() > 1 && runnerLanes[1].type == 1);

            if (prevIsRoad1 && prevIsRoad2) {
              newLane.type = 0; // 강제 잔디
            } else {
              // 그 외에는 40% 확률로 도로 생성 (난이도 하향: 60% -> 40%)
              newLane.type = (random(0, 10) < 4) ? 1 : 0;
            }

            newLane.bgObj = lv_obj_create(ui_RunnerGameContainer);
            lv_obj_set_size(newLane.bgObj, 135, 20);
            lv_obj_set_pos(newLane.bgObj, 0, newLane.y);
            lv_obj_set_style_border_width(newLane.bgObj, 0, 0);
            lv_obj_set_style_radius(newLane.bgObj, 0, 0);
            lv_obj_set_style_pad_all(newLane.bgObj, 0, 0);

            if (newLane.type == 0) {
              lv_obj_set_style_bg_color(newLane.bgObj, lv_color_hex(0x1f3c25),
                                        0); // 숲색
            } else {
              lv_obj_set_style_bg_color(newLane.bgObj, lv_color_hex(0x2b2b2b),
                                        0); // 아스팔트 회색
              lv_obj_set_style_border_color(newLane.bgObj,
                                            lv_color_hex(0x3e3e3e), 0);
              lv_obj_set_style_border_width(newLane.bgObj, 1, 0);
            }

            lv_obj_move_to_index(newLane.bgObj, 0); // 맨 뒤로 보냄

            if (newLane.type == 1) {
              newLane.dir = (random(0, 2) == 0) ? -1 : 1;
              // 점수에 따라 최대 속도 증가 (난이도 하향: 속도 증가율 절반으로
              // 감소 및 최대 속도 제한)
              float baseSpeed = 1.0f + (runnerScore * 0.0005f);
              if (baseSpeed > 3.0f)
                baseSpeed = 3.0f;
              newLane.speed = baseSpeed + (random(-3, 3) * 0.1f);

              newLane.spawnInterval = random(2000, 4500); // 스폰 간격 상향
              newLane.lastSpawnTime =
                  millis() - random(0, 1500); // 다음 자연 스폰 분산

              // 새 도로 레인이 생성될 때 미리 차를 배치 (70% 확률)
              if (random(0, 10) < 7) {
                RunnerObstacle newObs;
                newObs.y = newLane.y + 2; // Y = 2
                newObs.width = random(20, 30);
                newObs.height = 14;
                newObs.vx = newLane.speed * newLane.dir;
                newObs.x = random(10, 110);

                newObs.obj = lv_obj_create(ui_RunnerGameContainer);
                lv_obj_set_size(newObs.obj, newObs.width, newObs.height);
                lv_obj_set_pos(newObs.obj, (int)newObs.x, newObs.y);

                uint32_t carColor;
                int colSel = random(0, 4);
                if (colSel == 0)
                  carColor = 0xE74C3C;
                else if (colSel == 1)
                  carColor = 0x3498DB;
                else if (colSel == 2)
                  carColor = 0xF1C40F;
                else
                  carColor = 0x9B59B6;

                lv_obj_set_style_bg_color(newObs.obj, lv_color_hex(carColor),
                                          0);
                lv_obj_set_style_border_width(newObs.obj, 0, 0);
                lv_obj_set_style_radius(newObs.obj, 2, 0);

                // 헤드라이트
                lv_obj_t *lightL = lv_obj_create(newObs.obj);
                lv_obj_t *lightR = lv_obj_create(newObs.obj);
                lv_obj_set_size(lightL, 2, 2);
                lv_obj_set_size(lightR, 2, 2);
                lv_obj_set_style_bg_color(lightL, lv_color_hex(0xFFFF00), 0);
                lv_obj_set_style_bg_color(lightR, lv_color_hex(0xFFFF00), 0);
                lv_obj_set_style_border_width(lightL, 0, 0);
                lv_obj_set_style_border_width(lightR, 0, 0);

                if (newLane.dir == 1) {
                  lv_obj_set_pos(lightL, newObs.width - 3, 2);
                  lv_obj_set_pos(lightR, newObs.width - 3, newObs.height - 4);
                } else {
                  lv_obj_set_pos(lightL, 1, 2);
                  lv_obj_set_pos(lightR, 1, newObs.height - 4);
                }

                runnerObstacles.push_back(newObs);
              }
            } else {
              newLane.dir = 0;
              newLane.speed = 0.0f;
              newLane.spawnInterval = 0;
              newLane.lastSpawnTime = 0;
            }

            runnerLanes.insert(runnerLanes.begin(), newLane);

            // 화면 아래로 밀려난 장애물 제거 (메모리 해제)
            for (int i = (int)runnerObstacles.size() - 1; i >= 0; i--) {
              if (runnerObstacles[i].y >= 240) {
                if (runnerObstacles[i].obj)
                  lv_obj_del(runnerObstacles[i].obj);
                runnerObstacles.erase(runnerObstacles.begin() + i);
              }
            }
          }
        }

        // 도로 레인에서 장애물 스폰 체크
        unsigned long currentTime = millis();
        for (auto &lane : runnerLanes) {
          if (lane.type == 1 && (currentTime - lane.lastSpawnTime >
                                 (unsigned long)lane.spawnInterval)) {
            RunnerObstacle newObs;
            newObs.y =
                lane.y + 2; // 레인 중간에 배치 (레인 높이 20, 차량 높이 14)
            newObs.width = random(20, 35);
            newObs.height = 14;
            newObs.vx = lane.speed * lane.dir;

            if (lane.dir == 1) {
              newObs.x = -newObs.width; // 왼쪽 화면 밖 시작
            } else {
              newObs.x = 135; // 오른쪽 화면 밖 시작
            }

            newObs.obj = lv_obj_create(ui_RunnerGameContainer);
            lv_obj_set_size(newObs.obj, newObs.width, newObs.height);
            lv_obj_set_pos(newObs.obj, (int)newObs.x, newObs.y);

            // 다양한 자동차 색상 선택
            uint32_t carColor;
            int colSel = random(0, 4);
            if (colSel == 0)
              carColor = 0xE74C3C; // 빨강 세단
            else if (colSel == 1)
              carColor = 0x3498DB; // 파랑 스포츠카
            else if (colSel == 2)
              carColor = 0xF1C40F; // 노랑 택시
            else
              carColor = 0x9B59B6; // 보라 크루저

            lv_obj_set_style_bg_color(newObs.obj, lv_color_hex(carColor), 0);
            lv_obj_set_style_border_width(newObs.obj, 0, 0);
            lv_obj_set_style_radius(newObs.obj, 2, 0);

            // 자동차 헤드라이트 추가 (노란색 점)
            lv_obj_t *lightL = lv_obj_create(newObs.obj);
            lv_obj_t *lightR = lv_obj_create(newObs.obj);
            lv_obj_set_size(lightL, 2, 2);
            lv_obj_set_size(lightR, 2, 2);
            lv_obj_set_style_bg_color(lightL, lv_color_hex(0xFFFF00), 0);
            lv_obj_set_style_bg_color(lightR, lv_color_hex(0xFFFF00), 0);
            lv_obj_set_style_border_width(lightL, 0, 0);
            lv_obj_set_style_border_width(lightR, 0, 0);

            if (lane.dir == 1) { // 오른쪽 주행
              lv_obj_set_pos(lightL, newObs.width - 3, 2);
              lv_obj_set_pos(lightR, newObs.width - 3, newObs.height - 4);
            } else { // 왼쪽 주행
              lv_obj_set_pos(lightL, 1, 2);
              lv_obj_set_pos(lightR, 1, newObs.height - 4);
            }

            runnerObstacles.push_back(newObs);
            lane.lastSpawnTime = currentTime;
            lane.spawnInterval =
                random(2000, 4500); // 다음 간격 재설정 (난이도 하향)
          }
        }

        // 장애물 업데이트 및 충돌 감지
        for (int i = (int)runnerObstacles.size() - 1; i >= 0; i--) {
          RunnerObstacle &obs = runnerObstacles[i];
          obs.x += obs.vx; // 이동

          if (obs.obj)
            lv_obj_set_x(obs.obj, (int)obs.x);

          // 화면 밖으로 완전히 벗어난 차량 영구 삭제 (메모리 누수 원천 차단)
          if ((obs.vx > 0 && obs.x > 135) || (obs.vx < 0 && obs.x < -40)) {
            if (obs.obj)
              lv_obj_del(obs.obj);
            runnerObstacles.erase(runnerObstacles.begin() + i);
            continue;
          }

          // 충돌 판정 (플레이어 크기: 20x15, X좌표: 57)
          // 충돌 박스를 좌우 3px씩 안쪽으로 좁혀 판정을 너그럽게 함 (플레이어
          // 가로 14px로만 충돌 감지)
          int playerX = 57 + 3;
          int playerWidth = 14;

          if (abs(obs.y - 2 - runnerPlayerY) < 10) { // Y축 충돌
            // X축 충돌 범위 체크
            if (!(playerX + playerWidth < (int)obs.x ||
                  playerX > (int)obs.x + obs.width)) {
              // 충돌!
              isRunnerGameOver = true;
              soundError();
              Serial.printf("[RUNNER] Collision! Final Score: %d\n",
                            runnerScore);
              break;
            }
          }
        }
      } // [NEW] Close else block for isRunnerReady

      updateRunnerGameDisplay();
    } else {
      // 게임 오버 - A 버튼으로 재시작
      if (inputA) {
        resetRunnerGame();
      }
    }

    lv_timer_handler();
    delay(10);
    return;
  }

  // ── [NEW] 플래피 버드 게임 핵심 로직 (비밀기능 7번) ──
  if (currentAppMode == MODE_SECRET_7) {
    // 1. 버튼 타이머 및 활동 타이머 업데이트
    if (M5.BtnB.wasPressed()) {
      btnBLastPressTime = millis();
      lastActivityTime = millis();
      btnBLongPressHandled = false;
      btnB10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputA = M5.BtnA.wasPressed();
    if (inputA) {
      btnALastPressTime = millis();
      lastActivityTime = millis();
      btnALongPressHandled = false;
      btnA10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }

    // 2. 탈출 로직 (Button B 3초/설정된 시간 홀드)
    unsigned long secTarget = (unsigned long)secretHoldTime * 1000;
    if (M5.BtnB.isPressed() && (millis() - btnBLastPressTime > secTarget) &&
        !btnBLongPressHandled) {
      currentAppMode = MODE_SECRET;
      btnB10SecHandled = true;
      btnBLongPressHandled = true;
      lastActivityTime = millis();

      for (auto &pipe : flappyPipes) {
        if (pipe.topObj)
          lv_obj_del(pipe.topObj);
        if (pipe.bottomObj)
          lv_obj_del(pipe.bottomObj);
      }
      flappyPipes.clear();
      if (ui_FlappyContainer)
        lv_obj_add_flag(ui_FlappyContainer, LV_OBJ_FLAG_HIDDEN);

      updateSecretMenuDisplay();
      soundSuccess();
      Serial.println("[MODE] Flappy Bird -> SECRET MENU (B Hold)");
      return;
    }

    if (!isFlappyGameOver) {
      if (isFlappyReady) {
        // 준비 상태: A 버튼 누르면 게임 시작
        if (inputA) {
          isFlappyReady = false;
          flappyBirdYSpeed = -1.8f; // 첫 날갯짓도 짧게 시작
          flappyPressStartTime = millis();
          flappyHoldActive = true;
          soundBeep();
          lastFlappyPipeSpawnTime = millis();
        }
      } else {
        // 게임 플레이 중: A 버튼 짧게 누르면 짤게 jump, 누르고 있으면 누른
        // 시간에 비례해 높이 jump
        if (inputA) {
          flappyBirdYSpeed = -1.8f; // 진짜 짧은 기본 점프 (Short Jump)
          flappyPressStartTime = millis();
          flappyHoldActive = true;
          soundBeep();
        } else if (M5.BtnA.isPressed() && flappyHoldActive) {
          unsigned long heldDuration = millis() - flappyPressStartTime;
          if (heldDuration < 160) {
            // 버튼을 누르고 있는 동안 추가 상승력 제공
            flappyBirdYSpeed -= 0.32f;
            if (flappyBirdYSpeed < -4.6f)
              flappyBirdYSpeed = -4.6f; // 최대 상승 속도 제한
          } else {
            flappyHoldActive = false;
          }
        } else {
          flappyHoldActive = false; // 버튼을 떼면 부스트 종료
        }

        // 중력 및 속도 감쇠 (기존 0.35f -> 0.26f로 중력 완화하여 부드러운 하강)
        flappyBirdYSpeed += 0.26f;
        if (flappyBirdYSpeed > 5.5f)
          flappyBirdYSpeed = 5.5f; // 최고 하강속도 제한 (기존 8.0f -> 5.5f)
        flappyBirdY += flappyBirdYSpeed;

        // 화면 상하단 이탈 체크 (새 높이 12px)
        if (flappyBirdY < 0.0f || flappyBirdY > 228.0f) {
          isFlappyGameOver = true;
          soundError();
          // 하이스코어 체크 및 저장
          if (flappyScore > flappyHighScore) {
            flappyHighScore = flappyScore;
            Preferences pref;
            pref.begin("flappy", false);
            pref.putInt("hiscore", flappyHighScore);
            pref.end();
            soundSuccess();
          }
        }

        // 파이프 스폰 (스폰 간격 기존 1.6초 -> 1.9초로 확장하여 안정적인 플레이
        // 보장)
        unsigned long now = millis();
        if (now - lastFlappyPipeSpawnTime > 1900) {
          FlappyPipe pipe;
          pipe.x = 135.0f;
          // 구멍 높낮이의 변동 폭 축소 (기존 60~180 -> 80~160)하여 불가능한
          // 급격한 경사 배제
          pipe.gapY = random(80, 160);
          // 통과용 구멍 크기 확장 (기존 55.0f -> 65.0f)
          pipe.gapHeight = 65.0f;
          pipe.passed = false;

          // Top Pipe
          pipe.topObj = lv_obj_create(ui_FlappyContainer);
          lv_obj_set_size(pipe.topObj, 20,
                          (int)(pipe.gapY - pipe.gapHeight / 2.0f));
          lv_obj_set_pos(pipe.topObj, (int)pipe.x, 0);
          lv_obj_set_style_bg_color(pipe.topObj, lv_color_hex(0x2ec4b6),
                                    0); // 예쁜 초록색
          lv_obj_set_style_border_width(pipe.topObj, 0, 0);
          lv_obj_set_style_radius(pipe.topObj, 1, 0);

          // Bottom Pipe
          pipe.bottomObj = lv_obj_create(ui_FlappyContainer);
          int botHeight = 240 - (int)(pipe.gapY + pipe.gapHeight / 2.0f);
          lv_obj_set_size(pipe.bottomObj, 20, botHeight);
          lv_obj_set_pos(pipe.bottomObj, (int)pipe.x,
                         (int)(pipe.gapY + pipe.gapHeight / 2.0f));
          lv_obj_set_style_bg_color(pipe.bottomObj, lv_color_hex(0x2ec4b6), 0);
          lv_obj_set_style_border_width(pipe.bottomObj, 0, 0);
          lv_obj_set_style_radius(pipe.bottomObj, 1, 0);

          flappyPipes.push_back(pipe);
          lastFlappyPipeSpawnTime = now;
        }

        // 파이프 이동 및 충돌 체크 (이동 속도 기존 2.0f -> 1.6f로 하향하여
        // 여유로운 제어 제공)
        float pipeSpeed = 1.6f;
        for (int i = (int)flappyPipes.size() - 1; i >= 0; i--) {
          FlappyPipe &pipe = flappyPipes[i];
          pipe.x -= pipeSpeed;

          // LVGL 오브젝트 갱신
          if (pipe.topObj)
            lv_obj_set_x(pipe.topObj, (int)pipe.x);
          if (pipe.bottomObj)
            lv_obj_set_x(pipe.bottomObj, (int)pipe.x);

          // 화면 밖으로 나간 파이프 삭제
          if (pipe.x < -20.0f) {
            if (pipe.topObj)
              lv_obj_del(pipe.topObj);
            if (pipe.bottomObj)
              lv_obj_del(pipe.bottomObj);
            flappyPipes.erase(flappyPipes.begin() + i);
            continue;
          }

          // 충돌 감지 (Bird: X=30, size=12, Y=flappyBirdY)
          float birdLeft = 30.0f;
          float birdRight = 42.0f;
          float birdTop = flappyBirdY;
          float birdBottom = flappyBirdY + 12.0f;

          float pipeLeft = pipe.x;
          float pipeRight = pipe.x + 20.0f;
          float gapTop = pipe.gapY - pipe.gapHeight / 2.0f;
          float gapBottom = pipe.gapY + pipe.gapHeight / 2.0f;

          // X축 겹침 체크
          if (birdRight > pipeLeft && birdLeft < pipeRight) {
            // Y축 겹침 체크 (구멍 바깥 영역과 충돌)
            if (birdTop < gapTop || birdBottom > gapBottom) {
              isFlappyGameOver = true;
              soundError();
              if (flappyScore > flappyHighScore) {
                flappyHighScore = flappyScore;
                Preferences pref;
                prefsBegin(pref, "flappy", false);
                pref.putInt("hiscore", flappyHighScore);
                pref.end();
                soundSuccess();
              }
              break;
            }
          }

          // 점수 획득 체크
          if (!pipe.passed && pipeRight < birdLeft) {
            pipe.passed = true;
            flappyScore++;
            soundBeep();
          }
        }
      }
      updateFlappyDisplay();
    } else {
      // 게임 오버 상태: A 버튼 누르면 리셋
      if (inputA) {
        resetFlappyGame();
      }
    }
    lv_timer_handler();
    delay(20);
    return;
  }

  if (currentAppMode == MODE_SECRET_8) {
    // 1. 버튼 타이머 및 활동 타이머 업데이트
    if (M5.BtnB.wasPressed()) {
      btnBLastPressTime = millis();
      lastActivityTime = millis();
      btnBLongPressHandled = false;
      btnB10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputA = M5.BtnA.wasPressed();
    if (inputA) {
      btnALastPressTime = millis();
      lastActivityTime = millis();
      btnALongPressHandled = false;
      btnA10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputB = M5.BtnB.wasPressed();

    // 2. 탈출 로직 (Button B 3초/설정된 시간 홀드)
    unsigned long secTarget = (unsigned long)secretHoldTime * 1000;
    if (M5.BtnB.isPressed() && (millis() - btnBLastPressTime > secTarget) &&
        !btnBLongPressHandled) {
      currentAppMode = MODE_SECRET;
      btnB10SecHandled = true;
      btnBLongPressHandled = true;
      lastActivityTime = millis();

      for (auto &b : vectorBuildings) {
        if (b.obj)
          lv_obj_del(b.obj);
      }
      vectorBuildings.clear();
      for (auto &o : vectorObstacles) {
        if (o.obj)
          lv_obj_del(o.obj);
      }
      vectorObstacles.clear();

      if (ui_VectorContainer)
        lv_obj_add_flag(ui_VectorContainer, LV_OBJ_FLAG_HIDDEN);

      updateSecretMenuDisplay();
      soundSuccess();
      Serial.println("[MODE] Vector Classic -> SECRET MENU (B Hold)");
      return;
    }

    if (!isVectorGameOver) {
      if (isVectorReady) {
        // 준비 상태: A 누르면 시작
        if (inputA) {
          isVectorReady = false;
          vectorPlayerState = VSTATE_RUNNING;
          lastVectorBuildingSpawnTime = millis();
          vectorStateTimer = millis();
          soundBeep();
        }
      } else {
        // --- 조작 처리 ---
        // A 버튼: 점프 / 공중제비
        if (inputA) {
          if (vectorPlayerState == VSTATE_RUNNING ||
              vectorPlayerState == VSTATE_SLIDING) {
            vectorPlayerYSpeed = -4.0f;
            vectorPlayerState = VSTATE_JUMPING;
            soundBeep();
          } else if (vectorPlayerState == VSTATE_JUMPING) {
            // 더블 점프 대신 멋진 공중제비(Frontflip) 시전!
            vectorPlayerYSpeed = -3.5f;
            vectorPlayerState = VSTATE_FLIPPING;
            vectorStateTimer = millis();
            soundBeep();
          }
        }

        // B 버튼: 슬라이딩 (지상 질주 중에만 가능)
        if (inputB && !btnB10SecHandled &&
            vectorPlayerState == VSTATE_RUNNING) {
          vectorPlayerState = VSTATE_SLIDING;
          vectorStateTimer = millis();
          soundBeep();
        }

        // --- 캐릭터 애니메이션 및 상태 프레임 업데이트 ---
        vectorPlayerAnimFrame = (vectorPlayerAnimFrame + 1) % 8;
        vectorHunterAnimFrame = (vectorHunterAnimFrame + 1) % 8;

        // 슬라이딩 지속 시간 처리 (600ms)
        if (vectorPlayerState == VSTATE_SLIDING &&
            millis() - vectorStateTimer > 600) {
          vectorPlayerState = VSTATE_RUNNING;
        }
        // 비틀거림 지속 시간 처리 (800ms)
        if (vectorPlayerState == VSTATE_STUMBLING &&
            millis() - vectorStateTimer > 800) {
          vectorPlayerState = VSTATE_RUNNING;
        }
        // 공중제비 완료 처리
        if (vectorPlayerState == VSTATE_FLIPPING &&
            millis() - vectorStateTimer > 700) {
          vectorPlayerState = VSTATE_JUMPING;
        }

        // --- 스피드 설정 ---
        float scrollSpeed = 3.2f;
        if (vectorPlayerState == VSTATE_STUMBLING) {
          scrollSpeed =
              1.0f; // 비틀거리면 맵 진행 속도가 대폭 느려짐 -> 헌터가 접근
        }

        // --- 플레이어 물리 계산 ---
        if (vectorPlayerState == VSTATE_JUMPING ||
            vectorPlayerState == VSTATE_FLIPPING) {
          vectorPlayerYSpeed += 0.28f; // 중력
          vectorPlayerY += vectorPlayerYSpeed;
        }

        // --- 헌터 물리 및 AI 추적 ---
        // 헌터의 발밑 빌딩 탐색
        float hunterAbsX = 35.0f + vectorHunterX;
        float hunterGroundY = 240.0f;

        for (const auto &b : vectorBuildings) {
          if (hunterAbsX >= b.x && hunterAbsX <= b.x + b.width) {
            hunterGroundY = 240.0f - b.height;
            break;
          }
        }

        // 헌터 점프 AI: 갭 직전이거나 플레이어가 떠 있으면 추격 점프
        if (vectorHunterState == VSTATE_RUNNING) {
          // 앞에 낭떠러지가 다가오는지 감지
          bool gapAhead = true;
          float detectRange = hunterAbsX + 25.0f;
          for (const auto &b : vectorBuildings) {
            if (detectRange >= b.x && detectRange <= b.x + b.width) {
              gapAhead = false;
              break;
            }
          }
          if (gapAhead ||
              (vectorPlayerY < hunterGroundY - 20.0f && random(0, 100) < 5)) {
            vectorHunterState = VSTATE_JUMPING;
            vectorHunterYSpeed = -4.0f;
          }
        }

        if (vectorHunterState == VSTATE_JUMPING) {
          vectorHunterYSpeed += 0.28f;
          vectorHunterY += vectorHunterYSpeed;
          if (vectorHunterY >= hunterGroundY) {
            vectorHunterY = hunterGroundY;
            vectorHunterState = VSTATE_RUNNING;
            vectorHunterYSpeed = 0.0f;
          }
        } else {
          vectorHunterY = hunterGroundY;
        }

        // --- 헌터 거리 제어 (추격자 거리 갱신) ---
        if (vectorPlayerState == VSTATE_STUMBLING) {
          vectorHunterX += 1.6f; // 비틀거리면 헌터가 급속도로 좁혀옴
        } else {
          vectorHunterX -= 0.1f; // 완벽하게 달리고 있다면 천천히 뒤로 물러남
          if (vectorHunterX < -65.0f)
            vectorHunterX = -65.0f;
        }

        // 헌터 검거 조건
        if (vectorHunterX >= -12.0f) {
          vectorPlayerState = VSTATE_TASERED;
          isVectorGameOver = true;
          soundError();
          // 하이스코어 체크 및 저장
          if (vectorScore > vectorHighScore) {
            vectorHighScore = vectorScore;
            Preferences pref;
            prefsBegin(pref, "vector", false);
            pref.putInt("hiscore", vectorHighScore);
            pref.end();
            soundSuccess();
          }
        }

        // --- 충돌 판정: 발판 착지 검사 ---
        float playerGroundY = 240.0f;

        for (const auto &b : vectorBuildings) {
          if (35.0f >= b.x && 35.0f <= b.x + b.width) {
            playerGroundY = 240.0f - b.height;
            break;
          }
        }

        if (vectorPlayerState == VSTATE_JUMPING ||
            vectorPlayerState == VSTATE_FLIPPING) {
          // 낙하 중 발판 상단 터치 시 착지 (최대 18px 낙하 속도까지 포착
          // 가능하도록 완화)
          if (vectorPlayerYSpeed >= 0.0f && playerGroundY < 239.0f &&
              vectorPlayerY >= playerGroundY - 3.0f &&
              vectorPlayerY <= playerGroundY + 18.0f) {
            vectorPlayerY = playerGroundY;
            vectorPlayerYSpeed = 0.0f;
            vectorPlayerState = VSTATE_RUNNING;
          }
        } else {
          // 지상 질주 중 발판 끝을 넘어가면 자동으로 낙하 상태 돌입
          if (vectorPlayerY < playerGroundY - 2.0f ||
              vectorPlayerY > playerGroundY + 2.0f) {
            vectorPlayerState = VSTATE_JUMPING;
            vectorPlayerYSpeed = 0.5f;
          }
        }

        // 낭떠러지 추락사 조건
        if (vectorPlayerY > 240.0f) {
          isVectorGameOver = true;
          soundError();
          if (vectorScore > vectorHighScore) {
            vectorHighScore = vectorScore;
            Preferences pref;
            prefsBegin(pref, "vector", false);
            pref.putInt("hiscore", vectorHighScore);
            pref.end();
            soundSuccess();
          }
        }

        // --- 빌딩 스크롤 및 스폰 ---
        for (int i = (int)vectorBuildings.size() - 1; i >= 0; i--) {
          VectorBuilding &b = vectorBuildings[i];
          b.x -= scrollSpeed;
          lv_obj_set_pos(b.obj, (int)b.x, 240 - (int)b.height);

          // 화면 밖으로 이탈한 빌딩 삭제
          if (b.x + b.width < -20.0f) {
            if (b.obj)
              lv_obj_del(b.obj);
            vectorBuildings.erase(vectorBuildings.begin() + i);
          }
        }

        // 새로운 빌딩 스폰
        if (!vectorBuildings.empty()) {
          const auto &lastB = vectorBuildings.back();
          if (lastB.x + lastB.width < 135.0f + 40.0f) {
            VectorBuilding newB;
            float gap = random(30, 48); // 뛰어넘을 수 있는 간격
            newB.x = lastB.x + lastB.width + gap;
            newB.width = random(95, 150);
            newB.height = random(70, 92);
            newB.obj = lv_obj_create(ui_VectorContainer);
            lv_obj_set_size(newB.obj, (int)newB.width, (int)newB.height);
            lv_obj_set_pos(newB.obj, (int)newB.x, 240 - (int)newB.height);
            lv_obj_set_style_bg_color(newB.obj, lv_color_hex(0x0f172a), 0);
            lv_obj_set_style_border_width(newB.obj, 0, 0);
            lv_obj_set_style_radius(newB.obj, 0, 0);
            vectorBuildings.push_back(newB);

            // 50% 확률로 빌딩 옥상에 파쿠르 장애물 생성
            if (random(0, 100) < 55) {
              VectorObstacle obs;
              obs.isHigh =
                  (random(0, 100) < 40); // 40%는 높은 장애물(슬라이딩 회피)
              obs.width = 6.0f;
              obs.height = obs.isHigh ? 14.0f : 10.0f;
              obs.x = newB.x + random(25, (int)(newB.width - 25));
              obs.passed = false;
              obs.obj = lv_obj_create(ui_VectorContainer);
              lv_obj_set_size(obs.obj, (int)obs.width, (int)obs.height);

              if (obs.isHigh) {
                // 머리 높이에 매달린 파이프
                lv_obj_set_pos(obs.obj, (int)obs.x,
                               240 - (int)newB.height - 25);
                lv_obj_set_style_bg_color(obs.obj, lv_color_hex(0xf59e0b),
                                          0); // 옐로우 오렌지 경고색
              } else {
                // 지상의 턱/환풍기
                lv_obj_set_pos(obs.obj, (int)obs.x,
                               240 - (int)newB.height - (int)obs.height);
                lv_obj_set_style_bg_color(obs.obj, lv_color_hex(0xef4444),
                                          0); // 빨간색
              }
              lv_obj_set_style_border_width(obs.obj, 0, 0);
              lv_obj_set_style_radius(obs.obj, 1, 0);
              vectorObstacles.push_back(obs);
            }
          }
        }

        // --- 장애물 스크롤 및 충돌 처리 ---
        for (int i = (int)vectorObstacles.size() - 1; i >= 0; i--) {
          VectorObstacle &o = vectorObstacles[i];
          o.x -= scrollSpeed;
          lv_obj_set_x(o.obj, (int)o.x);

          // 화면 탈출 시 제거
          if (o.x < -15.0f) {
            if (o.obj)
              lv_obj_del(o.obj);
            vectorObstacles.erase(vectorObstacles.begin() + i);
            continue;
          }

          // 충돌 검출 (플레이어 X = 35, 너비=10, Y축 바운드 체크)
          if (!o.passed && o.x > 25.0f && o.x < 45.0f) {
            if (o.isHigh) {
              // 높은 장애물: 슬라이딩 중이 아니면 충돌
              if (vectorPlayerState != VSTATE_SLIDING) {
                o.passed = true;
                vectorPlayerState = VSTATE_STUMBLING;
                vectorStateTimer = millis();
                soundError();
              }
            } else {
              // 낮은 장애물: 점프 중이거나 높은 공중에 떠 있지 않으면 충돌
              if (vectorPlayerState == VSTATE_RUNNING ||
                  vectorPlayerState == VSTATE_SLIDING) {
                o.passed = true;
                vectorPlayerState = VSTATE_STUMBLING;
                vectorStateTimer = millis();
                soundError();
              }
            }
          }

          // 무사 통과 시 점수 획득
          if (!o.passed && o.x < 25.0f) {
            o.passed = true;
            vectorScore += 5; // 장애물 통과 보너스
            soundBeep();
          }
        }

        // 시간 누적에 따른 소폭의 기본 점수 증가
        if (vectorPlayerAnimFrame == 0) {
          vectorScore += 1;
        }
      }

      updateVectorDisplay();
    } else {
      // 게임 오버 상태: A 버튼 클릭 시 재시작
      if (inputA) {
        resetVectorGame();
      }
    }

    lv_timer_handler();
    delay(20);
    return;
  }

  if (currentAppMode == MODE_SECRET_9) {
    // 1. 버튼 타이머 및 활동 타이머 업데이트
    if (M5.BtnB.wasPressed()) {
      btnBLastPressTime = millis();
      lastActivityTime = millis();
      btnBLongPressHandled = false;
      btnB10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputA = M5.BtnA.wasPressed();
    if (inputA) {
      btnALastPressTime = millis();
      lastActivityTime = millis();
      btnALongPressHandled = false;
      btnA10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputB = M5.BtnB.wasPressed();

    // 2. 탈출 로직 (Button B 3초 홀드)
    unsigned long secTarget = (unsigned long)secretHoldTime * 1000;
    if (M5.BtnB.isPressed() && (millis() - btnBLastPressTime > secTarget) &&
        !btnBLongPressHandled) {
      currentAppMode = MODE_SECRET;
      btnB10SecHandled = true;
      btnBLongPressHandled = true;
      lastActivityTime = millis();

      // 리소스 해제
      for (auto &b : stackBlocks) {
        if (b.obj)
          lv_obj_del(b.obj);
      }
      stackBlocks.clear();
      for (auto &s : stackShards) {
        if (s.obj)
          lv_obj_del(s.obj);
      }
      stackShards.clear();
      if (ui_StackActiveObj) {
        lv_obj_del(ui_StackActiveObj);
        ui_StackActiveObj = nullptr;
      }

      if (ui_StackContainer)
        lv_obj_add_flag(ui_StackContainer, LV_OBJ_FLAG_HIDDEN);

      updateSecretMenuDisplay();
      soundSuccess();
      Serial.println("[MODE] Stack -> SECRET MENU (B Hold)");
      return;
    }

    if (!isStackGameOver) {
      if (isStackReady) {
        // 대기 상태: A 누르면 시작
        if (inputA) {
          isStackReady = false;
          soundBeep();
        }
      } else {
        // --- 1. 활성 블록 움직임 ---
        stackActiveX += (stackActiveSpeed * stackActiveDir);
        // 화면 좌우 튕김 체크
        if (stackActiveX <= 0) {
          stackActiveX = 0;
          stackActiveDir = 1;
        } else if (stackActiveX + stackActiveWidth >= 135.0f) {
          stackActiveX = 135.0f - stackActiveWidth;
          stackActiveDir = -1;
        }
        if (ui_StackActiveObj) {
          lv_obj_set_pos(ui_StackActiveObj, (int)stackActiveX, stackActiveY);
        }

        // --- 2. A 버튼: 블록 배치 ---
        if (inputA) {
          // 최상단 블록 참조
          if (!stackBlocks.empty()) {
            const auto &prev = stackBlocks.back();

            float left1 = prev.x;
            float right1 = prev.x + prev.width;
            float left2 = stackActiveX;
            float right2 = stackActiveX + stackActiveWidth;

            float left = std::max(left1, left2);
            float right = std::min(right1, right2);
            float overlap = right - left;

            if (overlap > 0) {
              // --- 2a. 성공적으로 올려놓음 ---
              // 오차가 극히 작으면 PERFECT 판정 (2.5px 이하)
              float diff = std::abs(stackActiveX - prev.x);
              bool isPerfect = (diff <= 2.5f);

              if (isPerfect) {
                // 완전히 딱 맞춤: 소리 및 콤보 누적
                stackActiveX = prev.x; // 정밀 스냅
                stackPerfectCombo++;
                soundSuccess(); // 고음 콤보 비프

                // 콤보 보너스: 3콤보마다 깎여나갔던 너비를 6픽셀씩 보강 (최대
                // 80)
                if (stackPerfectCombo >= 3 && stackActiveWidth < 80.0f) {
                  stackActiveWidth = std::min(80.0f, stackActiveWidth + 6.0f);
                  stackActiveX =
                      std::max(0.0f, stackActiveX - 3.0f); // 양쪽으로 넓힘
                  if (stackActiveX + stackActiveWidth > 135.0f) {
                    stackActiveX = 135.0f - stackActiveWidth;
                  }
                }
              } else {
                stackPerfectCombo = 0;
                soundBeep(); // 일반 안착음
              }

              // --- 2b. 잘려 나가는 영역 파편(Shard) 스폰 ---
              float shardX = 0;
              float shardWidth = 0;
              if (left2 < left1) {
                // 왼쪽으로 삐져나온 부분
                shardX = left2;
                shardWidth = left1 - left2;
              } else if (right2 > right1) {
                // 오른쪽으로 삐져나온 부분
                shardX = right1;
                shardWidth = right2 - right1;
              }

              if (shardWidth > 0 && !isPerfect) {
                // 파편 LVGL 객체 생성 및 물리 구조체 삽입
                uint32_t activeColor = getStackColorHex(stackScore + 1);
                lv_obj_t *shardObj = lv_obj_create(ui_StackContainer);
                lv_obj_set_size(shardObj, (int)shardWidth, 12);
                lv_obj_set_pos(shardObj, (int)shardX, stackActiveY);
                lv_obj_set_style_bg_color(shardObj, lv_color_hex(activeColor),
                                          0);
                lv_obj_set_style_bg_opa(shardObj, LV_OPA_COVER, 0);
                lv_obj_set_style_border_width(shardObj, 0, 0);
                lv_obj_set_style_radius(shardObj, 1, 0);

                StackShard newShard = {shardX, shardWidth, (float)stackActiveY,
                                       0.0f,   shardObj,   activeColor};
                stackShards.push_back(newShard);
              }

              // 활성 블록 업데이트
              stackActiveWidth = overlap;
              stackActiveX = left;

              // 활성 블록의 크기/위치를 고정하여 Placed 블록으로 전환
              if (ui_StackActiveObj) {
                lv_obj_set_size(ui_StackActiveObj, (int)stackActiveWidth, 12);
                lv_obj_set_pos(ui_StackActiveObj, (int)stackActiveX,
                               stackActiveY);
              }

              uint32_t activeColor = getStackColorHex(stackScore + 1);
              StackBlock newBlock = {stackActiveX, stackActiveWidth,
                                     stackActiveY, ui_StackActiveObj,
                                     activeColor};
              stackBlocks.push_back(newBlock);
              ui_StackActiveObj = nullptr;

              // 점수 획득
              stackScore++;

              // --- 2c. 자동 스크롤 연산 ---
              // 만약 Y 좌표가 화면 높이의 절반(100)보다 높이 도달하면 전체
              // 타일을 밑으로 12px씩 하향 스크롤
              if (stackActiveY < 100) {
                // 모든 기존 타일 Y축 +12 하강 이동
                for (auto &b : stackBlocks) {
                  b.y += 12;
                  if (b.obj)
                    lv_obj_set_y(b.obj, b.y);
                }
                // 파편들의 Y좌표도 스크롤
                for (auto &s : stackShards) {
                  s.y += 12.0f;
                  if (s.obj)
                    lv_obj_set_y(s.obj, (int)s.y);
                }
                // 활성 블록 스폰 Y축을 한 수준 뒤로 당김
                stackActiveY += 12;
              }

              // 화면 아래로 탈출한 블록 제거 (메모리 관리 및 렌더링 성능
              // 최적화)
              for (int i = (int)stackBlocks.size() - 1; i >= 0; i--) {
                if (stackBlocks[i].y > 230) {
                  if (stackBlocks[i].obj)
                    lv_obj_del(stackBlocks[i].obj);
                  stackBlocks.erase(stackBlocks.begin() + i);
                }
              }

              // --- 2d. 다음 활성 블록 생성 ---
              stackActiveY -= 12;
              // 스폰 위치는 좌/우 무작위
              if (random(0, 2) == 0) {
                stackActiveX = 0;
                stackActiveDir = 1;
              } else {
                stackActiveX = 135.0f - stackActiveWidth;
                stackActiveDir = -1;
              }

              // 속도 제어: 점수가 오름에 따라 미세 가속
              stackActiveSpeed =
                  2.0f + std::min(4.0f, (float)(stackScore * 0.08f));

              ui_StackActiveObj = lv_obj_create(ui_StackContainer);
              uint32_t nextColor = getStackColorHex(stackScore + 1);
              lv_obj_set_size(ui_StackActiveObj, (int)stackActiveWidth, 12);
              lv_obj_set_pos(ui_StackActiveObj, (int)stackActiveX,
                             stackActiveY);
              lv_obj_set_style_bg_color(ui_StackActiveObj,
                                        lv_color_hex(nextColor), 0);
              lv_obj_set_style_bg_opa(ui_StackActiveObj, LV_OPA_COVER, 0);
              lv_obj_set_style_border_width(ui_StackActiveObj, 0, 0);
              lv_obj_set_style_radius(ui_StackActiveObj, 2, 0);
            } else {
              // --- 2e. 겹침 실패: 게임오버 ---
              isStackGameOver = true;
              soundError();

              // 전체를 파편으로 떨어뜨림
              if (ui_StackActiveObj) {
                uint32_t activeColor = getStackColorHex(stackScore + 1);
                StackShard newShard = {stackActiveX,        stackActiveWidth,
                                       (float)stackActiveY, 0.0f,
                                       ui_StackActiveObj,   activeColor};
                stackShards.push_back(newShard);
                ui_StackActiveObj = nullptr;
              }

              // NVS 하이스코어 갱신 및 저장
              if (stackScore > stackHighScore) {
                stackHighScore = stackScore;
                Preferences pref;
                prefsBegin(pref, "stack", false);
                pref.putInt("hiscore", stackHighScore);
                pref.end();
              }
            }
          }
        }
      }

      // --- 3. 파편(Shard) 낙하 물리 처리 ---
      for (int i = (int)stackShards.size() - 1; i >= 0; i--) {
        auto &s = stackShards[i];
        s.ySpeed += 0.45f; // 중력 가속도
        s.y += s.ySpeed;
        if (s.obj) {
          lv_obj_set_y(s.obj, (int)s.y);
        }

        // 화면 밖으로 나가면 해제
        if (s.y > 240.0f) {
          if (s.obj)
            lv_obj_del(s.obj);
          stackShards.erase(stackShards.begin() + i);
        }
      }

      updateStackDisplay();
    } else {
      // 게임 오버 대기 상태: A 누르면 리셋
      if (inputA) {
        resetStackGame();
      }

      // 게임 오버 상태 중에도 기존에 남은 파편 낙하는 끝까지 처리
      for (int i = (int)stackShards.size() - 1; i >= 0; i--) {
        auto &s = stackShards[i];
        s.ySpeed += 0.45f;
        s.y += s.ySpeed;
        if (s.obj)
          lv_obj_set_y(s.obj, (int)s.y);
        if (s.y > 240.0f) {
          if (s.obj)
            lv_obj_del(s.obj);
          stackShards.erase(stackShards.begin() + i);
        }
      }
    }

    lv_timer_handler();
    delay(20);
    return;
  }

  // ── [NEW] 스페이스 슈터 게임 핵심 로직 (비밀기능 10번) ──
  if (currentAppMode == MODE_SECRET_10) {
    // 1. 버튼 타이머 및 활동 타이머 업데이트
    if (M5.BtnB.wasPressed()) {
      btnBLastPressTime = millis();
      lastActivityTime = millis();
      btnBLongPressHandled = false;
      btnB10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputA = M5.BtnA.wasPressed();
    if (inputA) {
      btnALastPressTime = millis();
      lastActivityTime = millis();
      btnALongPressHandled = false;
      btnA10SecHandled = false;
      if (!screenOn) {
        set_backlight(true);
        screenOn = true;
        soundWake();
      }
    }
    bool inputB = M5.BtnB.wasPressed();

    // 2. 탈출 로직 (Button B 3초 홀드)
    unsigned long secTarget = (unsigned long)secretHoldTime * 1000;
    if (M5.BtnB.isPressed() && (millis() - btnBLastPressTime > secTarget) &&
        !btnBLongPressHandled) {
      currentAppMode = MODE_SECRET;
      btnB10SecHandled = true;
      btnBLongPressHandled = true;
      lastActivityTime = millis();

      // 리소스 해제
      for (auto &l : shooterLasers) {
        if (l.obj)
          lv_obj_del(l.obj);
      }
      shooterLasers.clear();
      for (auto &e : shooterEnemies) {
        if (e.obj)
          lv_obj_del(e.obj);
      }
      shooterEnemies.clear();
      for (auto &s : starfield) {
        if (s.obj)
          lv_obj_del(s.obj);
      }
      starfield.clear();

      if (ui_ShooterContainer)
        lv_obj_add_flag(ui_ShooterContainer, LV_OBJ_FLAG_HIDDEN);

      updateSecretMenuDisplay();
      soundSuccess();
      Serial.println("[MODE] Space Shooter -> SECRET MENU (B Hold)");
      return;
    }

    if (!isShooterGameOver) {
      if (isShooterReady) {
        // 대기 상태: A 누르면 시작
        if (inputA) {
          isShooterReady = false;
          soundBeep();
        }
      } else {
        // --- 1. 플레이어 자동 이동 및 B 버튼 방향 전환 ---
        shooterPlayerX += shooterPlayerDir * shooterPlayerSpeed;
        if (shooterPlayerX <= 4) {
          shooterPlayerX = 4;
          shooterPlayerDir = 1.0f;
        } else if (shooterPlayerX >= 116) {
          shooterPlayerX = 116;
          shooterPlayerDir = -1.0f;
        }
        if (ui_ShooterPlayer) {
          lv_obj_set_pos(ui_ShooterPlayer, (int)shooterPlayerX,
                         (int)shooterPlayerY);
        }

        if (inputB && !btnB10SecHandled) {
          shooterPlayerDir *= -1.0f;
          soundBeep();
        }

        // --- 2. A 버튼: 레이저 발사 (쿨다운 300ms) ---
        if (inputA) {
          unsigned long now = millis();
          if (now - lastPlayerFireTime > 300) {
            lastPlayerFireTime = now;

            lv_obj_t *laserObj = lv_obj_create(ui_ShooterContainer);
            lv_obj_set_size(laserObj, 3, 8);
            lv_obj_set_pos(laserObj, (int)shooterPlayerX + 6,
                           (int)shooterPlayerY - 8);
            lv_obj_set_style_bg_color(laserObj, lv_color_hex(0x00ffff), 0);
            lv_obj_set_style_bg_opa(laserObj, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(laserObj, 0, 0);
            lv_obj_set_style_radius(laserObj, 1, 0);

            ShooterLaser newLaser = {shooterPlayerX + 6.0f,
                                     shooterPlayerY - 8.0f, false, laserObj};
            shooterLasers.push_back(newLaser);
            playTone(800, 50);
          }
        }

        // --- 3. 스타필드 이동 ---
        for (auto &s : starfield) {
          s.y += s.speed;
          if (s.y > 240.0f) {
            s.y = 0.0f;
            s.x = random(0, 135);
          }
          if (s.obj) {
            lv_obj_set_pos(s.obj, (int)s.x, (int)s.y);
          }
        }

        // --- 4. 에일리언 생성 ---
        unsigned long now = millis();

        
        unsigned long spawnInterval =
            std::max(1000UL, 2500UL - (shooterScore * 50));
        if (now - lastEnemySpawnTime > spawnInterval) {
          lastEnemySpawnTime = now;
          lv_obj_t *enemyObj = lv_obj_create(ui_ShooterContainer);
          lv_obj_set_size(enemyObj, 12, 12);
          float ex = random(10, 113);
          float ey = 15.0f;
          lv_obj_set_pos(enemyObj, (int)ex, (int)ey);
          lv_obj_set_style_bg_color(enemyObj, lv_color_hex(0xff00ff), 0);
          lv_obj_set_style_bg_opa(enemyObj, LV_OPA_COVER, 0);
          lv_obj_set_style_border_width(enemyObj, 0, 0);
          lv_obj_set_style_radius(enemyObj, 3, 0);

          float espeed = 0.6f + std::min(1.2f, (float)(shooterScore * 0.03f));
          ShooterEnemy newEnemy = {ex, ey, espeed, 1, enemyObj};
          shooterEnemies.push_back(newEnemy);
        }

        // --- 5. 에일리언 이동 및 적 레이저 발사 ---
        unsigned long enemyFireInterval = 1600;
        if (!shooterEnemies.empty() &&
            (now - lastEnemyFireTime > enemyFireInterval)) {
          lastEnemyFireTime = now;
          int idx = random(0, shooterEnemies.size());
          const auto &e = shooterEnemies[idx];

          lv_obj_t *eLaserObj = lv_obj_create(ui_ShooterContainer);
          lv_obj_set_size(eLaserObj, 3, 8);
          lv_obj_set_pos(eLaserObj, (int)e.x + 4, (int)e.y + 12);
          lv_obj_set_style_bg_color(eLaserObj, lv_color_hex(0xffaa00), 0);
          lv_obj_set_style_bg_opa(eLaserObj, LV_OPA_COVER, 0);
          lv_obj_set_style_border_width(eLaserObj, 0, 0);
          lv_obj_set_style_radius(eLaserObj, 1, 0);

          ShooterLaser newLaser = {e.x + 4.0f, e.y + 12.0f, true, eLaserObj};
          shooterLasers.push_back(newLaser);
          playTone(400, 30);
        }

        for (int i = (int)shooterEnemies.size() - 1; i >= 0; i--) {
          auto &e = shooterEnemies[i];
          e.y += e.speedX;
          if (e.obj) {
            lv_obj_set_pos(e.obj, (int)e.x, (int)e.y);
          }

          if (e.y > 215.0f) {
            shooterHP -= 20;
            if (shooterHP < 0)
              shooterHP = 0;
            soundError();
            if (e.obj)
              lv_obj_del(e.obj);
            shooterEnemies.erase(shooterEnemies.begin() + i);
            continue;
          }

          if (e.y + 12.0f >= shooterPlayerY && e.y <= shooterPlayerY + 10.0f) {
            if (e.x + 12.0f >= shooterPlayerX &&
                e.x <= shooterPlayerX + 15.0f) {
              shooterHP -= 25;
              if (shooterHP < 0)
                shooterHP = 0;
              soundError();
              if (e.obj)
                lv_obj_del(e.obj);
              shooterEnemies.erase(shooterEnemies.begin() + i);
              continue;
            }
          }
        }

        // --- 6. 레이저 이동 및 충돌 판정 ---
        for (int i = (int)shooterLasers.size() - 1; i >= 0; i--) {
          auto &l = shooterLasers[i];
          if (l.isEnemy) {
            l.y += 3.0f;
            if (l.obj)
              lv_obj_set_y(l.obj, (int)l.y);

            if (l.y > 240.0f) {
              if (l.obj)
                lv_obj_del(l.obj);
              shooterLasers.erase(shooterLasers.begin() + i);
              continue;
            }

            if (l.y + 8.0f >= shooterPlayerY && l.y <= shooterPlayerY + 10.0f) {
              if (l.x + 3.0f >= shooterPlayerX &&
                  l.x <= shooterPlayerX + 15.0f) {
                shooterHP -= 15;
                if (shooterHP < 0)
                  shooterHP = 0;
                soundError();
                if (l.obj)
                  lv_obj_del(l.obj);
                shooterLasers.erase(shooterLasers.begin() + i);
                continue;
              }
            }
          } else {
            l.y -= 4.5f;
            if (l.obj)
              lv_obj_set_y(l.obj, (int)l.y);

            if (l.y < 0.0f) {
              if (l.obj)
                lv_obj_del(l.obj);
              shooterLasers.erase(shooterLasers.begin() + i);
              continue;
            }

            bool hit = false;
            for (int j = (int)shooterEnemies.size() - 1; j >= 0; j--) {
              auto &e = shooterEnemies[j];
              if (l.y <= e.y + 12.0f && l.y + 8.0f >= e.y) {
                if (l.x + 3.0f >= e.x && l.x <= e.x + 12.0f) {
                  hit = true;
                  shooterScore += 10;
                  playTone(600, 30);

                  if (e.obj)
                    lv_obj_del(e.obj);
                  shooterEnemies.erase(shooterEnemies.begin() + j);
                  break;
                }
              }
            }

            if (hit) {
              if (l.obj)
                lv_obj_del(l.obj);
              shooterLasers.erase(shooterLasers.begin() + i);
              continue;
            }
          }
        }

        // --- 7. 체력 0일 때 게임 오버 ---
        if (shooterHP <= 0) {
          isShooterGameOver = true;
          soundError();

          if (shooterScore > shooterHighScore) {
            shooterHighScore = shooterScore;
            Preferences prefs;
            prefsBegin(prefs, "shooter", false);
            prefs.putInt("hiscore", shooterHighScore);
            prefs.end();
          }
        }
      }
      updateShooterDisplay();
    } else {
      if (inputA) {
        resetShooterGame();
      }
    }

    lv_timer_handler();
    delay(20);
    return;
  }

  // [NEW] First frame UI initialization (deferred from setup to avoid memory
  // issues)
  static bool firstFrameDone = false;
  if (!firstFrameDone) {
    delay(200); // Extra safety delay for LVGL task scheduler

    // Initialize SquareLine UI
    ui_init();
    Serial.println("[LOOP] UI initialized");

    // [NEW] 사운드 상태 표시용 's' 라벨 생성 (B 라벨 옆에 배치)
    if (ui_Screen1) {
      ui_SoundLabel = lv_label_create(ui_Screen1);
      lv_label_set_text(ui_SoundLabel, "S");
      lv_obj_set_style_text_font(ui_SoundLabel, &lv_font_montserrat_12, 0); // 원래 크기 복구
      lv_obj_set_align(ui_SoundLabel, LV_ALIGN_CENTER);
      lv_obj_set_x(ui_SoundLabel, 40); // Label8(B)이 54이므로 왼쪽에 배치
      lv_obj_set_y(ui_SoundLabel, -113);
    }

    // [NEW] Spinner2 중앙에 버튼 누름 피드백용 원형 지시기 생성
    if (ui_Spinner2) {
      ui_BtnIndicator = lv_obj_create(ui_Spinner2);
      lv_obj_set_size(ui_BtnIndicator, 16, 16);
      lv_obj_set_align(ui_BtnIndicator, LV_ALIGN_CENTER);
      lv_obj_set_style_radius(ui_BtnIndicator, 8, LV_PART_MAIN | LV_STATE_DEFAULT); // 완벽한 원
      lv_obj_set_style_border_width(ui_BtnIndicator, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_bg_color(ui_BtnIndicator, lv_color_hex(0x535353), LV_PART_MAIN | LV_STATE_DEFAULT); // 스피너 도는 색과 동일한 회색
      lv_obj_set_style_bg_opa(ui_BtnIndicator, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_clear_flag(ui_BtnIndicator, LV_OBJ_FLAG_SCROLLABLE);
    }
    
    // [NEW] WiFi 접속 상태 표시를 위한 라벨 (메인화면 project 위)
    if (ui_Screen1) {
      ui_WiFiSSIDLabel = lv_label_create(ui_Screen1);
      lv_obj_set_width(ui_WiFiSSIDLabel, 100);
      lv_obj_set_align(ui_WiFiSSIDLabel, LV_ALIGN_CENTER);
      lv_obj_set_x(ui_WiFiSSIDLabel, -7);
      lv_obj_set_y(ui_WiFiSSIDLabel, -112); // project(-98) 위쪽
      lv_label_set_long_mode(ui_WiFiSSIDLabel, LV_LABEL_LONG_SCROLL_CIRCULAR); // 길면 스크롤
      lv_obj_set_style_text_align(ui_WiFiSSIDLabel, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_text_font(ui_WiFiSSIDLabel, &ui_font_Font1, 0);
      lv_obj_set_style_text_color(ui_WiFiSSIDLabel, lv_color_hex(0x00FFFF), 0); // 눈에 띄게 시안색
      // Default: hidden (user prefers no SSID on main screen)
      lv_label_set_text(ui_WiFiSSIDLabel, ""); // 기본 빈 문자열
      lv_obj_add_flag(ui_WiFiSSIDLabel, LV_OBJ_FLAG_HIDDEN);
    }
    
    delay(100);

    // UI display initialization
    lv_obj_set_width(ui_MainText1, 135);
    lv_label_set_long_mode(ui_MainText1, LV_LABEL_LONG_WRAP);
    lv_arc_set_range(ui_Arc1, 0, 100);

    if (wordbook.getWordCount() > 0) {
      updateWordDisplay(wordbook.getCurrentWord());
    } else {
      lv_label_set_text(ui_MainText1, "Vocabulary");
      lv_obj_set_style_pad_left(ui_MainText1, 5, LV_PART_MAIN);
      lv_obj_set_style_pad_right(ui_MainText1, 5, LV_PART_MAIN);
      lv_obj_set_style_pad_top(ui_MainText1, 5, LV_PART_MAIN);
      lv_obj_set_style_pad_bottom(ui_MainText1, 5, LV_PART_MAIN);
      lv_label_set_text(ui_Label2, "0/0");
    }

    if (ui_Label6) {
      if (aAssignedAuto) lv_label_set_text(ui_Label6, "a");
      else lv_label_set_text(ui_Label6, autoTransitionEnabled ? "A" : "G");
    }
    if (ui_Label7)
      lv_label_set_text(ui_Label7, btnADirectionForward ? ">>" : "<<");

    // Bluetooth status
#ifdef ENABLE_BLUETOOTH
    updateBluetoothStatusLabel();
#endif

    // Sound UI
    updateSoundStatusUI();

    Serial.println("[LOOP] First frame UI initialization complete");
    firstFrameDone = true;
  }

  // [NEW] BLE 데이터 수신 시 소리 재생
#ifdef ENABLE_BLUETOOTH
  if (bluetoothEnabled && SerialBT.lastDataReceived) {
    SerialBT.lastDataReceived = false;
    Serial.println("[SOUND] BLE data received - playing notification sound");
    playTone(1000, 100);
    delay(50);
    playTone(800, 100);
  }
#endif

  // ── BtnA wasPressed ───────────────────────────────────
  if (M5.BtnA.wasPressed()) {
    Serial.println(">>> BTN A WAS PRESSED <<<");
    btnALastPressTime = millis();
    btnALongPressHandled = false;
    btnA1SecHandled = false;
    btnA2SecHandled = false;
    btnA3SecHandled = false;
    btnA10SecHandled = false;

    if (wordbook.getWordCount() > 0) {
      Serial.printf("[BTN A] %d/%d\n", wordbook.getCurrentIndex() + 1,
                    wordbook.getWordCount());
    }

    lastActivityTime = millis();
    if (!screenOn) {
      set_backlight(true);
      screenOn = true;
      soundWake();
    } else {
      soundBeep();
    }
  }

  // ── BtnB wasPressed ───────────────────────────────────
  if (M5.BtnB.wasPressed()) {
    Serial.println(">>> BTN B WAS PRESSED <<<");
    btnBLastPressTime = millis();
    btnBLongPressHandled = false;
    btnB1SecHandled = false;
    btnB10SecHandled = false;

    if (wordbook.getWordCount() > 0) {
      Serial.printf("[BTN B] %d/%d\n", wordbook.getCurrentIndex() + 1,
                    wordbook.getWordCount());
    }

    lastActivityTime = millis();
    if (!screenOn) {
      set_backlight(true);
      screenOn = true;
      soundWake();
    } else {
      // soundBeep(); // [MOD] 눌렀을 때가 아니라 뗄 때 동작하는 느낌을 주기 위해 소리 제거
    }
  }

  if (M5.BtnA.isPressed() && !M5.BtnB.isPressed() && btnALastPressTime > 0) {
    unsigned long dur = millis() - btnALastPressTime;
    unsigned long btTarget = (unsigned long)btHoldTime * 1000;

    // ── BT Config 모드, 카운터 항목 빠른 자동 변경 (A버튼 길게 누름) ──────────────────────────
    if (dur > 500 && currentAppMode == MODE_BT_CONFIG) {
      static unsigned long lastFastIncTime = 0;
      unsigned long fastInterval = (btConfigIndex == 0 || btConfigIndex == 3 || btConfigIndex == 4 || btConfigIndex == 5) ? 80 : 150;
      if (millis() - lastFastIncTime > fastInterval) {
        lastFastIncTime = millis();
        bool changed = false;

        if (btConfigIndex == 0) { // Device Name (0~99)
          deviceNameIndex++;
          if (deviceNameIndex > 99) deviceNameIndex = 0;
          sprintf(BT_DEVICE_NAME, "M5S3_%02d", deviceNameIndex);
          changed = true;
        } else if (btConfigIndex == 3) { // Volume (0~160, 5단위)
          Preferences pref;
          pref.begin("settings", false);
          int v = pref.getInt("vol", 30) + 5;
          if (v > 160) v = 0;
          M5.Speaker.setVolume(v);
          pref.putInt("vol", v);
          pref.end();
          changed = true;
        } else if (btConfigIndex == 4) { // Max Brightness (10~255, 10단위)
          int b = current_brightness + 10;
          if (b > 255) b = 10;
          if (b > 240 && b < 255) b = 255;
          set_backlight_brightness(b);
          changed = true;
        } else if (btConfigIndex == 5) { // Min Brightness (5~255, 5단위, 0 없음)
          int b = min_brightness + 5;
          if (b > 255) b = 5;
          min_brightness = b;
          M5.Display.setBrightness(min_brightness);
          changed = true;
        } else if (btConfigIndex == 6) { // Dim Time
          if (dimTimeSec == 0) dimTimeSec = 3;
          else if (dimTimeSec == 3) dimTimeSec = 5;
          else if (dimTimeSec == 5) dimTimeSec = 8;
          else if (dimTimeSec == 8) dimTimeSec = 10;
          else if (dimTimeSec == 10) dimTimeSec = 15;
          else if (dimTimeSec == 15) dimTimeSec = 20;
          else if (dimTimeSec == 20) dimTimeSec = 30;
          else dimTimeSec = 0;
          changed = true;
        } else if (btConfigIndex == 7) { // Auto Transition
          if (!autoTransitionEnabled) {
            autoTransitionEnabled = true;
            autoTransitionInterval = 1000;
          } else {
            autoTransitionInterval += 1000;
            if (autoTransitionInterval > 10000) {
              autoTransitionEnabled = false;
              autoTransitionInterval = 1000;
            }
          }
          changed = true;
        } else if (btConfigIndex == 8) { // Slim Mode
          if (slimModeTime < 5) slimModeTime += 1;
          else if (slimModeTime < 20) slimModeTime += 5;
          else if (slimModeTime < 30) slimModeTime = 30;
          else slimModeTime = 0;
          changed = true;
        } else if (btConfigIndex == 11) { // BT Hold Time
          btHoldTime = (btHoldTime >= 10) ? 1 : btHoldTime + 1;
          changed = true;
        } else if (btConfigIndex == 12) { // Secret Hold Time
          secretHoldTime = (secretHoldTime >= 10) ? 1 : secretHoldTime + 1;
          changed = true;
        } else if (btConfigIndex == 13) { // A Btn Dir Time
          aBtnDirTime = (aBtnDirTime >= 10) ? 1 : aBtnDirTime + 1;
          changed = true;
        } else if (btConfigIndex == 14) { // A Btn Auto Time
          aBtnAutoTime = (aBtnAutoTime >= 10) ? 1 : aBtnAutoTime + 1;
          changed = true;
        }

        if (changed) {
          updateBTConfigDisplay();
          btnALongPressHandled = true; // wasReleased 시 단일 클릭 처리 방지
          pendingBTConfigSave = true;  // 릴리즈 시 NVS 일괄 저장
        }
      }
    }
    // ── IR 모드 탈출 (10초 고정) ──────────────────────────
    else if (dur > 10000 && !btnA10SecHandled && currentAppMode == MODE_IR_REMOTE) {

      Serial.println("[BTN A 10SEC] IR Mode -> WORDBOOK");
      currentAppMode = MODE_WORDBOOK;
      btnA10SecHandled = true;
      btnALongPressHandled = true;
      lastActivityTime = millis();

      // UI 복구
      updateWordDisplay(wordbook.getCurrentWord());

      soundSuccess();
      // enablePowerSavingMode(); // [NEW] 단어장 모드 복귀 시 절전 모드 활성화 (제거: 리셋 유발)
      Serial.println("[MODE] Switched back to WORDBOOK (A 10s)");
    }
    // 설정된 시간 → 블루투스 토글 (단어장 모드에서만)
    else if (dur > btTarget && !btnA10SecHandled &&
             currentAppMode == MODE_WORDBOOK) {
      Serial.printf("[BTN A %ds TRIGGERED] Toggling Bluetooth...\n",
                    btHoldTime);

      btnA10SecHandled = true;
      // 아직 20초가 남았으므로 btnALongPressHandled는 설정하지 않음
#ifdef ENABLE_BLUETOOTH
      if (bluetoothEnabled) {
        Serial.println("\n[BLE] Shutting down...");
        SerialBT.end();
        esp_bt_controller_disable(); // RF 모뎀 물리적 비활성화로 전력 완전 차단
        bluetoothEnabled = false;
        Serial.println("[BLE] DISABLED (A 10s)\n");
        updateBluetoothStatusLabel();
        soundBTOff();
      } else {
        Serial.println("\n========================================");
        Serial.println("[BLE] Initializing NimBLE UART...");
        SerialBT.begin(BT_DEVICE_NAME);
        bluetoothEnabled = true;
        Serial.println("========================================");
        Serial.println("[BLE] INITIALIZATION COMPLETE!");
        Serial.printf("[BLE] Device name: %s\n", BT_DEVICE_NAME);
        Serial.println("[BLE] Ready to connect from nRF Connect");
        Serial.println("========================================\n");
        updateBluetoothStatusLabel();
        soundBTOn();
      }
#endif
      saveSettings();
      btnALongPressHandled = true;
      lastActivityTime = millis();
    }
    // 3초 → 캔슬 피드백 (단어장/클라우드 모드)
    else if (dur > 3000 && !btnA3SecHandled &&
             (currentAppMode == MODE_WORDBOOK || currentAppMode == MODE_CLOUD_WEB)) {
      soundError();
      if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
      if (ui_Label7) lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
      btnA3SecHandled = true;
    }
    else if (dur > std::max(aBtnDirTime, aBtnAutoTime) * 1000UL && !btnA2SecHandled && !btnA3SecHandled &&
             (currentAppMode == MODE_WORDBOOK || currentAppMode == MODE_CLOUD_WEB)) {
        if (aBtnDirTime >= aBtnAutoTime) {
          // Dir Logic
          soundBeep();
          if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
          if (ui_Label7) lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
          btnA2SecHandled = true;
        } else {
          // Auto Logic
          soundBeep();
          if (ui_Label7) lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
          if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
          btnA2SecHandled = true;
        }
    }
    else if (dur > std::min(aBtnDirTime, aBtnAutoTime) * 1000UL && !btnA1SecHandled && !btnA2SecHandled && !btnA3SecHandled &&
             (currentAppMode == MODE_WORDBOOK || currentAppMode == MODE_CLOUD_WEB)) {
        if (aBtnDirTime >= aBtnAutoTime) {
          // Auto Logic
          soundBeep();
          if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
          btnA1SecHandled = true;
        } else {
          // Dir Logic
          soundBeep();
          if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
          if (ui_Label7) lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0x00FF00), LV_PART_MAIN | LV_STATE_DEFAULT);
          btnA1SecHandled = true;
        }
    }
    // 1초 → 달력 년도 감소 (달력 모드)
    else if (dur > 1000 && !btnA1SecHandled &&
             currentAppMode == MODE_CALENDAR) {
      if (ui_Calendar) {
         const lv_calendar_date_t * d = lv_calendar_get_showed_date(ui_Calendar);
         uint32_t y = d->year;
         uint32_t m = d->month;
         if (y > 1900) y--;
         lv_calendar_set_showed_date(ui_Calendar, y, m);
         soundBeep();
      }
      btnA1SecHandled = true;
      lastActivityTime = millis();
    }
  }

  // ── BtnA wasReleased (short/long press actions) ────────────────────
  if (M5.BtnA.wasReleased()) {
    if (btnALastPressTime > 0 && !btnALongPressHandled) {
      if (currentAppMode == MODE_WORDBOOK || currentAppMode == MODE_CLOUD_WEB) {
        if (btnA3SecHandled) {
          // 3초 릴리스: 기능 취소
          if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
          if (ui_Label7) lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
          Serial.println("=== BTN A 3 SEC RELEASE (Canceled) ===");
        } else if (btnA2SecHandled) {
          bool isDirFirst = (aBtnDirTime >= aBtnAutoTime);
          if (isDirFirst) {
            // 방향 전환 (더 긴 시간)
            btnADirectionForward = !btnADirectionForward;
            if (ui_Label7) {
              lv_label_set_text(ui_Label7, btnADirectionForward ? ">>" : "<<");
              lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (btnADirectionForward) soundNext(); else soundPrev();
            saveSettings();
            Serial.println("=== BTN A DIR TOGGLE ===");
          } else {
            // 자동 넘기기 토글 (더 긴 시간)
            if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (ui_Label7) lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (autoTransitionEnabled) {
              autoTransitionEnabled = false;
              Serial.println("Auto: STOPPED");
              soundAutoStop();
              if (ui_Label6) lv_label_set_text(ui_Label6, "G");
            } else {
              autoTransitionEnabled = true;
              lastTransitionTime = millis();
              Serial.println("Auto: STARTED");
              soundAutoStart();
              if (ui_Label6) lv_label_set_text(ui_Label6, "A");
            }
          }
        } else if (btnA1SecHandled) {
          bool isDirFirst = (aBtnDirTime >= aBtnAutoTime);
          if (isDirFirst) {
            // 자동 넘기기 토글 (더 짧은 시간)
            if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (ui_Label7) lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (autoTransitionEnabled) {
              autoTransitionEnabled = false;
              Serial.println("Auto: STOPPED");
              soundAutoStop();
              if (ui_Label6) lv_label_set_text(ui_Label6, "G");
            } else {
              autoTransitionEnabled = true;
              lastTransitionTime = millis();
              Serial.println("Auto: STARTED");
              soundAutoStart();
              if (ui_Label6) lv_label_set_text(ui_Label6, "A");
            }
          } else {
            // 방향 전환 (더 짧은 시간)
            btnADirectionForward = !btnADirectionForward;
            if (ui_Label7) {
              lv_label_set_text(ui_Label7, btnADirectionForward ? ">>" : "<<");
              lv_obj_set_style_text_color(ui_Label7, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            }
            if (ui_Label6) lv_obj_set_style_text_color(ui_Label6, lv_color_hex(0xFFFFFF), LV_PART_MAIN | LV_STATE_DEFAULT);
            if (btnADirectionForward) soundNext(); else soundPrev();
            saveSettings();
            Serial.println("=== BTN A DIR TOGGLE ===");
          }
        } else {
          // 짧은 클릭: 내비게이션
          if (currentAppMode == MODE_WORDBOOK) {
            Serial.println("=== BTN A SHORT PRESS (Wordbook) ===");
            navigateButtonADirection();
          } else {
            Serial.println("=== BTN A SHORT PRESS (Cloud) ===");
            cloudButtonPressed = 1;
            portENTER_CRITICAL(&cloudMutex);
            int currentSize = cloudWordsList.size();
            portEXIT_CRITICAL(&cloudMutex);
            if (currentSize > 0) {
              String displayStr = getNextText(true, btnADirectionForward);
              Preferences pref;
              pref.begin("cloud", false);
              pref.putInt("idx", cloudWordIndex);
              pref.end();
              updateWordDisplay(displayStr);
              if (btnADirectionForward) soundNext(); else soundPrev();
              Serial.println("[CLOUD] Instant Loaded: " + displayStr);
            } else {
              updateWordDisplay("서버 동기화 중...");
              soundError();
            }
          }
        }
      } else if (currentAppMode == MODE_IR_REMOTE && !btnA1SecHandled) {
        Serial.printf("=== LG IR SEND: Index %d ===\n", irRemoteIndex);

        uint32_t codes[] = {
            0x20DF23DC, // LG Power ON
            0x20DFA35C, // LG Power OFF
            0x20DF00FF, // CH Up
            0x20DF807F, // CH Down
            0x20DF40BF, // Vol Up
            0x20DFC03F  // Vol Down
        };
        const char *names[] = {"POWER ON", "POWER OFF", "CH UP",
                               "CH DOWN",  "VOL UP",    "VOL DOWN"};
        uint32_t currentCode = codes[irRemoteIndex % 6];
        const char *currentName = names[irRemoteIndex % 6];

        // 본체 LED(GPIO 10) 켜기
        pinMode(10, OUTPUT);
        digitalWrite(10, LOW);

        String feedback = "SENDING...\nLG " + String(currentName);

        if (ui_IRLabel)
          lv_label_set_text(ui_IRLabel, feedback.c_str());
        lv_timer_handler();

        M5.Power.setExtOutput(true);
        delay(50);

        Serial.printf("[IR] Sending 0x%08X (LG %s)\n", currentCode,
                      currentName);
        for (int i = 0; i < 10; i++) {
          irsend.sendNEC(currentCode, 32);
          delay(40);
        }

        M5.Power.setExtOutput(false); // [OPTIMIZE] IR 송신 완료 즉시 5V Boost OFF (배터리 절약)
        soundBeep();
        digitalWrite(10, HIGH); // LED 끄기
        delay(200);
        updateIRRemoteDisplay();
      } else if (currentAppMode == MODE_IR_CLONE) {
        if (irCloneIndex == 0) {
          // 1번 항목: 클로닝 대기 토글
          isCloningWait = !isCloningWait;
          if (isCloningWait) {
            // [FIX] KT 리모컨 등 긴 신호 대응을 위해 타임아웃 및 임계값 조정
            // 시도 (라이브러리 버전에 따라 지원되는 설정을 최대한 활용)
            irrecv.enableIRIn(true);
            soundSuccess();
            Serial.println(
                "[IR CLONE] Waiting for signal (KT Remote Optimized)...");
          } else {

            irrecv.disableIRIn();
            soundBeep();
          }
          updateIRCloneDisplay();
        } else {
          // 복제된 항목 발사
          int listIdx = irCloneIndex - 1;
          if (listIdx >= 0 && listIdx < (int)irCloneList.size()) {
            IRCloneItem item = irCloneList[listIdx];

            Serial.println("\n[CLONE SEND] START");

            // [CRITICAL] 수신기 종료
            irrecv.disableIRIn();
            delay(100);

            if (item.value == 0) {
              Serial.println("[ERROR] value=0");
              soundError();
              updateIRCloneDisplay();
            } else {
              // UI 업데이트 및 Watchdog 케어
              String feedback =
                  "SENDING...\n0x" + String((uint32_t)item.value, 16);
              if (ui_IRCloneLabel)
                lv_label_set_text(ui_IRCloneLabel, feedback.c_str());
              lv_timer_handler(); // [CRITICAL] UI 즉시 렌더링

              pinMode(10, OUTPUT);
              digitalWrite(10, LOW);

              M5.Power.setExtOutput(true);
              delay(100);

              uint32_t necValue = (uint32_t)(item.value & 0xFFFFFFFF);
              Serial.printf("[CLONE] Sending 0x%08X (3 repeats)\n", necValue);

              // [FIX] 리셋 방지를 위해 반복 횟수 조정 및 M5.update() 추가
              for (int i = 0; i < 3; i++) {
                M5.update(); // Watchdog 리셋
                irsend.sendNEC(necValue, 32);
                delay(75);
              }

              M5.Power.setExtOutput(false); // [OPTIMIZE] 복제 IR 송신 완료 즉시 5V Boost OFF (배터리 절약)
              soundBeep();
              digitalWrite(10, HIGH);
              delay(200);

              Serial.println("[CLONE OK]");
              updateIRCloneDisplay();
            }
          }
        }
      } else if (currentAppMode == MODE_BT_CONFIG) {
        // A 버튼: 값 변경 (Toggle / Cycle)
        soundBeep();
        lastActivityTime = millis(); // 활동 시간 초기화

        if (btConfigIndex == 0) { // Device Name
          deviceNameIndex++;
          if (deviceNameIndex > 99)
            deviceNameIndex = 0;
          sprintf(BT_DEVICE_NAME, "M5S3_%02d", deviceNameIndex);
          saveSettings();
        } else if (btConfigIndex == 1) { // Bluetooth
          bluetoothEnabled = !bluetoothEnabled;
#ifdef ENABLE_BLUETOOTH
          if (bluetoothEnabled) {
            SerialBT.begin(BT_DEVICE_NAME);
            soundBTOn();
          } else {
            SerialBT.end();
            esp_bt_controller_disable(); // RF 모뎀 물리적 비활성화로 전력 완전
                                         // 차단
            soundBTOff();
          }
          updateBluetoothStatusLabel();
#endif
          saveSettings();                // [NEW] 실시간 저장
        } else if (btConfigIndex == 2) { // Sound
          soundEnabled = !soundEnabled;
          setAmplifier(false); // [OPTIMIZE] 평상시 앰프 OFF 유지 (효과음 재생 시 자동 ON/OFF)
          updateSoundStatusUI();
          if (soundEnabled)
            soundSuccess();
          saveSettings();                // [NEW] 실시간 저장
        } else if (btConfigIndex == 3) { // Volume (5 단위)
          Preferences pref;
          pref.begin("settings", false);
          int v = pref.getInt("vol", 30);
          v += 5;
          if (v > 160)
            v = 0;
          M5.Speaker.setVolume(v);
          pref.putInt("vol", v);
          pref.end();
          saveSettings();

        } else if (btConfigIndex == 4) { // Max Brightness (10 단위)
          int b = current_brightness + 10;
          if (b > 255)
            b = 10;
          if (b > 240 && b < 255)
            b = 255;
          set_backlight_brightness(b);
          saveSettings();
        } else if (btConfigIndex == 5) { // Min Brightness (디밍 최소 밝기: 5~255)
          int b = min_brightness + 5;
          if (b > 255)
            b = 5; // 0 없음! 5~255 순환
          min_brightness = b;
          M5.Display.setBrightness(min_brightness); // [피드백] 변경된 최소 밝기를 화면에 즉시 표시
          saveSettings();
        } else if (btConfigIndex == 6) { // Dim Time (디밍 대기 시간, 초단위)
          if (dimTimeSec == 0) dimTimeSec = 3;
          else if (dimTimeSec == 3) dimTimeSec = 5;
          else if (dimTimeSec == 5) dimTimeSec = 8;
          else if (dimTimeSec == 8) dimTimeSec = 10;
          else if (dimTimeSec == 10) dimTimeSec = 15;
          else if (dimTimeSec == 15) dimTimeSec = 20;
          else if (dimTimeSec == 20) dimTimeSec = 30;
          else dimTimeSec = 0; // OFF
          saveSettings();
        } else if (btConfigIndex == 7) { // Auto Transition (1초 단위, 최대 10초)

          if (!autoTransitionEnabled) {
            autoTransitionEnabled = true;
            autoTransitionInterval = 1000;
          } else {
            autoTransitionInterval += 1000;
            if (autoTransitionInterval > 10000) {
              autoTransitionEnabled = false;
              autoTransitionInterval = 1000; // 리셋
            }
          }

          Preferences autoPrefs;
          autoPrefs.begin("autotransition", false);
          autoPrefs.putBool("enabled", autoTransitionEnabled);
          autoPrefs.putInt("interval", autoTransitionInterval);
          autoPrefs.end();
          saveSettings();                // [NEW] 실시간 저장
        } else if (btConfigIndex == 8) { // Slim Mode (세분화)
          if (slimModeTime < 5)
            slimModeTime += 1;
          else if (slimModeTime < 20)
            slimModeTime += 5;
          else if (slimModeTime < 30)
            slimModeTime = 30;
          else
            slimModeTime = 0; // OFF

          saveSettings();
        } else if (btConfigIndex == 9) { // LED Toggle

          bool isOff = (digitalRead(10) == HIGH);
          digitalWrite(10, isOff ? LOW : HIGH);
          saveSettings();                // [NEW] 실시간 저장
        } else if (btConfigIndex == 10) { // Auto Direction
          btnADirectionForward = !btnADirectionForward;
          Preferences pref;
          pref.begin("settings", false);
          pref.putBool("fwd", btnADirectionForward);
          pref.end();
          saveSettings();                // [NEW] 실시간 저장
        } else if (btConfigIndex == 11) { // BT Hold Time
          btHoldTime++;
          if (btHoldTime > 10)
            btHoldTime = 1;
          saveSettings();
        } else if (btConfigIndex == 12) { // Secret Hold Time
          secretHoldTime++;
          if (secretHoldTime > 10)
            secretHoldTime = 1;
          saveSettings();
        } else if (btConfigIndex == 13) { // A Btn Dir Toggle Time
          aBtnDirTime++;
          if (aBtnDirTime > 10)
            aBtnDirTime = 1;
          saveSettings();
        } else if (btConfigIndex == 14) { // A Btn Auto Toggle Time
          aBtnAutoTime++;
          if (aBtnAutoTime > 10)
            aBtnAutoTime = 1;
          saveSettings();
        }

        updateBTConfigDisplay();

      } else if (currentAppMode == MODE_SECRET) {

        // [NEW] 비밀 메뉴 항목 선택 (A 버튼)
        Serial.printf("[SECRET] Selected item %d\n", secretMenuIndex);

        if (secretMenuIndex == 0) {
          // 0번: 클라우드 모드 진입
          currentAppMode = MODE_CLOUD_WEB;
          cloudNeedsInitialDisplay = true; // [NEW] 다시 진입할 때마다 자동으로 첫 화면 띄우도록 설정
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
            
          updateWordDisplay("Cloud 연결 중...");
          
          // Wi-Fi 연결 시도 (web_server_manager에 저장된 정보 이용)
          setupWiFiWebManager(); 
          if (WiFi.status() == WL_CONNECTED) {
              updateWordDisplay("Cloud 대기중");
              Serial.println("[MODE] Entered CLOUD WEB via Secret Menu");
          } else {
              updateWordDisplay("WiFi 연결안됨");
          }
        } else if (secretMenuIndex == 1) {
          // 1번: WiFi 핫스팟 모드 진입
          currentAppMode = MODE_WIFI_HOTSPOT;
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);

          lastWebAction = "Hotspot Init...";
          updateWiFiWebDisplay();
          lv_timer_handler(); // 즉시 화면 갱신 강제

          setupWiFiHotspot();  // [NEW] WiFi 핫스팟 직접 시작
          updateWiFiWebDisplay();
          Serial.println("[MODE] Entered WIFI HOTSPOT via Secret Menu");
        } else if (secretMenuIndex == 2) {
          // 2번: 저장된 WiFi 목록
          currentAppMode = MODE_WIFI_LIST;
          wifiListScrollIndex = 0;
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
          updateWiFiListDisplay();
          Serial.println("[MODE] Entered WIFI LIST via Secret Menu");
        } else if (secretMenuIndex == 3) {
          // 3번: BT Config 모드 진입
          currentAppMode = MODE_BT_CONFIG;
          btConfigIndex = 0;
          savedBrightStateBeforeConfig = isManualMinBright; // [NEW] 진입 전 밝기 상태 백업
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
          updateBTConfigDisplay();
          Serial.println("[MODE] Entered BT CONFIG via Secret Menu");
        } else if (secretMenuIndex == 4) {
          // 4번: 리모컨 모드 진입
          currentAppMode = MODE_IR_REMOTE;
          irRemoteIndex = 0;
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
          updateIRRemoteDisplay();
          Serial.println("[MODE] Entered IR REMOTE via Secret Menu");
        } else if (secretMenuIndex == 5) {
          // 5번: IR 수신 모드 진입
          currentAppMode = MODE_IR_RECEIVE;
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
          setAmplifier(false);
          M5.Speaker.setVolume(0);
          delay(50);
          irrecv.enableIRIn(true);
          updateIRRecvDisplay();
          Serial.println("[MODE] Entered IR RECEIVER via Secret Menu");
        } else if (secretMenuIndex == 6) {
          // 6번: IR 복제 모드 진입
          currentAppMode = MODE_IR_CLONE;
          irCloneIndex = 0;
          isCloningWait = false;
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
          setAmplifier(false);
          M5.Speaker.setVolume(0);
          delay(50);
          irsend.begin();
          updateIRCloneDisplay();
          Serial.println("[MODE] Entered IR CLONE via Secret Menu");
        } else if (secretMenuIndex == 7) {
          // 7번: 블루투스 무전기 모드 진입
          currentAppMode = MODE_BT_WALKIE;
          btWalkieRole = 0;
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
#ifdef ENABLE_BLUETOOTH
          if (bluetoothEnabled) {
            SerialBT.end();
            esp_bt_controller_disable(); // RF 모뎀 물리적 비활성화로 전력 완전 차단
            bluetoothEnabled = false;
          }
#endif
          auto mic_cfg = M5.Mic.config();
          mic_cfg.sample_rate = 8000;
          M5.Mic.config(mic_cfg);
          auto spk_cfg = M5.Speaker.config();
          spk_cfg.sample_rate = 8000;
          M5.Speaker.config(spk_cfg);
          M5.Speaker.begin();
          updateBTWalkieDisplay();
          Serial.println("[MODE] Entered BT WALKIE via Secret Menu");
        } else if (secretMenuIndex == 8) {
          // 8번: Stack 게임 모드 진입
          currentAppMode = MODE_SECRET_9;
          soundSuccess();
          if (ui_SecretContainer)
            lv_obj_add_flag(ui_SecretContainer, LV_OBJ_FLAG_HIDDEN);
          resetStackGame();
          Serial.println("[MODE] Entered STACK GAME via Secret Menu");
        } else {
          soundSuccess();
          updateSecretMenuDisplay();
        }

      } else if (currentAppMode == MODE_CALENDAR) {
        // 달력 모드에서 A 버튼 짧게 누름: 이전 달로 이동
        if (ui_Calendar) {
           const lv_calendar_date_t * d = lv_calendar_get_showed_date(ui_Calendar);
           uint32_t y = d->year;
           uint32_t m = d->month;
           m--;
           if (m < 1) { m = 12; y--; }
           lv_calendar_set_showed_date(ui_Calendar, y, m);
           soundBeep();
        }
      } else if (currentAppMode == MODE_WIFI_LIST) {
        // [MOD] A 버튼: 스크롤 (원래 B 버튼 기능)
        wifiListScrollIndex += 3;
        soundBeep();
        updateWiFiListDisplay();
      } else if (currentAppMode == MODE_SECRET_7 ||
                 currentAppMode == MODE_SECRET_8 ||
                 currentAppMode == MODE_SECRET_9 ||
                 currentAppMode == MODE_SECRET_10) {
        soundBeep();
        Serial.println("=== BTN A SHORT PRESS (Secret Sub) ===");
      }
    }
    btnALastPressTime = 0;
    btnALongPressHandled = false;
    btnA1SecHandled = false;
  }

  // ── BtnB isPressed (long press) ───────────────────────
  if (M5.BtnB.isPressed() && !M5.BtnA.isPressed() && btnBLastPressTime > 0) {
    unsigned long dur = millis() - btnBLastPressTime;
    unsigned long secTarget = (unsigned long)secretHoldTime * 1000;

    // 설정된 시간 → 비밀 메뉴 또는 IR 모드 ──────────────────────────
    if (dur > secTarget && !btnB10SecHandled) {
      Serial.printf("[BTN B %ds TRIGGERED] currentAppMode: %d\n",
                    secretHoldTime, currentAppMode);

      if (currentAppMode == MODE_WORDBOOK) {
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();
        secretMenuIndex = 0; // 진입 시 인덱스 초기화
        if (ui_Label5) lv_obj_set_style_text_color(ui_Label5, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        updateSecretMenuDisplay();
        soundWake();
        // disablePowerSavingMode(); // [NEW] 단어장 모드 탈출 시 절전 모드 해제 (제거: 리셋 유발)
        Serial.println("[MODE] Switched to SECRET MENU (B 5s)");
      } else if (currentAppMode == MODE_SECRET) {
        currentAppMode = MODE_WORDBOOK;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();
        wordSubIndex = 0; // 복귀 시 한글부터 보여주기
        if (ui_Label5) lv_obj_set_style_text_color(ui_Label5, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        updateWordDisplay(getWordPart(wordbook.getCurrentWord(), 0));
        soundSuccess();
        // enablePowerSavingMode(); // [NEW] 단어장 모드 복귀 시 절전 모드 활성화 (제거: 리셋 유발)
        Serial.println("[MODE] Switched back to WORDBOOK (B 5s)");
      } else if (currentAppMode == MODE_BT_CONFIG) {
        // [MOD] BT Config -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        // [NEW] BT Config 나가기 전, 진입 전의 원래 밝기 상태로 100% 복원!
        isManualMinBright = savedBrightStateBeforeConfig;
        isDimmed = savedBrightStateBeforeConfig;
        if (savedBrightStateBeforeConfig) {
          M5.Display.setBrightness(min_brightness);
        } else {
          M5.Display.setBrightness(current_brightness);
        }

        if (ui_BTConfigContainer)
          lv_obj_add_flag(ui_BTConfigContainer, LV_OBJ_FLAG_HIDDEN);

        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] BT Config -> SECRET MENU (B Hold, Restored brightness)");
      } else if (currentAppMode == MODE_IR_REMOTE) {

        // [MOD] IR Remote -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();
        if (ui_IRContainer)
          lv_obj_add_flag(ui_IRContainer, LV_OBJ_FLAG_HIDDEN);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] IR Remote -> SECRET MENU (B Hold)");

      } else if (currentAppMode == MODE_IR_RECEIVE) {
        // [MOD] IR Receiver -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        irrecv.disableIRIn();
        clearIRCloneList();
        setAmplifier(false); // [OPTIMIZE] 평상시 앰프 OFF 유지
        {
          Preferences pref;
          pref.begin("settings", true);
          int savedVol = pref.getInt("vol", 30);
          if (savedVol > 160)
            savedVol = 160;
          M5.Speaker.setVolume(savedVol);
          pref.end();
        }
        delay(50);

        if (ui_IRRecvContainer)
          lv_obj_add_flag(ui_IRRecvContainer, LV_OBJ_FLAG_HIDDEN);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] IR Receiver -> SECRET MENU (B Hold)");

      } else if (currentAppMode == MODE_IR_CLONE) {
        // [MOD] IR Clone -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        irrecv.disableIRIn();
        clearIRCloneList();
        setAmplifier(false); // [OPTIMIZE] 평상시 앰프 OFF 유지
        {
          Preferences pref;
          pref.begin("settings", true);
          int savedVol = pref.getInt("vol", 30);
          if (savedVol > 160)
            savedVol = 160;
          M5.Speaker.setVolume(savedVol);
          pref.end();
        }
        delay(50);

        if (ui_IRCloneContainer)
          lv_obj_add_flag(ui_IRCloneContainer, LV_OBJ_FLAG_HIDDEN);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] IR Clone -> SECRET MENU (B Hold)");

      } else if (currentAppMode == MODE_BT_WALKIE) {
        // [MOD] BT Walkie -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        M5.Mic.end();
        isBTWalkieTalking = false;
        isBTWalkieReceiving = false;
        btWalkieRole = 0;

#ifdef ENABLE_BLUETOOTH
        NimBLEDevice::getScan()->stop();
#endif

        if (ui_BTWalkieContainer)
          lv_obj_add_flag(ui_BTWalkieContainer, LV_OBJ_FLAG_HIDDEN);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] BT Walkie -> SECRET MENU (B Hold)");

      } else if (currentAppMode == MODE_GAME) {
        // [MOD] Catch Game -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        for (auto &obj : fallingObjects) {
          if (obj.obj) {
            lv_obj_del(obj.obj);
            obj.obj = nullptr;
          }
        }
        fallingObjects.clear();

        if (ui_GameContainer)
          lv_obj_add_flag(ui_GameContainer, LV_OBJ_FLAG_HIDDEN);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] Catch Game -> SECRET MENU (B Hold)");

      } else if (currentAppMode == MODE_RUNNER_GAME) {
        // [MOD] Runner Game -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        for (auto &obj : runnerObstacles) {
          if (obj.obj) {
            lv_obj_del(obj.obj);
            obj.obj = nullptr;
          }
        }
        runnerObstacles.clear();

        if (ui_RunnerGameContainer)
          lv_obj_add_flag(ui_RunnerGameContainer, LV_OBJ_FLAG_HIDDEN);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] Runner Game -> SECRET MENU (B Hold)");

      } else if (currentAppMode == MODE_SECRET_7 ||
                 currentAppMode == MODE_SECRET_8 ||
                 currentAppMode == MODE_SECRET_9 ||
                 currentAppMode == MODE_SECRET_10 ||
                 currentAppMode == MODE_CLOUD_WEB) {
        // [MOD] Secret Sub / Cloud -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        if (ui_SecretSubContainer)
          lv_obj_add_flag(ui_SecretSubContainer, LV_OBJ_FLAG_HIDDEN);
        if (ui_Label5) lv_obj_set_style_text_color(ui_Label5, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] Secret Sub / Cloud -> SECRET MENU (B Hold)");
      } else if (currentAppMode == MODE_CALENDAR) {
        // [MOD] Calendar -> SECRET MENU
        currentAppMode = MODE_SECRET;
        btnB10SecHandled = true;
        btnBLongPressHandled = true;
        lastActivityTime = millis();

        if (ui_CalendarContainer)
          lv_obj_add_flag(ui_CalendarContainer, LV_OBJ_FLAG_HIDDEN);
        updateSecretMenuDisplay();
        soundSuccess();
        Serial.println("[MODE] Calendar -> SECRET MENU (B Hold)");
      }
    }

    // 1초 → 달력 년도 증가 (달력 모드)
    else if (dur > 1000 && !btnB1SecHandled && currentAppMode == MODE_CALENDAR) {
      if (ui_Calendar) {
         const lv_calendar_date_t * d = lv_calendar_get_showed_date(ui_Calendar);
         uint32_t y = d->year;
         uint32_t m = d->month;
         if (y < 2100) y++;
         lv_calendar_set_showed_date(ui_Calendar, y, m);
         soundBeep();
      }
      btnB1SecHandled = true;
      lastActivityTime = millis();
    }
  }

  // ── BtnB wasReleased (short press) ────────────────────
  if (M5.BtnB.wasReleased()) {
    if (btnBLastPressTime > 0 && !btnBLongPressHandled) {
      if ((currentAppMode == MODE_WORDBOOK || currentAppMode == MODE_CLOUD_WEB) && !btnB1SecHandled) {
        if (currentAppMode == MODE_WORDBOOK) {
          if (wordbook.getWordCount() > 0) {
            // B는 A와 반대 방향
            String word = getNextText(false, !btnADirectionForward);
            if (word != "") {
              updateWordDisplay(word);
              wordbook.saveCurrentIndex();
#ifdef ENABLE_BLUETOOTH
              btPrintln(btnADirectionForward ? "B << Prev: " + word
                                             : "B >> Next: " + word);
#endif
            }

            // [FIX] 화면을 먼저 즉시 갱신한 후 효과음 재생
            if (btnADirectionForward)
              soundPrev();
            else
              soundNext();
          }
        } else {
          // Cloud Mode B short press
          cloudButtonPressed = 2; // [NEW] 웹/앱에 B버튼 눌림 알림
          portENTER_CRITICAL(&cloudMutex);
          int currentSize = cloudWordsList.size();
          portEXIT_CRITICAL(&cloudMutex);
          if (currentSize > 0) {
            String displayStr = getNextText(true, !btnADirectionForward);
            
            Preferences pref;
            pref.begin("cloud", false);
            pref.putInt("idx", cloudWordIndex);
            pref.end();
            
            updateWordDisplay(displayStr);
            if (btnADirectionForward) soundNext(); else soundPrev();
            Serial.println("[CLOUD] Manual B Loaded: " + displayStr);
          } else {
            updateWordDisplay("서버 동기화 중...");
            soundError();
          }
        }
      } else if (currentAppMode == MODE_SECRET) {
        // [NEW] 비밀 메뉴 항목 이동 (B 버튼)
        const int secretMenuCount = 9; // [MOD] Added Stack Game
        secretMenuIndex = (secretMenuIndex + 1) % secretMenuCount; // 0~8
        updateSecretMenuDisplay();
        soundBeep();
        Serial.printf("[SECRET] Navigated to item %d\n", secretMenuIndex);
      } else if (currentAppMode == MODE_BT_CONFIG) {
        // [NEW] BT 설정 항목 이동 (B 버튼)
        lastActivityTime = millis();
        btConfigIndex = (btConfigIndex + 1) % 15; // 0~14

        // 4번(MaxBri)일 때는 MaxBri 설정값 확인을 위해 current_brightness 표시
        // 5번(MinBri)일 때는 MinBri 설정값 확인을 위해 min_brightness 표시
        // 그 외의 항목들은 메뉴 진입 전 원래 밝기 상태 유지!
        if (btConfigIndex == 4) {
          M5.Display.setBrightness(current_brightness);
        } else if (btConfigIndex == 5) {
          M5.Display.setBrightness(min_brightness);
        } else {
          if (savedBrightStateBeforeConfig) {
            M5.Display.setBrightness(min_brightness);
            isManualMinBright = true;
            isDimmed = true;
          } else {
            M5.Display.setBrightness(current_brightness);
            isManualMinBright = false;
            isDimmed = false;
          }
        }

        updateBTConfigDisplay();
        soundBeep();
        Serial.printf("[BT CONFIG] Navigated to item %d\n", btConfigIndex);

      } else if (currentAppMode == MODE_IR_REMOTE) {
        // [NEW] 리모컨 항목 이동 (B 버튼)
        irRemoteIndex = (irRemoteIndex + 1) % 6;
        updateIRRemoteDisplay();
        soundBeep();
        Serial.printf("[IR REMOTE] Navigated to index %d\n", irRemoteIndex);
      } else if (currentAppMode == MODE_IR_CLONE) {
        // [NEW] IR 복제 항목 이동
        int maxIdx = irCloneList.size() + 1;
        irCloneIndex = (irCloneIndex + 1) % maxIdx;
        updateIRCloneDisplay();
        soundBeep();
        Serial.printf("[IR CLONE] Navigated to index %d\n", irCloneIndex);
      } else if (currentAppMode == MODE_WIFI_WEB) {
        // WiFi Web 모드 종료
        stopWiFiWebManager();
        if (ui_WiFiWebContainer)
          lv_obj_add_flag(ui_WiFiWebContainer, LV_OBJ_FLAG_HIDDEN);
        currentAppMode = MODE_SECRET;
        updateSecretMenuDisplay();
        soundBeep();
        Serial.println("[MODE] Exit WIFI WEB to SECRET MENU");
      } else if (currentAppMode == MODE_WIFI_HOTSPOT) {
        // [NEW] WiFi 핫스팟 모드 종료
        stopWiFiWebManager();
        if (ui_WiFiWebContainer)
          lv_obj_add_flag(ui_WiFiWebContainer, LV_OBJ_FLAG_HIDDEN);
        currentAppMode = MODE_SECRET;
        updateSecretMenuDisplay();
        soundBeep();
        Serial.println("[MODE] Exit WIFI HOTSPOT to SECRET MENU");
      } else if (currentAppMode == MODE_WIFI_LIST) {
        // [MOD] B 버튼: 나가기 (원래 A 버튼 기능)
        if (ui_WiFiListContainer)
          lv_obj_add_flag(ui_WiFiListContainer, LV_OBJ_FLAG_HIDDEN);
        currentAppMode = MODE_SECRET;
        updateSecretMenuDisplay();
        soundBeep();
        Serial.println("[MODE] Exit WIFI LIST to SECRET MENU");
      } else if (currentAppMode == MODE_CALENDAR) {
        // 달력 모드에서 B 버튼 짧게 누름: 다음 달로 이동
        if (ui_Calendar) {
           const lv_calendar_date_t * d = lv_calendar_get_showed_date(ui_Calendar);
           uint32_t y = d->year;
           uint32_t m = d->month;
           m++;
           if (m > 12) { m = 1; y++; }
           lv_calendar_set_showed_date(ui_Calendar, y, m);
           soundBeep();
        }
      }
      lastActivityTime = millis();
    }
    btnBLastPressTime = 0;
    btnBLongPressHandled = false;
    btnB1SecHandled = false;
  }

  // ── 양쪽 버튼 동시 → 슬립 해제 & 사운드 토글 ──────────
  static unsigned long bothBtnsStart = 0;
  static int bothBtnsLevel = 0; // 0: none, 1: 2s handled, 2: 3s handled
  static bool pendingSoundSave = false;

  if (M5.BtnA.isPressed() && M5.BtnB.isPressed()) {
    if (bothBtnsStart == 0)
      bothBtnsStart = millis();
    
    // 동시 누름이 감지되면 개별 버튼의 누름 시간(LastPressTime)을 무효화하여 
    // 나중에 한쪽을 먼저 떼더라도 G 색상이 변하는 등의 개별 동작이 실행되지 않도록 함
    btnALastPressTime = 0;
    btnBLastPressTime = 0;
    btnALongPressHandled = true;
    btnBLongPressHandled = true;
    
    unsigned long dur = millis() - bothBtnsStart;

    // 0.35초 이상 동시 누르면 최대 밝기 <-> 최소 밝기 토글
    if (dur >= 350 && bothBtnsLevel < 1) {
      if (isManualMinBright || isDimmed) {
        // [현재 최소 상태 -> 최대 밝기로 전환 & 수동 모드 해제]
        isManualMinBright = false;
        isDimmed = false;
        uint8_t targetMax = (current_brightness > 0) ? current_brightness : 25;
        M5.Display.setBrightness(targetMax);
        soundWake();
        Serial.printf("[BTN A+B] Toggled to MAX Brightness (%d)\n", targetMax);
      } else {
        // [현재 최대 상태 -> 수동 최소 밝기 모드로 고정 (단일 버튼 눌러도 유지)]
        isManualMinBright = true;
        isDimmed = true;
        preDimBrightness = current_brightness;
        M5.Display.setBrightness(min_brightness);
        soundBeep();
        Serial.printf("[BTN A+B] Toggled to MANUAL MIN Brightness (%d)\n", min_brightness);
      }
      lastBothBtnsAction = millis(); // 릴리즈 시 디밍 해제 방지
      saveSettings();                // [NEW] 최종 밝기 상태를 NVS에 즉시 영구 저장!
      bothBtnsLevel = 1;
      btnALongPressHandled = true; // 밝기 토글 시 A버튼 단일 클릭 효과 방지
      btnBLongPressHandled = true; // 밝기 토글 시 B버튼 단일 클릭 효과 방지
    }

    if (!screenOn) {
      set_backlight(true);
      screenOn = true;
    }
    lastActivityTime = millis();
  } else {
    if (bothBtnsLevel > 0) {
      btnALongPressHandled = true;
      btnBLongPressHandled = true;
      lastBothBtnsAction = millis(); // 손을 뗀 순간에도 타임스탬프 갱신하여 릴리즈 방어 지속
    }
    bothBtnsStart = 0;
    bothBtnsLevel = 0;
  }

  // ── 시리얼/블루투스 입력 처리 (버퍼링 방식) ──────────────────
  // ── 시리얼/블루투스 입력 처리 (버퍼링 + 타임아웃 방식) ──────────
  static String serialInputBuf = "";
  static unsigned long lastSerialByteTime = 0;

  while (Serial.available()) {
    char c = Serial.read();
    serialInputBuf += c;
    lastSerialByteTime = millis();
    if (c == '\n' || c == '\r' || serialInputBuf.length() >= 256)
      break;
  }

  if (serialInputBuf.length() > 0) {
    if (serialInputBuf.endsWith("\n") || serialInputBuf.endsWith("\r") ||
        serialInputBuf.length() >= 256 ||
        (millis() - lastSerialByteTime > 500)) {
      serialInputBuf.trim();
      if (serialInputBuf.length() > 0) {
        lastActivityTime = millis();
        processCommand(serialInputBuf);
      }
      serialInputBuf = "";
    }
  }

#ifdef ENABLE_BLUETOOTH
  static String btInputBuf = "";
  static unsigned long lastBTByteTime = 0;
  if (bluetoothEnabled) {
    while (SerialBT.available()) {
      char c = SerialBT.read();
      btInputBuf += c;
      lastBTByteTime = millis();
      if (c == '\n' || c == '\r' || btInputBuf.length() >= 256)
        break;
    }

    if (btInputBuf.length() > 0) {
      // BLE sends long data as multiple 20-byte packets with gaps between them.
      // Use 1000ms timeout to ensure all packets arrive before processing.
      bool hasNewline = btInputBuf.endsWith("\n") || btInputBuf.endsWith("\r");
      bool bufferFull = btInputBuf.length() >= 256;
      bool timedOut = (millis() - lastBTByteTime > 1000);
      
      if (hasNewline || bufferFull || timedOut) {
        btInputBuf.trim();
        if (btInputBuf.length() > 0) {
          lastActivityTime = millis();
          processCommand(btInputBuf);
        }
        btInputBuf = "";
      }
    }
  }

#endif

  // ── IR 수신 처리 (IR 수신 모드에서만 활발히 동작) ───────────────────
  // [FIX] 중복 신호 필터링을 위한 정적 변수
  static uint64_t lastIRValue = 0;
  static uint32_t lastIRBits = 0;
  static uint32_t lastIRDecodeType = 0;
  static unsigned long lastIRReceiveTime = 0;

  if (currentAppMode == MODE_IR_RECEIVE ||
      (currentAppMode == MODE_IR_CLONE && isCloningWait)) {

    if (irrecv.decode(&results)) {
      // [FIX] 수신 감도 극대화: KT 리모컨 및 특수 신호 대응을 위해 최소 비트를
      // 4로 하향
      if (results.bits < 4) {
        Serial.printf("[IR DEBUG] Filtered: Too few bits (%d)\n",
                      (int)results.bits);
        irrecv.resume();
        return;
      }

      // [FIX] 데드타임 단축 (100ms)
      if (millis() - lastIRReceiveTime < 100) {
        irrecv.resume();
        return;
      }

      Serial.printf("[IR DEBUG] Raw Received! Bits: %d, Type: %d\n",
                    (int)results.bits, (int)results.decode_type);
      // 새로운 신호 처리
      String protocol = typeToString(results.decode_type);
      String value = uint64ToString(results.value, 16);
      String bits = String(results.bits);

      String info = "# IR DETECTED #\n\n";
      info += "Protocol: " + protocol + "\n";
      info += "Value: 0x" + value + "\n";
      info += "Bits: " + bits + "\n\n";
      info += "(Hold B 5s Exit)";

      updateIRRecvDisplay(info);
      Serial.printf("[IR RECV] Protocol: %s, Value: 0x%s, Bits: %s\n",
                    protocol.c_str(), value.c_str(), bits.c_str());

      soundBeep();

      lastIRValue = results.value;
      lastIRBits = results.bits;
      lastIRDecodeType = results.decode_type;
      lastIRReceiveTime = millis();

      // [NEW] IR 복제 모드인 경우 목록에 추가
      if (currentAppMode == MODE_IR_CLONE && isCloningWait) {
        // [CRITICAL FIX] 최대 항목 수 체크 - 메모리 누수 방지
        if (irCloneList.size() >= MAX_IR_CLONE_ITEMS) {
          Serial.printf("[IR CLONE WARNING] Max items (%d) reached\n",
                        MAX_IR_CLONE_ITEMS);
          soundError();
          updateIRCloneDisplay();
          irrecv.resume();
          isCloningWait = false;
          return;
        }

        IRCloneItem newItem;
        newItem.type = results.decode_type;
        newItem.value = results.value;
        newItem.bits = results.bits;

        // [CRITICAL] bits 자동 수정 (주요 리모컨 포맷만 지원)
        // 정상적인 리모컨: 16비트 (SAMSUNG), 32비트 (NEC/LG), 48비트 등
        if (newItem.bits < 8) {
          // 비트가 너무 작으면 무시
          Serial.printf("[IR CLONE] Ignoring signal with %d bits\n",
                        newItem.bits);
          irrecv.resume();
          isCloningWait = false;
          return;
        } else if (newItem.bits > 64) {
          // 비트가 너무 크면 상위 32비트만 사용 (NEC 호환)
          Serial.printf(
              "[IR CLONE] Bits too large (%d), truncating to 32-bit NEC\n",
              newItem.bits);
          newItem.bits = 32;
        }

        // [NEW] LG TV 리모컨 검증
        String lgVerifiedName = "";
        if (results.decode_type == NEC || results.decode_type == LG) {
          uint32_t val = (uint32_t)results.value;
          if (val == 0x20DF23DC)
            lgVerifiedName = "[LG] Power ON";
          else if (val == 0x20DFA35C)
            lgVerifiedName = "[LG] Power OFF";
          else if (val == 0x20DF10EF)
            lgVerifiedName = "[LG] Power Toggle";
          else if (val == 0x20DF40BF)
            lgVerifiedName = "[LG] Vol UP";
          else if (val == 0x20DFC03F)
            lgVerifiedName = "[LG] Vol DOWN";
          else if (val == 0x20DF00FF)
            lgVerifiedName = "[LG] CH UP";
          else if (val == 0x20DF807F)
            lgVerifiedName = "[LG] CH DOWN";
        }

        if (lgVerifiedName != "") {
          newItem.name = lgVerifiedName;
          Serial.println("[IR CLONE VERIFIED] " + lgVerifiedName);
        } else {
          // 이름 설정 (프로토콜 + HEX)
          newItem.name = typeToString(results.decode_type) + "_0x" +
                         uint64ToString(results.value, 16);
        }

        irCloneList.push_back(newItem);
        isCloningWait = false;

        updateIRCloneDisplay();
        Serial.printf("[IR CLONE] Saved: %s (Bits: %d) [%d/%d]\n",
                      newItem.name.c_str(), (int)newItem.bits,
                      (int)irCloneList.size(), MAX_IR_CLONE_ITEMS);

        irrecv.disableIRIn();
        return;
      }

      irrecv.resume();
    }
  } else {
    // IR 수신 모드가 아닐 때는 수신기가 꺼져 있으므로 아무 처리도 하지 않음
  }

  // ── 자동 단어 넘기기 (단어장 모드 및 클라우드 모드) ──────────────────────────
  bool canAutoTransition = false;
  if (autoTransitionEnabled) {
      if (currentAppMode == MODE_WORDBOOK && wordbook.getWordCount() > 0) canAutoTransition = true;
      if (currentAppMode == MODE_CLOUD_WEB) {
          portENTER_CRITICAL(&cloudMutex);
          int sz = cloudWordsList.size();
          portEXIT_CRITICAL(&cloudMutex);
          if (sz > 0) canAutoTransition = true;
      }
  }

  if (canAutoTransition) {
    lastActivityTime = millis(); // [NEW] 자동 넘김 모드 중에는 슬립 모드 진입 방지
    if (millis() - lastTransitionTime >= (unsigned long)autoTransitionInterval) {
      Serial.println("=== AUTO TRANSITION ===");
      if (currentAppMode == MODE_WORDBOOK) {
        navigateButtonADirection();
      } else {
        portENTER_CRITICAL(&cloudMutex);
        int sz = cloudWordsList.size();
        portEXIT_CRITICAL(&cloudMutex);
        if (sz > 0) {
          String displayStr = getNextText(true, btnADirectionForward);
          
          updateWordDisplay(displayStr);
          if (btnADirectionForward) soundNext(); else soundPrev();
          Serial.println("[CLOUD] Auto Loaded: " + displayStr);
        }
      }
      lastTransitionTime = millis();
    }
  }

  // ── 최대 절전 모드 (Light Sleep) ──────────────────────
  unsigned long currentTime = millis();
  unsigned long timeSinceLastActivity = currentTime - lastActivityTime;
  unsigned long slimTimeout = (unsigned long)slimModeTime * 60 * 1000;

  // ── 지능형 자동 디밍 (15초 무활동 시 화면 어두워짐) ──
  bool allowDimming = true;
  if (currentAppMode == MODE_GAME || currentAppMode == MODE_RUNNER_GAME ||
      currentAppMode == MODE_SECRET_7 || currentAppMode == MODE_SECRET_8 ||
      currentAppMode == MODE_SECRET_9 || currentAppMode == MODE_SECRET_10 ||
      currentAppMode == MODE_BT_WALKIE) {
    allowDimming = false;
  }
  if (autoTransitionEnabled && currentAppMode == MODE_WORDBOOK) {
    allowDimming = false;
  }

  // ── 지능형 자동 디밍 (설정된 DimTime 및 MinBri 연동) ──
  bool isDeviceCharging = M5.Power.isCharging();
  unsigned long dimThreshold = (dimTimeSec > 0) ? ((unsigned long)dimTimeSec * 1000) : 0;
  if (dimTimeSec > 0 && allowDimming && screenOn && !isDimmed && timeSinceLastActivity >= dimThreshold) {
    isDimmed = true;
    preDimBrightness = current_brightness;
    uint8_t targetDim = min_brightness;
    if (isDeviceCharging && targetDim > 2) {
      targetDim = 2; // 충전 중에는 충전 전류 집중을 위해 최대 2로 낮춤
    }
    M5.Display.setBrightness(targetDim);
    Serial.printf("[POWER] Screen dimmed (charge=%d, bright=%d, threshold=%lus)\n",
                  isDeviceCharging, targetDim, dimThreshold / 1000);
  }

  if (timeSinceLastActivity < 1000 && !screenOn) {
    M5.Display.wakeup();
    set_backlight(true);
    screenOn = true;
  }
  // ── 지능형 슬림 모드 (Light Sleep - 오늘자 버전) ────────
  // 블루투스가 연결된 상태에서는 절전 모드로 진입하지 않도록 방어 로직 포함
  if ((forceSleepNow || (slimModeTime > 0 && timeSinceLastActivity > slimTimeout)) && screenOn &&
      !SerialBT.connected()) {
    forceSleepNow = false;

    Serial.println("[SLEEP] Entering Light Sleep mode.");

    // 학습 상태 저장
    wordbook.saveCurrentIndex();

    // [NEW] 디밍 상태 해제 (깨어났을 때 원래 밝기로 복구하기 위함)
    isDimmed = false;

    // 버튼이 눌려있으면 뗄 때까지 대기 (실수 진입 방지)
    while (M5.BtnA.isPressed() || M5.BtnB.isPressed() || M5.BtnPWR.isPressed()) {
      M5.update();
      delay(10);
    }

    // 화면 백라이트 끄기 및 디스플레이 컨트롤러 슬립 진입
    set_backlight(false);
    M5.Display.sleep(); // [OPTIMIZE] ST7789 LCD 드라이버 슬립 모드 (패널 대기 전류 차단)
    screenOn = false;
    lv_timer_handler();
    delay(50);

    // [OPTIMIZE] 슬림 모드 대기 중 전력 소모를 최저로 낮추기 위해 주변장치 전원 완전 차단
    M5.Power.setExtOutput(false); // 5V Boost 승압 회로 차단
    setAmplifier(false);          // 앰프 차단
    WiFi.mode(WIFI_OFF);          // WiFi 모뎀 완전 차단
    digitalWrite(10, HIGH);       // [OPTIMIZE] 슬립 중 LED 소등 (전류 누수 차단)
    setSystemClock(80);           // CPU 주파수 80MHz 유지

    // [S3 전용] 버튼 GPIO 핀 (M5StickC Plus S3 규격)
    pinMode(1, INPUT_PULLUP);
    pinMode(2, INPUT_PULLUP);
    gpio_wakeup_enable((gpio_num_t)1, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable((gpio_num_t)2, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();

    // Light sleep 진입
    esp_light_sleep_start();

    // 깨어난 직후 처리
    M5.update();                 // 깨어나자마자 버튼 상태 갱신
    lastActivityTime = millis(); // 타이머 초기화
    M5.Display.wakeup();         // [OPTIMIZE] ST7789 LCD 드라이버 복구
    set_backlight(true);
    screenOn = true;
    Serial.println("[SLEEP] Woke up from Light Sleep!");
  }

  // ── 배터리 모니터링 (M5Unified Power API 사용) ────────
  static int lastBatteryPercentage = -1;
  static bool lastChargingState = false;
  static unsigned long lastBatteryUpdate = 0;
  static bool firstBatteryCheck = true;

  // 5초마다 배터리(ADC) 업데이트 (첫 번째는 즉시 실행)
  if (firstBatteryCheck || millis() - lastBatteryUpdate >= 5000) {
    firstBatteryCheck = false;

    // [S3 적용] M5Unified API로 전압을 가져와 커스텀 기준으로 백분율 계산
    int32_t batteryVoltage = M5.Power.getBatteryVoltage(); // mV 단위
    float batteryV = batteryVoltage / 1000.0f;
    int batteryLevel = getBatteryPercentage(batteryV); // 수정된 기준 적용
    bool isCharging = M5.Power.isCharging(); // 충전 상태 확인

    lv_bar_set_value(ui_Bar1, batteryLevel, LV_ANIM_OFF);
    lv_label_set_text(ui_Label4, (String(batteryLevel) + "%").c_str());

    // [FAST CHARGE] 평상시 5V BOOST(ExtOutput)는 상시 OFF 유지!
    // 충전 중에는 앰프/주변장치를 완전 차단하여 충전 전류가 배터리로 100% 집중되도록 함
    static bool wasChargingLastCycle = false;
    if (isCharging && !wasChargingLastCycle) {
      M5.Power.setExtOutput(false);
      setAmplifier(false);
      Serial.println("[FAST CHARGE] Charging started! Power rails minimized for maximum charge speed.");
    } else if (!isCharging && wasChargingLastCycle) {
      M5.Power.setExtOutput(false); // 케이블 분리되어도 5V BOOST는 OFF 유지 (배터리 보존)
      Serial.println("[FAST CHARGE] Disconnected from charger.");
    }
    wasChargingLastCycle = isCharging;

    if (isCharging) {
      lv_obj_set_style_bg_color(ui_Bar1, lv_color_hex(0xFF6B6B), LV_PART_INDICATOR | LV_STATE_DEFAULT);
      lv_obj_set_style_text_color(ui_Label4, lv_color_hex(0xFF6B6B), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_text_color(ui_Label3, lv_color_hex(0xFF6B6B), LV_PART_MAIN | LV_STATE_DEFAULT);
    } else {
      lv_obj_set_style_bg_color(ui_Bar1, lv_color_hex(0xB0B0B0), LV_PART_INDICATOR | LV_STATE_DEFAULT);
      lv_obj_set_style_text_color(ui_Label4, lv_color_hex(0xB0B0B0), LV_PART_MAIN | LV_STATE_DEFAULT);
      lv_obj_set_style_text_color(ui_Label3, lv_color_hex(0xB0B0B0), LV_PART_MAIN | LV_STATE_DEFAULT);
    }

    if (abs(batteryLevel - lastBatteryPercentage) >= 1 || isCharging != lastChargingState) {
      lv_label_set_text(ui_Label3, (String(batteryV, 2) + "V").c_str());
      lastBatteryPercentage = batteryLevel;
      lastChargingState = isCharging;
      Serial.printf("Battery: %.2fV (%d%%)%s\n", batteryV, batteryLevel, isCharging ? " [Charging]" : "");
    }
    lastBatteryUpdate = millis();
  }

  // ── BT 연결 상태 표시 (5초마다) ──────────────────────
#ifdef ENABLE_BLUETOOTH
  static unsigned long lastBTCheck = 0;
  if (millis() - lastBTCheck > 5000) {
    if (ui_Label5) {
      lv_obj_set_style_bg_opa(ui_Label5, LV_OPA_COVER,
                              LV_PART_MAIN | LV_STATE_DEFAULT);
      if (!bluetoothEnabled)
        lv_obj_set_style_bg_color(ui_Label5, lv_color_hex(0x808080),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
      else if (SerialBT.connected())
        lv_obj_set_style_bg_color(ui_Label5, lv_color_hex(0x00FF00),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
      else
        lv_obj_set_style_bg_color(ui_Label5, lv_color_hex(0x002EFF),
                                  LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lastBTCheck = millis();
  }
#endif

  if (currentAppMode == MODE_WIFI_WEB) {
    handleWiFiWebManager();
    // lastWebAction 변경 감지 시 즉시 갱신
    static String prevWebAction = "";
    if (prevWebAction != lastWebAction) {
      updateWiFiWebDisplay();
      prevWebAction = lastWebAction;
    }
    // AP 모드에서는 2초마다 주기적 갱신 (클라이언트 접속 수 등 반영)
    static unsigned long lastWiFiDisplayRefresh = 0;
    if (isWiFiAPMode() && millis() - lastWiFiDisplayRefresh > 2000) {
      updateWiFiWebDisplay();
      lastWiFiDisplayRefresh = millis();
    }
  }

  // [NEW] MODE_WIFI_HOTSPOT: WiFi 핫스팟 모드 처리
  if (currentAppMode == MODE_WIFI_HOTSPOT) {
    handleWiFiWebManager();
    // lastWebAction 변경 감지 시 즉시 갱신
    static String prevWebActionHotspot = "";
    if (prevWebActionHotspot != lastWebAction) {
      updateWiFiWebDisplay();
      prevWebActionHotspot = lastWebAction;
    }
    // 2초마다 주기적 갱신 (연결된 클라이언트 수 표시)
    static unsigned long lastHotspotDisplayRefresh = 0;
    if (millis() - lastHotspotDisplayRefresh > 2000) {
      updateWiFiWebDisplay();
      lastHotspotDisplayRefresh = millis();
    }
  }



  lv_timer_handler();
  
  // ── [NEW] 모든 모드에서 버튼 눌림 시각적 표시 (Spinner2 중앙 ui_BtnIndicator) ──
  static bool btnA_prev = false;
  static bool btnB_prev = false;
  bool btnA_curr = M5.BtnA.isPressed();
  bool btnB_curr = M5.BtnB.isPressed();

  if (ui_BtnIndicator) {
    if ((btnA_curr != btnA_prev) || (btnB_curr != btnB_prev)) {
      if (btnA_curr && btnB_curr) {
        lv_obj_set_style_bg_color(ui_BtnIndicator, lv_color_hex(0xFFFF00), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_BtnIndicator, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else if (btnA_curr && !btnB_curr) {
        lv_obj_set_style_bg_color(ui_BtnIndicator, lv_color_hex(0xFF0000), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_BtnIndicator, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else if (!btnA_curr && btnB_curr) {
        lv_obj_set_style_bg_color(ui_BtnIndicator, lv_color_hex(0x0000FF), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_BtnIndicator, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
      } else {
        lv_obj_set_style_bg_color(ui_BtnIndicator, lv_color_hex(0x535353), LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_bg_opa(ui_BtnIndicator, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
      }
    }
  }
  btnA_prev = btnA_curr;
  btnB_prev = btnB_curr;

  // [OPTIMIZE] 동적 루프 딜레이: 화면 무활동 디밍 또는 화면 OFF 시에만 80ms로 전력 절감
  // 사용자가 A+B로 수동 최소 밝기를 켜고 조작 중일 때(!isManualMinBright)는 30ms 풀성능 유지!
  bool isIdleLowPower = (!screenOn || (isDimmed && !isManualMinBright));
  uint32_t loopDelay = isIdleLowPower ? 80 : 30;
  delay(loopDelay);

  // ── BT Config 설정 고속 변경 후 지연 저장 ──────────────────────────
  if ((pendingNameSave || pendingBTConfigSave) && !M5.BtnA.isPressed()) {
    saveSettings();
    pendingNameSave = false;
    pendingBTConfigSave = false;
    Serial.println("[BT CONFIG] Saved settings after fast increment");
  }

  // ── [FIX] 지연된 사운드 설정 저장 + M5PM1 앰프 제어 + 확인음 ──
  // 버튼 인터럽트, IR수신, 화면 갱신이 끝난 가장 안전한 시점에서 실행
  if (pendingSoundSave && !M5.BtnA.isPressed() && !M5.BtnB.isPressed()) {
    uint32_t prevFreq = currentCpuFreq;
    setSystemClock(240); // 톤 재생 시 클럭 깨짐 방지를 위해 240MHz로 일시 승격

    Preferences pref;
    pref.begin("settings", false);
    pref.putBool("sound", soundEnabled);

    if (soundEnabled) {
      // [핵심] 사운드가 ON될 때 M5PM1 PYG3 핀으로 앰프 활성화
      // M5.Speaker.begin() 제거: OS 스레드 재생성 시 프리징되는 버그 완벽 방지
      setAmplifier(true);
      int savedVol = pref.getInt("vol", 30);
      if (savedVol > 160)
        savedVol = 160;
      M5.Speaker.setVolume(savedVol);
      delay(50); // 앰프 켜질 시간

      // 확인음 (끊김 현상 방지를 위해 정확한 길이만큼만 대기)
      M5.Speaker.tone(1760, 100);
      delay(100);
      M5.Speaker.tone(2637, 150);
      delay(150);
      setAmplifier(false); // [OPTIMIZE] 확인음 재생 완료 후 앰프 대기 차단
    } else {
      // [핵심] 사운드가 OFF될 때 M5PM1 PYG3 핀으로 앰프 완전 차단
      setAmplifier(false);
      delay(50);
    }
    pref.end();
    Serial.printf("[SOUND] Saved to NVS. M5PM1 Amp: %s (Standby OFF)\n",
                  soundEnabled ? "ON" : "OFF");

    pendingSoundSave = false;
    setSystemClock(prevFreq); // 재생 완료 후 원래 주파수로 복구
  }

  // ── 30초마다 디버그 출력 ──────────────────────────────
  static unsigned long lastDebugPrint = 0;
  if (millis() - lastDebugPrint > 30000) {
    Serial.printf("[DEBUG] words=%d screen=%s inactive=%lus\n",
                  wordbook.getWordCount(), screenOn ? "ON" : "OFF",
                  timeSinceLastActivity / 1000);
    lastDebugPrint = millis();
  }
}