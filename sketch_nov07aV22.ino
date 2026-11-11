#include <SPI.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <HardwareSerial.h>
#include <Adafruit_Fingerprint.h>
#include <Adafruit_MCP23X17.h>

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

// =====================================
// WIFI SETTINGS
// =====================================
const char* WIFI_SSID = "IPS-Library_2.4G";
const char* WIFI_PASSWORD = "042515888";

const char* GOOGLE_SCRIPT_URL =
  "https://script.google.com/macros/s/AKfycbxDRWRKT7ZTsYqJC8ysXZgy7qADFCDZidnacem6X1rO4_NuAqYeaTHvyNcpo-6InsAqRw/exec";

bool wifiReady = false;
bool googleSheetsReady = false;

// =====================================
// DISPLAY AND TOUCH
// =====================================
TFT_eSPI tft = TFT_eSPI();

#define TOUCH_CS 16
XPT2046_Touchscreen touch(TOUCH_CS);

// =====================================
// AS608 FINGERPRINT
// =====================================
#define AS608_RX_PIN 32
#define AS608_TX_PIN 33

HardwareSerial fingerSerial(2);
Adafruit_Fingerprint finger(&fingerSerial);

Preferences preferences;

bool fingerprintReady = false;
bool fingerprintModelReady = false;

uint16_t enrolledFingerID = 0;
uint16_t pendingFingerID = 0;

int pendingMemberSlot = -1;

String pendingMemberID = "";
String pendingMemberName = "";
String pendingMemberPhone = "";

// =====================================
// BUZZER & MOTOR & LIMIT SWITCH PINS
// =====================================
#define BUZZER_PIN       25   // ขา Buzzer (พิน 25)
#define MOTOR_IN1        26
#define MOTOR_IN2        27
#define LIMIT_LEFT_PIN   13   // ลิมิตสวิตช์ฝั่งซ้าย (เปิด)
#define LIMIT_RIGHT_PIN  12   // ลิมิตสวิตช์ฝั่งขวา (ปิด)

// =====================================
// MCP23017
// =====================================
#define MCP_SDA 21
#define MCP_SCL 22
#define MCP_ADDRESS 0x20

Adafruit_MCP23X17 mcp;

bool mcpReady = false;
bool keyPresent[10];

// =====================================
// SCREEN
// =====================================
#define SCREEN_W 320
#define SCREEN_H 480

// =====================================
// TOUCH CALIBRATION
// =====================================
const float A = -0.08557635;
const float B = -0.00098885;
const float C = 338.057385;

const float D =  0.00063175;
const float E = -0.13001551;
const float F = 507.442665;

// =====================================
// COLORS
// =====================================
#define BG       0x1082
#define HEADER   0x2124
#define BLUE     0x219F
#define GREEN    0x05C8
#define RED      0xF800
#define PURPLE   0x801F
#define WHITE    TFT_WHITE
#define YELLOW   TFT_YELLOW
#define GRAY     TFT_DARKGREY
#define BLACK    TFT_BLACK

// =====================================
// SYSTEM SETTINGS
// =====================================
#define MAX_KEYS 10

struct KeyRecord {
  bool used;
  String memberID;
  String name;
  String phone;
  uint16_t fingerID;
};

KeyRecord keyData[MAX_KEYS];
String currentBorrowerName[MAX_KEYS];

// =====================================
// PAGE CONTROL
// =====================================
enum Page {
  PAGE_HOME,
  PAGE_KEY,
  PAGE_ADD,
  PAGE_REMOVE,
  PAGE_ENROLL,
  PAGE_SUCCESS,
  PAGE_ERROR
};

Page currentPage = PAGE_HOME;

// =====================================
// ADD FORM
// =====================================
String inputName = "";
String inputPhone = "";

enum InputField {
  FIELD_NAME,
  FIELD_PHONE
};

InputField selectedField = FIELD_NAME;

// =====================================
// REMOVE POPUP
// =====================================
bool showDeletePopup = false;
int pendingRemove = -1;

// =====================================
// KEYBOARD
// =====================================
const char keyboardRows[6][7] = {
  "ABCDEF",
  "GHIJKL",
  "MNOPQR",
  "STUVWX",
  "YZ0123",
  "456789"
};

const int KEY_X = 10;
const int KEY_Y = 160;
const int KEY_W = 47;
const int KEY_H = 33;
const int KEY_GAP = 5;

// =====================================
// STATUS
// =====================================
String enrollStatus = "System ready";

// =====================================
// TOUCH CONVERSION
// =====================================
void convertTouch(int rawX, int rawY, int &screenX, int &screenY) {
  float x = A * rawX + B * rawY + C;
  float y = D * rawX + E * rawY + F;
  screenX = constrain((int)round(x), 0, SCREEN_W - 1);
  screenY = constrain((int)round(y), 0, SCREEN_H - 1);
}

// =====================================
// WIFI CONNECTION & AUTO-RECONNECT
// =====================================
void connectWiFi() {
  Serial.println("\n[WIFI] Connecting to WiFi...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startTime < 15000) {
    delay(500);
    Serial.print(".");
  }

  wifiReady = (WiFi.status() == WL_CONNECTED);
  if (wifiReady) {
    Serial.println("\n[WIFI] CONNECTED SUCCESSFULLY!");
    Serial.print("[WIFI] IP ADDRESS: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n[WIFI ERROR] CONNECTION FAILED TIMEOUT!");
  }
}

void checkWiFiAutoReconnect() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WIFI WARNING] Connection lost! Reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startAttemptTime = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
      delay(500);
      Serial.print(".");
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n[WIFI] Reconnected successfully!");
      wifiReady = true;
    } else {
      Serial.println("\n[WIFI ERROR] Reconnect failed! Will retry later.");
      wifiReady = false;
    }
  } else {
    wifiReady = true;
  }
}

// =====================================
// HELPER: URL ENCODE
// =====================================
String urlEncode(String str) {
  String encoded = "";
  char c;
  char code0;
  char code1;
  for (int i = 0; i < str.length(); i++) {
    c = str.charAt(i);
    if (isalnum(c)) {
      encoded += c;
    } else {
      code1 = (c & 0xf) + '0';
      if ((c & 0xf) > 9) code1 = (c & 0xf) - 10 + 'A';
      c = (c >> 4) & 0xf;
      code0 = c + '0';
      if (c > 9) code0 = c - 10 + 'A';
      encoded += '%';
      encoded += code0;
      encoded += code1;
    }
  }
  return encoded;
}

// =====================================
// GOOGLE SHEETS REQUEST (GET MODE - FIXED 405 REDIRECT)
// =====================================
bool sendGoogleRequest(const String &payload) {
  Serial.println("\n--- [GOOGLE REQUEST START (GET MODE)] ---");
  Serial.print("[PAYLOAD SEND]: ");
  Serial.println(payload);

  checkWiFiAutoReconnect();
  if (WiFi.status() != WL_CONNECTED) {
    wifiReady = false;
    Serial.println("[HTTP ERROR] WiFi not connected.");
    return false;
  }
  wifiReady = true;

  String encodedPayload = urlEncode(payload);
  String fullUrl = String(GOOGLE_SCRIPT_URL) + "?data=" + encodedPayload;

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(20000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

  if (!http.begin(client, fullUrl)) {
    Serial.println("[HTTP ERROR] http.begin() failed.");
    return false;
  }

  int statusCode = http.GET();
  String response = http.getString();
  
  Serial.print("[HTTP STATUS CODE]: ");
  Serial.println(statusCode);
  Serial.print("[HTTP RESPONSE BODY]: ");
  Serial.println(response);

  bool success = false;
  if (statusCode >= 200 && statusCode < 300) {
    DynamicJsonDocument doc(2048);
    DeserializationError err = deserializeJson(doc, response);
    if (err) {
      Serial.print("[JSON ERROR] Parse failed: ");
      Serial.println(err.c_str());
    } else {
      success = doc["success"] | false;
      Serial.print("[JSON PARSE SUCCESS]: ");
      Serial.println(success ? "TRUE" : "FALSE");
    }
  } else {
    Serial.println("[HTTP ERROR] Status code out of 2xx range.");
  }

  http.end();
  googleSheetsReady = success;
  Serial.println("--- [GOOGLE REQUEST END] ---\n");
  return success;
}

bool testGoogleSheets() {
  Serial.println("[PING] Testing Google Sheets connection...");
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[PING ERROR] WiFi disconnected.");
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String(GOOGLE_SCRIPT_URL) + "?action=ping";

  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.setTimeout(15000);

  if (!http.begin(client, url)) {
    Serial.println("[PING ERROR] http.begin() failed.");
    return false;
  }

  int code = http.GET();
  String response = http.getString();
  Serial.print("[PING STATUS CODE]: ");
  Serial.println(code);
  Serial.print("[PING RESPONSE]: ");
  Serial.println(response);

  bool success = false;
  if (code >= 200 && code < 300) {
    DynamicJsonDocument doc(1024);
    if (!deserializeJson(doc, response)) {
      success = doc["success"] | false;
    }
  }

  http.end();
  googleSheetsReady = success;
  Serial.println(success ? "[PING] Google Sheets Connected Successfully!" : "[PING] Google Sheets Ping Failed!");
  return success;
}

String generateMemberID(int slot) {
  String id = "M";
  id += String((unsigned long)millis());
  id += "_";
  id += String(slot + 1);
  return id;
}

bool logHistory(String memberID, String name, String keyID, String eventName) {
  Serial.print("[LOG HISTORY] Event: ");
  Serial.print(eventName);
  Serial.print(" | Member: ");
  Serial.println(name);
  
  DynamicJsonDocument doc(512);
  doc["action"] = "logHistory";
  doc["memberID"] = memberID;
  doc["name"] = name;
  doc["keyID"] = keyID;
  doc["event"] = eventName;

  String payload;
  serializeJson(doc, payload);
  return sendGoogleRequest(payload);
}

// =====================================
// SAVE & LOAD MEMBERS TO FLASH
// =====================================
void saveMembersToFlash() {
  Serial.println("[FLASH] Saving members to local Flash memory...");
  Preferences prefs;
  prefs.begin("members", false);
  for (int i = 0; i < MAX_KEYS; i++) {
    String keyPrefix = "m" + String(i) + "_";
    prefs.putBool((keyPrefix + "used").c_str(), keyData[i].used);
    if (keyData[i].used) {
      prefs.putString((keyPrefix + "id").c_str(), keyData[i].memberID);
      prefs.putString((keyPrefix + "name").c_str(), keyData[i].name);
      prefs.putString((keyPrefix + "phone").c_str(), keyData[i].phone);
      prefs.putUShort((keyPrefix + "fid").c_str(), keyData[i].fingerID);
    }
  }
  prefs.end();
  Serial.println("[FLASH] Save completed.");
}

void loadMembersFromFlash() {
  Serial.println("[FLASH] Loading members from local Flash memory...");
  Preferences prefs;
  prefs.begin("members", true);
  for (int i = 0; i < MAX_KEYS; i++) {
    String keyPrefix = "m" + String(i) + "_";
    keyData[i].used = prefs.getBool((keyPrefix + "used").c_str(), false);
    if (keyData[i].used) {
      keyData[i].memberID = prefs.getString((keyPrefix + "id").c_str(), "");
      keyData[i].name = prefs.getString((keyPrefix + "name").c_str(), "");
      keyData[i].phone = prefs.getString((keyPrefix + "phone").c_str(), "");
      keyData[i].fingerID = prefs.getUShort((keyPrefix + "fid").c_str(), 0);
      Serial.print(" - Slot ");
      Serial.print(i);
      Serial.print(": Loaded Name = ");
      Serial.println(keyData[i].name);
    } else {
      keyData[i].memberID = "";
      keyData[i].name = "";
      keyData[i].phone = "";
      keyData[i].fingerID = 0;
    }
  }
  prefs.end();
  Serial.println("[FLASH] Load completed.");
}

// =====================================
// MOTOR & BUZZER
// =====================================
#define BUZZer_CHANNEL 0

void initBuzzer() {
  ledcSetup(BUZZer_CHANNEL, 2000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZer_CHANNEL);
  ledcWrite(BUZZer_CHANNEL, 0);
}

void playTone(uint32_t frequency, uint32_t duration) {
  ledcSetup(BUZZer_CHANNEL, frequency, 8);
  ledcAttachPin(BUZZER_PIN, BUZZer_CHANNEL);
  ledcWrite(BUZZer_CHANNEL, 128);
  delay(duration);
  ledcWrite(BUZZer_CHANNEL, 0);
}

void soundBuzzer() { playTone(2600, 200); }
void soundSystemReady() { playTone(2000, 100); delay(150); playTone(2500, 150); }
void soundFingerSuccess() { playTone(3000, 300); }

void motorStop() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, LOW);
}

void motorBrake(int brakeDelayTime) {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, HIGH); 
  delay(brakeDelayTime);         
  motorStop();                   
}

void motorOpenLeft() {
  digitalWrite(MOTOR_IN1, HIGH);
  digitalWrite(MOTOR_IN2, LOW);
}

void motorCloseRight() {
  digitalWrite(MOTOR_IN1, LOW);
  digitalWrite(MOTOR_IN2, HIGH);
}

void runDoorCycle() {
  Serial.println("[MOTOR] Opening door (Moving Left)...");
  motorOpenLeft();
  unsigned long startTime = millis();
  while (digitalRead(LIMIT_LEFT_PIN) == HIGH) {
    if (millis() - startTime > 15000) {
      Serial.println("[MOTOR WARNING] Limit left timeout!");
      break;
    }
    delay(5);
  }
  
  motorBrake(100);
  Serial.println("[MOTOR] Door opened. Holding for 4 seconds...");
  delay(4000);

  Serial.println("[MOTOR] Closing door (Moving Right)...");
  motorCloseRight();
  startTime = millis();
  while (digitalRead(LIMIT_RIGHT_PIN) == HIGH) {
    if (millis() - startTime > 15000) {
      Serial.println("[MOTOR WARNING] Limit right timeout!");
      break;
    }
    delay(5);
  }
  
  motorBrake(300);
  Serial.println("[MOTOR] Door cycle completed.");
}

// =====================================
// INITIALIZE MCP23017 & AS608
// =====================================
bool initializeMCP23017() {
  Wire.begin(MCP_SDA, MCP_SCL);
  if (!mcp.begin_I2C(MCP_ADDRESS)) {
    Serial.println("[MCP23017 ERROR] Failed to find MCP23017 chip!");
    return false;
  }
  Serial.println("[MCP23017] Initialized successfully.");
  for (int i = 0; i < MAX_KEYS; i++) {
    mcp.pinMode(i, INPUT_PULLUP);
    keyPresent[i] = false;
    currentBorrowerName[i] = "";
  }
  return true;
}

void readKeyStatus() {
  if (!mcpReady) return;
  for (int i = 0; i < MAX_KEYS; i++) {
    bool firstRead = (mcp.digitalRead(i) == LOW);
    delayMicroseconds(30000); 
    bool secondRead = (mcp.digitalRead(i) == LOW);
    if (firstRead == secondRead) {
      keyPresent[i] = firstRead;
    }
  }
}

bool initializeFingerprint() {
  Serial.println("[FINGERPRINT] Initializing AS608 sensor...");
  fingerSerial.begin(57600, SERIAL_8N1, AS608_RX_PIN, AS608_TX_PIN);
  delay(500);
  finger.begin(57600);
  delay(500);

  if (!finger.verifyPassword()) {
    Serial.println("[FINGERPRINT ERROR] Did not find fingerprint sensor!");
    return false;
  }
  finger.getParameters();
  Serial.print("[FINGERPRINT] Sensor connected. Capacity: ");
  Serial.println(finger.capacity);
  return finger.capacity > 0;
}

uint16_t findAvailableFingerID() {
  if (!fingerprintReady) return 0;
  preferences.begin("smartkey", false);
  uint16_t nextID = preferences.getUShort("nextID", 1);
  preferences.end();
  Serial.print("[FINGERPRINT] Next available Finger ID: ");
  Serial.println(nextID);
  if (nextID < 1 || nextID > finger.capacity) return 0;
  return nextID;
}

void advanceFingerID(uint16_t savedID) {
  uint16_t nextID = savedID + 1;
  preferences.begin("smartkey", false);
  preferences.putUShort("nextID", nextID);
  preferences.end();
}

bool waitForFingerImage(const String &title, const String &message) {
  drawEnrollMessage(title, message);
  unsigned long startTime = millis();
  while (millis() - startTime < 20000) {
    uint8_t p = finger.getImage();
    if (p == FINGERPRINT_OK) {
      Serial.println("[FINGERPRINT] Image captured successfully.");
      return true;
    }
    delay(100);
  }
  Serial.println("[FINGERPRINT ERROR] Image capture timeout.");
  return false;
}

bool waitForFingerRemoval() {
  drawEnrollMessage("LIFT YOUR FINGER", "Remove finger from sensor");
  unsigned long startTime = millis();
  while (millis() - startTime < 15000) {
    if (finger.getImage() == FINGERPRINT_NOFINGER) {
      Serial.println("[FINGERPRINT] Finger removed.");
      return true;
    }
    delay(100);
  }
  Serial.println("[FINGERPRINT ERROR] Finger removal timeout.");
  return false;
}

bool scanFingerprintTwice() {
  Serial.println("\n--- [ENROLL START] ---");
  fingerprintModelReady = false;
  pendingFingerID = 0;

  if (!fingerprintReady) {
    enrollStatus = "AS608 not connected";
    Serial.println("[ENROLL ERROR] AS608 not ready.");
    return false;
  }

  uint16_t freeID = findAvailableFingerID();
  if (freeID == 0) {
    enrollStatus = "No free fingerprint ID";
    Serial.println("[ENROLL ERROR] No free fingerprint ID.");
    return false;
  }

  pendingFingerID = freeID;

  if (!waitForFingerImage("FIRST SCAN", "Place your finger")) {
    enrollStatus = "First scan timeout";
    return false;
  }

  if (finger.image2Tz(1) != FINGERPRINT_OK) {
    enrollStatus = "First image conversion failed";
    Serial.println("[ENROLL ERROR] First image2Tz failed.");
    return false;
  }

  if (finger.fingerFastSearch() == FINGERPRINT_OK) {
    enrollStatus = "Fingerprint already registered";
    Serial.println("[ENROLL ERROR] Fingerprint already exists in database.");
    return false;
  }

  if (!waitForFingerRemoval()) {
    enrollStatus = "Please lift your finger";
    return false;
  }

  delay(500);

  if (!waitForFingerImage("SECOND SCAN", "Place same finger again")) {
    enrollStatus = "Second scan timeout";
    return false;
  }

  if (finger.image2Tz(2) != FINGERPRINT_OK) {
    enrollStatus = "Second image conversion failed";
    Serial.println("[ENROLL ERROR] Second image2Tz failed.");
    return false;
  }

  if (finger.createModel() != FINGERPRINT_OK) {
    enrollStatus = "Fingerprints do not match";
    Serial.println("[ENROLL ERROR] Fingerprints do not match.");
    return false;
  }

  fingerprintModelReady = true;
  Serial.println("[ENROLL] Fingerprint scanned and matched successfully twice.");
  return true;
}

// =====================================
// UI DRAWING FUNCTIONS
// =====================================
void drawHeader(const char* title) {
  tft.fillRect(0, 0, 320, 55, HEADER);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(WHITE, HEADER);
  tft.drawString(title, 160, 27, 4);
}

void drawBackButton() {
  tft.fillRoundRect(10, 428, 100, 40, 8, GRAY);
  tft.drawRoundRect(10, 428, 100, 40, 8, WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(WHITE, GRAY);
  tft.drawString("< BACK", 60, 448, 2);
}

void drawHome() {
  tft.fillScreen(BG);
  drawHeader("KEY SYSTEM");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(YELLOW, BG);
  tft.drawString("MAIN MENU", 160, 82, 2);

  tft.fillRoundRect(25, 115, 270, 85, 12, BLUE);
  tft.drawRoundRect(25, 115, 270, 85, 12, WHITE);
  tft.setTextColor(WHITE, BLUE);
  tft.drawString("KEY", 160, 158, 4);

  tft.fillRoundRect(25, 220, 270, 85, 12, GREEN);
  tft.drawRoundRect(25, 220, 270, 85, 12, WHITE);
  tft.setTextColor(WHITE, GREEN);
  tft.drawString("+ ADD", 160, 263, 4);

  tft.fillRoundRect(25, 325, 270, 85, 12, RED);
  tft.drawRoundRect(25, 325, 270, 85, 12, WHITE);
  tft.setTextColor(WHITE, RED);
  tft.drawString("REMOVE", 160, 368, 4);

  tft.setTextColor(TFT_LIGHTGREY, BG);
  tft.drawString(wifiReady ? "WIFI CONNECTED" : "WIFI OFFLINE", 160, 450, 2);
}

void drawKeyPage() {
  tft.fillScreen(BG);
  drawHeader("KEY STATUS");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, BG);
  tft.drawString("GREEN = ON HOOK", 160, 78, 2);
  tft.setTextColor(RED, BG);
  tft.drawString("RED = REMOVED", 160, 102, 2);

  if (!mcpReady) {
    tft.setTextColor(RED, BG);
    tft.drawString("MCP23017 NOT FOUND", 160, 140, 2);
    drawBackButton();
    return;
  }

  readKeyStatus();

  for (int i = 0; i < MAX_KEYS; i++) {
    int col = i % 5;
    int row = i / 5;
    int cx = 35 + col * 63;
    int cy = 175 + row * 125;
    uint16_t color = keyPresent[i] ? TFT_GREEN : RED;

    tft.fillCircle(cx, cy, 22, color);
    tft.drawCircle(cx, cy, 23, WHITE);
    tft.setTextColor(BLACK, color);

    char number[5];
    sprintf(number, "%02d", i + 1);
    tft.drawString(number, cx, cy, 2);

    tft.setTextColor(WHITE, BG);
    String customKeyName = "";
    if (i == 0)      customKeyName = "COM 1";
    else if (i == 1) customKeyName = "COM 2";
    else if (i == 2) customKeyName = "COM 3";
    else if (i == 3) customKeyName = "THEORY 6";
    else if (i == 4) customKeyName = "SCIENCE";
    else if (i == 5) customKeyName = "BEAUTY";
    else if (i == 6) customKeyName = "AUTO REPAIR";
    else if (i == 7) customKeyName = "AWAKE SHOP";
    else if (i == 8) customKeyName = "MULTI LEARN";
    else if (i == 9) customKeyName = "MOTORCYCLE";
    else {
      char keyLabel[12];
      sprintf(keyLabel, "KEY %02d", i + 1);
      customKeyName = String(keyLabel);
    }

    tft.drawString(customKeyName, cx, cy + 36, 1);

    if (!keyPresent[i] && currentBorrowerName[i].length() > 0) {
      String shortName = currentBorrowerName[i];
      if (shortName.length() > 9) shortName = shortName.substring(0, 9);
      tft.setTextColor(YELLOW, BG);
      tft.drawString(shortName, cx, cy + 53, 1);
    }
  }
  drawBackButton();
}

// =====================================
// CHECK FINGERPRINT SENSOR (BORROW / RETURN)
// =====================================
void checkFingerprintSensor() {
  if (!fingerprintReady) return;

  uint8_t p = finger.getImage();
  if (p != FINGERPRINT_OK) return;

  p = finger.image2Tz();
  if (p != FINGERPRINT_OK) return;

  p = finger.fingerFastSearch();
  if (p == FINGERPRINT_OK) {
    uint16_t foundFingerID = finger.fingerID;
    Serial.print("\n[SCANNER] Fingerprint matched! Found ID: ");
    Serial.println(foundFingerID);
    
    String memberID = "";
    String memberName = "";
    int targetSlot = -1;

    for (int i = 0; i < MAX_KEYS; i++) {
      if (keyData[i].used && keyData[i].fingerID == foundFingerID) {
        memberID = keyData[i].memberID;
        memberName = keyData[i].name;
        targetSlot = i;
        break;
      }
    }

    if (memberID == "" || targetSlot == -1) {
      Serial.println("[SECURITY ALERT] Unregistered or deleted fingerprint detected! Access Denied.");
      finger.deleteModel(foundFingerID);
      playTone(1000, 400);
      return; 
    }

    Serial.print("[SCANNER] Welcome, ");
    Serial.print(memberName);
    Serial.print(" (ID: ");
    Serial.print(memberID);
    Serial.println(")");

    soundFingerSuccess();

    bool initialKeyStatus[MAX_KEYS];
    if (mcpReady) {
      readKeyStatus();
      for (int i = 0; i < MAX_KEYS; i++) {
        initialKeyStatus[i] = keyPresent[i];
      }
    }

    runDoorCycle();

    if (mcpReady) {
      readKeyStatus();
    }

    String eventName = "BORROW_KEY";
    for (int i = 0; i < MAX_KEYS; i++) {
      if (initialKeyStatus[i] == true && keyPresent[i] == false) {
        targetSlot = i;
        eventName = "BORROW_KEY";
        currentBorrowerName[i] = memberName;
        break;
      } else if (initialKeyStatus[i] == false && keyPresent[i] == true) {
        targetSlot = i;
        eventName = "RETURN_KEY";
        currentBorrowerName[i] = "";
        break;
      }
    }

    char buf[10];
    sprintf(buf, "KEY-%02d", targetSlot + 1);
    String keyID = String(buf);

    if (currentPage == PAGE_KEY) {
      drawKeyPage();
    }

    // 1. ส่งข้อมูลประวัติการยืม-คืนไปบันทึกแท็บ History
    DynamicJsonDocument doc(512);
    doc["action"] = "borrowReturn";
    doc["memberID"] = memberID;
    doc["name"] = memberName;
    doc["keyID"] = keyID;
    doc["event"] = eventName;

    String payload;
    serializeJson(doc, payload);
    sendGoogleRequest(payload);

    // 2. ส่งข้อมูลอัปเดตสถานะกุญแจไปที่แท็บ Keys ใน Google Sheets
    DynamicJsonDocument docKey(512);
    docKey["action"] = "updateKeyStatus";
    docKey["keyID"] = keyID;
    docKey["status"] = (eventName == "BORROW_KEY") ? "Borrowed" : "Available";
    docKey["borrower"] = (eventName == "BORROW_KEY") ? memberName : "";

    String payloadKey;
    serializeJson(docKey, payloadKey);
    sendGoogleRequest(payloadKey);

    delay(1000);
  }
}

// =====================================
// FORM AND PAGE CONTROL
// =====================================
void drawInputField(int y, const char* label, String value, bool selected) {
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(WHITE, BG);
  tft.drawString(label, 12, y - 19, 2);

  uint16_t borderColor = selected ? TFT_YELLOW : TFT_WHITE;
  tft.fillRoundRect(10, y, 300, 36, 6, HEADER);
  tft.drawRoundRect(10, y, 300, 36, 6, borderColor);
  tft.setTextColor(WHITE, HEADER);

  String displayText = value.length() == 0 ? "_" : value;
  tft.drawString(displayText, 18, y + 10, 2);
}

void drawAddPage() {
  tft.fillScreen(BG);
  drawHeader("ADD MEMBER");
  drawInputField(77, "SHORT NAME", inputName, selectedField == FIELD_NAME);
  drawInputField(130, "PHONE NUMBER", inputPhone, selectedField == FIELD_PHONE);

  for (int row = 0; row < 6; row++) {
    for (int col = 0; col < 6; col++) {
      int x = KEY_X + col * (KEY_W + KEY_GAP);
      int y = KEY_Y + row * (KEY_H + 3);
      tft.fillRoundRect(x, y, KEY_W, KEY_H, 5, HEADER);
      tft.drawRoundRect(x, y, KEY_W, KEY_H, 5, WHITE);

      char letter[2] = { keyboardRows[row][col], '\0' };
      tft.setTextDatum(MC_DATUM);
      tft.setTextColor(WHITE, HEADER);
      tft.drawString(letter, x + KEY_W / 2, y + KEY_H / 2, 2);
    }
  }

  tft.fillRoundRect(10, 382, 185, 32, 5, BLUE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(WHITE, BLUE);
  tft.drawString("SPACE", 102, 398, 2);

  tft.fillRoundRect(205, 382, 105, 32, 5, RED);
  tft.setTextColor(WHITE, RED);
  tft.drawString("DEL", 257, 398, 2);

  tft.fillRoundRect(10, 426, 140, 43, 8, GRAY);
  tft.setTextColor(WHITE, GRAY);
  tft.drawString("CANCEL", 80, 447, 2);

  tft.fillRoundRect(170, 426, 140, 43, 8, GREEN);
  tft.setTextColor(WHITE, GREEN);
  tft.drawString("SAVE", 240, 447, 2);
}

void drawTrashIcon(int x, int y) {
  tft.drawRect(x - 9, y - 6, 18, 19, WHITE);
  tft.drawLine(x - 12, y - 9, x + 12, y - 9, WHITE);
  tft.drawLine(x - 5, y - 13, x + 5, y - 13, WHITE);
  tft.drawLine(x - 4, y - 3, x - 4, y + 9, WHITE);
  tft.drawLine(x + 4, y - 3, x + 4, y + 9, WHITE);
}

void drawRemovePage() {
  tft.fillScreen(BG);
  drawHeader("REMOVE MEMBER");
  int rowY = 68;

  for (int i = 0; i < MAX_KEYS; i++) {
    if (!keyData[i].used) continue;
    tft.fillRoundRect(10, rowY, 300, 32, 5, HEADER);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(WHITE, HEADER);

    String label = keyData[i].name;
    if (label.length() > 15) label = label.substring(0, 15);

    tft.drawString(label, 18, rowY + 8, 2);
    tft.fillRoundRect(267, rowY + 2, 38, 28, 5, RED);
    drawTrashIcon(286, rowY + 16);
    rowY += 36;
  }
  drawBackButton();
}

void drawDeletePopup() {
  tft.fillRect(0, 55, 320, 425, BLACK);
  tft.fillRoundRect(25, 165, 270, 150, 12, HEADER);
  tft.drawRoundRect(25, 165, 270, 150, 12, WHITE);
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(YELLOW, HEADER);
  tft.drawString("CONFIRM DELETE?", 160, 195, 2);

  tft.setTextColor(WHITE, HEADER);
  String name = (pendingRemove >= 0 && pendingRemove < MAX_KEYS) ? keyData[pendingRemove].name : "";
  tft.drawString(name, 160, 225, 2);

  tft.fillRoundRect(42, 260, 105, 40, 7, GRAY);
  tft.setTextColor(WHITE, GRAY);
  tft.drawString("CANCEL", 94, 280, 2);

  tft.fillRoundRect(172, 260, 105, 40, 7, GREEN);
  tft.setTextColor(WHITE, GREEN);
  tft.drawString("OK", 224, 280, 2);
}

void drawEnrollMessage(const String &title, const String &message) {
  tft.fillScreen(BG);
  drawHeader("FINGERPRINT");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(YELLOW, BG);
  tft.drawString(title, 160, 145, 2);
  tft.setTextColor(WHITE, BG);
  tft.drawCentreString(message, 160, 195, 2);
  tft.setTextColor(TFT_LIGHTGREY, BG);
  tft.drawCentreString("Please follow the instructions", 160, 245, 2);
  tft.fillRoundRect(10, 428, 100, 40, 8, GRAY);
  tft.setTextColor(WHITE, GRAY);
  tft.drawString("< BACK", 60, 448, 2);
}

void drawEnrollPage() {
  tft.fillScreen(BG);
  drawHeader("CONFIRM FINGERPRINT");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, BG);
  tft.drawString("SCAN SUCCESS", 160, 115, 2);
  tft.setTextColor(WHITE, BG);
  tft.drawString("Name: " + pendingMemberName, 160, 165, 2);
  tft.drawString("Phone: " + pendingMemberPhone, 160, 200, 2);
  tft.setTextColor(YELLOW, BG);
  tft.drawString("Fingerprint ID: " + String(pendingFingerID), 160, 245, 2);

  tft.fillRoundRect(15, 340, 135, 55, 8, RED);
  tft.setTextColor(WHITE, RED);
  tft.drawString("CANCEL", 82, 367, 2);

  tft.fillRoundRect(170, 340, 135, 55, 8, GREEN);
  tft.setTextColor(WHITE, GREEN);
  tft.drawString("CONFIRM", 237, 367, 2);
}

void drawSuccessPage() {
  tft.fillScreen(BG);
  drawHeader("MEMBER SAVED");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_GREEN, BG);
  tft.drawString("MEMBER SAVED", 160, 135, 2);
  tft.setTextColor(WHITE, BG);
  tft.drawString("Name: " + inputName, 160, 180, 2);
  tft.drawString("Phone: " + inputPhone, 160, 215, 2);
  tft.drawString("Fingerprint ID: " + String(enrolledFingerID), 160, 260, 2);

  tft.fillRoundRect(40, 350, 240, 55, 8, GREEN);
  tft.drawRoundRect(40, 350, 240, 55, 8, WHITE);
  tft.setTextColor(WHITE, GREEN);
  tft.drawString("BACK TO HOME", 160, 378, 2);
}

void drawErrorPage(const String &message) {
  tft.fillScreen(BG);
  drawHeader("SYSTEM ERROR");
  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(RED, BG);
  tft.drawString("FAILED", 160, 150, 4);
  tft.setTextColor(WHITE, BG);
  tft.drawCentreString(message, 160, 210, 2);

  tft.fillRoundRect(40, 350, 240, 55, 8, GRAY);
  tft.drawRoundRect(40, 350, 240, 55, 8, WHITE);
  tft.setTextColor(WHITE, GRAY);
  tft.drawString("BACK TO HOME", 160, 378, 2);
}

void openPage(Page page) {
  currentPage = page;
  switch (page) {
    case PAGE_HOME: drawHome(); break;
    case PAGE_KEY: drawKeyPage(); break;
    case PAGE_ADD:
      inputName = "";
      inputPhone = "";
      selectedField = FIELD_NAME;
      drawAddPage();
      break;
    case PAGE_REMOVE:
      showDeletePopup = false;
      pendingRemove = -1;
      drawRemovePage();
      break;
    case PAGE_ENROLL: drawEnrollPage(); break;
    case PAGE_SUCCESS: drawSuccessPage(); break;
    case PAGE_ERROR: drawErrorPage(enrollStatus); break;
  }
}

// =====================================
// SAVE & DELETE MEMBER ACTIONS
// =====================================
void saveMember() {
  Serial.println("\n[UI ACTION] Save Member button pressed.");
  if (inputName.length() == 0 || inputPhone.length() == 0) {
    enrollStatus = "Enter name and phone first";
    Serial.println("[ERROR] Name or phone is empty.");
    openPage(PAGE_ERROR);
    return;
  }

  if (!fingerprintReady) {
    enrollStatus = "AS608 not connected";
    Serial.println("[ERROR] AS608 not connected.");
    openPage(PAGE_ERROR);
    return;
  }

  checkWiFiAutoReconnect();
  if (WiFi.status() != WL_CONNECTED) {
    enrollStatus = "WiFi not connected";
    Serial.println("[ERROR] WiFi not connected.");
    openPage(PAGE_ERROR);
    return;
  }

  int slot = -1;
  for (int i = 0; i < MAX_KEYS; i++) {
    if (!keyData[i].used) {
      slot = i;
      break;
    }
  }

  if (slot == -1) {
    enrollStatus = "All 10 slots are full";
    Serial.println("[ERROR] All 10 slots are full.");
    openPage(PAGE_ERROR);
    return;
  }

  pendingMemberSlot = slot;
  pendingMemberID = generateMemberID(slot);
  pendingMemberName = inputName;
  pendingMemberPhone = inputPhone;

  Serial.print("[ADD] Assigned Slot: ");
  Serial.print(slot);
  Serial.print(" | MemberID: ");
  Serial.println(pendingMemberID);

  if (!scanFingerprintTwice()) {
    pendingFingerID = 0;
    fingerprintModelReady = false;
    openPage(PAGE_ERROR);
    return;
  }

  openPage(PAGE_ENROLL);
}

void confirmAndSaveMember() {
  Serial.println("\n[UI ACTION] Confirm & Save Member triggered.");
  if (!fingerprintModelReady || pendingFingerID == 0 || pendingMemberSlot < 0 || pendingMemberSlot >= MAX_KEYS) {
    enrollStatus = "Fingerprint data not ready";
    Serial.println("[ERROR] Fingerprint data not ready for confirmation.");
    openPage(PAGE_ERROR);
    return;
  }

  checkWiFiAutoReconnect();
  if (WiFi.status() != WL_CONNECTED) {
    enrollStatus = "WiFi not connected";
    openPage(PAGE_ERROR);
    return;
  }

  drawEnrollMessage("SAVING", "Saving fingerprint...");

  if (finger.storeModel(pendingFingerID) != FINGERPRINT_OK) {
    enrollStatus = "Cannot store fingerprint";
    Serial.println("[AS608 ERROR] Cannot store fingerprint model.");
    openPage(PAGE_ERROR);
    return;
  }
  Serial.println("[AS608] Fingerprint stored successfully in sensor.");

  DynamicJsonDocument doc(512);
  doc["action"] = "addMember";
  doc["memberID"] = pendingMemberID;
  doc["name"] = pendingMemberName;
  doc["phone"] = pendingMemberPhone;
  doc["fingerID"] = String(pendingFingerID);
  doc["role"] = "Member";
  doc["status"] = "Active";

  String payload;
  serializeJson(doc, payload);

  bool success = sendGoogleRequest(payload);

  if (!success) {
    finger.deleteModel(pendingFingerID);
    enrollStatus = "Google Sheets save failed";
    Serial.println("[ERROR] Google Sheets addMember failed! Rolling back fingerprint model.");
    openPage(PAGE_ERROR);
    return;
  }

  advanceFingerID(pendingFingerID);

  int slot = pendingMemberSlot;
  keyData[slot].memberID = pendingMemberID;
  keyData[slot].name = pendingMemberName;
  keyData[slot].phone = pendingMemberPhone;
  keyData[slot].fingerID = pendingFingerID;
  keyData[slot].used = true;

  saveMembersToFlash();
  enrolledFingerID = pendingFingerID;

  logHistory(pendingMemberID, pendingMemberName, "", "ADD_MEMBER");

  fingerprintModelReady = false;
  Serial.println("[SUCCESS] Member added successfully!");
  openPage(PAGE_SUCCESS);
}

void deleteMember(int index) {
  Serial.println("\n[UI ACTION] Delete Member triggered.");
  if (index < 0 || index >= MAX_KEYS || !keyData[index].used) {
    Serial.println("[ERROR] Invalid index or slot not used.");
    return;
  }

  checkWiFiAutoReconnect();
  if (WiFi.status() != WL_CONNECTED) {
    enrollStatus = "WiFi not connected";
    showDeletePopup = false;
    Serial.println("[ERROR] WiFi not connected during delete.");
    openPage(PAGE_ERROR);
    return;
  }

  String memberID = keyData[index].memberID;
  String memberName = keyData[index].name;
  uint16_t fid = keyData[index].fingerID;

  Serial.print("[DELETE] Deleting Member: ");
  Serial.print(memberName);
  Serial.print(" (ID: ");
  Serial.print(memberID);
  Serial.print(", FingerID: ");
  Serial.print(fid);
  Serial.println(")");

  DynamicJsonDocument doc(256);
  doc["action"] = "deleteMember";
  doc["memberID"] = memberID;

  String payload;
  serializeJson(doc, payload);

  bool success = sendGoogleRequest(payload);

  if (!success) {
    enrollStatus = "Google Sheets delete failed";
    showDeletePopup = false;
    Serial.println("[ERROR] Google Sheets delete failed!");
    openPage(PAGE_ERROR);
    return;
  }

  if (fid > 0 && fingerprintReady) {
    if (finger.deleteModel(fid) == FINGERPRINT_OK) {
      Serial.println("[AS608] Fingerprint model deleted from sensor.");
    } else {
      Serial.println("[AS608 WARNING] Failed to delete fingerprint model from sensor or not found.");
    }
  }

  logHistory(memberID, memberName, "", "DELETE_MEMBER");

  keyData[index].used = false;
  keyData[index].memberID = "";
  keyData[index].name = "";
  keyData[index].phone = "";
  keyData[index].fingerID = 0;

  saveMembersToFlash();

  Serial.println("[SUCCESS] Member deleted successfully.");
  showDeletePopup = false;
  pendingRemove = -1;
  openPage(PAGE_REMOVE);
}

// =====================================
// KEYBOARD HANDLERS & TOUCH
// =====================================
void addCharacter(char c) {
  if (selectedField == FIELD_NAME) {
    if (inputName.length() < 12) inputName += c;
  } else {
    if (c >= '0' && c <= '9') {
      if (inputPhone.length() < 15) inputPhone += c;
    }
  }
  drawAddPage();
}

void deleteCharacter() {
  if (selectedField == FIELD_NAME) {
    if (inputName.length() > 0) inputName.remove(inputName.length() - 1);
  } else {
    if (inputPhone.length() > 0) inputPhone.remove(inputPhone.length() - 1);
  }
  drawAddPage();
}

void handleHomeTouch(int x, int y) {
  if (y >= 115 && y <= 200) openPage(PAGE_KEY);
  else if (y >= 220 && y <= 305) openPage(PAGE_ADD);
  else if (y >= 325 && y <= 410) openPage(PAGE_REMOVE);
}

void handleAddTouch(int x, int y) {
  if (y >= 77 && y <= 113) { selectedField = FIELD_NAME; drawAddPage(); return; }
  if (y >= 130 && y <= 166) { selectedField = FIELD_PHONE; drawAddPage(); return; }

  if (y >= KEY_Y && y < KEY_Y + 6 * (KEY_H + 3)) {
    int row = (y - KEY_Y) / (KEY_H + 3);
    int col = (x - KEY_X) / (KEY_W + KEY_GAP);
    if (row >= 0 && row < 6 && col >= 0 && col < 6) {
      if ((x - KEY_X) % (KEY_W + KEY_GAP) < KEY_W) {
        addCharacter(keyboardRows[row][col]);
      }
    }
    return;
  }

  if (y >= 382 && y <= 414 && x >= 10 && x <= 195) { if (selectedField == FIELD_NAME) addCharacter(' '); return; }
  if (y >= 382 && y <= 414 && x >= 205 && x <= 310) { deleteCharacter(); return; }
  if (y >= 426 && y <= 469 && x >= 10 && x <= 150) { openPage(PAGE_HOME); return; }
  if (y >= 426 && y <= 469 && x >= 170 && x <= 310) { saveMember(); return; }
}

void handleRemoveTouch(int x, int y) {
  if (showDeletePopup) {
    if (y >= 260 && y <= 300 && x >= 42 && x <= 147) { showDeletePopup = false; pendingRemove = -1; drawRemovePage(); return; }
    if (y >= 260 && y <= 300 && x >= 172 && x <= 277) { deleteMember(pendingRemove); return; }
    return;
  }

  if (y >= 428 && y <= 468 && x >= 10 && x <= 110) { openPage(PAGE_HOME); return; }

  int rowY = 68;
  for (int i = 0; i < MAX_KEYS; i++) {
    if (!keyData[i].used) continue;
    if (y >= rowY && y <= rowY + 32 && x >= 267 && x <= 305) {
      pendingRemove = i;
      showDeletePopup = true;
      drawDeletePopup();
      return;
    }
    rowY += 36;
  }
}

void handleTouch(int x, int y) {
  switch (currentPage) {
    case PAGE_HOME: handleHomeTouch(x, y); break;
    case PAGE_KEY: if (y >= 428 && y <= 468 && x >= 10 && x <= 110) openPage(PAGE_HOME); break;
    case PAGE_ADD: handleAddTouch(x, y); break;
    case PAGE_REMOVE: handleRemoveTouch(x, y); break;
    case PAGE_ENROLL:
      if (y >= 340 && y <= 395 && x >= 15 && x <= 150) {
        pendingFingerID = 0; pendingMemberSlot = -1; fingerprintModelReady = false;
        pendingMemberID = ""; pendingMemberName = ""; pendingMemberPhone = "";
        openPage(PAGE_ADD);
        return;
      }
      if (y >= 340 && y <= 395 && x >= 170 && x <= 305) { confirmAndSaveMember(); return; }
      break;
    case PAGE_SUCCESS:
    case PAGE_ERROR:
      if (y >= 340 && y <= 415 && x >= 30 && x <= 290) openPage(PAGE_HOME);
      break;
  }
}

// =====================================
// SETUP
// =====================================
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n==========================");
  Serial.println("SMART KEY BOX V2 - SECURE");
  Serial.println("==========================");

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);
  initBuzzer();

  pinMode(MOTOR_IN1, OUTPUT);
  pinMode(MOTOR_IN2, OUTPUT);
  motorStop();

  pinMode(LIMIT_LEFT_PIN, INPUT_PULLUP);
  pinMode(LIMIT_RIGHT_PIN, INPUT_PULLUP);

  SPI.begin(18, 19, 23);
  tft.init();
  tft.setRotation(0);
  touch.begin();
  touch.setRotation(0);

  mcpReady = initializeMCP23017();
  for (int i = 0; i < MAX_KEYS; i++) keyPresent[i] = false;

  loadMembersFromFlash();
  fingerprintReady = initializeFingerprint();

  if (mcpReady) readKeyStatus();

  connectWiFi();

  if (wifiReady) {
    googleSheetsReady = testGoogleSheets();
  }

  soundSystemReady();
  openPage(PAGE_HOME);
}

// =====================================
// LOOP
// =====================================
void loop() {
  checkWiFiAutoReconnect();

  static unsigned long lastKeyRead = 0;
  static bool oldKeyStatus[MAX_KEYS] = { false };
  static bool firstReadFlag = true;

  if (millis() - lastKeyRead >= 150) {
    lastKeyRead = millis();

    if (mcpReady) {
      readKeyStatus();

      bool changed = false;
      for (int i = 0; i < MAX_KEYS; i++) {
        if (firstReadFlag || oldKeyStatus[i] != keyPresent[i]) {
          oldKeyStatus[i] = keyPresent[i];
          changed = true;
        }
      }
      firstReadFlag = false;

      if (changed && currentPage == PAGE_KEY) {
        drawKeyPage();
      }
    }
  }

  if (currentPage == PAGE_KEY) {
    checkFingerprintSensor();
  }

  if (touch.touched()) {
    delay(40);
    if (touch.touched()) {
      TS_Point p = touch.getPoint();
      int x, y;
      convertTouch(p.x, p.y, x, y);
      handleTouch(x, y);

      while (touch.touched()) {
        delay(10);
      }
      delay(120);
    }
  }
}
