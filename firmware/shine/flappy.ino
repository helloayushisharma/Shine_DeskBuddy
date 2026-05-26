// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ayushi Sharma and Neha Sadaye
//
// ============================================================
// FLAPPY BIRD — visual overhaul + tuned physics for 480x320 TFT
// ============================================================

// ============================================================
// CONSTANTS
// ============================================================
// Physics — tuned for 60 FPS (halved from 30 FPS values)
const float FB_GRAVITY      = 0.19f;   // gentler fall
const float FB_FLAP_VEL     = -3.8f;   // controlled flap
const float FB_TERMINAL_VEL = 4.5f;    // slower max fall

const int FB_BIRD_W    = 34;
const int FB_BIRD_H    = 24;
const int FB_BIRD_X    = 90;
const int FB_HITBOX_INSET = 4;  // forgiving collision

const int FB_PIPE_W       = 52;
const int FB_PIPE_CAP_W   = 60;
const int FB_PIPE_CAP_H   = 18;
const int FB_PIPE_GAP     = 110;  // wider gap = easier
const int FB_PIPE_SCROLL  = 2;    // slower scroll = more time to react
const int FB_PIPE_SPACING = 220;
const int FB_PIPE_GAP_MIN = 90;   // ensures cap doesn't clip top (gap/2 + capH + margin)
const int FB_PIPE_GAP_MAX = 190;  // ensures cap doesn't clip ground

const int FB_GROUND_H = 56;
const int FB_SKY_H    = SH - FB_GROUND_H;  // 264

const unsigned long FB_FRAME_MS = 16;  // target 60 FPS

// Colors (matched to original screenshots)
const uint16_t COL_SKY      = 0x4E79;  // #4EC0CA teal sky
const uint16_t COL_SKY_BOT  = 0x6EBE;  // slightly lighter toward horizon
const uint16_t COL_CLOUD    = 0xE73C;  // off-white clouds
const uint16_t COL_CITY     = 0x9E13;  // light green city silhouette
const uint16_t COL_PIPE     = 0x5DE5;  // #73BF2E
const uint16_t COL_PIPE_LT  = 0x8EA3;  // #99D42C highlight
const uint16_t COL_PIPE_DK  = 0x4461;  // #456B1A dark edge
const uint16_t COL_PIPE_CAP = 0x5DE5;  // same green, with dark border
const uint16_t COL_GRASS_1  = 0x5E05;  // bright green grass
const uint16_t COL_GRASS_2  = 0x4CE1;  // dark green stripe
const uint16_t COL_GROUND   = 0xD507;  // #D6A038 tan dirt
const uint16_t COL_GROUND_LN= 0xCCA5;  // slightly darker dirt line
const uint16_t COL_BIRD     = 0xFE49;  // #F7C948 yellow body
const uint16_t COL_BIRD_RED = 0xFA20;  // #F64000 red-orange head
const uint16_t COL_BIRD_WHT = 0xFFDF;  // cream belly
const uint16_t COL_BEAK     = 0xE305;  // #E8662B orange beak
const uint16_t COL_WING_DK  = 0xDEA0;  // wing shadow

// ============================================================
// STATE
// ============================================================
enum FlappyState { FB_SPLASH, FB_PLAYING, FB_DYING, FB_GAMEOVER };
FlappyState fbState = FB_SPLASH;

float fbBirdY = 0, fbBirdVel = 0, fbPrevBirdY = 0;
int   fbBirdFrame = 0;
unsigned long fbLastBirdAnim = 0;

#define FB_MAX_PIPES 4
struct FBPipe { int x, gapY; bool scored, active; };
FBPipe fbPipes[FB_MAX_PIPES];
int fbActivePipes = 0;

int fbScore = 0, fbHighScore = 0, fbPrevScore = -1;
int fbGroundScroll = 0;
unsigned long fbLastFrame = 0, fbDyingStart = 0, fbSplashTime = 0;
bool fbTouchDown = false;
float fbSplashBobY = 120;  // init to valid Y (SH/2 - 40), not 0

TFT_eSprite birdSpr = TFT_eSprite(&tft);

// ============================================================
// SPRITES
// ============================================================
void allocateFlappySprites() {
  birdSpr.setColorDepth(16);
  birdSpr.setAttribute(PSRAM_ENABLE, false);  // internal SRAM for speed + DMA
  birdSpr.createSprite(FB_BIRD_W, FB_BIRD_H);
  allocateGroundSprite();
  Serial.printf("Flappy sprites allocated. Free PSRAM: %d\n", ESP.getFreePsram());
}
void freeFlappySprites() {
  birdSpr.deleteSprite();
  freeGroundSprite();
  Serial.printf("Flappy sprites freed. Free PSRAM: %d\n", ESP.getFreePsram());
}

// ============================================================
// BIRD SPRITE — matches original: orange body, white belly, red crown
// ============================================================
void drawBirdSprite(int frame, int birdY = -1) {
  // Use correct sky zone color based on bird's vertical position
  uint16_t bgCol = (birdY >= 0 && birdY >= FB_SKY_H / 2) ? COL_SKY_BOT : COL_SKY;
  birdSpr.fillSprite(bgCol);

  // Body base (yellow-orange)
  birdSpr.fillEllipse(15, 12, 13, 10, COL_BIRD);
  // White belly (bottom half)
  birdSpr.fillEllipse(14, 16, 10, 6, COL_BIRD_WHT);
  // Red-orange crown (top)
  birdSpr.fillEllipse(18, 5, 8, 5, COL_BIRD_RED);

  // Wing (position varies by frame)
  int wingY = (frame == 0) ? 6 : (frame == 1) ? 10 : 14;
  birdSpr.fillEllipse(8, wingY, 7, 4, COL_WING_DK);
  birdSpr.fillEllipse(8, wingY - 1, 6, 3, COL_BIRD);

  // Eye (large white with small black pupil — signature flappy look)
  birdSpr.fillCircle(22, 8, 5, COL_WHITE);
  birdSpr.fillCircle(24, 8, 2, TFT_BLACK);

  // Beak (two triangles for open-mouth effect)
  birdSpr.fillTriangle(26, 10, 33, 12, 26, 14, COL_BEAK);    // upper beak
  birdSpr.fillTriangle(26, 14, 31, 14, 26, 17, 0xD2C3);       // lower beak (darker)

  // Outline hint (1px dark line on top)
  birdSpr.drawEllipse(15, 12, 13, 10, 0x8400);
}

// ============================================================
// BACKGROUND — sky gradient + clouds + city silhouette
// ============================================================
void drawFlappySkyAndClouds() {
  // Sky gradient (darker top, lighter bottom)
  tft.fillRect(0, 0, SW, FB_SKY_H / 2, COL_SKY);
  tft.fillRect(0, FB_SKY_H / 2, SW, FB_SKY_H / 2, COL_SKY_BOT);

  // City silhouette (simple rectangles at horizon)
  int cityY = FB_SKY_H - 50;
  int bldgWidths[] = {20, 15, 25, 12, 30, 18, 22, 14, 28, 16, 20, 25, 15, 30, 18};
  int bldgHeights[] = {35, 25, 45, 20, 40, 30, 38, 22, 42, 28, 32, 44, 26, 36, 34};
  int bx = 0;
  for (int i = 0; i < 15 && bx < SW; i++) {
    tft.fillRect(bx, cityY + (50 - bldgHeights[i]), bldgWidths[i], bldgHeights[i], COL_CITY);
    bx += bldgWidths[i] + random(5, 15);
  }

  // Clouds (fluffy ellipse clusters)
  drawCloud(40, 40, 50);
  drawCloud(180, 65, 40);
  drawCloud(340, 30, 55);
  drawCloud(120, 90, 35);
  drawCloud(420, 75, 45);
  drawCloud(260, 50, 38);
}

void drawCloud(int cx, int cy, int w) {
  int h = w / 3;
  tft.fillEllipse(cx, cy, w / 2, h, COL_CLOUD);
  tft.fillEllipse(cx - w / 4, cy + 2, w / 3, h - 2, COL_CLOUD);
  tft.fillEllipse(cx + w / 4, cy + 1, w / 3, h - 1, COL_CLOUD);
}

// ============================================================
// GROUND — sprite-based for smooth scrolling without SPI overhead
// ============================================================
TFT_eSprite groundSpr = TFT_eSprite(&tft);
bool groundSprAllocated = false;

void allocateGroundSprite() {
  if (groundSprAllocated) return;
  groundSpr.setColorDepth(16);
  groundSpr.setAttribute(PSRAM_ENABLE, false);  // internal SRAM for speed
  void* p = groundSpr.createSprite(SW, 14);
  if (p) groundSprAllocated = true;
}

void freeGroundSprite() {
  if (groundSprAllocated) {
    groundSpr.deleteSprite();
    groundSprAllocated = false;
  }
}

void drawGroundSolid() {
  // Dirt body (static, drawn once)
  tft.fillRect(0, FB_SKY_H + 14, SW, FB_GROUND_H - 14, COL_GROUND);
  tft.fillRect(0, FB_SKY_H + 15, SW, 2, COL_GROUND_LN);
}

// Render grass into sprite and push in 1 SPI burst (replaces 365 fillRects)
void drawGroundDashes() {
  if (!groundSprAllocated) {
    tft.fillRect(0, FB_SKY_H, SW, 14, COL_GRASS_1);
    return;
  }
  groundSpr.fillSprite(COL_GRASS_1);
  for (int x = -(fbGroundScroll % 20) - 20; x < SW + 20; x += 20) {
    for (int row = 0; row < 14; row++) {
      int sx = x + row;
      if (sx >= 0 && sx + 8 < SW)
        groundSpr.fillRect(sx, row, 8, 1, COL_GRASS_2);
    }
  }
  groundSpr.pushSprite(0, FB_SKY_H);  // single push: 480x14 = 6720px
}

// ============================================================
// PIPES — proper green gradient with bordered caps
// ============================================================
void drawPipeInitial(int pipeIdx) {
  FBPipe& p = fbPipes[pipeIdx];
  if (!p.active || p.x >= SW) return;

  int gapTop = p.gapY - FB_PIPE_GAP / 2;
  int gapBot = p.gapY + FB_PIPE_GAP / 2;
  int capX = p.x - (FB_PIPE_CAP_W - FB_PIPE_W) / 2;
  int bodyL = max(p.x, 0);
  int bodyR = min(p.x + FB_PIPE_W, SW);
  int capL = max(capX, 0);
  int capR = min(capX + FB_PIPE_CAP_W, SW);
  int bodyW = bodyR - bodyL;
  int capW = capR - capL;

  // No startWrite here — caller holds the SPI transaction open

  // Upper pipe body
  int upperH = gapTop - FB_PIPE_CAP_H;
  if (upperH > 0 && bodyW > 0) {
    tft.fillRect(bodyL, 0, bodyW, upperH, COL_PIPE);
    // Left highlight
    if (p.x >= 0) tft.fillRect(bodyL, 0, 4, upperH, COL_PIPE_LT);
    // Right dark edge
    if (bodyR <= SW) tft.fillRect(bodyR - 4, 0, 4, upperH, COL_PIPE_DK);
  }
  // Upper cap
  if (gapTop > 0 && capW > 0) {
    int capTop = max(gapTop - FB_PIPE_CAP_H, 0);
    tft.fillRect(capL, capTop, capW, FB_PIPE_CAP_H, COL_PIPE_CAP);
    tft.drawRect(capL, capTop, capW, FB_PIPE_CAP_H, COL_PIPE_DK);
    // Highlight inside cap
    if (capX >= 0) tft.fillRect(capL + 2, capTop + 2, 4, FB_PIPE_CAP_H - 4, COL_PIPE_LT);
  }

  // Lower cap
  if (capW > 0) {
    tft.fillRect(capL, gapBot, capW, FB_PIPE_CAP_H, COL_PIPE_CAP);
    tft.drawRect(capL, gapBot, capW, FB_PIPE_CAP_H, COL_PIPE_DK);
    if (capX >= 0) tft.fillRect(capL + 2, gapBot + 2, 4, FB_PIPE_CAP_H - 4, COL_PIPE_LT);
  }
  // Lower body
  int lowerTop = gapBot + FB_PIPE_CAP_H;
  if (lowerTop < FB_SKY_H && bodyW > 0) {
    tft.fillRect(bodyL, lowerTop, bodyW, FB_SKY_H - lowerTop, COL_PIPE);
    if (p.x >= 0) tft.fillRect(bodyL, lowerTop, 4, FB_SKY_H - lowerTop, COL_PIPE_LT);
    if (bodyR <= SW) tft.fillRect(bodyR - 4, lowerTop, 4, FB_SKY_H - lowerTop, COL_PIPE_DK);
  }

}

// Differential update — only redraw edge strips
void updatePipeDelta(int pipeIdx) {
  FBPipe& p = fbPipes[pipeIdx];
  if (!p.active) return;

  int gapTop = p.gapY - FB_PIPE_GAP / 2;
  int gapBot = p.gapY + FB_PIPE_GAP / 2;
  int capOff = (FB_PIPE_CAP_W - FB_PIPE_W) / 2;

  // No startWrite here — caller holds the SPI transaction open

  // Erase body trailing edge
  int trailX = p.x + FB_PIPE_W;
  if (trailX >= 0 && trailX < SW) {
    int eW = min(FB_PIPE_SCROLL, SW - trailX);
    int upperH = gapTop - FB_PIPE_CAP_H;
    if (upperH > 0) tft.fillRect(trailX, 0, eW, upperH, COL_SKY_BOT);
    tft.fillRect(trailX, gapTop, eW, FB_PIPE_GAP, COL_SKY_BOT);
    int ltop = gapBot + FB_PIPE_CAP_H;
    if (ltop < FB_SKY_H) tft.fillRect(trailX, ltop, eW, FB_SKY_H - ltop, COL_SKY_BOT);
  }

  // Erase cap trailing edge (cap right edge = p.x - capOff + FB_PIPE_CAP_W)
  int capTrail = p.x - capOff + FB_PIPE_CAP_W;
  if (capTrail >= 0 && capTrail < SW) {
    int eW = min(FB_PIPE_SCROLL, SW - capTrail);
    int capTop = max(gapTop - FB_PIPE_CAP_H, 0);
    tft.fillRect(capTrail, capTop, eW, FB_PIPE_CAP_H, COL_SKY_BOT);
    tft.fillRect(capTrail, gapBot, eW, FB_PIPE_CAP_H, COL_SKY_BOT);
  }

  // Draw body leading edge
  if (p.x >= 0 && p.x < SW) {
    int dW = min(FB_PIPE_SCROLL, SW - p.x);
    int upperH = gapTop - FB_PIPE_CAP_H;
    if (upperH > 0) tft.fillRect(p.x, 0, dW, upperH, COL_PIPE_LT);  // leading = highlight
    int ltop = gapBot + FB_PIPE_CAP_H;
    if (ltop < FB_SKY_H) tft.fillRect(p.x, ltop, dW, FB_SKY_H - ltop, COL_PIPE_LT);
  }

  // Draw cap leading edge
  int capX = p.x - capOff;
  if (capX >= 0 && capX < SW) {
    int dW = min(FB_PIPE_SCROLL, SW - capX);
    int capTop = max(gapTop - FB_PIPE_CAP_H, 0);
    tft.fillRect(capX, capTop, dW, FB_PIPE_CAP_H, COL_PIPE_DK);
    tft.fillRect(capX, gapBot, dW, FB_PIPE_CAP_H, COL_PIPE_DK);
  }
}

// Full redraw for freeze frame
void drawPipeFull(int pipeIdx) {
  drawPipeInitial(pipeIdx);
}

// ============================================================
// SPAWN / REMOVE / COLLISION / SCORE
// ============================================================
void spawnPipe() {
  for (int i = 0; i < FB_MAX_PIPES; i++) {
    if (!fbPipes[i].active) {
      fbPipes[i].x = SW;
      fbPipes[i].gapY = random(FB_PIPE_GAP_MIN, FB_PIPE_GAP_MAX);
      fbPipes[i].scored = false;
      fbPipes[i].active = true;
      fbActivePipes++;
      return;
    }
  }
}

void removeOffScreenPipes() {
  for (int i = 0; i < FB_MAX_PIPES; i++) {
    if (fbPipes[i].active && fbPipes[i].x + FB_PIPE_CAP_W < -5) {
      fbPipes[i].active = false;
      fbActivePipes--;
    }
  }
}

bool shouldSpawnPipe() {
  int maxX = -999;
  for (int i = 0; i < FB_MAX_PIPES; i++)
    if (fbPipes[i].active && fbPipes[i].x > maxX) maxX = fbPipes[i].x;
  return (maxX < SW - FB_PIPE_SPACING) || fbActivePipes == 0;
}

bool checkFlappyCollision() {
  int bT = (int)fbBirdY + FB_HITBOX_INSET;
  int bB = (int)fbBirdY + FB_BIRD_H - FB_HITBOX_INSET;
  int bL = FB_BIRD_X + FB_HITBOX_INSET;
  int bR = FB_BIRD_X + FB_BIRD_W - FB_HITBOX_INSET;
  if (bB >= FB_SKY_H) return true;
  if (bT <= 0) return true;
  for (int i = 0; i < FB_MAX_PIPES; i++) {
    if (!fbPipes[i].active) continue;
    int pL = fbPipes[i].x, pR = pL + FB_PIPE_W;
    if (bR > pL && bL < pR) {
      int gT = fbPipes[i].gapY - FB_PIPE_GAP / 2;
      int gB = fbPipes[i].gapY + FB_PIPE_GAP / 2;
      if (bT < gT || bB > gB) return true;
    }
  }
  return false;
}

void checkFlappyScore() {
  int bCX = FB_BIRD_X + FB_BIRD_W / 2;
  for (int i = 0; i < FB_MAX_PIPES; i++) {
    if (!fbPipes[i].active || fbPipes[i].scored) continue;
    if (bCX > fbPipes[i].x + FB_PIPE_W / 2) {
      fbPipes[i].scored = true;
      fbScore++;
    }
  }
}

// ============================================================
// SCORE DISPLAY — large white text with dark shadow (like original)
// ============================================================
void drawFlappyScore() {
  if (fbScore == fbPrevScore) return;
  fbPrevScore = fbScore;
  tft.fillRect(SW / 2 - 35, 8, 70, 32, COL_SKY);
  String s = String(fbScore);
  // Shadow
  tft.setTextColor(0x31A6, COL_SKY);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.drawString(s, SW / 2 + 2, 10);
  // White
  tft.setTextColor(COL_WHITE, COL_SKY);
  tft.drawString(s, SW / 2, 8);
}

// ============================================================
// GAME OVER
// ============================================================
void drawFlappyGameOver() {
  int pw = 220, ph = 110;
  int px = (SW - pw) / 2, py = (FB_SKY_H - ph) / 2;

  tft.fillRoundRect(px, py, pw, ph, 8, 0xDED8);
  tft.drawRoundRect(px, py, pw, ph, 8, 0xAB00);
  tft.drawRoundRect(px + 1, py + 1, pw - 2, ph - 2, 7, 0xAB00);

  tft.setTextDatum(TC_DATUM);
  tft.setTextColor(TFT_BLACK, 0xDED8);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("Game Over", SW / 2, py + 10);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("Score: " + String(fbScore), SW / 2, py + 40);

  if (fbScore > fbHighScore) {
    fbHighScore = fbScore;
    tft.setTextColor(0xF800, 0xDED8);
    tft.drawString("New Best!", SW / 2, py + 62);
  } else {
    tft.drawString("Best: " + String(fbHighScore), SW / 2, py + 62);
  }
  tft.setTextColor(0x6B4D, 0xDED8);
  tft.drawString("Tap to restart", SW / 2, py + 86);
}

// ============================================================
// ENTER FLAPPY MODE
// ============================================================
void enterFlappyMode() {
  fbState = FB_SPLASH;
  fbBirdY = SH / 2 - 40;
  fbBirdVel = 0;
  fbBirdFrame = 1;
  fbScore = 0;
  fbPrevScore = -1;
  fbActivePipes = 0;
  fbGroundScroll = 0;
  fbTouchDown = false;
  fbSplashTime = millis();
  fbSplashBobY = fbBirdY;  // prevent stale erase on first frame
  for (int i = 0; i < FB_MAX_PIPES; i++) fbPipes[i].active = false;

  // Draw full background (static — only done once)
  drawFlappySkyAndClouds();
  drawGroundSolid();
  drawGroundDashes();

  // Bird
  drawBirdSprite(1);
  birdSpr.pushSprite(FB_BIRD_X, (int)fbBirdY);

  // "Get Ready!" text
  tft.setTextColor(COL_WHITE, COL_SKY);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSansBold12pt7b);
  tft.drawString("Get Ready!", SW / 2, 50);

  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("Tap to flap", SW / 2, 80);

  fbLastFrame = millis();
}

// ============================================================
// MAIN LOOP
// ============================================================
void loopFlappy(unsigned long now, bool touching) {

  switch (fbState) {

    case FB_SPLASH: {
      if (now - fbLastFrame >= FB_FRAME_MS) {
        fbLastFrame = now;

        // Erase old bird
        tft.fillRect(FB_BIRD_X, (int)fbSplashBobY, FB_BIRD_W, FB_BIRD_H, COL_SKY_BOT);

        // Bob animation (sine wave)
        float bobT = (float)(now - fbSplashTime) / 600.0f;
        fbSplashBobY = SH / 2 - 40 + sin(bobT * 3.14159f) * 10.0f;

        // Wing animation
        if (now - fbLastBirdAnim > 150) {
          fbLastBirdAnim = now;
          fbBirdFrame = (fbBirdFrame + 1) % 3;
        }
        drawBirdSprite(fbBirdFrame);
        birdSpr.pushSprite(FB_BIRD_X, (int)fbSplashBobY);

        // Scroll ground
        fbGroundScroll = (fbGroundScroll + 1) % 20;
        drawGroundDashes();
      }

      if (touching && !fbTouchDown) {
        fbTouchDown = true;
        startFlappyGame();
      }
      if (!touching) fbTouchDown = false;
      break;
    }

    case FB_PLAYING: {
      if (now - fbLastFrame < FB_FRAME_MS) return;
      fbLastFrame = now;

      // Input — flap
      if (touching && !fbTouchDown) {
        fbTouchDown = true;
        fbBirdVel = FB_FLAP_VEL;
      }
      if (!touching) fbTouchDown = false;

      // Physics
      fbBirdVel += FB_GRAVITY;
      if (fbBirdVel > FB_TERMINAL_VEL) fbBirdVel = FB_TERMINAL_VEL;
      fbPrevBirdY = fbBirdY;
      fbBirdY += fbBirdVel;

      // Wing animation
      if (now - fbLastBirdAnim > 100) {
        fbLastBirdAnim = now;
        fbBirdFrame = (fbBirdFrame + 1) % 3;
      }

      // Pipes
      for (int i = 0; i < FB_MAX_PIPES; i++)
        if (fbPipes[i].active) fbPipes[i].x -= FB_PIPE_SCROLL;

      removeOffScreenPipes();  // free slots before spawning
      if (shouldSpawnPipe()) spawnPipe();
      checkFlappyScore();

      // Collision
      if (checkFlappyCollision()) {
        fbState = FB_DYING;
        flappyPlaying = false;
        fbDyingStart = now;
        tft.fillRect(FB_BIRD_X - 4, (int)fbBirdY - 4,
                     FB_BIRD_W + 8, FB_BIRD_H + 8, COL_WHITE);
        fbPrevBirdY = fbBirdY;  // so dying frame erases the correct position
        break;
      }

      fbGroundScroll = (fbGroundScroll + FB_PIPE_SCROLL) % 20;

      // ---- RENDER (differential, all batched in one SPI transaction) ----
      tft.startWrite();  // hold CS low for entire frame

      int eraseY = (int)fbPrevBirdY;
      int newY = (int)fbBirdY;
      // Erase old bird strip (only non-overlapping part)
      if (eraseY != newY) {
        if (eraseY < newY) {
          int h = min(newY - eraseY, FB_BIRD_H);
          tft.fillRect(FB_BIRD_X, eraseY, FB_BIRD_W, h, COL_SKY_BOT);
        } else {
          int h = min(eraseY - newY, FB_BIRD_H);
          tft.fillRect(FB_BIRD_X, newY + FB_BIRD_H, FB_BIRD_W, h, COL_SKY_BOT);
        }
      }

      // Pipes (edge strips only, no inner startWrite — already in transaction)
      for (int i = 0; i < FB_MAX_PIPES; i++) {
        if (!fbPipes[i].active) continue;
        if (fbPipes[i].x <= SW - FB_PIPE_SCROLL && fbPipes[i].x > SW - FB_PIPE_SCROLL - FB_PIPE_SCROLL) {
          drawPipeInitial(i);
        } else {
          updatePipeDelta(i);
        }
      }

      // Ground (sprite push — 1 SPI burst)
      drawGroundDashes();

      // Bird (opaque push — 1 setWindow instead of ~45)
      drawBirdSprite(fbBirdFrame, newY);
      birdSpr.pushSprite(FB_BIRD_X, newY);

      tft.endWrite();  // release SPI

      // Score (outside main transaction — only draws when changed)
      drawFlappyScore();
      break;
    }

    case FB_DYING: {
      if (now - fbLastFrame < FB_FRAME_MS) return;
      fbLastFrame = now;

      tft.fillRect(FB_BIRD_X, (int)fbPrevBirdY, FB_BIRD_W, FB_BIRD_H, COL_SKY_BOT);
      fbBirdVel += FB_GRAVITY;
      fbPrevBirdY = fbBirdY;
      fbBirdY += fbBirdVel;

      if (fbBirdY < SH) {
        drawBirdSprite(2, (int)fbBirdY);
        birdSpr.pushSprite(FB_BIRD_X, (int)fbBirdY);
      }

      if (fbBirdY + FB_BIRD_H >= FB_SKY_H || now - fbDyingStart > 1200) {
        fbState = FB_GAMEOVER;
        // Clean redraw for game over panel
        drawFlappySkyAndClouds();
        for (int i = 0; i < FB_MAX_PIPES; i++)
          if (fbPipes[i].active) drawPipeFull(i);
        drawGroundSolid();
        drawGroundDashes();
        drawFlappyGameOver();
      }
      break;
    }

    case FB_GAMEOVER: {
      if (touching && !fbTouchDown && (now - fbDyingStart > 1200)) {
        fbTouchDown = true;
        enterFlappyMode();
      }
      if (!touching) fbTouchDown = false;
      break;
    }
  }
}

// ============================================================
// START GAME
// ============================================================
void startFlappyGame() {
  fbState = FB_PLAYING;
  flappyPlaying = true;
  fbBirdY = SH / 2 - 30;
  fbBirdVel = FB_FLAP_VEL;
  fbPrevBirdY = fbBirdY;
  fbBirdFrame = 0;
  fbScore = 0;
  fbPrevScore = -1;
  fbActivePipes = 0;
  fbGroundScroll = 0;
  fbLastFrame = millis();
  for (int i = 0; i < FB_MAX_PIPES; i++) fbPipes[i].active = false;

  // Clear splash text (redraw sky area where text was)
  tft.fillRect(0, 40, SW, 55, COL_SKY);
  // Re-draw clouds that may have been covered
  drawCloud(40, 40, 50);
  drawCloud(180, 65, 40);
  drawCloud(260, 50, 38);

  spawnPipe();
}
