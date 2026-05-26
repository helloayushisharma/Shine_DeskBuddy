// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ayushi Sharma and Neha Sadaye
//
// ============================================================
// Shi+ne - by Ayushi and Neha
// ============================================================
// Multi-app mode system with Win10 Mobile tile launcher:
//   - Shi+ne mode (Notion desk companion)
//   - Flappy Bird game
//   - Win10 Mobile tile home screen (hold 5s to access)
//   - Turnstile transition animations between modes
//   - Sprite memory freed/reallocated on mode switch
// ============================================================


#include <TFT_eSPI.h>
#include <SPI.h>
#include <ESP32Servo.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>


// ============================================================
// FILL THESE IN
// ============================================================
const char* WIFI_SSID     = "Your WiFi name";
const char* WIFI_PASSWORD = "Your WiFi password";
const char* NOTION_TOKEN  = "Notion Token";
const char* DATABASE_ID   = "Database ID";


const char* CHECKBOX_PROPERTY = "Done";
const char* TITLE_PROPERTY    = "Name";


// ============================================================
// SERVO TUNABLES
// ============================================================
// SERVO_RISE_MODE: 1 = single-write (smoothest), 0 = chunked
#define SERVO_RISE_MODE 0


const int           SERVO_RISE_STEP_DEG   = 15;
const unsigned long SERVO_RISE_STEP_MS    = 40;
const unsigned long SERVO_HOLD_MS         = 10000;


// SERVO_RETURN_MODE: 0 = waypoints, 1 = physics spring
#define SERVO_RETURN_MODE 1


const int           SERVO_RETURN_WAYPOINTS[]   = {40, 15, 5, 0};
const int           SERVO_RETURN_WAYPOINT_COUNT = 4;
const unsigned long SERVO_RETURN_DWELL_MS      = 300;


const float         PHYSICS_SPRING_K  = 7.11f;
const float         PHYSICS_SPRING_D  = 5.33f;
const unsigned long PHYSICS_TICK_MS   = 20;
const float         PHYSICS_DONE_POS  = 0.5f;
const float         PHYSICS_DONE_VEL  = 1.0f;


const unsigned long WIFI_CHECK_MS         = 2000;
const unsigned long WIFI_BACKOFF_START_MS = 5000;
const unsigned long WIFI_BACKOFF_MAX_MS   = 60000;


const int           PUPIL_STEP_PX = 5;
const unsigned long PUPIL_STEP_MS = 25;


// COUNTER_ANIM_MODE: 0 = slot machine, 1 = bounce
#define COUNTER_ANIM_MODE 0


#if COUNTER_ANIM_MODE == 0
 const int           COUNTER_ANIM_FRAMES = 10;
 const unsigned long COUNTER_ANIM_MS     = 35;
#else
 const int           COUNTER_ANIM_FRAMES = 5;
 const unsigned long COUNTER_ANIM_MS     = 50;
#endif


const int           COUNTER_ROLL_FRAMES = COUNTER_ANIM_FRAMES;
const unsigned long COUNTER_ROLL_MS     = COUNTER_ANIM_MS;


const unsigned long TASK_ADDED_SURPRISED_MS = 600;


// ============================================================
// HARDWARE
// ============================================================
TFT_eSPI tft = TFT_eSPI();
Servo flagServo;


#define SERVO_PIN   32
#define FLAG_DOWN   0
#define FLAG_UP     90


// ============================================================
// SCREEN
// ============================================================
const int SW = 480;
const int SH = 320;


const int L_EX = 165;
const int R_EX = 315;
const int EY   = 110;


const int EYE_R   = 56;
const int PUPIL_R = 25;


const int MOUTH_Y = 185;
#define TEXT_ZONE_TOP 220


#define CONF_L_X   5
#define CONF_L_W   55
#define CONF_R_X   420
#define CONF_R_W   55


#define COL_BG    TFT_BLACK
#define COL_WHITE TFT_WHITE
#define COL_PUPIL TFT_BLACK
#define COL_LID   TFT_BLACK
#define COL_DIM   0x6B4D


uint16_t confColors[] = {0xFFE0, 0x07FF, 0x07E0, 0xFBE0, 0xFFFF};
#define NUM_CONF_COLORS 5


// ============================================================
// SPRITES (shi+ne mode — allocated/freed on mode switch)
// ============================================================
TFT_eSprite eyeSpr = TFT_eSprite(&tft);
TFT_eSprite mouthSpr = TFT_eSprite(&tft);
TFT_eSprite confLSpr = TFT_eSprite(&tft);
TFT_eSprite confRSpr = TFT_eSprite(&tft);
TFT_eSprite counterSpr = TFT_eSprite(&tft);


#define EYE_SPR_W (EYE_R * 2 + 6)
#define EYE_SPR_H (EYE_R * 2 + 6)
#define MOUTH_SPR_W 100
#define MOUTH_SPR_H 35
#define COUNTER_SPR_W 320
#define COUNTER_SPR_H 36


// ============================================================
// APP MODE SYSTEM
// ============================================================
enum AppMode { APP_SHINE, APP_FLAPPY, APP_TILES };
AppMode currentApp = APP_SHINE;
bool transitionActive = false;
bool flappyPlaying = false;  // set by flappy.ino when game is active


// Async Notion polling (FreeRTOS task on core 0)
SemaphoreHandle_t notionMutex = NULL;
TaskHandle_t notionTaskHandle = NULL;
volatile bool notionPollBusy     = false;
volatile bool notionResultReady  = false;
int  notionResultDone  = 0;
int  notionResultTotal = 0;
char notionResultTask[64] = "";


// Hold-to-switch detection
bool          holdActive          = false;
unsigned long holdStartTime       = 0;
bool          holdProgressVisible = false;
const unsigned long HOLD_THRESHOLD_MS = 5000;
const uint16_t COL_ACCENT = 0x0479;   // Win10 accent blue (#0078D7)


// Hold circle indicator sprite
const int HOLD_CIRCLE_R    = 22;  // outer radius
const int HOLD_CIRCLE_THICK = 3;  // ring thickness
const int HOLD_SPR_SIZE    = (HOLD_CIRCLE_R + 2) * 2;  // sprite dimensions
TFT_eSprite holdSpr = TFT_eSprite(&tft);
bool holdSprAllocated = false;
int holdSprX = 0, holdSprY = 0;  // screen position of sprite top-left


// ============================================================
// EYE STATE
// ============================================================
int curPupilX = 0, curPupilY = 0;
bool eyesDrawn = false;


// ============================================================
// STATE (shi+ne mode)
// ============================================================
enum BuddyMood {
 MOOD_IDLE, MOOD_HAPPY, MOOD_TASK_ADDED, MOOD_AGITATED,
 MOOD_TICKLE, MOOD_CONNECTING, MOOD_ERROR
};


BuddyMood currentMood = MOOD_CONNECTING;
int completedCount = 0, totalCount = 0;
int previousCount = -1, previousTotal = -1;
String lastTaskName = "";


unsigned long lastPollTime = 0;
unsigned long moodStartTime = 0;
unsigned long lastAnimFrame = 0;
const unsigned long POLL_INTERVAL     = 3000;
const unsigned long HAPPY_DURATION    = 7000;
const unsigned long ADDED_DURATION    = 4000;
const unsigned long AGITATED_DURATION = 3000;
const unsigned long TICKLE_DURATION   = 1200;


// Servo state machine
enum ServoState { SERVO_S_IDLE, SERVO_S_RISING, SERVO_S_HOLDING, SERVO_S_RETURNING };
ServoState    servoState        = SERVO_S_IDLE;
int           servoCurrentAngle = 0;
unsigned long lastServoStep     = 0;
unsigned long servoHoldStart    = 0;
int           returnWaypointIndex = 0;


// Physics return state
float         physicsP        = 0;
float         physicsV        = 0;
unsigned long lastPhysicsTick = 0;


// Task-added sub-phase
enum TaskAddedPhase { TA_SURPRISED, TA_NORMAL };
TaskAddedPhase taskAddedPhase      = TA_NORMAL;
unsigned long  taskAddedPhaseStart = 0;


// WiFi watchdog state
bool          wifiOffline            = false;
bool          wifiIndicatorOnScreen  = false;
unsigned long lastWifiCheck          = 0;
unsigned long lastReconnectAttempt   = 0;
unsigned long currentBackoffMs       = WIFI_BACKOFF_START_MS;


// Pupil saccade tween
int           targetPupilX  = 0, targetPupilY = 0;
bool          pupilMoving   = false;
unsigned long lastPupilStep = 0;


// Counter slot-machine roll
int  displayedDone = -1, displayedTotal = -1;
bool counterRolling = false;
int  counterRollFrame = 0;
unsigned long lastCounterRollFrame = 0;
int  counterRollFromDone, counterRollFromTotal;
int  counterRollToDone,   counterRollToTotal;


int idleStep = 0;
unsigned long lastIdleStep = 0;
const unsigned long IDLE_STEP_TIME = 1200;


bool isBlinking = false;
int blinkFrame = 0;
unsigned long lastBlinkFrame = 0;


int happyPhase = 0;
unsigned long lastHappySwap = 0;
bool happyFaceIsCresc = false;
int happyTaskCount = 1;


// Touch
int tapCount = 0;
unsigned long lastTapTime = 0;
bool touchWasDown = false;


#define NUM_CONFETTI 9
struct Confetti {
 float x, y, speed;
 int size;
 uint16_t color;
 bool leftSide;
};
Confetti conf[NUM_CONFETTI];
bool confettiActive = false;


// Bee state (drawn by shine_mode.ino, freed by freeShineSprites)
TFT_eSprite beeSpr = TFT_eSprite(&tft);
bool beeSprAllocated = false;
bool beeActive = false;
float beeT = 0;
int beePrevX = -50, beePrevY = -50;
unsigned long lastBeeFrame = 0;
unsigned long beeStartTime = 0;
unsigned long nextBeeTime = 0;
int beeWingFrame = 0;


// ============================================================
// SPRITE ALLOCATION / DEALLOCATION
// ============================================================
void allocateShineSprites() {
 eyeSpr.setColorDepth(16);
 eyeSpr.createSprite(EYE_SPR_W, EYE_SPR_H);


 mouthSpr.setColorDepth(16);
 mouthSpr.createSprite(MOUTH_SPR_W, MOUTH_SPR_H);


 confLSpr.setColorDepth(16);
 confLSpr.createSprite(CONF_L_W, SH);


 confRSpr.setColorDepth(16);
 confRSpr.createSprite(CONF_R_W, SH);


 counterSpr.setColorDepth(16);
 counterSpr.createSprite(COUNTER_SPR_W, COUNTER_SPR_H);


 Serial.printf("shi+ne sprites allocated. Free PSRAM: %d\n", ESP.getFreePsram());
}


void freeShineSprites() {
 eyeSpr.deleteSprite();
 mouthSpr.deleteSprite();
 confLSpr.deleteSprite();
 confRSpr.deleteSprite();
 counterSpr.deleteSprite();
 freeBeeSprite();
 beeActive = false;


 Serial.printf("shi+ne sprites freed. Free PSRAM: %d\n", ESP.getFreePsram());
}


// ============================================================
// ASYNC NOTION POLL (runs on core 0 — WiFi core)
// ============================================================
void notionPollTaskFn(void* param) {
 for (;;) {
   ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // sleep until signaled


   if (WiFi.status() != WL_CONNECTED) {
     notionPollBusy = false;
     continue;
   }


   WiFiClientSecure client;
   client.setInsecure();
   HTTPClient http;


   char url[128];
   snprintf(url, sizeof(url), "https://api.notion.com/v1/databases/%s/query", DATABASE_ID);


   http.useHTTP10(true);  // disable chunked encoding so stream is raw JSON
   http.begin(client, url);


   char authHeader[128];
   snprintf(authHeader, sizeof(authHeader), "Bearer %s", NOTION_TOKEN);
   http.addHeader("Authorization", authHeader);
   http.addHeader("Notion-Version", "2022-06-28");
   http.addHeader("Content-Type", "application/json");


   int httpCode = http.POST("{\"sorts\":[{\"timestamp\":\"last_edited_time\",\"direction\":\"descending\"}]}");;
   Serial.printf("Notion async: %d\n", httpCode);


   if (httpCode == 200) {
     // Stream parsing — no 30KB String allocation
     WiFiClient* stream = http.getStreamPtr();
     DynamicJsonDocument doc(24576);
     DeserializationError err = deserializeJson(doc, *stream);


     if (!err) {
       JsonArray results = doc["results"];
       int localTotal = results.size();
       int localDone = 0;
       char localTask[64] = "";


       for (int i = 0; i < localTotal; i++) {
         JsonObject page = results[i];
         bool isDone = page["properties"][CHECKBOX_PROPERTY]["checkbox"] | false;
         if (isDone) {
           localDone++;
           // Capture FIRST completed task name (Notion returns newest first)
           if (localTask[0] == '\0') {
             const char* name = page["properties"][TITLE_PROPERTY]["title"][0]["plain_text"];
             if (name) snprintf(localTask, sizeof(localTask), "%s", name);
           }
         }
       }


       // Copy results under mutex
       if (xSemaphoreTake(notionMutex, pdMS_TO_TICKS(100))) {
         notionResultDone = localDone;
         notionResultTotal = localTotal;
         strncpy(notionResultTask, localTask, sizeof(notionResultTask));
         notionResultReady = true;
         xSemaphoreGive(notionMutex);
       }
     } else {
       Serial.printf("JSON parse error: %s\n", err.c_str());
     }
   } else {
     Serial.printf("Notion API error: %d\n", httpCode);
   }
   http.end();
   notionPollBusy = false;
 }
}


// ============================================================
// BOOT ANIMATION STATE
// ============================================================
int  bootPhase = 0;
unsigned long bootPhaseStart = 0;
unsigned long bootStart = 0;
bool bootComplete = false;


// ============================================================
// SETUP (non-blocking — animation runs in loop during WiFi connect)
// ============================================================
void setup() {
 Serial.begin(115200);
 delay(100);


 tft.init();
 tft.setRotation(3);
 tft.fillScreen(COL_BG);


 allocateShineSprites();


 uint16_t calData[5] = {300, 3600, 300, 3600, 1};
 tft.setTouch(calData);


 flagServo.attach(SERVO_PIN);
 safeServoWrite(FLAG_DOWN);
 servoCurrentAngle = FLAG_DOWN;
 servoState = SERVO_S_IDLE;


 // Start WiFi (non-blocking)
 WiFi.begin(WIFI_SSID, WIFI_PASSWORD);


 currentMood = MOOD_CONNECTING;
 currentApp = APP_SHINE;
 bootStart = millis();
 bootPhaseStart = millis();
 bootPhase = 0;


 lastPollTime        = millis();
 lastIdleStep        = millis();
 lastWifiCheck       = millis();
 lastReconnectAttempt = millis();


 // Start async Notion poll task
 notionMutex = xSemaphoreCreateMutex();
 xTaskCreatePinnedToCore(notionPollTaskFn, "notion", 8192, NULL, 1, &notionTaskHandle, 0);


 Serial.println("Booting with animation...");
}


// ============================================================
// BOOT ANIMATION (runs in main loop during MOOD_CONNECTING)
// ============================================================
void runBootAnimation(unsigned long now) {
 unsigned long phaseElapsed = now - bootPhaseStart;
 bool wifiReady = (WiFi.status() == WL_CONNECTED);


 switch (bootPhase) {
   case 0: // Dark pause (300ms)
     if (phaseElapsed >= 300) {
       bootPhase = 1;
       bootPhaseStart = now;
     }
     break;


   case 1: // Eyes opening — thin lines growing to full circles (700ms)
   {
     float t = min(phaseElapsed / 700.0f, 1.0f);
     int eyeH = (int)(t * EYE_R * 2);
     if (eyeH < 6) eyeH = 6;


     // Draw opening eyes using blink frames
     pushOneEyeBlink(L_EX, EY, eyeH, 0, 0);
     pushOneEyeBlink(R_EX, EY, eyeH, 0, 0);


     if (t >= 1.0f) {
       spriteEyes(0, 0);  // full open
       bootPhase = 2;
       bootPhaseStart = now;
     }
     delay(25);  // ~40 FPS for opening animation
     break;
   }


   case 2: // Look left (400ms)
     if (phaseElapsed < 150) {
       // Tween to left
       int px = (int)(-14.0f * min(phaseElapsed / 150.0f, 1.0f));
       spriteEyes(px, 0);
     }
     if (phaseElapsed >= 400) {
       bootPhase = 3;
       bootPhaseStart = now;
     }
     break;


   case 3: // Look right (400ms)
     if (phaseElapsed < 150) {
       int px = (int)((-14 + 28.0f * min(phaseElapsed / 150.0f, 1.0f)));
       spriteEyes(px, 0);
     }
     if (phaseElapsed >= 400) {
       bootPhase = 4;
       bootPhaseStart = now;
     }
     break;


   case 4: // Look center + smile (300ms)
     if (phaseElapsed < 100) {
       int px = (int)(14.0f * (1.0f - min(phaseElapsed / 100.0f, 1.0f)));
       spriteEyes(px, 0);
     } else if (phaseElapsed == 100 || (phaseElapsed > 100 && phaseElapsed < 120)) {
       spriteEyes(0, 0);
       spriteSmile(18, 6);
     }
     if (phaseElapsed >= 300) {
       bootPhase = 5;
       bootPhaseStart = now;
       // Show connecting text
       drawBottomText("Connecting...");
     }
     break;


   case 5: // Waiting for WiFi — blink periodically, show "Connecting..."
   {
     // Blink every 2 seconds
     unsigned long blinkCycle = phaseElapsed % 2000;
     if (blinkCycle < 150) {
       int bH = EYE_R * 2;
       if (blinkCycle < 30) bH = EYE_R;
       else if (blinkCycle < 60) bH = 10;
       else if (blinkCycle < 90) bH = 6;
       else if (blinkCycle < 120) bH = 10;
       else bH = EYE_R;
       pushOneEyeBlink(L_EX, EY, bH, 0, 0);
       pushOneEyeBlink(R_EX, EY, bH, 0, 0);
     } else if (blinkCycle >= 150 && blinkCycle < 180) {
       spriteEyes(0, 0);
     }


     // Animate dots: "Connecting.", "Connecting..", "Connecting..."
     int dots = ((phaseElapsed / 500) % 3) + 1;
     char connText[20] = "Connecting";
     for (int d = 0; d < dots; d++) strcat(connText, ".");
     tft.fillRect(0, TEXT_ZONE_TOP, SW, SH - TEXT_ZONE_TOP, COL_BG);
     tft.setTextColor(COL_DIM, COL_BG);
     tft.setTextDatum(TC_DATUM);
     tft.setFreeFont(&FreeSans9pt7b);
     tft.drawString(connText, SW / 2, TEXT_ZONE_TOP + 20);


     // Check WiFi
     if (wifiReady) {
       bootPhase = 6;
       bootPhaseStart = now;
     }
     // Timeout after 20 seconds
     if (now - bootStart > 20000 && !wifiReady) {
       bootPhase = 7;  // error
       bootPhaseStart = now;
     }
     delay(30);
     break;
   }


   case 6: // WiFi connected! Brief celebration (500ms)
     if (phaseElapsed < 50) {
       drawHappyCrescents();
       spriteSmile(22, 9);
       drawBottomText("Connected!");
     }
     if (phaseElapsed >= 500) {
       // Transition to normal idle
       wifiOffline = false;
       pollNotion();  // sync initial data
       previousCount = completedCount;
       previousTotal = totalCount;
       displayedDone  = completedCount;
       displayedTotal = totalCount;
       currentMood = MOOD_IDLE;
       clearScreen();
       spriteEyes(0, 0);
       spriteSmile(18, 6);
       drawTaskCounter();
       idleStep = 0;
       lastIdleStep = now;
       bootComplete = true;
     }
     break;


   case 7: // WiFi failed
     currentMood = MOOD_ERROR;
     wifiOffline = true;
     clearScreen();
     spriteEyes(0, 0);
     spriteFrown(16, 5);
     drawBottomText("WiFi failed :(");
     drawOfflineIndicator();
     bootComplete = true;
     break;
 }
}


// ============================================================
// MAIN LOOP — mode dispatch
// ============================================================
void loop() {
 unsigned long now = millis();


 // Boot animation runs until complete
 if (!bootComplete) {
   runBootAnimation(now);
   return;
 }


 // WiFi watchdog runs in ALL modes
 checkWifi(now);


 // Servo state machine runs in ALL modes (flower may be mid-return)
 updateServo(now);


 // Transition animation takes over the entire loop
 if (transitionActive) {
   runTransitionFrame(now);
   return;
 }


 // Cheap touch check: getTouchRawZ (~0.1ms) instead of getTouch (~1.5-20ms)
 // Only do full coordinate read when tiles mode needs X/Y for hit testing
 uint16_t touchX = 0, touchY = 0;
 bool isTouching = (tft.getTouchRawZ() > 500);
 if (isTouching && currentApp == APP_TILES) {
   // Need actual coordinates for tile hit testing
   isTouching = tft.getTouch(&touchX, &touchY);
 }


 // Hold-to-switch runs in shi+ne and flappy (not tiles)
 if (currentApp != APP_TILES) {
   updateHoldDetection(now, isTouching);
 }


 // Mode dispatch
 switch (currentApp) {
   case APP_SHINE:
     loopShine(now, isTouching);
     delay(2);  // yield to WiFi stack
     break;
   case APP_FLAPPY:
     loopFlappy(now, isTouching);
     break;
   case APP_TILES:
     loopTiles(now, isTouching, touchX, touchY);
     delay(10);
     break;
 }
}


// ============================================================
// HOLD DETECTION — 5s hold triggers tile launcher
// Uses a circular arc indicator at screen center
// ============================================================
void updateHoldDetection(unsigned long now, bool touching) {
 if (touching) {
   if (!holdActive) {
     holdActive = true;
     holdStartTime = now;


     // Allocate hold circle sprite (small — ~2KB)
     if (!holdSprAllocated) {
       holdSpr.setColorDepth(16);
       void* hp = holdSpr.createSprite(HOLD_SPR_SIZE, HOLD_SPR_SIZE);
       if (hp) holdSprAllocated = true;
     }
     // Position: screen center
     holdSprX = SW / 2 - HOLD_SPR_SIZE / 2;
     holdSprY = SH / 2 - HOLD_SPR_SIZE / 2;
   }


   unsigned long elapsed = now - holdStartTime;


   if (elapsed >= HOLD_THRESHOLD_MS) {
     holdActive = false;
     eraseHoldCircle();
     freeHoldSprite();
     Serial.println("Hold detected — transitioning to tiles");
     beginTransition(APP_TILES);
     return;
   }


   // Draw circular arc progress (clockwise from 12 o'clock)
   if (elapsed > 200) {  // small dead zone so quick taps don't flash
     drawHoldCircle((float)elapsed / (float)HOLD_THRESHOLD_MS);
     holdProgressVisible = true;
   }


 } else {
   if (holdActive) {
     holdActive = false;
     if (holdProgressVisible) {
       eraseHoldCircle();
       holdProgressVisible = false;
     }
     freeHoldSprite();
   }
 }
}


// Draw the arc into the sprite and push it
void drawHoldCircle(float progress) {
 if (!holdSprAllocated) return;


 int cx = HOLD_SPR_SIZE / 2;
 int cy = HOLD_SPR_SIZE / 2;


 holdSpr.fillSprite(TFT_MAGENTA);


 // Dark backing ring
 holdSpr.fillCircle(cx, cy, HOLD_CIRCLE_R, 0x2104);
 holdSpr.fillCircle(cx, cy, HOLD_CIRCLE_R - HOLD_CIRCLE_THICK - 2, TFT_MAGENTA);


 // Progress arc using TFT_eSPI's built-in drawArc
 // drawArc angles: 0 = 6 o'clock, clockwise. 12 o'clock = 180.
 uint32_t startDeg = 180;
 uint32_t endDeg = startDeg + (uint32_t)(progress * 360.0f);
 if (endDeg > 540) endDeg = 540;  // clamp at full circle


 holdSpr.drawArc(cx, cy, HOLD_CIRCLE_R, HOLD_CIRCLE_R - HOLD_CIRCLE_THICK,
                 startDeg % 360, endDeg % 360, COL_ACCENT, TFT_MAGENTA, false);


 // Handle wrap-around: if endDeg > 360+180=540, it wraps past 6 o'clock
 if (endDeg > 360) {
   holdSpr.drawArc(cx, cy, HOLD_CIRCLE_R, HOLD_CIRCLE_R - HOLD_CIRCLE_THICK,
                   0, endDeg % 360, COL_ACCENT, TFT_MAGENTA, false);
 }


 holdSpr.pushSprite(holdSprX, holdSprY, TFT_MAGENTA);
}


// Erase the circle area by redrawing what was behind it
void eraseHoldCircle() {
 if (currentApp == APP_SHINE) {
   // Redraw the area — for shi+ne, just fill with black and let the
   // next idle frame redraw the face elements if they overlap
   tft.fillRect(holdSprX, holdSprY, HOLD_SPR_SIZE, HOLD_SPR_SIZE, COL_BG);
   // Force eyes + mouth redraw on next frame
   eyesDrawn = false;
 } else if (currentApp == APP_FLAPPY) {
   // Fill with sky color (the circle is in the sky area)
   tft.fillRect(holdSprX, holdSprY, HOLD_SPR_SIZE, HOLD_SPR_SIZE, 0x6EBE);
 }
}


void freeHoldSprite() {
 if (holdSprAllocated) {
   holdSpr.deleteSprite();
   holdSprAllocated = false;
 }
}


// Switch to tiles with animation
void switchToTiles() {
 beginTransition(APP_TILES);
}


// ============================================================
// CLEAR SCREEN HELPER (preserves offline indicator)
// ============================================================
void clearScreen() {
 tft.fillScreen(COL_BG);
 wifiIndicatorOnScreen = false;
 if (wifiOffline) drawOfflineIndicator();
}


// ============================================================
// SAFE SERVO WRITE
// ============================================================
void safeServoWrite(int angle) {
 flagServo.write(constrain(angle, FLAG_DOWN, FLAG_UP));
}


// Sub-degree write via writeMicroseconds (used by physics return)
void writeServoMicros(float angle) {
 if (angle < (float)FLAG_DOWN) angle = (float)FLAG_DOWN;
 if (angle > (float)FLAG_UP)   angle = (float)FLAG_UP;
 int us = 500 + (int)((angle / 180.0f) * 2000.0f);
 flagServo.writeMicroseconds(us);
}


// ============================================================
// SERVO STATE MACHINE (non-blocking rise -> hold -> return)
// ============================================================
void startServoRise(unsigned long now) {
#if SERVO_RISE_MODE == 1
 safeServoWrite(FLAG_UP);
 servoCurrentAngle = FLAG_UP;
 servoState = SERVO_S_HOLDING;
 servoHoldStart = now;
#else
 servoState = SERVO_S_RISING;
 lastServoStep = now;
#endif
}


void updateServo(unsigned long now) {
 switch (servoState) {
   case SERVO_S_IDLE:
     return;


   case SERVO_S_RISING:
     if (now - lastServoStep < SERVO_RISE_STEP_MS) return;
     lastServoStep = now;
     servoCurrentAngle += SERVO_RISE_STEP_DEG;
     if (servoCurrentAngle >= FLAG_UP) {
       servoCurrentAngle = FLAG_UP;
       servoState = SERVO_S_HOLDING;
       servoHoldStart = now;
     }
     safeServoWrite(servoCurrentAngle);
     break;


   case SERVO_S_HOLDING:
     if (now - servoHoldStart >= SERVO_HOLD_MS) {
#if SERVO_RETURN_MODE == 0
       returnWaypointIndex = 0;
       safeServoWrite(SERVO_RETURN_WAYPOINTS[0]);
       servoCurrentAngle = SERVO_RETURN_WAYPOINTS[0];
       lastServoStep = now;
#else
       physicsP = (float)FLAG_UP;
       physicsV = 0;
       lastPhysicsTick = now;
#endif
       servoState = SERVO_S_RETURNING;
     }
     break;


   case SERVO_S_RETURNING:
#if SERVO_RETURN_MODE == 0
     if (now - lastServoStep < SERVO_RETURN_DWELL_MS) return;
     lastServoStep = now;
     returnWaypointIndex++;
     if (returnWaypointIndex >= SERVO_RETURN_WAYPOINT_COUNT) {
       servoState = SERVO_S_IDLE;
       happyTaskCount = 1;
       return;
     }
     safeServoWrite(SERVO_RETURN_WAYPOINTS[returnWaypointIndex]);
     servoCurrentAngle = SERVO_RETURN_WAYPOINTS[returnWaypointIndex];
#else
     {
       unsigned long elapsed = now - lastPhysicsTick;
       if (elapsed < PHYSICS_TICK_MS) return;
       lastPhysicsTick = now;
       if (elapsed > 50) elapsed = 50;  // stability clamp
       float dt = elapsed / 1000.0f;


       float err = physicsP - (float)FLAG_DOWN;
       float acc = -PHYSICS_SPRING_K * err - PHYSICS_SPRING_D * physicsV;
       physicsV += acc * dt;
       physicsP += physicsV * dt;


       if (fabs(err) < PHYSICS_DONE_POS && fabs(physicsV) < PHYSICS_DONE_VEL) {
         physicsP = (float)FLAG_DOWN;
         writeServoMicros(physicsP);
         servoCurrentAngle = FLAG_DOWN;
         servoState = SERVO_S_IDLE;
         happyTaskCount = 1;
         return;
       }


       writeServoMicros(physicsP);
       servoCurrentAngle = (int)physicsP;
     }
#endif
     break;
 }
}


// ============================================================
// WIFI WATCHDOG (exponential backoff + offline indicator)
// ============================================================
void drawOfflineIndicator() {
 int x = 462, y = 4;
 int w = 14;
 tft.fillTriangle(x + w/2, y, x, y + w, x + w, y + w, TFT_ORANGE);
 tft.drawLine(x + w/2, y + 4, x + w/2, y + w - 4, COL_BG);
 tft.drawLine(x + w/2 - 1, y + 4, x + w/2 - 1, y + w - 4, COL_BG);
 tft.fillCircle(x + w/2, y + w - 2, 1, COL_BG);
 wifiIndicatorOnScreen = true;
}


void clearOfflineIndicator() {
 tft.fillRect(460, 2, 20, 20, COL_BG);
 wifiIndicatorOnScreen = false;
}


void checkWifi(unsigned long now) {
 if (now - lastWifiCheck < WIFI_CHECK_MS) return;
 lastWifiCheck = now;


 bool connected = (WiFi.status() == WL_CONNECTED);


 if (connected) {
   if (wifiOffline) {
     wifiOffline = false;
     currentBackoffMs = WIFI_BACKOFF_START_MS;
     if (currentApp == APP_SHINE) clearOfflineIndicator();
     wifiIndicatorOnScreen = false;
     Serial.println("WiFi: back online");


     // If stuck in MOOD_ERROR from boot and in shi+ne mode, recover
     if (currentApp == APP_SHINE && currentMood == MOOD_ERROR) {
       pollNotion();
       previousCount = completedCount;
       previousTotal = totalCount;
       currentMood = MOOD_IDLE;
       clearScreen();
       spriteEyes(0, 0);
       spriteSmile(18, 6);
       drawTaskCounter();
       displayedDone  = completedCount;
       displayedTotal = totalCount;
       idleStep = 0;
       lastIdleStep = millis();
     }
   }
   return;
 }


 // Disconnected
 if (!wifiOffline) {
   wifiOffline = true;
   Serial.println("WiFi: connection dropped");
 }
 // Only draw indicator in shi+ne mode
 if (currentApp == APP_SHINE && !wifiIndicatorOnScreen) {
   drawOfflineIndicator();
 }


 // Non-blocking reconnect with exponential backoff
 // Skip during active flappy gameplay — WiFi.disconnect blocks 100-300ms
 if (currentApp == APP_FLAPPY && flappyPlaying) return;


 if (now - lastReconnectAttempt > currentBackoffMs) {
   Serial.printf("WiFi: reconnect attempt (next backoff %lu ms)\n", currentBackoffMs);
   WiFi.disconnect();
   WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
   lastReconnectAttempt = now;
   currentBackoffMs = min(currentBackoffMs * 2, WIFI_BACKOFF_MAX_MS);
 }
}



