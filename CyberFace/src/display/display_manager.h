#pragma once
#include <U8g2lib.h>
#include "../system/config.h"
#include "../hardware/rtc.h"
#include "../hardware/battery.h"
#include "../animations/animation_engine.h"
#include "../games/dino_game.h"
#include "../connectivity/sync_manager.h"

class DisplayManager {
  U8G2 &u8g2;
  RTCManager &rtc;
  BatteryManager &battery;
  AnimationEngine &anim;
  DinoGame &dino;
  SyncManager *syncMgr = nullptr;

  ScreenMode currentMode = MODE_HOME, previousMode = MODE_HOME;
  unsigned long lastActivityTime = 0, lastFlashTime = 0, swStartMs = 0;
  uint32_t swAccumMs = 0;
  uint8_t brightnessLevel = 5;
  bool critFlash = false, swRunning = false;

  bool getBatDisplay(bool &hasBat, float &pct, bool &fromBle, bool &chg) const {
    fromBle = !battery.isAvailable() && syncMgr && syncMgr->hasHostBat();
    hasBat = battery.isAvailable() || fromBle;
    pct = battery.isAvailable() ? battery.getPercent() : (fromBle ? syncMgr->getHostBatPct() : 0.0f);
    chg = battery.isAvailable() ? battery.isCharging() : (syncMgr && syncMgr->isHostBatCharging());
    return hasBat;
  }

  void formatBatStr(char *buf, size_t len, bool &hasBat, float &pct, bool &fromBle, bool &chg) const {
    if (getBatDisplay(hasBat, pct, fromBle, chg)) snprintf(buf, len, "%.0f%%", pct);
    else snprintf(buf, len, "--%%");
  }

  void renderHomeMode() {
    u8g2.setFont(u8g2_font_5x7_tf);
    char ds[20], ts[16], bs[10];
    rtc.getDateString(ds, sizeof(ds));
    rtc.getTimeString(ts, sizeof(ts), DEFAULT_12_HOUR_FORMAT);
    u8g2.drawStr(2, 7, ds);
    u8g2.drawStr(2, 15, ts);

    bool hasBat, fromBle, chg; float pct;
    formatBatStr(bs, sizeof(bs), hasBat, pct, fromBle, chg);
    bool hide = false;
    if (battery.isCritical()) {
      if (millis() - lastFlashTime >= 400) { critFlash = !critFlash; lastFlashTime = millis(); }
      hide = critFlash;
    }
    if (!hide) {
      const int bx = 110, by = 4, bw = 14, bh = 8;
      u8g2.drawStr(bx - u8g2.getStrWidth(bs) - 2, 11, bs);
      u8g2.drawFrame(bx, by, bw, bh);
      u8g2.drawBox(bx + bw, by + 2, 2, 4);
      if (hasBat) {
        int fw = (int)((bw - 4) * pct / 100.0f);
        if (fw > 0) u8g2.drawBox(bx + 2, by + 2, fw, bh - 4);
      }
    }
    u8g2.drawHLine(0, 17, 128);
    anim.drawFace(u8g2, false);
  }

  void renderClockMode() {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(2, 9, "DINO CLOCK");
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(69, 8, rtc.getSyncSource());

    bool hasBat, fromBle, chg; float pct; char bs[8];
    formatBatStr(bs, sizeof(bs), hasBat, pct, fromBle, chg);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(126 - u8g2.getStrWidth(bs), 9, bs);
    u8g2.drawHLine(0, 11, 128);

    char bigTime[16], ds[24];
    rtc.getLargeTimeString(bigTime, sizeof(bigTime), DEFAULT_12_HOUR_FORMAT);
    u8g2.setFont(u8g2_font_logisoso18_tn);
    u8g2.drawStr((128 - u8g2.getStrWidth(bigTime)) / 2, 36, bigTime);
    if (DEFAULT_12_HOUR_FORMAT) {
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(111, 36, rtc.getDateTime().hour() >= 12 ? "PM" : "AM");
    }
    rtc.getDateString(ds, sizeof(ds));
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr((128 - u8g2.getStrWidth(ds)) / 2, 50, ds);
    u8g2.drawRBox(54, 56, 8, 5, 1);
    u8g2.drawRBox(66, 56, 8, 5, 1);
  }

  void renderBatteryMode() {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(2, 9, "POWER DIAGNOSTICS");
    u8g2.drawHLine(0, 11, 128);

    bool hasBat, fromBle, chg; float pct;
    if (!getBatDisplay(hasBat, pct, fromBle, chg)) {
      u8g2.drawStr(10, 32, "No battery sensor");
      u8g2.setFont(u8g2_font_5x7_tf);
      u8g2.drawStr(10, 46, "Pair Bluetooth to sync");
      return;
    }

    char pctStr[8]; snprintf(pctStr, sizeof(pctStr), "%.0f", pct);
    u8g2.setFont(u8g2_font_logisoso18_tn);
    u8g2.drawStr(24, 32, pctStr);
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(56, 24, "%");
    if (fromBle) {
      u8g2.drawStr(74, 24, "VIA BLE");
      u8g2.drawStr(74, 34, chg ? "CHARGING" : (pct <= 20.0f ? "LOW BAT" : "BT LINKED"));
    } else {
      char vs[16]; snprintf(vs, sizeof(vs), "%.2f V", battery.getVoltage());
      u8g2.drawStr(78, 24, vs);
      u8g2.drawStr(78, 34, battery.getStatusString());
    }

    u8g2.drawFrame(10, 42, 104, 14);
    u8g2.drawBox(114, 46, 4, 6);
    int fw = (int)(98.0f * pct / 100.0f);
    if (fw > 0) u8g2.drawBox(13, 45, fw, 8);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(12, 62, fromBle ? "BLUETOOTH BATTERY SYNC" : "MAX17048 LI-ION FUEL GAUGE");
  }

  void renderFullFaceMode() {
    u8g2.drawHLine(2, 2, 8);   u8g2.drawVLine(2, 2, 8);
    u8g2.drawHLine(118, 2, 8); u8g2.drawVLine(125, 2, 8);
    u8g2.drawHLine(2, 61, 8);  u8g2.drawVLine(2, 54, 8);
    u8g2.drawHLine(118, 61, 8);u8g2.drawVLine(125, 54, 8);
    anim.drawFace(u8g2, true);
  }

  void renderConnectMode() {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(2, 9, "WIFI & BT SYNC");
    u8g2.setFont(u8g2_font_5x7_tf);
    const char* src = rtc.getSyncSource();
    u8g2.drawStr(126 - u8g2.getStrWidth(src), 8, src);
    u8g2.drawHLine(0, 11, 128);

    char line[32], ts[16];
    if (syncMgr && syncMgr->isWifiConnected()) {
      snprintf(line, sizeof(line), "WF:%s", syncMgr->getActiveSsid());
      line[21] = '\0';
      u8g2.drawStr(2, 22, line);
      snprintf(line, sizeof(line), "IP:%s", WiFi.localIP().toString().c_str());
      u8g2.drawStr(2, 31, line);
    } else {
      u8g2.drawStr(2, 22, (syncMgr && syncMgr->hasSavedWifi()) ? "WF:Connecting/Retry" : "WF:Not saved yet");
      u8g2.drawStr(2, 31, "AP:DINO-KEY 192.168.4.1");
    }
    snprintf(line, sizeof(line), "BT:%s", (syncMgr && syncMgr->isBleConnected()) ? "DINO KEY (LINKED)" : "DINO KEY (READY)");
    u8g2.drawStr(2, 42, line);
    rtc.getTimeString(ts, sizeof(ts));
    snprintf(line, sizeof(line), "TM:%s [%s]", ts, rtc.isLiveSynced() ? "SYNCED" : "OFFLINE");
    u8g2.drawStr(2, 52, line);
    u8g2.drawStr(2, 62, "2x click: Retry Wi-Fi");
  }

  void renderStopwatchMode() {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(2, 9, "STOPWATCH");
    u8g2.setFont(u8g2_font_5x7_tf);
    const char* st = swRunning ? "RUN" : (swAccumMs > 0 ? "PAUSED" : "READY");
    u8g2.drawStr(126 - u8g2.getStrWidth(st), 8, st);
    u8g2.drawHLine(0, 11, 128);

    uint32_t ms = getStopwatchMs(), tot = ms / 1000UL, tenths = (ms % 1000UL) / 100UL;
    uint32_t mins = (tot / 60UL) % 60UL, hrs = tot / 3600UL, secs = tot % 60UL;
    char mainStr[12];
    if (hrs > 0) snprintf(mainStr, sizeof(mainStr), "%02lu:%02lu:%02lu", (unsigned long)hrs, (unsigned long)mins, (unsigned long)secs);
    else snprintf(mainStr, sizeof(mainStr), "%02lu:%02lu", (unsigned long)mins, (unsigned long)secs);

    u8g2.setFont(u8g2_font_logisoso18_tn);
    int mw = u8g2.getStrWidth(mainStr);
    if (hrs == 0) {
      char dec[4]; snprintf(dec, sizeof(dec), ".%lu", (unsigned long)tenths);
      int sx = (128 - (mw + 16)) / 2;
      u8g2.drawStr(sx, 40, mainStr);
      u8g2.setFont(u8g2_font_6x10_tf);
      u8g2.drawStr(sx + mw + 2, 40, dec);
    } else {
      u8g2.drawStr((128 - mw) / 2, 40, mainStr);
    }
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(4, 62, "2x:Start/Stop  Hold:Reset");
  }

  void renderBrightnessMode() {
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(2, 9, "BRIGHTNESS");
    u8g2.setFont(u8g2_font_5x7_tf);
    char lv[10]; snprintf(lv, sizeof(lv), "LVL %u/5", (unsigned)brightnessLevel);
    u8g2.drawStr(126 - u8g2.getStrWidth(lv), 8, lv);
    u8g2.drawHLine(0, 11, 128);
    for (uint8_t i = 0; i < 5; i++) {
      if (i < brightnessLevel) u8g2.drawBox(14 + i * 21, 22, 16, 18);
      else u8g2.drawFrame(14 + i * 21, 22, 16, 18);
    }
    char pct[16]; snprintf(pct, sizeof(pct), "%u%%", (unsigned)(brightnessLevel * 20));
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr((128 - u8g2.getStrWidth(pct)) / 2, 51, pct);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(12, 62, "2x click: Adjust (1-5)");
  }

  void renderSleepMode() {
    drawPixelFace(u8g2, FACE_SLEEPING, true);
    u8g2.setFont(u8g2_font_5x7_tf);
    u8g2.drawStr(92, 18, "z");
    u8g2.drawStr(100, 12, "Z");
    u8g2.drawStr(18, 60, "PRESS TO WAKE (3M SLEEP)");
  }

public:
  DisplayManager(U8G2 &display, RTCManager &r, BatteryManager &b, AnimationEngine &a, DinoGame &d)
    : u8g2(display), rtc(r), battery(b), anim(a), dino(d) {}

  void setSyncManager(SyncManager* s) { syncMgr = s; }
  uint8_t getBrightnessLevel() const { return brightnessLevel; }
  uint8_t getContrastValue() const {
    static const uint8_t vals[5] = {15, 60, 120, 190, 255};
    return vals[constrain((int)brightnessLevel, 1, 5) - 1];
  }

  void setBrightnessLevel(uint8_t lvl, bool persist = true) {
    brightnessLevel = (uint8_t)constrain((int)lvl, 1, 5);
    if (currentMode != MODE_SLEEP) u8g2.setContrast(getContrastValue());
    if (persist && syncMgr && syncMgr->getPrefs()) syncMgr->getPrefs()->putUChar("bri_lvl", brightnessLevel);
  }
  void cycleBrightness() { setBrightnessLevel((brightnessLevel % 5) + 1, true); }

  void toggleStopwatch() {
    unsigned long now = millis();
    if (swRunning) { swAccumMs += (uint32_t)(now - swStartMs); swRunning = false; }
    else { swStartMs = now; swRunning = true; }
  }
  void resetStopwatch() { swRunning = false; swAccumMs = 0; swStartMs = 0; }
  bool hasStopwatchTime() const { return swRunning || swAccumMs > 0; }
  uint32_t getStopwatchMs() const { return swAccumMs + (swRunning ? (uint32_t)(millis() - swStartMs) : 0); }

  void begin(bool showBootSplash = true) {
    u8g2.setBusClock(I2C_FREQUENCY);
    u8g2.begin();
    if (syncMgr && syncMgr->getPrefs()) {
      brightnessLevel = (uint8_t)constrain((int)syncMgr->getPrefs()->getUChar("bri_lvl", 5), 1, 5);
    }
    u8g2.setContrast(getContrastValue());
    if (showBootSplash) DinoGame::playBootSplash(u8g2);
    lastActivityTime = millis();
  }

  void registerActivity() {
    lastActivityTime = millis();
    if (currentMode == MODE_SLEEP) {
      currentMode = previousMode;
      u8g2.setPowerSave(0);
      u8g2.setContrast(getContrastValue());
    }
  }

  void checkInactivity() {
    if (currentMode == MODE_DINO && dino.isPlaying()) { lastActivityTime = millis(); return; }
    if (currentMode != MODE_SLEEP && (millis() - lastActivityTime >= INACTIVITY_SLEEP_MS)) {
      previousMode = currentMode;
      currentMode = MODE_SLEEP;
      u8g2.setContrast(15);
    }
  }

  void setMode(ScreenMode mode) {
    if (currentMode != MODE_SLEEP && mode == MODE_SLEEP) { previousMode = currentMode; u8g2.setContrast(15); }
    else if (currentMode == MODE_SLEEP && mode != MODE_SLEEP) { u8g2.setPowerSave(0); u8g2.setContrast(getContrastValue()); }
    currentMode = mode;
    lastActivityTime = millis();
  }
  ScreenMode getMode() const { return currentMode; }

  void nextMode() {
    registerActivity();
    if (currentMode == MODE_SLEEP) { currentMode = previousMode; u8g2.setContrast(getContrastValue()); }
    else currentMode = (ScreenMode)((currentMode + 1) % 8);
  }

  void toggleSleep() {
    if (currentMode == MODE_SLEEP) {
      currentMode = previousMode; u8g2.setPowerSave(0); u8g2.setContrast(getContrastValue()); lastActivityTime = millis();
    } else {
      dino.stop(); previousMode = currentMode; currentMode = MODE_SLEEP; u8g2.setContrast(15);
    }
  }

  void render() {
    checkInactivity();
    u8g2.clearBuffer();
    u8g2.setDrawColor(1);
    switch (currentMode) {
      case MODE_HOME:       renderHomeMode(); break;
      case MODE_CLOCK:      renderClockMode(); break;
      case MODE_STOPWATCH:  renderStopwatchMode(); break;
      case MODE_BATTERY:    renderBatteryMode(); break;
      case MODE_DINO:       dino.draw(u8g2); break;
      case MODE_FULL_FACE:  renderFullFaceMode(); break;
      case MODE_CONNECT:    renderConnectMode(); break;
      case MODE_BRIGHTNESS: renderBrightnessMode(); break;
      case MODE_SLEEP:      renderSleepMode(); break;
      default: break;
    }
    u8g2.sendBuffer();
  }
};
