// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ayushi Sharma and Neha Sadaye
//
// ============================================================
// TRANSITION ANIMATION — fast column-wipe with sprite entry
// ============================================================
// Exit: black columns sweep across right-to-left (just fillRect, fast)
// Entry: new mode rendered to full-screen sprite, revealed left-to-right
//        via windowed pushSprite (one SPI burst per frame, smooth)
//
// No readRect, no pixel scaling. Uses PSRAM sprite for the entry.
// Total transition time: ~300-400ms.

TFT_eSprite transSpr = TFT_eSprite(&tft);

enum TransPhase { TRANS_NONE, TRANS_EXIT, TRANS_MIDPOINT, TRANS_ENTRY, TRANS_DONE };

struct TransitionState {
  TransPhase phase;
  AppMode source;
  AppMode target;
  int frame;
};

TransitionState trans = { TRANS_NONE, APP_SHINE, APP_SHINE, 0 };

// Exit: progressively cover screen with black from right to left
// Each value is the X coordinate where black starts (covers X..479)
// Built-in exponential ease: small steps first, big jump at end
const int WIPE_EXIT_STEPS = 6;
const int wipeExitX[6] = { 440, 380, 280, 160, 60, 0 };

// Entry: progressively reveal new content from left to right
// Even steps for uniform frame timing (~15ms each at 27MHz)
const int WIPE_ENTRY_STEPS = 6;
const int wipeEntryW[6] = { 80, 160, 240, 320, 400, 480 };

// ============================================================
// BEGIN TRANSITION
// ============================================================
void beginTransition(AppMode target) {
  transitionActive = true;
  trans.source = currentApp;
  trans.target = target;
  trans.phase = TRANS_EXIT;
  trans.frame = 0;
}

// ============================================================
// RUN ONE TRANSITION FRAME
// ============================================================
void runTransitionFrame(unsigned long now) {
  switch (trans.phase) {

    // ---- EXIT: black columns sweep right-to-left ----
    case TRANS_EXIT: {
      int x = wipeExitX[trans.frame];
      // Draw black from x to screen edge — covers old content progressively
      tft.fillRect(x, 0, SW - x, SH, TFT_BLACK);

      trans.frame++;
      if (trans.frame >= WIPE_EXIT_STEPS) {
        trans.phase = TRANS_MIDPOINT;
      }
      break;
    }

    // ---- MIDPOINT: swap sprites, render new mode to sprite ----
    case TRANS_MIDPOINT: {
      // Screen is fully black now

      // Free old mode sprites
      if (trans.source == APP_SHINE) freeShineSprites();
      else if (trans.source == APP_FLAPPY) freeFlappySprites();

      // Allocate new mode sprites
      if (trans.target == APP_SHINE) allocateShineSprites();
      else if (trans.target == APP_FLAPPY) allocateFlappySprites();

      // Allocate full-screen sprite for entry animation
      transSpr.setColorDepth(16);
      void* p = transSpr.createSprite(SW, SH);
      if (!p) {
        Serial.println("FATAL: transition sprite alloc failed!");
        transitionActive = false;
        trans.phase = TRANS_NONE;
        currentApp = trans.target;
        enterMode(trans.target);
        return;
      }

      // Render new mode's initial screen into the sprite
      renderModeToSprite(trans.target);

      trans.phase = TRANS_ENTRY;
      trans.frame = 0;
      break;
    }

    // ---- ENTRY: reveal new content left-to-right (delta strips only) ----
    case TRANS_ENTRY: {
      int w = wipeEntryW[trans.frame];
      int prevW = (trans.frame == 0) ? 0 : wipeEntryW[trans.frame - 1];
      int deltaW = w - prevW;
      // Push ONLY the new strip (not everything from 0 again)
      transSpr.pushSprite(prevW, 0, prevW, 0, deltaW, SH);

      trans.frame++;
      if (trans.frame >= WIPE_ENTRY_STEPS) {
        trans.phase = TRANS_DONE;
      }
      break;
    }

    // ---- DONE: cleanup and enter mode ----
    case TRANS_DONE: {
      transSpr.deleteSprite();
      Serial.printf("Transition done. Free PSRAM: %d\n", ESP.getFreePsram());

      currentApp = trans.target;
      transitionActive = false;
      trans.phase = TRANS_NONE;

      // Final live render
      enterMode(trans.target);
      break;
    }

    default:
      transitionActive = false;
      trans.phase = TRANS_NONE;
      break;
  }
}

// ============================================================
// RENDER MODE TO SPRITE (using sprite's own drawing methods)
// ============================================================
// Uses TFT_eSprite drawing API — no byte-swap issues, no buffer math.
// The sprite handles color format internally, and pushSprite sends
// it correctly to the TFT.

void renderModeToSprite(AppMode mode) {
  switch (mode) {
    case APP_TILES:  renderTilesToSprite();  break;
    case APP_SHINE:  renderShineToSprite();  break;
    case APP_FLAPPY: renderFlappyToSprite(); break;
  }
}

// ---- Tile screen ----
void renderTilesToSprite() {
  transSpr.fillSprite(COL_TILE_BG);

  // Left tile (shi+ne)
  transSpr.fillRoundRect(TILE_LEFT_X, TILE_LEFT_Y, TILE_W, TILE_H, 4, COL_SHINE_TILE);
  transSpr.drawRoundRect(TILE_LEFT_X, TILE_LEFT_Y, TILE_W, TILE_H, 4, 0x3186);

  // Mini face
  transSpr.fillCircle(TILE_LEFT_X + 45, TILE_LEFT_Y + 48, 18, COL_WHITE);
  transSpr.fillCircle(TILE_LEFT_X + 95, TILE_LEFT_Y + 48, 18, COL_WHITE);
  transSpr.fillCircle(TILE_LEFT_X + 45, TILE_LEFT_Y + 48, 8, COL_PUPIL);
  transSpr.fillCircle(TILE_LEFT_X + 95, TILE_LEFT_Y + 48, 8, COL_PUPIL);

  int smileCX = TILE_LEFT_X + 70;
  int smileY = TILE_LEFT_Y + 82;
  for (int x = -12; x <= 12; x++) {
    float n = (float)x / 12.0f;
    int y = (int)(4.0f * (1.0f - n * n));
    transSpr.fillCircle(smileCX + x, smileY + y, 1, COL_WHITE);
  }

  transSpr.setTextColor(COL_WHITE);
  transSpr.setTextDatum(TC_DATUM);
  transSpr.setFreeFont(&FreeSans9pt7b);
  transSpr.drawString("shi+ne", TILE_LEFT_X + TILE_W / 2, TILE_LEFT_Y + TILE_H - 28);

  // Right tile (Flappy Bird)
  transSpr.fillRoundRect(TILE_RIGHT_X, TILE_RIGHT_Y, TILE_W, TILE_H, 4, COL_FLAPPY_TILE);

  transSpr.fillCircle(TILE_RIGHT_X + 70, TILE_RIGHT_Y + 52, 14, 0xFE49);
  transSpr.fillCircle(TILE_RIGHT_X + 64, TILE_RIGHT_Y + 54, 7, COL_WING_DK);
  transSpr.fillCircle(TILE_RIGHT_X + 75, TILE_RIGHT_Y + 47, 4, COL_WHITE);
  transSpr.fillCircle(TILE_RIGHT_X + 76, TILE_RIGHT_Y + 47, 2, TFT_BLACK);
  transSpr.fillTriangle(TILE_RIGHT_X + 82, TILE_RIGHT_Y + 51,
                        TILE_RIGHT_X + 82, TILE_RIGHT_Y + 59,
                        TILE_RIGHT_X + 92, TILE_RIGHT_Y + 55, COL_BEAK);

  transSpr.setTextColor(COL_WHITE);
  transSpr.setTextDatum(TC_DATUM);
  transSpr.setFreeFont(&FreeSans9pt7b);
  transSpr.drawString("Flappy", TILE_RIGHT_X + TILE_W / 2, TILE_RIGHT_Y + TILE_H - 28);
}

// ---- shi+ne idle ----
void renderShineToSprite() {
  transSpr.fillSprite(COL_BG);

  // Eyes
  int sprCX = EYE_SPR_W / 2;
  int sprCY = EYE_SPR_H / 2;
  transSpr.fillCircle(L_EX, EY, EYE_R, COL_WHITE);
  transSpr.drawCircle(L_EX, EY, EYE_R, COL_LID);
  transSpr.fillCircle(L_EX, EY, PUPIL_R, COL_PUPIL);
  transSpr.fillCircle(R_EX, EY, EYE_R, COL_WHITE);
  transSpr.drawCircle(R_EX, EY, EYE_R, COL_LID);
  transSpr.fillCircle(R_EX, EY, PUPIL_R, COL_PUPIL);

  // Smile
  int mouthCX = SW / 2;
  for (int x = -18; x <= 18; x++) {
    float n = (float)x / 18.0f;
    int y = (int)(6.0f * (1.0f - n * n));
    transSpr.fillCircle(mouthCX + x, MOUTH_Y + y, 2, COL_WHITE);
  }

  // Counter text
  transSpr.setTextColor(COL_DIM);
  transSpr.setTextDatum(TC_DATUM);
  transSpr.setFreeFont(&FreeSansBold12pt7b);
  String cText = String(completedCount) + " / " + String(totalCount) + " tasks done";
  transSpr.drawString(cText, SW / 2, TEXT_ZONE_TOP + 20);
}

// ---- Flappy splash ----
void renderFlappyToSprite() {
  // Sky gradient
  transSpr.fillRect(0, 0, SW, FB_SKY_H / 2, COL_SKY);
  transSpr.fillRect(0, FB_SKY_H / 2, SW, FB_SKY_H / 2, COL_SKY_BOT);

  // Clouds
  transSpr.fillEllipse(40, 40, 25, 10, COL_CLOUD);
  transSpr.fillEllipse(180, 65, 20, 8, COL_CLOUD);
  transSpr.fillEllipse(340, 30, 28, 10, COL_CLOUD);
  transSpr.fillEllipse(420, 75, 22, 9, COL_CLOUD);

  // Ground
  transSpr.fillRect(0, FB_SKY_H, SW, 14, COL_GRASS_1);
  transSpr.fillRect(0, FB_SKY_H + 14, SW, FB_GROUND_H - 14, COL_GROUND);

  // Bird
  int bx = SW / 2 - 17, by = SH / 2 - 42;
  transSpr.fillEllipse(bx + 15, by + 12, 13, 10, COL_BIRD);
  transSpr.fillEllipse(bx + 14, by + 16, 10, 6, COL_BIRD_WHT);
  transSpr.fillEllipse(bx + 18, by + 5, 8, 5, COL_BIRD_RED);
  transSpr.fillCircle(bx + 22, by + 8, 5, COL_WHITE);
  transSpr.fillCircle(bx + 24, by + 8, 2, TFT_BLACK);
  transSpr.fillTriangle(bx + 26, by + 10, bx + 33, by + 12, bx + 26, by + 14, COL_BEAK);

  // Title
  transSpr.setTextColor(COL_WHITE);
  transSpr.setTextDatum(TC_DATUM);
  transSpr.setFreeFont(&FreeSansBold12pt7b);
  transSpr.drawString("Get Ready!", SW / 2, 50);
}

// ============================================================
// ENTER MODE (final live render after transition)
// ============================================================
void enterMode(AppMode mode) {
  switch (mode) {
    case APP_SHINE:  enterShineMode();  break;
    case APP_TILES:  drawTileScreen();  break;
    case APP_FLAPPY: enterFlappyMode(); break;
  }
}

// ============================================================
// DIRECT SWITCH (fallback)
// ============================================================
void directSwitch(AppMode target) {
  if (currentApp == APP_SHINE) freeShineSprites();
  else if (currentApp == APP_FLAPPY) freeFlappySprites();

  if (target == APP_SHINE) allocateShineSprites();
  else if (target == APP_FLAPPY) allocateFlappySprites();

  currentApp = target;
  enterMode(target);
}
