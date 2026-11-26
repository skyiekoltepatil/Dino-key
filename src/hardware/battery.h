#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MAX1704X.h>
#include "../system/config.h"

class BatteryManager {
  Adafruit_MAX17048 maxlipo;
  bool available = false;
  float percent = 88.0f;
  float voltage = 4.02f;
  float chargeRate = 0.0f;
  unsigned long lastReadTime = 0;

public:
  bool begin() {
    available = maxlipo.begin();
    if (available) update(true);
    return available;
  }

  void update(bool force = false) {
    unsigned long now = millis();
    if (!available) {
      if (now - lastReadTime >= BATTERY_POLL_MS) {
        lastReadTime = now;
        available = maxlipo.begin();
      }
      return;
    }
    if (!force && (now - lastReadTime < BATTERY_POLL_MS)) return;
    lastReadTime = now;
    float p = maxlipo.cellPercent();
    float v = maxlipo.cellVoltage();
    chargeRate = maxlipo.chargeRate();
    if (!isnan(p) && p >= 0.0f && p <= 150.0f) {
      percent = constrain(p, 0.0f, 100.0f);
      voltage = v;
    } else {
      available = maxlipo.begin();
    }
  }

  bool isAvailable() const { return available; }
  float getPercent() const { return percent; }
  float getVoltage() const { return voltage; }
  bool isLow() const { return percent <= BATTERY_LOW_THRESHOLD; }
  bool isCritical() const { return percent <= BATTERY_CRIT_THRESHOLD; }
  bool isCharging() const { return chargeRate > 0.05f; }

  const char* getStatusString() const {
    if (!available) return "UNAVAILABLE";
    if (isCharging()) return "CHARGING";
    if (isCritical()) return "CRITICAL";
    if (isLow()) return "LOW BATTERY";
    return "HEALTHY";
  }
};
