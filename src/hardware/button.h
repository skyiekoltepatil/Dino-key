#pragma once
#include <Arduino.h>
#include "../system/config.h"

enum ButtonEvent {
  BTN_NONE,
  BTN_PRESS_DOWN,
  BTN_SINGLE_CLICK,
  BTN_DOUBLE_CLICK,
  BTN_LONG_PRESS
};

class ButtonController {
  int pin;
  bool touchMode;
  int lastReading = -1;
  bool debouncedActive = false;
  unsigned long lastDebounceTime = 0;
  unsigned long pressStartTime = 0;
  unsigned long lastClickReleaseTime = 0;
  bool longPressTriggered = false;
  bool pendingPressEvent = false;
  int clickCount = 0;

  inline bool isActiveLevel(int r) const { return touchMode ? (r == HIGH) : (r == LOW); }

public:
  ButtonController(int buttonPin = PIN_BUTTON, bool isTouch = USE_TOUCH_SENSOR)
    : pin(buttonPin), touchMode(isTouch) {}

  void begin() {
#if INPUT_AUTO_DETECT
    pinMode(pin, INPUT_PULLUP);
    delay(5);
    int vUp = digitalRead(pin);
    pinMode(pin, INPUT_PULLDOWN);
    delay(5);
    int vDn = digitalRead(pin);
    touchMode = (vUp == LOW && vDn == LOW);
#endif
    pinMode(pin, touchMode ? INPUT : INPUT_PULLUP);
    lastReading = digitalRead(pin);
    debouncedActive = false;
  }

  void waitForRelease(unsigned long maxWaitMs = 2000) {
    unsigned long start = millis();
    while (isActiveLevel(digitalRead(pin)) && (millis() - start < maxWaitMs)) delay(10);
  }

  ButtonEvent update() {
    unsigned long now = millis();
    int r = digitalRead(pin);
    if (lastReading == -1) lastReading = r;
    if (r != lastReading) lastDebounceTime = now;
    lastReading = r;

    bool activeNow = isActiveLevel(r);
    if ((now - lastDebounceTime) >= BUTTON_DEBOUNCE_MS && activeNow != debouncedActive) {
      debouncedActive = activeNow;
      if (debouncedActive) {
        pressStartTime = now;
        longPressTriggered = false;
        pendingPressEvent = true;
      } else if (!longPressTriggered) {
        clickCount++;
        lastClickReleaseTime = now;
      }
    }

    if (pendingPressEvent) { pendingPressEvent = false; return BTN_PRESS_DOWN; }
    if (debouncedActive && !longPressTriggered && (now - pressStartTime >= BUTTON_LONG_PRESS_MS)) {
      longPressTriggered = true;
      clickCount = 0;
      return BTN_LONG_PRESS;
    }
    if (clickCount > 0 && !debouncedActive) {
      if (clickCount >= 2) { clickCount = 0; return BTN_DOUBLE_CLICK; }
      if (now - lastClickReleaseTime >= BUTTON_DOUBLE_CLICK_MS) { clickCount = 0; return BTN_SINGLE_CLICK; }
    }
    return BTN_NONE;
  }
};
