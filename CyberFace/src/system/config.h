#pragma once
#include <Arduino.h>

// Branding & Display (1.3" SH1106 128x64 I2C OLED, U8G2_R2 = 180 deg flip)
#define BOOT_TITLE          "DINO KEY"
#define BOOT_OWNER          "ANAGH KOLTE"
#define BOOT_VERSION        "v1.0"
#define OLED_ROTATION       U8G2_R2
#define SCREEN_WIDTH        128
#define SCREEN_HEIGHT       64
#define OLED_CONTRAST       255

// I2C & Input Pins (Seeed XIAO ESP32-C3 / ESP32-C3 SuperMini)
#define PIN_SDA             6
#define PIN_SCL             7
#define I2C_FREQUENCY       400000
#define PIN_BUTTON          3
#define INPUT_AUTO_DETECT   true
#define USE_TOUCH_SENSOR    false

// Timing & Thresholds
#define FRAME_INTERVAL_MS       20
#define SLEEP_FRAME_INTERVAL_MS 1000
#define BATTERY_POLL_MS         5000
#define RTC_POLL_MS             1000
#define INACTIVITY_SLEEP_MS     180000UL
#define BUTTON_DEBOUNCE_MS      20
#define BUTTON_DOUBLE_CLICK_MS  300
#define BUTTON_LONG_PRESS_MS    700
#define BATTERY_LOW_THRESHOLD   20.0f
#define BATTERY_CRIT_THRESHOLD  5.0f

// Clock & Connectivity Defaults
#define DEFAULT_12_HOUR_FORMAT  true
#define AP_SSID                 "DINO-KEY"
#define BLE_NAME                "DINO KEY"
#define ENABLE_WIFI_NTP         1
#define DEFAULT_TZ_MIN          330
static const char* WIFI_SSID = "YourWiFiName";
static const char* WIFI_PASS = "YourWiFiPassword";

enum ScreenMode {
  MODE_HOME = 0,
  MODE_CLOCK,
  MODE_STOPWATCH,
  MODE_BATTERY,
  MODE_DINO,
  MODE_FULL_FACE,
  MODE_CONNECT,
  MODE_BRIGHTNESS,
  MODE_SLEEP,
  TOTAL_SCREEN_MODES
};
