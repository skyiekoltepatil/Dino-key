#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <RTClib.h>
#include <Adafruit_MAX1704X.h>

#include "src/system/config.h"
#include "src/hardware/rtc.h"
#include "src/hardware/battery.h"
#include "src/hardware/button.h"
#include "src/animations/animation_engine.h"
#include "src/games/dino_game.h"
#include "src/connectivity/sync_manager.h"
#include "src/display/display_manager.h"

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(OLED_ROTATION, U8X8_PIN_NONE, PIN_SCL, PIN_SDA);
RTCManager rtc;
BatteryManager battery;
ButtonController button(PIN_BUTTON, USE_TOUCH_SENSOR);
AnimationEngine animation;
DinoGame dinoGame;
SyncManager syncMgr(rtc, battery, animation);
DisplayManager display(u8g2, rtc, battery, animation, dinoGame);

RTC_DATA_ATTR uint32_t savedDinoBest = 0;
unsigned long lastDisplayFrame = 0;

static void onRemoteMode(ScreenMode m) { display.setMode(m); }
static void onRemoteAct() { display.registerActivity(); }
static void onRemoteBri(uint8_t b) { display.setBrightnessLevel(b, true); }
static void onRemoteSw(bool rst) { if (rst) display.resetStopwatch(); else display.toggleStopwatch(); }
static uint8_t onGetBri() { return display.getBrightnessLevel(); }

void setup() {
  Serial.begin(115200);
  delay(100);
  randomSeed(esp_random());

  button.begin();
  syncMgr.beginPrefs();
  syncMgr.setCallbacks(onRemoteMode, onRemoteAct, onRemoteBri, onRemoteSw, onGetBri);
  display.setSyncManager(&syncMgr);

  uint32_t hi = syncMgr.loadDinoBest();
  if (hi > savedDinoBest) savedDinoBest = hi;
  dinoGame.setBestScore(savedDinoBest);

  display.begin(true);
  rtc.begin();
  battery.begin();
  animation.begin();
  syncMgr.beginRadios();
  button.waitForRelease();
  display.render();
  Serial.println(F("DINO KEY V1 Ready!"));
}

void loop() {
  syncMgr.update();
  unsigned long now = millis();
  ButtonEvent btn = button.update();

  if (btn != BTN_NONE) {
    display.registerActivity();
    ScreenMode m = display.getMode();
    if (m == MODE_DINO && dinoGame.isPlaying()) {
      if (btn == BTN_PRESS_DOWN) dinoGame.jump();
      else if (btn == BTN_LONG_PRESS) { dinoGame.stop(); display.toggleSleep(); }
    } else if (!(m == MODE_DINO && (now - dinoGame.getOverTime() < 800))) {
      if (btn == BTN_SINGLE_CLICK) {
        display.nextMode();
      } else if (btn == BTN_DOUBLE_CLICK) {
        if (m == MODE_DINO) dinoGame.start();
        else if (m == MODE_CONNECT) syncMgr.retryWifiNow();
        else if (m == MODE_STOPWATCH) display.toggleStopwatch();
        else if (m == MODE_BRIGHTNESS) display.cycleBrightness();
        else if (m == MODE_HOME || m == MODE_FULL_FACE) animation.nextFace();
      } else if (btn == BTN_LONG_PRESS) {
        if (m == MODE_STOPWATCH && display.hasStopwatchTime()) display.resetStopwatch();
        else display.toggleSleep();
      }
    }
  }

  rtc.update();
  battery.update();

  if (display.getMode() == MODE_HOME || display.getMode() == MODE_FULL_FACE) {
    animation.update();
  } else if (display.getMode() == MODE_DINO && dinoGame.isPlaying()) {
    dinoGame.update();
    if (dinoGame.getBestScore() > savedDinoBest) {
      savedDinoBest = dinoGame.getBestScore();
      syncMgr.saveDinoBest(savedDinoBest);
    }
  }

  unsigned long interval = (display.getMode() == MODE_SLEEP) ? SLEEP_FRAME_INTERVAL_MS : FRAME_INTERVAL_MS;
  if (now - lastDisplayFrame >= interval) {
    lastDisplayFrame = now;
    display.render();
  }
}
