#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "../system/config.h"

struct DinoCactus { float x; int w, h; };

class DinoGame {
public:
  static const int GROUND_Y = 56, DINO_X = 10, DINO_W = 11, DINO_H = 11;

private:
  bool playing = false;
  uint32_t score = 0, bestScore = 0, gameOverAt = 0, lastFrameMs = 0;
  float dinoY = 0.0f, dinoVy = 0.0f, dist = 0.0f, groundShift = 0.0f;
  DinoCactus cacti[2];

  float currentSpeed() const { return min(160.0f, 60.0f + score * 0.15f); }
  void placeCactus(int i, float afterX) {
    cacti[i].x = afterX + currentSpeed() * 0.6f + random(25, 75);
    cacti[i].w = random(4, 8);
    cacti[i].h = random(8, 15);
  }

public:
  bool isPlaying() const { return playing; }
  uint32_t getOverTime() const { return gameOverAt; }
  uint32_t getScore() const { return score; }
  uint32_t getBestScore() const { return bestScore; }
  void setBestScore(uint32_t b) { bestScore = b; }

  static void drawDinoSprite(U8G2 &oled, int x, int y, bool running, bool forceStep = false, bool stepAlt = false) {
    oled.setDrawColor(1);
    oled.drawBox(x + 5, y, 6, 4);
    oled.setDrawColor(0);
    oled.drawPixel(x + 9, y + 1);
    oled.setDrawColor(1);
    oled.drawBox(x + 2, y + 3, 6, 6);
    oled.drawBox(x, y + 4, 2, 2);
    oled.drawPixel(x + 8, y + 5);
    bool step = running && (forceStep ? stepAlt : ((millis() / 100) % 2));
    oled.drawBox(x + 3, y + 9, 2, step ? 1 : 2);
    oled.drawBox(x + 6, y + 9, 2, step ? 2 : 1);
  }

  static void playBootSplash(U8G2 &oled) {
    for (int f = 0; f < 16; f++) {
      oled.clearBuffer();
      oled.setDrawColor(1);
      oled.drawHLine(0, GROUND_Y, 128);
      drawDinoSprite(oled, 6 + f, GROUND_Y - DINO_H, f < 14, true, (f % 2) == 1);
      oled.setFont(u8g2_font_logisoso16_tr);
      oled.drawStr((128 - oled.getStrWidth(BOOT_TITLE)) / 2, 24, BOOT_TITLE);
      oled.setFont(u8g2_font_5x7_tf);
      oled.drawStr((128 - oled.getStrWidth(BOOT_OWNER)) / 2, 36, BOOT_OWNER);
      oled.drawStr(78, 53, BOOT_VERSION);
      oled.sendBuffer();
      delay(95);
    }
    delay(250);
  }

  void start() {
    playing = true; score = 0; dist = 0.0f; dinoY = 0.0f; dinoVy = 0.0f; groundShift = 0.0f;
    placeCactus(0, 110.0f);
    placeCactus(1, cacti[0].x);
    lastFrameMs = millis();
  }

  void stop() { playing = false; }
  void jump() { if (dinoY <= 0.0f) dinoVy = 230.0f; }

  void update() {
    if (!playing) return;
    uint32_t t = millis();
    float dt = min(0.05f, (t - lastFrameMs) / 1000.0f);
    lastFrameMs = t;

    if (dinoY > 0.0f || dinoVy > 0.0f) {
      dinoVy -= 900.0f * dt;
      dinoY += dinoVy * dt;
      if (dinoY <= 0.0f) { dinoY = 0.0f; dinoVy = 0.0f; }
    }

    float sp = currentSpeed();
    dist += sp * dt;
    score = (uint32_t)(dist / 5.0f);
    groundShift += sp * dt;

    for (int i = 0; i < 2; i++) {
      cacti[i].x -= sp * dt;
      if (cacti[i].x + cacti[i].w < 0) placeCactus(i, max(cacti[0].x, cacti[1].x));
      int dTop = GROUND_Y - DINO_H - (int)dinoY + 2, dBot = GROUND_Y - (int)dinoY, cTop = GROUND_Y - cacti[i].h;
      if (DINO_X + 2 < cacti[i].x + cacti[i].w && DINO_X + DINO_W - 2 > cacti[i].x && dBot > cTop && dTop < GROUND_Y) {
        playing = false;
        gameOverAt = t;
        if (score > bestScore) bestScore = score;
      }
    }
  }

  void draw(U8G2 &oled) {
    char buf[24];
    oled.setDrawColor(1);
    oled.drawHLine(0, GROUND_Y, 128);
    for (int gx = -((int)groundShift % 16); gx < 128; gx += 16) oled.drawPixel(gx + 5, GROUND_Y + 3);
    oled.setFont(u8g2_font_5x7_tf);
    snprintf(buf, sizeof(buf), "HI %05lu  %05lu", (unsigned long)bestScore, (unsigned long)score);
    oled.drawStr(128 - oled.getStrWidth(buf) - 1, 7, buf);

    if (playing) {
      drawDinoSprite(oled, DINO_X, GROUND_Y - DINO_H - (int)dinoY, dinoY <= 0.0f);
      for (int i = 0; i < 2; i++) {
        if (cacti[i].x >= 128) continue;
        int cx = (int)cacti[i].x;
        oled.drawBox(cx, GROUND_Y - cacti[i].h, cacti[i].w, cacti[i].h);
        if (cacti[i].h > 10) {
          oled.drawBox(cx - 2, GROUND_Y - cacti[i].h + 4, 2, 3);
          oled.drawBox(cx + cacti[i].w, GROUND_Y - cacti[i].h + 2, 2, 3);
        }
      }
    } else {
      drawDinoSprite(oled, DINO_X, GROUND_Y - DINO_H, false);
      oled.setFont(u8g2_font_7x14B_tf);
      const char* title = gameOverAt ? "GAME OVER" : "DINO RUN";
      oled.drawStr((128 - oled.getStrWidth(title)) / 2, 28, title);
      oled.setFont(u8g2_font_5x7_tf);
      const char* hint = "2x click: play";
      oled.drawStr((128 - oled.getStrWidth(hint)) / 2, 42, hint);
    }
  }
};
