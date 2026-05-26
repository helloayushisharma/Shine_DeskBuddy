// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Ayushi Sharma and Neha Sadaye
//
// ============================================================
// WIN10 MOBILE TILE HOME SCREEN
// ============================================================
// Dark themed launcher with two tiles: shi+ne and Flappy Bird.
// Accessed by holding the screen for 5 seconds from any app.
// Tap a tile to launch that app.

// Layout constants
const int TILE_W    = 140;
const int TILE_H    = 140;
const int TILE_GAP  = 20;
const int TILE_LEFT_X  = (SW - TILE_W * 2 - TILE_GAP) / 2;  // 90
const int TILE_LEFT_Y  = (SH - TILE_H) / 2;                  // 90
const int TILE_RIGHT_X = TILE_LEFT_X + TILE_W + TILE_GAP;     // 250
const int TILE_RIGHT_Y = TILE_LEFT_Y;                          // 90

const uint16_t COL_TILE_BG      = 0x18E3;  // #1A1A1A dark background
const uint16_t COL_SHINE_TILE   = 0x0000;  // black tile for shi+ne
const uint16_t COL_FLAPPY_TILE  = 0x3DE6;  // green (#73BF2E approx)
const uint16_t COL_TILE_LABEL   = 0xFFFF;  // white text

// Tile touch state
bool tileTouchDown = false;
AppMode selectedTile = APP_TILES;  // APP_TILES = no selection
unsigned long tileSelectTime = 0;

// ============================================================
// DRAW TILE SCREEN
// ============================================================
void drawTileScreen() {
  // Reset touch state on entry
  tileTouchDown = false;
  selectedTile = APP_TILES;

  tft.fillScreen(COL_TILE_BG);

  // --- Left tile: shi+ne ---
  tft.fillRoundRect(TILE_LEFT_X, TILE_LEFT_Y, TILE_W, TILE_H, 4, COL_SHINE_TILE);
  // Subtle border
  tft.drawRoundRect(TILE_LEFT_X, TILE_LEFT_Y, TILE_W, TILE_H, 4, 0x3186); // dark grey

  // Mini face: two eyes with pupils
  int miniEyeY = TILE_LEFT_Y + 48;
  int miniLEyeX = TILE_LEFT_X + 45;
  int miniREyeX = TILE_LEFT_X + 95;
  tft.fillCircle(miniLEyeX, miniEyeY, 18, COL_WHITE);
  tft.fillCircle(miniREyeX, miniEyeY, 18, COL_WHITE);
  tft.fillCircle(miniLEyeX, miniEyeY, 8, COL_PUPIL);
  tft.fillCircle(miniREyeX, miniEyeY, 8, COL_PUPIL);

  // Mini smile
  int smileY = TILE_LEFT_Y + 82;
  int smileCX = TILE_LEFT_X + 70;
  for (int x = -12; x <= 12; x++) {
    float n = (float)x / 12.0f;
    int y = (int)(4.0f * (1.0f - n * n));
    tft.fillCircle(smileCX + x, smileY + y, 1, COL_WHITE);
  }

  // Label
  tft.setTextColor(COL_TILE_LABEL, COL_SHINE_TILE);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("shi+ne", TILE_LEFT_X + TILE_W / 2, TILE_LEFT_Y + TILE_H - 28);

  // --- Right tile: Flappy Bird ---
  tft.fillRoundRect(TILE_RIGHT_X, TILE_RIGHT_Y, TILE_W, TILE_H, 4, COL_FLAPPY_TILE);

  // Bird icon (matches in-game bird)
  int birdCX = TILE_RIGHT_X + 70;
  int birdCY = TILE_RIGHT_Y + 52;
  tft.fillEllipse(birdCX, birdCY, 13, 10, 0xFE49);      // yellow body
  tft.fillEllipse(birdCX - 1, birdCY + 4, 10, 6, 0xFFDF); // white belly
  tft.fillEllipse(birdCX + 3, birdCY - 7, 8, 5, 0xFA20);  // red crown
  tft.fillCircle(birdCX + 7, birdCY - 3, 4, COL_WHITE);    // eye
  tft.fillCircle(birdCX + 8, birdCY - 3, 2, 0x0000);       // pupil
  tft.fillTriangle(birdCX + 11, birdCY - 1,
                   birdCX + 18, birdCY + 1,
                   birdCX + 11, birdCY + 3, 0xE305);        // beak

  // Label
  tft.setTextColor(COL_TILE_LABEL, COL_FLAPPY_TILE);
  tft.setTextDatum(TC_DATUM);
  tft.setFreeFont(&FreeSans9pt7b);
  tft.drawString("Flappy", TILE_RIGHT_X + TILE_W / 2, TILE_RIGHT_Y + TILE_H - 28);
}

// ============================================================
// TILE SCREEN LOOP
// ============================================================
void loopTiles(unsigned long now, bool touching, uint16_t tx, uint16_t ty) {

  if (touching && !tileTouchDown) {
    tileTouchDown = true;

    // Check left tile (shi+ne)
    if (tx >= TILE_LEFT_X && tx < TILE_LEFT_X + TILE_W &&
        ty >= TILE_LEFT_Y && ty < TILE_LEFT_Y + TILE_H) {
      // Highlight with accent border
      tft.drawRoundRect(TILE_LEFT_X - 2, TILE_LEFT_Y - 2,
                        TILE_W + 4, TILE_H + 4, 6, COL_ACCENT);
      selectedTile = APP_SHINE;
      tileSelectTime = now;
    }
    // Check right tile (Flappy Bird)
    else if (tx >= TILE_RIGHT_X && tx < TILE_RIGHT_X + TILE_W &&
             ty >= TILE_RIGHT_Y && ty < TILE_RIGHT_Y + TILE_H) {
      tft.drawRoundRect(TILE_RIGHT_X - 2, TILE_RIGHT_Y - 2,
                        TILE_W + 4, TILE_H + 4, 6, COL_ACCENT);
      selectedTile = APP_FLAPPY;
      tileSelectTime = now;
    }
  }

  if (!touching && tileTouchDown) {
    tileTouchDown = false;

    if (selectedTile != APP_TILES && (now - tileSelectTime < 500)) {
      Serial.printf("Tile selected: %s\n",
                    selectedTile == APP_SHINE ? "shi+ne" : "Flappy");
      launchFromTiles(selectedTile);
    } else {
      // Clear any highlight if tap was outside tiles or too slow
      if (selectedTile == APP_SHINE) {
        tft.drawRoundRect(TILE_LEFT_X - 2, TILE_LEFT_Y - 2,
                          TILE_W + 4, TILE_H + 4, 6, COL_TILE_BG);
      } else if (selectedTile == APP_FLAPPY) {
        tft.drawRoundRect(TILE_RIGHT_X - 2, TILE_RIGHT_Y - 2,
                          TILE_W + 4, TILE_H + 4, 6, COL_TILE_BG);
      }
    }
    selectedTile = APP_TILES;
  }
}

// ============================================================
// LAUNCH APP FROM TILES (with turnstile transition)
// ============================================================
void launchFromTiles(AppMode target) {
  beginTransition(target);
}
