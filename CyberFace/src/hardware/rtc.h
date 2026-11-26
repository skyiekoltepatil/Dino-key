#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>
#include <Preferences.h>
#include "../system/config.h"

RTC_DATA_ATTR uint32_t rkSoftEpochSaved = 0;
RTC_DATA_ATTR uint32_t rkSoftBuildTag   = 0;

class RTCManager {
  RTC_DS3231 rtc;
  Preferences* prefs = nullptr;
  bool available = false;
  bool liveSynced = false;
  int32_t tzOffsetMin = DEFAULT_TZ_MIN;
  String syncSource = "EST";
  DateTime currentDateTime;
  unsigned long lastReadTime = 0;
  unsigned long lastSoftTickMs = 0;
  unsigned long lastHwRetryMs = 0;
  unsigned long lastNvsSaveMs = 0;

public:
  void attachPrefs(Preferences* p) {
    prefs = p;
    if (prefs) tzOffsetMin = prefs->getLong("tzmin", DEFAULT_TZ_MIN);
  }

  void initSoftwareClock() {
    uint32_t buildUnix = DateTime(F(__DATE__), F(__TIME__)).unixtime() + 5;
    uint32_t nvsEpoch = prefs ? prefs->getULong("epoch", 0) : 0;
    uint32_t seed = buildUnix;
    if (nvsEpoch > seed) seed = nvsEpoch;
    if (rkSoftBuildTag == buildUnix && rkSoftEpochSaved > seed) seed = rkSoftEpochSaved;

    rkSoftBuildTag = buildUnix;
    rkSoftEpochSaved = seed;
    lastSoftTickMs = lastNvsSaveMs = millis();
    currentDateTime = DateTime(seed);
    syncSource = (nvsEpoch >= buildUnix) ? "NVS" : "EST";
    liveSynced = false;
  }

  bool begin() {
    initSoftwareClock();
    available = rtc.begin();
    if (available) {
      if (rtc.lostPower()) rtc.adjust(currentDateTime);
      else {
        DateTime hw = rtc.now();
        if (hw.year() >= 2024) setLocalEpoch(hw.unixtime(), "RTC", true, true);
      }
    }
    return available;
  }

  void setLocalEpoch(uint32_t localSec, const char* src, bool persistNvs = true, bool live = true) {
    rkSoftEpochSaved = localSec;
    lastSoftTickMs = millis();
    currentDateTime = DateTime(localSec);
    syncSource = src;
    if (live) liveSynced = true;
    if (available && live) rtc.adjust(currentDateTime);
    if (persistNvs && prefs) {
      prefs->putULong("epoch", localSec);
      prefs->putLong("tzmin", tzOffsetMin);
      prefs->putString("sync_src", syncSource);
      lastNvsSaveMs = millis();
    }
  }

  void applyUtcAndTz(uint32_t utcSec, int32_t tzMin, const char* src) {
    tzOffsetMin = tzMin;
    int64_t local64 = (int64_t)utcSec + (int64_t)tzOffsetMin * 60LL;
    if (local64 > 1700000000LL) setLocalEpoch((uint32_t)local64, src, true, true);
  }

  void update(bool force = false) {
    unsigned long now = millis();
    if (now - lastSoftTickMs >= 1000UL) {
      uint32_t d = (now - lastSoftTickMs) / 1000UL;
      rkSoftEpochSaved += d;
      lastSoftTickMs += d * 1000UL;
    }
    if (prefs && (now - lastNvsSaveMs >= 60000UL)) {
      lastNvsSaveMs = now;
      prefs->putULong("epoch", rkSoftEpochSaved);
    }
    if (!force && (now - lastReadTime < RTC_POLL_MS)) return;
    lastReadTime = now;
    if (available) {
      currentDateTime = rtc.now();
      rkSoftEpochSaved = currentDateTime.unixtime();
      liveSynced = true;
      if (syncSource != "NTP" && syncSource != "BLE" && syncSource != "WEB") syncSource = "RTC";
    } else {
      if (now - lastHwRetryMs >= 10000UL) { lastHwRetryMs = now; available = rtc.begin(); }
      currentDateTime = available ? rtc.now() : DateTime(rkSoftEpochSaved);
    }
  }

  bool isAvailable() const { return available; }
  bool isLiveSynced() const { return liveSynced; }
  int32_t getTzOffsetMin() const { return tzOffsetMin; }
  const char* getSyncSource() const { return syncSource.c_str(); }
  DateTime getDateTime() const { return currentDateTime; }

  void getDateString(char* buf, size_t maxLen) {
    static const char* days[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
    static const char* mons[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN", "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
    int dw = constrain(currentDateTime.dayOfTheWeek(), 0, 6);
    int mo = constrain(currentDateTime.month() - 1, 0, 11);
    snprintf(buf, maxLen, "%s %02d %s", days[dw], currentDateTime.day(), mons[mo]);
  }

  void getTimeString(char* buf, size_t maxLen, bool is12Hour = DEFAULT_12_HOUR_FORMAT) {
    int h = currentDateTime.hour(), m = currentDateTime.minute();
    if (is12Hour) {
      int dh = h % 12; if (dh == 0) dh = 12;
      snprintf(buf, maxLen, "%02d:%02d %s", dh, m, (h >= 12) ? "PM" : "AM");
    } else {
      snprintf(buf, maxLen, "%02d:%02d", h, m);
    }
  }

  void getLargeTimeString(char* buf, size_t maxLen, bool is12Hour = DEFAULT_12_HOUR_FORMAT) {
    int h = currentDateTime.hour(), m = currentDateTime.minute(), s = currentDateTime.second();
    if (is12Hour) { h %= 12; if (h == 0) h = 12; }
    snprintf(buf, maxLen, "%02d:%02d:%02d", h, m, s);
  }
};
