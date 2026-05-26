// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ayushi Sharma and Neha Sadaye
//
// ============================================================
// shi+ne MODE — face, moods, touch, confetti, Notion polling,
//               idle animation, blink, saccade, counter roll
// ============================================================
// All globals and #includes live in shine.ino (main file).
// Arduino IDE compiles all .ino files as one translation unit.

// ============================================================
// MAIN SHI+NE LOOP (called from loop() when currentApp == APP_SHINE)
// ============================================================
void loopShine(unsigned long now, bool isTouching) {

  // task-added sub-phase (surprised -> normal text, no delay)
  if (currentMood == MOOD_TASK_ADDED) {
    updateTaskAddedPhase(now);
  }

  if (currentMood == MOOD_HAPPY && (now - moodStartTime > HAPPY_DURATION)) {
    confettiActive = false;
    returnToIdle(now);
  }

  if (currentMood == MOOD_TASK_ADDED && (now - moodStartTime > ADDED_DURATION)) {
    returnToIdle(now);
  }

  if (currentMood == MOOD_AGITATED && (now - moodStartTime > AGITATED_DURATION)) {
    returnToIdle(now);
  }

  if (currentMood == MOOD_TICKLE && (now - moodStartTime > TICKLE_DURATION)) {
    returnToIdle(now);
  }

  // Exclude MOOD_HAPPY from polling. pollNotion() blocks for 1-2s on each
  // HTTPS call, which freezes confetti animation if it happens during HAPPY.
  // --- Async Notion polling: trigger background task, check results ---

  // Trigger a new poll if interval elapsed and no poll in progress
  if (!notionPollBusy && !holdActive &&
      currentMood != MOOD_ERROR && currentMood != MOOD_AGITATED &&
      currentMood != MOOD_TICKLE && currentMood != MOOD_HAPPY &&
      currentMood != MOOD_TASK_ADDED &&
      (now - lastPollTime > POLL_INTERVAL)) {
    lastPollTime = now;
    notionPollBusy = true;
    xTaskNotifyGive(notionTaskHandle);  // wake the background task
  }

  // Check if results are ready (non-blocking)
  if (notionResultReady && xSemaphoreTake(notionMutex, 0)) {
    completedCount = notionResultDone;
    totalCount = notionResultTotal;
    lastTaskName = String(notionResultTask);
    notionResultReady = false;
    xSemaphoreGive(notionMutex);

    // Process results — same trigger logic as before
    if (previousCount >= 0 && completedCount > previousCount) {
      int delta = completedCount - previousCount;

      if (servoState != SERVO_S_IDLE) {
        happyTaskCount += delta;
        if (currentMood != MOOD_HAPPY) {
          currentMood = MOOD_HAPPY;
          moodStartTime = now;
          clearScreen();
          drawHappyCrescents();
          spriteSmile(22, 9);
          happyPhase = 0;
          happyFaceIsCresc = false;
          initConfetti();
        } else {
          moodStartTime = now;
        }
        if (servoState == SERVO_S_RETURNING) {
          startServoRise(now);
        } else if (servoState == SERVO_S_HOLDING) {
          servoHoldStart = now;
        }
        drawCelebrationText(totalCount - completedCount);
      } else {
        happyTaskCount = delta;
        triggerCelebration();
      }
    } else if (previousTotal >= 0 && totalCount > previousTotal && completedCount == previousCount) {
      triggerTaskAdded();
    }
    previousCount = completedCount;
    previousTotal = totalCount;

    if (currentMood == MOOD_IDLE && !counterRolling &&
        (displayedDone != completedCount || displayedTotal != totalCount)) {
      startCounterRoll(displayedDone, displayedTotal, completedCount, totalCount);
    }
  }

  if (currentMood == MOOD_IDLE) {
    // Suppress eye animations during hold (22ms SPI blocks the hold circle)
    if (!holdActive) {
      handleIdleAnimation(now);
      handleSmoothBlink(now);
      handlePupilTween(now);
    }
    handleCounterRoll(now);
  }

  if (currentMood == MOOD_HAPPY) {
    handleHappyAnim(now);
  }

  // touch works during idle AND tickle (so rapid taps still count)
  if (currentMood == MOOD_IDLE || currentMood == MOOD_TICKLE) {
    handleShineTouch(now, isTouching);
  }
}

// Called when entering shi+ne mode from tiles
void enterShineMode() {
  currentMood = MOOD_IDLE;
  tft.fillScreen(COL_BG);
  wifiIndicatorOnScreen = false;

  // Immediate poll to catch up on any changes while away
  pollNotion();
  previousCount = completedCount;
  previousTotal = totalCount;
  displayedDone  = completedCount;
  displayedTotal = totalCount;
  lastPollTime = millis();

  spriteEyes(0, 0);
  spriteSmile(18, 6);
  drawTaskCounter();

  if (wifiOffline) drawOfflineIndicator();

  idleStep = 0;
  lastIdleStep = millis();
  pupilMoving = false;
  eyesDrawn = true;
}

// ============================================================
// RETURN TO IDLE
// ============================================================
void returnToIdle(unsigned long now) {
  currentMood = MOOD_IDLE;
  clearScreen();
  eyesDrawn = false;
  spriteEyes(0, 0);
  spriteSmile(18, 6);

  if (displayedDone >= 0 &&
      (displayedDone != completedCount || displayedTotal != totalCount)) {
    startCounterRoll(displayedDone, displayedTotal, completedCount, totalCount);
  } else {
    drawTaskCounter();
    displayedDone  = completedCount;
    displayedTotal = totalCount;
  }

  idleStep = 0;
  lastIdleStep = now;
  pupilMoving = false;
}

// ============================================================
// TOUCH - tap once = tickle, 5 rapid taps = agitated
// ============================================================
void handleShineTouch(unsigned long now, bool touching) {
  // Suppress taps while user is holding for mode switch
  if (holdActive && (now - holdStartTime > 300)) return;

  if (touching && !touchWasDown) {
    touchWasDown = true;

    if (now - lastTapTime > 4000) {
      tapCount = 0;
    }

    tapCount++;
    lastTapTime = now;
    Serial.printf("Tap %d\n", tapCount);

    if (tapCount >= 5) {
      triggerAgitated();
      tapCount = 0;
      return;
    }

    triggerTickle();
  }

  if (!touching && touchWasDown) {
    touchWasDown = false;
  }
}

// ============================================================
// TICKLE - happy crescents + smile + "it tickles"
// ============================================================
void triggerTickle() {
  currentMood = MOOD_TICKLE;
  moodStartTime = millis();

  clearScreen();
  eyesDrawn = false;

  drawHappyCrescents();
  spriteSmile(22, 9);

  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("it tickles", SW / 2, TEXT_ZONE_TOP + 20);
}

// ============================================================
// AGITATED - X eyes + frown + message
// ============================================================
void triggerAgitated() {
  currentMood = MOOD_AGITATED;
  moodStartTime = millis();

  clearScreen();
  eyesDrawn = false;

  drawXEyes();
  spriteBigFrown();

  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("Stop it, you moron.", SW / 2, TEXT_ZONE_TOP + 20);
}

void drawXEyes() {
  int xSize = 30;
  for (int t = -3; t <= 3; t++) {
    tft.drawLine(L_EX - xSize, EY - xSize + t, L_EX + xSize, EY + xSize + t, COL_WHITE);
    tft.drawLine(L_EX + xSize, EY - xSize + t, L_EX - xSize, EY + xSize + t, COL_WHITE);
    tft.drawLine(R_EX - xSize, EY - xSize + t, R_EX + xSize, EY + xSize + t, COL_WHITE);
    tft.drawLine(R_EX + xSize, EY - xSize + t, R_EX - xSize, EY + xSize + t, COL_WHITE);
  }
  eyesDrawn = false;
}

// ============================================================
// SPRITE: ONE EYE
// ============================================================
void pushOneEye(int cx, int cy, int pupilOffX, int pupilOffY) {
  int sprCX = EYE_SPR_W / 2;
  int sprCY = EYE_SPR_H / 2;

  eyeSpr.fillSprite(COL_BG);
  eyeSpr.fillCircle(sprCX, sprCY, EYE_R, COL_WHITE);
  eyeSpr.drawCircle(sprCX, sprCY, EYE_R, COL_LID);
  eyeSpr.fillCircle(sprCX + pupilOffX, sprCY + pupilOffY, PUPIL_R, COL_PUPIL);
  eyeSpr.pushSprite(cx - sprCX, cy - sprCY);
}

void pushOneEyeBlink(int cx, int cy, int h, int pupilOffX, int pupilOffY) {
  int sprCX = EYE_SPR_W / 2;
  int sprCY = EYE_SPR_H / 2;

  eyeSpr.fillSprite(COL_BG);

  if (h < 8) {
    for (int t = -2; t <= 2; t++) {
      eyeSpr.drawLine(sprCX - EYE_R + 12, sprCY + t,
                      sprCX + EYE_R - 12, sprCY + t, COL_WHITE);
    }
  } else {
    eyeSpr.fillCircle(sprCX, sprCY, EYE_R, COL_WHITE);
    eyeSpr.fillCircle(sprCX + pupilOffX, sprCY + pupilOffY, PUPIL_R, COL_PUPIL);

    int coverH = (EYE_R * 2 - h) / 2;
    if (coverH > 0) {
      eyeSpr.fillRect(0, 0, EYE_SPR_W, coverH, COL_BG);
      eyeSpr.fillRect(0, EYE_SPR_H - coverH, EYE_SPR_W, coverH, COL_BG);

      int topY = coverH;
      int botY = EYE_SPR_H - coverH;
      int dy1 = topY - sprCY;
      int dy2 = botY - sprCY;

      if (abs(dy1) < EYE_R) {
        float c = sqrt((float)(EYE_R * EYE_R - dy1 * dy1));
        eyeSpr.drawLine(sprCX - (int)c, topY, sprCX + (int)c, topY, COL_LID);
        eyeSpr.drawLine(sprCX - (int)c, topY + 1, sprCX + (int)c, topY + 1, COL_LID);
      }
      if (abs(dy2) < EYE_R) {
        float c = sqrt((float)(EYE_R * EYE_R - dy2 * dy2));
        eyeSpr.drawLine(sprCX - (int)c, botY, sprCX + (int)c, botY, COL_LID);
        eyeSpr.drawLine(sprCX - (int)c, botY - 1, sprCX + (int)c, botY - 1, COL_LID);
      }
    }
    eyeSpr.drawCircle(sprCX, sprCY, EYE_R, COL_LID);
  }

  eyeSpr.pushSprite(cx - sprCX, cy - sprCY);
}

// ============================================================
// SPRITE: BOTH EYES
// ============================================================
void spriteEyes(int pupilOffX, int pupilOffY) {
  pushOneEye(L_EX, EY, pupilOffX, pupilOffY);
  pushOneEye(R_EX, EY, pupilOffX, pupilOffY);
  curPupilX = pupilOffX;
  curPupilY = pupilOffY;
  eyesDrawn = true;
}

void movePupils(int newOffX, int newOffY) {
  if (newOffX == curPupilX && newOffY == curPupilY && eyesDrawn) return;
  spriteEyes(newOffX, newOffY);
}

// ============================================================
// SPRITE: MOUTH
// ============================================================
void spriteSmile(int w, int depth) {
  int cx = MOUTH_SPR_W / 2;
  int cy = 5;
  mouthSpr.fillSprite(COL_BG);
  for (int x = -w; x <= w; x++) {
    float n = (float)x / (float)w;
    int y = (int)(depth * (1.0 - n * n));
    mouthSpr.fillCircle(cx + x, cy + y, 2, COL_WHITE);
  }
  mouthSpr.pushSprite(SW / 2 - MOUTH_SPR_W / 2, MOUTH_Y - 5);
}

void spriteFrown(int w, int depth) {
  int cx = MOUTH_SPR_W / 2;
  int cy = 5;
  mouthSpr.fillSprite(COL_BG);
  for (int x = -w; x <= w; x++) {
    float n = (float)x / (float)w;
    int y = (int)(depth * n * n);
    mouthSpr.fillCircle(cx + x, cy + y, 2, COL_WHITE);
  }
  mouthSpr.pushSprite(SW / 2 - MOUTH_SPR_W / 2, MOUTH_Y - 5);
}

void spriteBigFrown() {
  int cx = MOUTH_SPR_W / 2;
  int cy = 5;
  int w = 40;
  mouthSpr.fillSprite(COL_BG);
  for (int x = -w; x <= w; x++) {
    float n = (float)x / (float)w;
    int y = (int)(15.0 * n * n);
    mouthSpr.fillCircle(cx + x, cy + y, 2, COL_WHITE);
  }
  mouthSpr.pushSprite(SW / 2 - MOUTH_SPR_W / 2, MOUTH_Y);
}

void spriteSmallO() {
  int cx = MOUTH_SPR_W / 2;
  int cy = MOUTH_SPR_H / 2;
  mouthSpr.fillSprite(COL_BG);
  mouthSpr.drawCircle(cx, cy, 8, COL_WHITE);
  mouthSpr.drawCircle(cx, cy, 7, COL_WHITE);
  mouthSpr.drawCircle(cx, cy, 6, COL_WHITE);
  mouthSpr.pushSprite(SW / 2 - MOUTH_SPR_W / 2, MOUTH_Y - 5);
}

// ============================================================
// SPRITE: CONFETTI
// ============================================================
void initConfetti() {
  for (int i = 0; i < NUM_CONFETTI; i++) {
    conf[i].leftSide = (i < 5);
    if (conf[i].leftSide) conf[i].x = random(3, CONF_L_W - 3);
    else conf[i].x = random(3, CONF_R_W - 3);
    conf[i].y = random(-80, 0);
    conf[i].speed = 1.2 + (random(0, 25) / 10.0);
    conf[i].size = (random(0, 3) == 0) ? random(10, 16) : random(3, 8);
    conf[i].color = confColors[random(NUM_CONF_COLORS)];
  }
  confettiActive = true;
}

void spriteConfetti() {
  if (!confettiActive) return;

  confLSpr.fillSprite(COL_BG);
  confRSpr.fillSprite(COL_BG);

  for (int i = 0; i < NUM_CONFETTI; i++) {
    conf[i].y += conf[i].speed;

    if (conf[i].y > SH + 15) {
      conf[i].y = random(-40, -5);
      conf[i].size = (random(0, 3) == 0) ? random(10, 16) : random(3, 8);
      conf[i].color = confColors[random(NUM_CONF_COLORS)];
      if (conf[i].leftSide) conf[i].x = random(3, CONF_L_W - 3);
      else conf[i].x = random(3, CONF_R_W - 3);
    }

    if (conf[i].y < 0 || conf[i].y >= SH) continue;

    int px = (int)conf[i].x;
    int py = (int)conf[i].y;
    int s = conf[i].size;

    if (conf[i].leftSide) {
      if (s <= 5) confLSpr.fillCircle(px, py, s / 2 + 1, conf[i].color);
      else confLSpr.fillRoundRect(px, py, s, s, 3, conf[i].color);
    } else {
      if (s <= 5) confRSpr.fillCircle(px, py, s / 2 + 1, conf[i].color);
      else confRSpr.fillRoundRect(px, py, s, s, 3, conf[i].color);
    }
  }

  confLSpr.pushSprite(CONF_L_X, 0);
  confRSpr.pushSprite(CONF_R_X, 0);

  if (wifiOffline) drawOfflineIndicator();
}

// ============================================================
// SPECIAL FACES
// ============================================================
void pushOneCrescent(int cx, int cy) {
  int sprCX = EYE_SPR_W / 2;
  int sprCY = EYE_SPR_H / 2;
  eyeSpr.fillSprite(COL_BG);
  for (int t = -4; t <= 4; t++) {
    for (int x = -42; x <= 42; x++) {
      float n = (float)x / 42.0;
      int y = -(int)(28.0 * (1.0 - n * n));
      eyeSpr.fillCircle(sprCX + x, sprCY + y + t, 1, COL_WHITE);
    }
  }
  eyeSpr.pushSprite(cx - sprCX, cy - sprCY);
}

void drawHappyCrescents() {
  pushOneCrescent(L_EX, EY);
  pushOneCrescent(R_EX, EY);
  eyesDrawn = false;
}

void drawSurprisedFace() {
  int bigR = EYE_R + 12;
  tft.fillCircle(L_EX, EY, bigR, COL_WHITE);
  tft.drawCircle(L_EX, EY, bigR, COL_LID);
  tft.fillCircle(L_EX, EY, PUPIL_R - 5, COL_PUPIL);

  tft.fillCircle(R_EX, EY, bigR, COL_WHITE);
  tft.drawCircle(R_EX, EY, bigR, COL_LID);
  tft.fillCircle(R_EX, EY, PUPIL_R - 5, COL_PUPIL);
  eyesDrawn = false;
}

// ============================================================
// NEW EXPRESSIONS
// ============================================================

// SLEEPY — half-lid eyes (droopy, like about to fall asleep)
void drawSleepyEyes(int pupilOffX, int pupilOffY) {
  int halfH = EYE_R;  // half the full height (EYE_R*2 = full)
  pushOneEyeBlink(L_EX, EY, halfH, pupilOffX, pupilOffY);
  pushOneEyeBlink(R_EX, EY, halfH, pupilOffX, pupilOffY);
  curPupilX = pupilOffX;
  curPupilY = pupilOffY;
  eyesDrawn = true;
}

// CURIOUS — left eye normal, right eye has a raised "eyebrow" arc above it
void drawCuriousEyes(int pupilOffX, int pupilOffY) {
  // Left eye: normal
  pushOneEye(L_EX, EY, pupilOffX, pupilOffY);

  // Right eye: slightly bigger + eyebrow arc above
  pushOneEye(R_EX, EY, pupilOffX, pupilOffY - 2);  // pupil shifted up slightly (alert)

  // Draw eyebrow arc above right eye (directly to tft, over the sprite)
  int browCX = R_EX;
  int browY = EY - EYE_R - 8;
  for (int x = -30; x <= 30; x++) {
    float n = (float)x / 30.0f;
    int y = -(int)(8.0f * (1.0f - n * n));
    for (int t = 0; t < 3; t++) {
      tft.drawPixel(browCX + x, browY + y + t, COL_WHITE);
    }
  }

  curPupilX = pupilOffX;
  curPupilY = pupilOffY;
  eyesDrawn = true;
}

// THINKING — dots mouth (three circles in a row, like "...")
void spriteThinkingMouth() {
  int cx = MOUTH_SPR_W / 2;
  int cy = MOUTH_SPR_H / 2;
  mouthSpr.fillSprite(COL_BG);
  mouthSpr.fillCircle(cx - 15, cy, 3, COL_WHITE);
  mouthSpr.fillCircle(cx, cy, 3, COL_WHITE);
  mouthSpr.fillCircle(cx + 15, cy, 3, COL_WHITE);
  mouthSpr.pushSprite(SW / 2 - MOUTH_SPR_W / 2, MOUTH_Y - 5);
}

// ============================================================
// SMOOTH BLINK
// ============================================================
void handleSmoothBlink(unsigned long now) {
  if (!isBlinking) return;
  if (now - lastBlinkFrame < 30) return;
  lastBlinkFrame = now;

  blinkFrame++;
  int fullH = EYE_R * 2;
  int h;

  switch (blinkFrame) {
    case 1: h = fullH * 3 / 4; break;
    case 2: h = fullH / 3;     break;
    case 3: h = 6;             break;
    case 4: h = fullH / 3;     break;
    case 5: h = fullH * 3 / 4; break;
    default:
      isBlinking = false;
      blinkFrame = 0;
      spriteEyes(curPupilX, curPupilY);
      return;
  }

  pushOneEyeBlink(L_EX, EY, h, curPupilX, curPupilY);
  pushOneEyeBlink(R_EX, EY, h, curPupilX, curPupilY);
}

void startBlink() {
  if (isBlinking) return;
  isBlinking = true;
  blinkFrame = 0;
  lastBlinkFrame = millis();
}

// ============================================================
// BEE — flies around the face, eyes track it
// ============================================================
// Bee constants (state variables are in shine.ino globals)
const int BEE_W = 14, BEE_H = 10;
const unsigned long BEE_DURATION = 6000;
const float BEE_SPEED = 0.003f;

void allocateBeeSprite() {
  if (beeSprAllocated) return;
  beeSpr.setColorDepth(16);
  beeSpr.setAttribute(PSRAM_ENABLE, false);
  void* p = beeSpr.createSprite(BEE_W, BEE_H);
  if (p) beeSprAllocated = true;
}

void freeBeeSprite() {
  if (beeSprAllocated) {
    beeSpr.deleteSprite();
    beeSprAllocated = false;
  }
}

// Draw bee into sprite — 2-frame wing animation
void renderBee(int wingFrame) {
  if (!beeSprAllocated) return;
  beeSpr.fillSprite(COL_BG);
  // Body (yellow-black stripes)
  beeSpr.fillEllipse(6, 5, 5, 3, 0xFFE0);   // yellow
  beeSpr.fillRect(4, 4, 2, 3, TFT_BLACK);    // stripe 1
  beeSpr.fillRect(8, 4, 2, 3, TFT_BLACK);    // stripe 2
  // Head
  beeSpr.fillCircle(12, 5, 2, TFT_BLACK);
  // Wings (flutter between up and down)
  if (wingFrame == 0) {
    beeSpr.fillEllipse(5, 1, 4, 2, 0xCE9A);  // wings up (translucent white-ish)
  } else {
    beeSpr.fillEllipse(5, 3, 4, 2, 0xCE9A);  // wings down
  }
  // Stinger
  beeSpr.drawPixel(0, 5, 0x8410);
}

// Figure-8 flight path around the face
void getBeePosition(float t, int* outX, int* outY) {
  // Lemniscate of Bernoulli (figure-8), centered on face
  // Scaled to orbit around the eye area
  float angle = t * 2.0f * 3.14159f;
  float denom = 1.0f + sin(angle) * sin(angle);
  float fx = cos(angle) / denom;
  float fy = sin(angle) * cos(angle) / denom;

  // Scale and center: orbits around face center (SW/2, EY)
  // Wide horizontal range (covers both eyes), moderate vertical
  *outX = (int)(SW / 2 + fx * 160.0f);
  *outY = (int)(EY - 20 + fy * 80.0f);
}

// Map bee screen position to pupil offset (eyes track the bee)
void trackBeeWithEyes(int beeX, int beeY) {
  // Average the direction from both eyes to the bee
  float midEyeX = (L_EX + R_EX) / 2.0f;
  float midEyeY = EY;

  float dx = beeX - midEyeX;
  float dy = beeY - midEyeY;
  float dist = sqrt(dx * dx + dy * dy);
  if (dist < 1) dist = 1;

  // Normalize and scale to max pupil offset (14px)
  int px = (int)(dx / dist * 14.0f);
  int py = (int)(dy / dist * 10.0f);
  px = constrain(px, -14, 14);
  py = constrain(py, -10, 10);

  spriteEyes(px, py);
}

void startBee(unsigned long now) {
  if (!beeSprAllocated) allocateBeeSprite();
  beeActive = true;
  beeT = 0;
  beeStartTime = now;
  lastBeeFrame = now;
  beePrevX = -50;
  beePrevY = -50;
  beeWingFrame = 0;
}

void handleBee(unsigned long now) {
  if (!beeActive) {
    // Check if it's time to spawn a bee
    if (nextBeeTime == 0) nextBeeTime = now + random(20000, 45000);
    if (now >= nextBeeTime) {
      startBee(now);
      nextBeeTime = 0;
    }
    return;
  }

  // Frame limiter (~30 FPS for bee)
  if (now - lastBeeFrame < 33) return;
  lastBeeFrame = now;

  // Check if bee visit is done
  if (now - beeStartTime > BEE_DURATION) {
    // Erase last bee position
    if (beePrevX >= 0 && beePrevX < SW && beePrevY >= 0 && beePrevY < SH) {
      tft.fillRect(beePrevX, beePrevY, BEE_W, BEE_H, COL_BG);
    }
    beeActive = false;
    // Return eyes to center
    movePupilsTo(0, 0);
    return;
  }

  // Advance parametric position
  beeT += BEE_SPEED;
  if (beeT > 1.0f) beeT -= 1.0f;

  int beeX, beeY;
  getBeePosition(beeT, &beeX, &beeY);

  // Erase previous position
  if (beePrevX >= 0 && beePrevX < SW && beePrevY >= 0 && beePrevY < SH) {
    tft.fillRect(beePrevX, beePrevY, BEE_W, BEE_H, COL_BG);
  }

  // Wing animation (flutter fast)
  beeWingFrame = (beeWingFrame + 1) % 2;
  renderBee(beeWingFrame);

  // Draw bee at new position
  if (beeX >= 0 && beeX < SW - BEE_W && beeY >= 0 && beeY < SH - BEE_H) {
    beeSpr.pushSprite(beeX, beeY, COL_BG);
  }

  // Eyes track the bee
  trackBeeWithEyes(beeX, beeY);

  beePrevX = beeX;
  beePrevY = beeY;
}

// ============================================================
// IDLE ANIMATION
// ============================================================
void handleIdleAnimation(unsigned long now) {
  // Bee takes priority over normal idle eye movement
  if (beeActive) {
    handleBee(now);
    return;
  }

  // Normal idle: check for bee spawn
  handleBee(now);

  if (isBlinking) return;
  if (pupilMoving) return;
  if (now - lastIdleStep < IDLE_STEP_TIME) return;
  lastIdleStep = now;

  switch (idleStep) {
    case 0:  movePupilsTo(0, 0);     break;
    case 1:  startBlink();           break;
    case 2:  movePupilsTo(14, 0);    break;
    case 3:  startBlink();           break;
    case 4:  movePupilsTo(-14, 0);   break;
    case 5:  startBlink();           break;
    case 6:  movePupilsTo(0, -14);   break;
    case 7:  startBlink();           break;
  }

  idleStep++;
  if (idleStep > 7) idleStep = 0;
}

// ============================================================
// CELEBRATION
// ============================================================
void triggerCelebration() {
  currentMood = MOOD_HAPPY;
  moodStartTime = millis();
  happyPhase = 0;
  happyFaceIsCresc = false;

  clearScreen();
  eyesDrawn = false;

  startServoRise(millis());

  drawHappyCrescents();
  spriteSmile(22, 9);

  int remaining = totalCount - completedCount;
  drawCelebrationText(remaining);

  initConfetti();
}

void drawCelebrationText(int remaining) {
  tft.fillRect(0, TEXT_ZONE_TOP, SW, SH - TEXT_ZONE_TOP, COL_BG);
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextDatum(TC_DATUM);

  tft.setFreeFont(&FreeSansBold12pt7b);
  String headerMsg;
  if (happyTaskCount <= 1) {
    headerMsg = "Yay, You did it!";
  } else {
    headerMsg = "+" + String(happyTaskCount) + " tasks done!";
  }
  tft.drawString(headerMsg, SW / 2, TEXT_ZONE_TOP + 5);

  tft.setFreeFont(&FreeSans9pt7b);
  if (happyTaskCount <= 1) {
    String taskMsg = lastTaskName;
    if (taskMsg.length() > 26) taskMsg = taskMsg.substring(0, 26) + "..";
    tft.drawString(taskMsg, SW / 2, TEXT_ZONE_TOP + 32);
  } else {
    tft.drawString("Nice work!", SW / 2, TEXT_ZONE_TOP + 32);
  }

  tft.setFreeFont(&FreeSansBold9pt7b);
  String goMsg;
  if (remaining <= 0) goMsg = "All tasks done!";
  else if (remaining == 1) goMsg = "1 task more to go";
  else goMsg = String(remaining) + " tasks more to go";
  tft.drawString(goMsg, SW / 2, TEXT_ZONE_TOP + 56);
}

void handleHappyAnim(unsigned long now) {
  if (now - lastAnimFrame > 80) {
    lastAnimFrame = now;
    spriteConfetti();
  }

  if (now - lastHappySwap > 1500) {
    lastHappySwap = now;
    happyPhase++;

    if (happyPhase % 2 == 0) {
      if (!happyFaceIsCresc) {
        drawHappyCrescents();
        spriteSmile(22, 9);
        happyFaceIsCresc = true;
      }
    } else {
      if (happyFaceIsCresc) {
        spriteEyes(0, 0);
        spriteSmile(22, 9);
        happyFaceIsCresc = false;
      }
    }
  }
}

// ============================================================
// TASK ADDED
// ============================================================
void triggerTaskAdded() {
  currentMood = MOOD_TASK_ADDED;
  moodStartTime = millis();
  taskAddedPhase = TA_SURPRISED;
  taskAddedPhaseStart = millis();

  clearScreen();
  eyesDrawn = false;

  drawSurprisedFace();
  spriteSmallO();
}

void updateTaskAddedPhase(unsigned long now) {
  if (taskAddedPhase != TA_SURPRISED) return;
  if (now - taskAddedPhaseStart < TASK_ADDED_SURPRISED_MS) return;

  taskAddedPhase = TA_NORMAL;

  clearScreen();
  spriteEyes(0, 0);
  spriteSmile(18, 6);

  int pending = totalCount - completedCount;
  tft.setTextColor(COL_WHITE, COL_BG);
  tft.setTextDatum(TC_DATUM);

  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("New task!", SW / 2, TEXT_ZONE_TOP + 5);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString(String(totalCount) + " tasks total", SW / 2, TEXT_ZONE_TOP + 32);

  tft.setFreeFont(&FreeSansBold9pt7b);
  String pendMsg = (pending == 1) ? "1 pending task" : String(pending) + " pending tasks";
  tft.drawString(pendMsg, SW / 2, TEXT_ZONE_TOP + 56);
}

// ============================================================
// TEXT
// ============================================================
void drawBottomText(String text) {
  tft.fillRect(0, TEXT_ZONE_TOP, SW, SH - TEXT_ZONE_TOP, COL_BG);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString(text, SW / 2, TEXT_ZONE_TOP + 20);
}

void drawTaskCounter() {
  tft.fillRect(0, TEXT_ZONE_TOP, SW, SH - TEXT_ZONE_TOP, COL_BG);
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d / %d tasks done", completedCount, totalCount);
  tft.drawString(buf, SW / 2, TEXT_ZONE_TOP + 20);
}

// ============================================================
// NOTION API
// ============================================================
void pollNotion() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("pollNotion: skipping (offline, watchdog will retry)");
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String url = String("https://api.notion.com/v1/databases/") + DATABASE_ID + "/query";

  http.begin(client, url);
  http.addHeader("Authorization", String("Bearer ") + NOTION_TOKEN);
  http.addHeader("Notion-Version", "2022-06-28");
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST("{}");
  Serial.printf("Notion response: %d\n", httpCode);

  if (httpCode == 200) {
    String payload = http.getString();
    DynamicJsonDocument doc(32768);
    DeserializationError err = deserializeJson(doc, payload);

    if (!err) {
      JsonArray results = doc["results"];
      totalCount = results.size();
      completedCount = 0;
      lastTaskName = "";

      for (int i = 0; i < totalCount; i++) {
        JsonObject page = results[i];
        bool isDone = page["properties"][CHECKBOX_PROPERTY]["checkbox"] | false;
        if (isDone) {
          completedCount++;
          JsonObject titleProp = page["properties"][TITLE_PROPERTY];
          if (titleProp.containsKey("title")) {
            JsonArray titleArr = titleProp["title"];
            if (titleArr.size() > 0) {
              lastTaskName = titleArr[0]["plain_text"].as<String>();
            }
          }
        }
      }
      Serial.printf("Tasks: %d/%d done\n", completedCount, totalCount);
    } else {
      Serial.println("JSON parse error: " + String(err.c_str()));
    }
  } else {
    Serial.println("Notion API error: " + String(httpCode));
    if (httpCode > 0) Serial.println(http.getString().substring(0, 500));
  }
  http.end();
}

// ============================================================
// PUPIL SACCADE TWEEN
// ============================================================
void movePupilsTo(int x, int y) {
  if (x == curPupilX && y == curPupilY) {
    pupilMoving = false;
    return;
  }
  targetPupilX = x;
  targetPupilY = y;
  pupilMoving  = true;
  lastPupilStep = millis();
}

void handlePupilTween(unsigned long now) {
  if (!pupilMoving) return;
  if (isBlinking) return;
  if (now - lastPupilStep < PUPIL_STEP_MS) return;
  lastPupilStep = now;

  int dx = targetPupilX - curPupilX;
  int dy = targetPupilY - curPupilY;

  if (dx == 0 && dy == 0) {
    pupilMoving = false;
    return;
  }

  int stepX = constrain(dx, -PUPIL_STEP_PX, PUPIL_STEP_PX);
  int stepY = constrain(dy, -PUPIL_STEP_PX, PUPIL_STEP_PX);

  spriteEyes(curPupilX + stepX, curPupilY + stepY);
}

// ============================================================
// TASK COUNTER ANIMATIONS
// ============================================================

// Slot-machine roll
void renderCounterSlotFrame(int frame) {
  counterSpr.fillSprite(COL_BG);
  counterSpr.setTextColor(COL_DIM);
  counterSpr.setTextDatum(TC_DATUM);
  counterSpr.setFreeFont(&FreeSansBold12pt7b);

  int progress = (frame * COUNTER_SPR_H) / COUNTER_ANIM_FRAMES;
  int yOld = 16 - progress;
  int yNew = 16 + COUNTER_SPR_H - progress;

  char oldBuf[40], newBuf[40];
  snprintf(oldBuf, sizeof(oldBuf), "%d / %d tasks done", counterRollFromDone, counterRollFromTotal);
  snprintf(newBuf, sizeof(newBuf), "%d / %d tasks done", counterRollToDone, counterRollToTotal);

  counterSpr.drawString(oldBuf, COUNTER_SPR_W / 2, yOld);
  counterSpr.drawString(newBuf, COUNTER_SPR_W / 2, yNew);

  counterSpr.pushSprite(SW / 2 - COUNTER_SPR_W / 2, TEXT_ZONE_TOP + 4);
}

// Bounce/pulse
void renderCounterBounceFrame(int frame) {
  counterSpr.fillSprite(COL_BG);
  counterSpr.setTextDatum(TC_DATUM);
  counterSpr.setFreeFont(&FreeSansBold12pt7b);

  int yOffset;
  switch (frame) {
    case 0: yOffset = -14; break;
    case 1: yOffset = -6;  break;
    case 2: yOffset = 4;   break;
    case 3: yOffset = -1;  break;
    case 4: yOffset = 0;   break;
    default: yOffset = 0;  break;
  }

  counterSpr.setTextColor(COL_DIM);
  char buf[40];
  snprintf(buf, sizeof(buf), "%d / %d tasks done", counterRollToDone, counterRollToTotal);
  counterSpr.drawString(buf, COUNTER_SPR_W / 2, 16 + yOffset);

  counterSpr.pushSprite(SW / 2 - COUNTER_SPR_W / 2, TEXT_ZONE_TOP + 4);
}

void renderCounterRollFrame(int frame) {
#if COUNTER_ANIM_MODE == 0
  renderCounterSlotFrame(frame);
#else
  renderCounterBounceFrame(frame);
#endif
}

void startCounterRoll(int oldDone, int oldTotal, int newDone, int newTotal) {
  counterRollFromDone  = oldDone;
  counterRollFromTotal = oldTotal;
  counterRollToDone    = newDone;
  counterRollToTotal   = newTotal;
  counterRolling       = true;
  counterRollFrame     = 0;
  lastCounterRollFrame = millis();
  tft.fillRect(0, TEXT_ZONE_TOP, SW, SH - TEXT_ZONE_TOP, COL_BG);
}

void handleCounterRoll(unsigned long now) {
  if (!counterRolling) return;
  if (now - lastCounterRollFrame < COUNTER_ROLL_MS) return;
  lastCounterRollFrame = now;

  renderCounterRollFrame(counterRollFrame);

  counterRollFrame++;
  if (counterRollFrame >= COUNTER_ROLL_FRAMES) {
    counterRolling = false;
    displayedDone  = counterRollToDone;
    displayedTotal = counterRollToTotal;
    drawTaskCounter();
  }
}
