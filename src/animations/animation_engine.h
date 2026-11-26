#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "pixel_faces.h"

enum BlinkPhase { BLINK_IDLE, BLINK_CLOSING, BLINK_CLOSED, BLINK_OPENING };

class AnimationEngine {
  int currentFaceIndex = FACE_HAPPY;
  BlinkPhase blinkPhase = BLINK_IDLE;
  unsigned long lastBlinkStep = 0;
  unsigned long nextBlinkTime = 3000;

  void scheduleNextBlink() { nextBlinkTime = millis() + random(2500, 6000); }

public:
  void begin() { scheduleNextBlink(); }

  void setFaceIndex(int idx) { currentFaceIndex = constrain(idx, 0, TOTAL_PIXEL_FACES - 1); }
  int getFaceIndex() const { return currentFaceIndex; }
  void nextFace() { currentFaceIndex = (currentFaceIndex + 1) % TOTAL_PIXEL_FACES; }

  void update() {
    if (currentFaceIndex == FACE_SLEEPY || currentFaceIndex == FACE_SLEEPING) {
      blinkPhase = BLINK_IDLE;
      return;
    }
    unsigned long now = millis();
    switch (blinkPhase) {
      case BLINK_IDLE:
        if (now >= nextBlinkTime) { blinkPhase = BLINK_CLOSING; lastBlinkStep = now; }
        break;
      case BLINK_CLOSING:
        if (now - lastBlinkStep >= 30) { blinkPhase = BLINK_CLOSED; lastBlinkStep = now; }
        break;
      case BLINK_CLOSED:
        if (now - lastBlinkStep >= 75) { blinkPhase = BLINK_OPENING; lastBlinkStep = now; }
        break;
      case BLINK_OPENING:
        if (now - lastBlinkStep >= 30) { blinkPhase = BLINK_IDLE; lastBlinkStep = now; scheduleNextBlink(); }
        break;
    }
  }

  void drawFace(U8G2 &u8g2, bool fullScreen = false) {
    int idx = (blinkPhase == BLINK_IDLE) ? currentFaceIndex : FACE_SLEEPY;
    drawPixelFace(u8g2, idx, fullScreen);
  }
};
