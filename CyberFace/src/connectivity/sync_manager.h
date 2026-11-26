#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <NimBLEDevice.h>
#include <NimBLEHIDDevice.h>
#if defined(CONFIG_NIMBLE_CPP_IDF)
#include "services/gatt/ble_svc_gatt.h"
#else
#include "nimble/nimble/host/services/gatt/include/services/gatt/ble_svc_gatt.h"
#endif
#include <time.h>

#include "../system/config.h"
#include "../hardware/rtc.h"
#include "../hardware/battery.h"
#include "../animations/animation_engine.h"

#define DINO_BLE_SERVICE_UUID "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define DINO_BLE_CHAR_UUID    "beb5483e-36e1-4688-b7f5-ea07361b26a8"

static const char SYNC_PORTAL_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>DINO KEY Setup</title><style>*{box-sizing:border-box;font-family:monospace}body{margin:0;padding:16px;background:#000;color:#fff;max-width:460px;margin:auto}h1{font-size:20px;margin:0 0 4px}.sub{font-size:12px;color:#aaa;margin-bottom:14px}.card{border:1px solid #444;border-radius:10px;padding:12px;margin-bottom:12px;background:#0a0a0a}.row{display:flex;justify-content:space-between;font-size:13px;margin:4px 0}.k{color:#999}.v{font-weight:bold;color:#fff}button,input,select{width:100%;padding:10px;margin-top:6px;border-radius:8px;border:1px solid #555;background:#141414;color:#fff;font-size:13px}button{background:#fff;color:#000;font-weight:bold;cursor:pointer;border:none}button.alt{background:#1c1c1c;color:#fff;border:1px solid #555}.net{padding:8px;margin-top:6px;border:1px solid #333;border-radius:6px;cursor:pointer;display:flex;justify-content:space-between;font-size:12px}.msg{font-size:12px;color:#0f0;margin-top:6px;min-height:16px}.tip{font-size:11px;color:#aaa;margin-top:6px;line-height:1.4}</style></head><body><h1>DINO KEY v1.0</h1><div class="sub">Clock Sync • 2.4GHz Wi-Fi • BLE Control</div><div class="card"><div class="row"><span class="k">Clock</span><span class="v" id="tm">--</span></div><div class="row"><span class="k">Time Source</span><span class="v" id="src">--</span></div><div class="row"><span class="k">Saved Wi-Fi</span><span class="v" id="st">--</span></div><div class="row"><span class="k">BLE ("DINO KEY")</span><span class="v" id="bl">Advertising</span></div><button onclick="syncNow()">Sync Time Now</button><div class="msg" id="smsg">Syncing phone/Mac clock...</div></div><div class="card"><div class="k">2.4 GHz Wi-Fi Setup</div><button class="alt" onclick="scanWifi()">Scan 2.4 GHz Networks</button><div id="nets"></div><select id="slot"><option value="1">Slot 1 (Router Wi-Fi)</option><option value="2">Slot 2 (Phone Hotspot)</option></select><input id="ssid" placeholder="Wi-Fi Name (SSID)"><input id="pass" type="password" placeholder="Password (blank if open)"><button onclick="saveWifi()">Save Wi-Fi &amp; Connect</button><button class="alt" onclick="clearWifi()">Forget Saved Wi-Fi</button><div class="msg" id="wmsg"></div><div class="tip">Tip: On iPhone Hotspot, enable <b>Maximize Compatibility</b> (2.4 GHz).</div></div><script>const $=id=>document.getElementById(id);async function refresh(){try{const d=await(await fetch('/api/status')).json();$('tm').textContent=d.date+' '+d.time;$('src').textContent=d.sync+(d.live?'':' (offline)');$('st').textContent=d.wifi+(d.staIp?' ('+d.staIp+')':'');$('bl').textContent=d.bleConn?'Connected':'Ready ("DINO KEY")';}catch(e){}}async function syncNow(){try{const r=await fetch('/api/sync?utc='+Math.floor(Date.now()/1000)+'&tzmin='+(-new Date().getTimezoneOffset()));$('smsg').textContent=await r.text();refresh();}catch(e){}}async function scanWifi(){const el=$('nets');el.innerHTML='<div class="tip">Scanning...</div>';const list=await(await fetch('/api/scan')).json();el.innerHTML=list.length?'':'<div class="tip">No 2.4GHz networks found.</div>';list.forEach(n=>{const d=document.createElement('div');d.className='net';d.innerHTML='<span>'+n.ssid+'</span><span>'+n.rssi+' dBm</span>';d.onclick=()=>{$('ssid').value=n.ssid;};el.appendChild(d);});}async function saveWifi(){const body=new URLSearchParams({slot:$('slot').value,ssid:$('ssid').value,pass:$('pass').value});$('wmsg').textContent=await(await fetch('/api/wifi',{method:'POST',body})).text();setTimeout(refresh,1500);}async function clearWifi(){$('wmsg').textContent=await(await fetch('/api/clearwifi')).text();refresh();}syncNow();setInterval(syncNow,5000);setInterval(refresh,3000);</script></body></html>)rawliteral";

class SyncManager {
  enum StaState { STA_IDLE, STA_CONNECTING, STA_CONNECTED, STA_COOLDOWN };
  enum BleCtsState {
    CTS_IDLE = 0, CTS_WAIT_SEC, CTS_DISC_SVC, CTS_DISC_SVC_WAIT,
    CTS_DISC_CHR, CTS_DISC_CHR_WAIT, CTS_READ_LOCAL, CTS_READ_LOCAL_WAIT,
    CTS_READ_TIME, CTS_READ_TIME_WAIT, CTS_DISC_BAT_SVC, CTS_DISC_BAT_SVC_WAIT,
    CTS_DISC_BAT_CHR, CTS_DISC_BAT_CHR_WAIT, CTS_READ_BAT, CTS_READ_BAT_WAIT, CTS_DONE
  };

  RTCManager& rtc;
  BatteryManager& battery;
  AnimationEngine& anim;
  Preferences prefs;
  WebServer server{80};
  DNSServer dnsServer;

  void (*onModeChangeCb)(ScreenMode) = nullptr;
  void (*onActivityCb)() = nullptr;
  void (*onBriChangeCb)(uint8_t) = nullptr;
  void (*onSwCmdCb)(bool reset) = nullptr;
  uint8_t (*getBriCb)() = nullptr;

  String wifiSsid1, wifiPass1, wifiSsid2, wifiPass2, activeSsid;
  StaState staState = STA_IDLE;
  int activeSlot = 0;
  unsigned long staStateMs = 0, staConnectedAtMs = 0, lastHttpTimeTryMs = 0, lastNtpMs = 0;
  bool ntpSyncedOnce = false, sntpStarted = false;

  bool hostBatValid = false, hostBatCharging = false;
  float hostBatPct = 0.0f;
  uint8_t lastBatPushed = 100;

  NimBLEServer* bleServer = nullptr;
  NimBLEHIDDevice* bleHid = nullptr;
  NimBLECharacteristic* bleHidInput = nullptr;
  NimBLECharacteristic* bleCmdChar = nullptr;
  NimBLECharacteristic* bleBatChar = nullptr;
  NimBLEAdvertisementData bleAdvData, bleScanData;

  volatile bool bleConnected = false, blePending = false, bleGotDirectCmd = false;
  volatile bool bleCtsPending = false, bleCtsHasTz = false, bleCtsEncrypted = false;
  volatile bool bleHostBatPending = false, bleSvcChangedPending = false;
  volatile uint16_t activeBleConnHandle = 0, bleCtsSvcStart = 0, bleCtsSvcEnd = 0;
  volatile uint16_t bleCtsTimeHandle = 0, bleCtsLocalHandle = 0;
  volatile uint16_t bleBatSvcStart = 0, bleBatSvcEnd = 0, blePeerBatHandle = 0;
  volatile uint32_t bleCtsLocalEpoch = 0;
  volatile int32_t bleCtsTzMin = DEFAULT_TZ_MIN;
  volatile uint8_t bleHostBatVal = 0, bleCtsAttempt = 0;
  volatile BleCtsState bleCtsState = CTS_IDLE;
  unsigned long bleConnAtMs = 0, bleNextCtsProbeMs = 0, bleNextBatPollMs = 0;
  bool bleCtsSecStarted = false;
  char bleBuf[192] = {0};
  portMUX_TYPE bleMux = portMUX_INITIALIZER_UNLOCKED;

  void setCts(BleCtsState st, unsigned long delayMs = 0) {
    portENTER_CRITICAL(&bleMux);
    bleCtsState = st;
    if (delayMs > 0) bleNextCtsProbeMs = millis() + delayMs;
    portEXIT_CRITICAL(&bleMux);
  }

  static int onBleBatReadCb(uint16_t, const struct ble_gatt_error* err, struct ble_gatt_attr* attr, void* arg) {
    auto* m = static_cast<SyncManager*>(arg);
    if (!m) return 0;
    if (err && err->status == 0 && attr && attr->om && OS_MBUF_PKTLEN(attr->om) >= 1) {
      uint8_t val = 0;
      os_mbuf_copydata(attr->om, 0, 1, &val);
      if (val <= 100) {
        portENTER_CRITICAL(&m->bleMux);
        m->bleHostBatVal = val;
        m->bleHostBatPending = true;
        m->bleNextBatPollMs = millis() + 60000UL;
        m->bleCtsState = CTS_DONE;
        portEXIT_CRITICAL(&m->bleMux);
        return 0;
      }
    }
    m->setCts(CTS_DONE);
    return 0;
  }

  static int onBleBatChrCb(uint16_t ch, const struct ble_gatt_error* err, const struct ble_gatt_chr* chr, void* arg) {
    auto* m = static_cast<SyncManager*>(arg);
    if (!m || !err) return 0;
    if (err->status == 0 && chr) {
      if (chr->uuid.u.type == BLE_UUID_TYPE_16 && chr->uuid.u16.value == 0x2A19) {
        portENTER_CRITICAL(&m->bleMux); m->blePeerBatHandle = chr->val_handle; portEXIT_CRITICAL(&m->bleMux);
      }
      return 0;
    }
    portENTER_CRITICAL(&m->bleMux);
    if (m->bleConnected && ch == m->activeBleConnHandle) {
      m->bleCtsState = m->blePeerBatHandle ? CTS_READ_BAT : CTS_DONE;
      m->bleNextCtsProbeMs = millis() + 40UL;
    }
    portEXIT_CRITICAL(&m->bleMux);
    return 0;
  }

  static int onBleBatSvcCb(uint16_t ch, const struct ble_gatt_error* err, const struct ble_gatt_svc* svc, void* arg) {
    auto* m = static_cast<SyncManager*>(arg);
    if (!m || !err) return 0;
    if (err->status == 0 && svc) {
      portENTER_CRITICAL(&m->bleMux); m->bleBatSvcStart = svc->start_handle; m->bleBatSvcEnd = svc->end_handle; portEXIT_CRITICAL(&m->bleMux);
      return 0;
    }
    portENTER_CRITICAL(&m->bleMux);
    if (m->bleConnected && ch == m->activeBleConnHandle) {
      if (m->bleBatSvcStart && m->bleBatSvcEnd >= m->bleBatSvcStart) { m->bleCtsState = CTS_DISC_BAT_CHR; m->bleNextCtsProbeMs = millis() + 40UL; }
      else m->bleCtsState = CTS_DONE;
    }
    portEXIT_CRITICAL(&m->bleMux);
    return 0;
  }

  static int onBleCtsTimeReadCb(uint16_t, const struct ble_gatt_error* err, struct ble_gatt_attr* attr, void* arg) {
    auto* m = static_cast<SyncManager*>(arg);
    if (!m || !err) return 0;
    if (err->status == 0 && attr && attr->om && OS_MBUF_PKTLEN(attr->om) >= 7) {
      uint8_t b[10];
      uint16_t len = min((uint16_t)10, OS_MBUF_PKTLEN(attr->om));
      os_mbuf_copydata(attr->om, 0, len, b);
      uint16_t yr = (uint16_t)b[0] | ((uint16_t)b[1] << 8);
      if (yr >= 2024 && yr <= 2099 && b[2] >= 1 && b[2] <= 12 && b[3] >= 1 && b[3] <= 31 && b[4] < 24 && b[5] < 60 && b[6] < 60) {
        portENTER_CRITICAL(&m->bleMux);
        m->bleCtsLocalEpoch = DateTime(yr, b[2], b[3], b[4], b[5], b[6]).unixtime();
        m->bleCtsPending = true;
        m->bleCtsState = m->blePeerBatHandle ? CTS_READ_BAT : CTS_DISC_BAT_SVC;
        m->bleNextCtsProbeMs = millis() + 60UL;
        portEXIT_CRITICAL(&m->bleMux);
        return 0;
      }
    }
    portENTER_CRITICAL(&m->bleMux);
    if (m->bleCtsAttempt < 10) { m->bleCtsAttempt++; m->bleCtsState = CTS_READ_TIME; m->bleNextCtsProbeMs = millis() + 1500UL; }
    else { m->bleCtsState = CTS_DISC_BAT_SVC; m->bleNextCtsProbeMs = millis() + 60UL; }
    portEXIT_CRITICAL(&m->bleMux);
    return 0;
  }

  static int onBleCtsLocalReadCb(uint16_t ch, const struct ble_gatt_error* err, struct ble_gatt_attr* attr, void* arg) {
    auto* m = static_cast<SyncManager*>(arg);
    if (!m) return 0;
    if (err && err->status == 0 && attr && attr->om && OS_MBUF_PKTLEN(attr->om) >= 1) {
      uint8_t b[4] = {0};
      uint16_t len = min((uint16_t)4, OS_MBUF_PKTLEN(attr->om));
      os_mbuf_copydata(attr->om, 0, len, b);
      int8_t q = (int8_t)b[0];
      if (q >= -48 && q <= 56) {
        int32_t tzm = (int32_t)q * 15 + ((len >= 2 && (b[1] == 2 || b[1] == 4 || b[1] == 8)) ? (int32_t)b[1] * 15 : 0);
        portENTER_CRITICAL(&m->bleMux); m->bleCtsTzMin = tzm; m->bleCtsHasTz = true; portEXIT_CRITICAL(&m->bleMux);
      }
    }
    portENTER_CRITICAL(&m->bleMux);
    if (m->bleConnected && ch == m->activeBleConnHandle) {
      m->bleCtsState = m->bleCtsTimeHandle ? CTS_READ_TIME : CTS_DONE;
      m->bleNextCtsProbeMs = millis() + 40UL;
    }
    portEXIT_CRITICAL(&m->bleMux);
    return 0;
  }

  static int onBleCtsChrCb(uint16_t ch, const struct ble_gatt_error* err, const struct ble_gatt_chr* chr, void* arg) {
    auto* m = static_cast<SyncManager*>(arg);
    if (!m || !err) return 0;
    if (err->status == 0 && chr) {
      if (chr->uuid.u.type == BLE_UUID_TYPE_16) {
        portENTER_CRITICAL(&m->bleMux);
        if (chr->uuid.u16.value == 0x2A2B) m->bleCtsTimeHandle = chr->val_handle;
        else if (chr->uuid.u16.value == 0x2A0F) m->bleCtsLocalHandle = chr->val_handle;
        portEXIT_CRITICAL(&m->bleMux);
      }
      return 0;
    }
    portENTER_CRITICAL(&m->bleMux);
    if (m->bleConnected && ch == m->activeBleConnHandle) {
      if (m->bleCtsTimeHandle) { m->bleCtsState = m->bleCtsLocalHandle ? CTS_READ_LOCAL : CTS_READ_TIME; m->bleNextCtsProbeMs = millis() + 40UL; }
      else m->bleCtsState = CTS_DONE;
    }
    portEXIT_CRITICAL(&m->bleMux);
    return 0;
  }

  static int onBleCtsSvcCb(uint16_t ch, const struct ble_gatt_error* err, const struct ble_gatt_svc* svc, void* arg) {
    auto* m = static_cast<SyncManager*>(arg);
    if (!m || !err) return 0;
    if (err->status == 0 && svc) {
      portENTER_CRITICAL(&m->bleMux); m->bleCtsSvcStart = svc->start_handle; m->bleCtsSvcEnd = svc->end_handle; portEXIT_CRITICAL(&m->bleMux);
      return 0;
    }
    portENTER_CRITICAL(&m->bleMux);
    if (m->bleConnected && ch == m->activeBleConnHandle) {
      if (m->bleCtsSvcStart && m->bleCtsSvcEnd >= m->bleCtsSvcStart) { m->bleCtsState = CTS_DISC_CHR; m->bleNextCtsProbeMs = millis() + 40UL; }
      else if (m->bleCtsAttempt < 10) { m->bleCtsAttempt++; m->bleCtsState = CTS_DISC_SVC; m->bleNextCtsProbeMs = millis() + 2500UL; }
      else { m->bleCtsState = CTS_DISC_BAT_SVC; m->bleNextCtsProbeMs = millis() + 60UL; }
    }
    portEXIT_CRITICAL(&m->bleMux);
    return 0;
  }

  void startBleAdv() {
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setAdvertisementData(bleAdvData);
    adv->setScanResponseData(bleScanData);
    adv->setScanResponse(true);
    adv->start();
  }

  class ServerCb : public NimBLEServerCallbacks {
    SyncManager* m;
  public:
    explicit ServerCb(SyncManager* mgr) : m(mgr) {}
    void onConnect(NimBLEServer*, ble_gap_conn_desc* desc) override {
      portENTER_CRITICAL(&m->bleMux);
      m->bleConnected = true; m->activeBleConnHandle = desc->conn_handle; m->bleConnAtMs = millis();
      m->bleCtsEncrypted = (desc->sec_state.encrypted != 0); m->bleCtsSecStarted = m->bleCtsEncrypted;
      m->bleGotDirectCmd = false; m->hostBatValid = false; m->bleHostBatPending = false;
      m->bleCtsSvcStart = m->bleCtsSvcEnd = m->bleCtsTimeHandle = m->bleCtsLocalHandle = 0;
      m->bleBatSvcStart = m->bleBatSvcEnd = m->blePeerBatHandle = 0;
      m->bleNextBatPollMs = 0; m->bleCtsAttempt = 0; m->bleCtsHasTz = false;
      m->bleCtsState = m->bleCtsEncrypted ? CTS_DISC_SVC : CTS_WAIT_SEC;
      m->bleNextCtsProbeMs = m->bleConnAtMs + (m->bleCtsEncrypted ? 450UL : 500UL);
      portEXIT_CRITICAL(&m->bleMux);
    }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
      if (!desc->sec_state.encrypted) return;
      portENTER_CRITICAL(&m->bleMux);
      m->bleCtsEncrypted = m->bleCtsSecStarted = m->bleSvcChangedPending = true;
      m->bleCtsAttempt = 0; m->bleCtsSvcStart = m->bleCtsSvcEnd = m->bleCtsTimeHandle = m->bleCtsLocalHandle = 0;
      m->bleCtsState = CTS_DISC_SVC; m->bleNextCtsProbeMs = millis() + 450UL;
      portEXIT_CRITICAL(&m->bleMux);
    }
    void onDisconnect(NimBLEServer*) override {
      portENTER_CRITICAL(&m->bleMux);
      m->bleConnected = m->bleCtsEncrypted = m->bleCtsSecStarted = m->bleHostBatPending = m->hostBatValid = false;
      m->blePeerBatHandle = 0; m->bleNextBatPollMs = 0; m->bleCtsState = CTS_IDLE; m->bleNextCtsProbeMs = 0;
      portEXIT_CRITICAL(&m->bleMux);
      m->startBleAdv();
    }
  };

  class CharCb : public NimBLECharacteristicCallbacks {
    SyncManager* m;
  public:
    explicit CharCb(SyncManager* mgr) : m(mgr) {}
    void onRead(NimBLECharacteristic* c) override { c->setValue(m->buildStatusJson().c_str()); }
    void onWrite(NimBLECharacteristic* c) override {
      std::string v = c->getValue();
      if (v.empty()) return;
      portENTER_CRITICAL(&m->bleMux);
      size_t n = min(v.size(), sizeof(m->bleBuf) - 1);
      memcpy(m->bleBuf, v.data(), n);
      m->bleBuf[n] = '\0';
      m->blePending = true;
      portEXIT_CRITICAL(&m->bleMux);
    }
  };

  bool getSlotCreds(int slot, String& outSsid, String& outPass) const {
    if (slot == 0 && wifiSsid1.length() > 0) { outSsid = wifiSsid1; outPass = wifiPass1; return true; }
    if (slot == 1 && wifiSsid2.length() > 0) { outSsid = wifiSsid2; outPass = wifiPass2; return true; }
    if (slot == 2 && strcmp(WIFI_SSID, "YourWiFiName") != 0 && strlen(WIFI_SSID) > 0) {
      outSsid = WIFI_SSID; outPass = WIFI_PASS; return true;
    }
    return false;
  }

  void updateBleStatus(bool notifyClients) {
    if (bleCmdChar) {
      bleCmdChar->setValue(buildStatusJson().c_str());
      if (notifyClients && bleConnected) bleCmdChar->notify();
    }
    if (bleBatChar) {
      uint8_t lvl = battery.isAvailable() ? (uint8_t)constrain((int)battery.getPercent(), 0, 100) : 100;
      bleBatChar->setValue(&lvl, 1);
      lastBatPushed = lvl;
      if (notifyClients && bleConnected) bleBatChar->notify();
    }
  }

public:
  SyncManager(RTCManager& r, BatteryManager& b, AnimationEngine& a) : rtc(r), battery(b), anim(a) {}

  void setCallbacks(void (*modeCb)(ScreenMode), void (*actCb)(), void (*briCb)(uint8_t) = nullptr,
                    void (*swCb)(bool) = nullptr, uint8_t (*briGetCb)() = nullptr) {
    onModeChangeCb = modeCb; onActivityCb = actCb; onBriChangeCb = briCb; onSwCmdCb = swCb; getBriCb = briGetCb;
  }

  Preferences* getPrefs() { return &prefs; }
  bool isBleConnected() const { return bleConnected; }
  bool isWifiConnected() const { return WiFi.status() == WL_CONNECTED; }
  const char* getActiveSsid() const { return activeSsid.c_str(); }
  bool hasHostBat() const { return hostBatValid || bleConnected; }
  float getHostBatPct() const { return hostBatValid ? hostBatPct : (float)lastBatPushed; }
  bool isHostBatCharging() const { return hostBatCharging; }
  uint32_t loadDinoBest() { return prefs.getULong("dino_hi", 0); }
  void saveDinoBest(uint32_t s) { prefs.putULong("dino_hi", s); }

  bool hasSavedWifi() const {
    String s, p;
    return getSlotCreds(0, s, p) || getSlotCreds(1, s, p) || getSlotCreds(2, s, p);
  }

  void beginPrefs() {
    prefs.begin("dinokey", false);
    rtc.attachPrefs(&prefs);
    wifiSsid1 = prefs.getString("ssid1", ""); wifiPass1 = prefs.getString("pass1", "");
    wifiSsid2 = prefs.getString("ssid2", ""); wifiPass2 = prefs.getString("pass2", "");
  }

  void saveWifi(int slot, const String& ssid, const String& pass) {
    if (ssid.length() == 0) return;
    if (slot == 2) {
      wifiSsid2 = ssid; wifiPass2 = pass;
      prefs.putString("ssid2", wifiSsid2); prefs.putString("pass2", wifiPass2);
      activeSlot = 1;
    } else {
      if (wifiSsid1.length() > 0 && wifiSsid1 != ssid) {
        wifiSsid2 = wifiSsid1; wifiPass2 = wifiPass1;
        prefs.putString("ssid2", wifiSsid2); prefs.putString("pass2", wifiPass2);
      }
      wifiSsid1 = ssid; wifiPass1 = pass;
      prefs.putString("ssid1", wifiSsid1); prefs.putString("pass1", wifiPass1);
      activeSlot = 0;
    }
    staState = STA_IDLE; staStateMs = 0;
  }

  void clearWifi() {
    wifiSsid1 = wifiPass1 = wifiSsid2 = wifiPass2 = activeSsid = "";
    prefs.putString("ssid1", ""); prefs.putString("pass1", "");
    prefs.putString("ssid2", ""); prefs.putString("pass2", "");
    WiFi.disconnect(false);
    staState = STA_COOLDOWN; staStateMs = millis();
  }

  void retryWifiNow() { activeSlot = 0; staState = STA_IDLE; staStateMs = 0; }

  String buildStatusJson() {
    char ds[20], ts[16];
    rtc.getDateString(ds, sizeof(ds));
    rtc.getTimeString(ts, sizeof(ts));
    String wf = hasSavedWifi() ? (staState == STA_CONNECTING ? "Connecting..." : "Offline") : "AP Only", ip = "";
    if (WiFi.status() == WL_CONNECTED) { wf = activeSsid; ip = WiFi.localIP().toString(); }
    int effBat = battery.isAvailable() ? (int)battery.getPercent() : (hostBatValid ? (int)hostBatPct : (bleConnected ? (int)lastBatPushed : -1));
    bool effChg = battery.isAvailable() ? battery.isCharging() : hostBatCharging;
    const char* batSrc = battery.isAvailable() ? "MAX17048" : ((hostBatValid || bleConnected) ? "BLE" : "NONE");
    uint8_t bri = getBriCb ? getBriCb() : 5;
    return String("{\"time\":\"") + ts + "\",\"date\":\"" + ds + "\",\"sync\":\"" + rtc.getSyncSource() +
           "\",\"live\":" + (rtc.isLiveSynced() ? "true" : "false") + ",\"bat\":" + effBat +
           ",\"batSrc\":\"" + batSrc + "\",\"batChg\":" + (effChg ? "true" : "false") +
           ",\"wifi\":\"" + wf + "\",\"staIp\":\"" + ip + "\",\"ap\":\"" + AP_SSID +
           "\",\"apIp\":\"" + WiFi.softAPIP().toString() + "\",\"apClients\":" + WiFi.softAPgetStationNum() +
           ",\"ssid1\":\"" + wifiSsid1 + "\",\"ssid2\":\"" + wifiSsid2 + "\",\"bri\":" + (int)bri +
           ",\"bleConn\":" + (bleConnected ? "true" : "false") + "}";
  }

  void executeCommand(const String& rawIn, const char* viaSrc) {
    String cmd = rawIn; cmd.trim();
    if (cmd.length() == 0) return;
    if (onActivityCb) onActivityCb();

    if (cmd.startsWith("T:")) {
      int p1 = cmd.indexOf(':', 2), p2 = (p1 > 0) ? cmd.indexOf(':', p1 + 1) : -1, p3 = (p2 > 0) ? cmd.indexOf(':', p2 + 1) : -1;
      uint32_t utc = (uint32_t)strtoul((p1 > 0 ? cmd.substring(2, p1) : cmd.substring(2)).c_str(), nullptr, 10);
      int32_t tzm = (p1 > 0) ? (int32_t)atol((p2 > 0 ? cmd.substring(p1 + 1, p2) : cmd.substring(p1 + 1)).c_str()) : rtc.getTzOffsetMin();
      if (utc > 1700000000UL) { bleGotDirectCmd = true; rtc.applyUtcAndTz(utc, tzm, viaSrc); }
      if (p2 > 0) {
        int bPct = atoi((p3 > 0 ? cmd.substring(p2 + 1, p3) : cmd.substring(p2 + 1)).c_str());
        bool chg = (p3 > 0) && (atoi(cmd.substring(p3 + 1).c_str()) != 0);
        if (bPct >= 0 && bPct <= 100) { hostBatPct = (float)bPct; hostBatCharging = chg; hostBatValid = true; }
      }
    } else if (cmd.startsWith("BAT:")) {
      int sep = cmd.indexOf(':', 4);
      int bPct = atoi((sep > 0 ? cmd.substring(4, sep) : cmd.substring(4)).c_str());
      bool chg = (sep > 0) && (atoi(cmd.substring(sep + 1).c_str()) != 0);
      if (bPct >= 0 && bPct <= 100) { hostBatPct = (float)bPct; hostBatCharging = chg; hostBatValid = true; }
    } else if (cmd.startsWith("EPOCH ")) {
      unsigned long utc = 0; long tzm = rtc.getTzOffsetMin();
      if (sscanf(cmd.c_str() + 6, "%lu %ld", &utc, &tzm) >= 1 && utc > 1700000000UL) {
        bleGotDirectCmd = true; rtc.applyUtcAndTz((uint32_t)utc, (int32_t)tzm, viaSrc);
      }
    } else if (cmd.startsWith("SET ")) {
      int y, mo, d, hh, mm, ss;
      if (sscanf(cmd.c_str() + 4, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss) == 6) {
        bleGotDirectCmd = true; rtc.setLocalEpoch(DateTime(y, mo, d, hh, mm, ss).unixtime(), viaSrc, true, true);
      }
    } else if (cmd.startsWith("W:") || cmd.startsWith("WIFI ")) {
      String pay = cmd.startsWith("W:") ? cmd.substring(2) : cmd.substring(5);
      int sep = pay.indexOf('|');
      if (sep > 0) { String s = pay.substring(0, sep); s.trim(); saveWifi(1, s, pay.substring(sep + 1)); }
    } else if (cmd.startsWith("M:") && onModeChangeCb) {
      int m = atoi(cmd.c_str() + 2);
      if (m >= 0 && m < 8) onModeChangeCb((ScreenMode)m);
    } else if (cmd.startsWith("F:")) {
      int f = atoi(cmd.c_str() + 2);
      if (f >= 0 && f < 20) anim.setFaceIndex(f);
    } else if (cmd.startsWith("B:") && onBriChangeCb) {
      onBriChangeCb((uint8_t)atoi(cmd.c_str() + 2));
    } else if (cmd == "SW:TOGGLE" && onSwCmdCb) {
      onSwCmdCb(false);
    } else if (cmd == "SW:RESET" && onSwCmdCb) {
      onSwCmdCb(true);
    } else if (cmd == "CLR") {
      clearWifi();
    } else if (cmd == "STATUS") {
      Serial.println(buildStatusJson());
    }
    updateBleStatus(true);
  }

  void beginRadios() {
    static const uint8_t hidMap[] = {
      0x05,0x0C,0x09,0x01,0xA1,0x01,0x85,0x01,0x15,0x00,0x25,0x01,0x75,0x01,
      0x95,0x03,0x09,0xCD,0x09,0xE9,0x09,0xEA,0x81,0x02,0x95,0x05,0x81,0x03,0xC0
    };
    NimBLEDevice::init(BLE_NAME);
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityInitKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);
    NimBLEDevice::setSecurityRespKey(BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID);

    bleServer = NimBLEDevice::createServer();
    bleServer->advertiseOnDisconnect(false);
    bleServer->setCallbacks(new ServerCb(this));

    bleHid = new NimBLEHIDDevice(bleServer);
    bleHid->manufacturer(BOOT_OWNER);
    bleHid->pnp(0x02, 0xE502, 0xA111, 0x0100);
    bleHid->hidInfo(0x00, 0x02);
    bleHid->reportMap((uint8_t*)hidMap, sizeof(hidMap));
    bleHidInput = bleHid->inputReport(1);
    uint8_t z = 0; bleHidInput->setValue(&z, 1);
    bleHid->setBatteryLevel(100);
    bleBatChar = bleHid->batteryLevel();

    NimBLEService* svc = bleServer->createService(DINO_BLE_SERVICE_UUID);
    bleCmdChar = svc->createCharacteristic(
      DINO_BLE_CHAR_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY
    );
    bleCmdChar->setCallbacks(new CharCb(this));
    bleHid->startServices();
    svc->start();

    bleAdvData = NimBLEAdvertisementData();
    bleAdvData.setFlags(BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP);
    bleAdvData.setAppearance(GENERIC_HID);
    bleAdvData.setCompleteServices16({NimBLEUUID((uint16_t)0x1812), NimBLEUUID((uint16_t)0x180F)});
    const uint8_t solCts[4] = {0x03, BLE_HS_ADV_TYPE_SOL_UUIDS16, 0x05, 0x18};
    bleAdvData.addData(std::string((const char*)solCts, sizeof(solCts)));
    bleAdvData.setName(BLE_NAME);

    bleScanData = NimBLEAdvertisementData();
    bleScanData.setCompleteServices(NimBLEUUID(DINO_BLE_SERVICE_UUID));
    bleScanData.setName(BLE_NAME);

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->setMinPreferred(0x06); adv->setMaxPreferred(0x12);
    startBleAdv();

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(AP_SSID, nullptr, 1, 0, 4);
    delay(100);
    WiFi.setTxPower(WIFI_POWER_13dBm);
    WiFi.setAutoReconnect(true);

    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", WiFi.softAPIP());
    auto sendPortal = [this]() { server.send_P(200, "text/html", SYNC_PORTAL_HTML); };
    static const char* portalPaths[] = {"/", "/hotspot-detect.html", "/library/test/success.html", "/generate_204", "/gen_204", "/connecttest.txt", "/ncsi.txt"};
    for (const char* p : portalPaths) server.on(p, HTTP_GET, sendPortal);
    server.on("/api/status", HTTP_GET, [this]() { server.send(200, "application/json", buildStatusJson()); });
    server.on("/api/sync", HTTP_GET, [this]() {
      if (!server.hasArg("utc")) { server.send(400, "text/plain", "Missing utc"); return; }
      uint32_t utc = (uint32_t)strtoul(server.arg("utc").c_str(), nullptr, 10);
      int32_t tzm = server.hasArg("tzmin") ? (int32_t)atol(server.arg("tzmin").c_str()) : rtc.getTzOffsetMin();
      if (utc > 1700000000UL) { rtc.applyUtcAndTz(utc, tzm, "WEB"); server.send(200, "text/plain", "Synced clock from browser!"); }
      else server.send(400, "text/plain", "Missing utc");
    });
    server.on("/api/scan", HTTP_GET, [this]() {
      WiFi.disconnect(false);
      int n = WiFi.scanNetworks(false, false);
      String out = "[";
      for (int i = 0; i < n && i < 12; i++) {
        String s = WiFi.SSID(i); s.replace("\"", "");
        out += String(i > 0 ? "," : "") + "{\"ssid\":\"" + s + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
      }
      out += "]";
      WiFi.scanDelete();
      if (hasSavedWifi()) staState = STA_IDLE;
      server.send(200, "application/json", out);
    });
    server.on("/api/wifi", HTTP_POST, [this]() {
      int slot = server.hasArg("slot") ? server.arg("slot").toInt() : 1;
      String ssid = server.arg("ssid"), pass = server.arg("pass");
      ssid.trim();
      if (ssid.length() == 0) { server.send(400, "text/plain", "SSID required"); return; }
      saveWifi(slot, ssid, pass);
      server.send(200, "text/plain", "Saved '" + ssid + "'. Connecting...");
    });
    server.on("/api/clearwifi", HTTP_GET, [this]() { clearWifi(); server.send(200, "text/plain", "Cleared saved Wi-Fi."); });
    server.on("/api/cmd", HTTP_GET, [this]() {
      if (server.hasArg("mode") && onModeChangeCb) { int m = server.arg("mode").toInt(); if (m >= 0 && m < 8) onModeChangeCb((ScreenMode)m); }
      if (server.hasArg("face")) { int f = server.arg("face").toInt(); if (f >= 0 && f < 20) anim.setFaceIndex(f); }
      if (server.hasArg("bri") && onBriChangeCb) onBriChangeCb((uint8_t)server.arg("bri").toInt());
      if (server.hasArg("sw") && onSwCmdCb) { String sw = server.arg("sw"); if (sw == "toggle") onSwCmdCb(false); else if (sw == "reset") onSwCmdCb(true); }
      if (server.hasArg("bat")) { int b = server.arg("bat").toInt(); if (b >= 0 && b <= 100) { hostBatPct = (float)b; hostBatCharging = server.arg("chg").toInt() != 0; hostBatValid = true; } }
      server.send(200, "text/plain", "OK");
    });
    server.onNotFound([this]() { server.sendHeader("Location", "http://192.168.4.1/", true); server.send(302, "text/plain", ""); });
    server.begin();
    if (MDNS.begin("dinokey")) MDNS.addService("http", "tcp", 80);
  }

  bool syncTimeViaHttpDate() {
    static const char* hosts[] = {"captive.apple.com", "clients3.google.com"};
    static const char* paths[] = {"/hotspot-detect.html", "/generate_204"};
    for (int i = 0; i < 2; i++) {
      WiFiClient c; c.setTimeout(1500);
      if (!c.connect(hosts[i], 80)) continue;
      c.print(String("HEAD ") + paths[i] + " HTTP/1.0\r\nHost: " + hosts[i] + "\r\nConnection: close\r\n\r\n");
      unsigned long t0 = millis();
      while (c.connected() && !c.available() && (millis() - t0 < 1500UL)) delay(5);
      while (c.available()) {
        String line = c.readStringUntil('\n'); line.trim();
        if (line.startsWith("Date:") || line.startsWith("date:")) {
          char mon[8] = {0}; int d, y, hh, mm, ss;
          const char* p = strchr(line.c_str(), ',');
          if (p && sscanf(p + 1, " %d %3s %d %d:%d:%d", &d, mon, &y, &hh, &mm, &ss) == 6 && y >= 2024) {
            const char* mos = "JanFebMarAprMayJunJulAugSepOctNovDec";
            if (const char* f = strstr(mos, mon)) {
              c.stop();
              rtc.applyUtcAndTz(DateTime(y, (int)((f - mos) / 3) + 1, d, hh, mm, ss).unixtime(), rtc.getTzOffsetMin(), "NET");
              return true;
            }
          }
        }
        if (line.length() == 0) break;
      }
      c.stop();
    }
    return false;
  }

  void update() {
    if (Serial.available()) executeCommand(Serial.readStringUntil('\n'), "USB");

    if (blePending) {
      char buf[192];
      portENTER_CRITICAL(&bleMux); strncpy(buf, bleBuf, sizeof(buf) - 1); buf[sizeof(buf) - 1] = '\0'; blePending = false; portEXIT_CRITICAL(&bleMux);
      executeCommand(String(buf), "BLE");
    }
    if (bleCtsPending) {
      portENTER_CRITICAL(&bleMux);
      uint32_t ep = bleCtsLocalEpoch; int32_t tzm = bleCtsTzMin; bool hz = bleCtsHasTz; bleCtsPending = false;
      portEXIT_CRITICAL(&bleMux);
      if (ep > 1700000000UL) {
        if (hz) rtc.applyUtcAndTz((uint32_t)((int64_t)ep - (int64_t)tzm * 60LL), tzm, "BLE");
        else rtc.setLocalEpoch(ep, "BLE", true, true);
      }
    }
    if (bleSvcChangedPending && bleConnected) { bleSvcChangedPending = false; ble_svc_gatt_changed(0x0001, 0xffff); }
    if (bleHostBatPending) {
      portENTER_CRITICAL(&bleMux); uint8_t bv = bleHostBatVal; bleHostBatPending = false; portEXIT_CRITICAL(&bleMux);
      if (bv <= 100) { hostBatPct = (float)bv; hostBatCharging = false; hostBatValid = true; }
    }
    if (bleConnected && bleBatChar) {
      uint8_t cur = battery.isAvailable() ? (uint8_t)constrain((int)battery.getPercent(), 0, 100) : 100;
      if (cur != lastBatPushed) { lastBatPushed = cur; bleBatChar->setValue(&cur, 1); bleBatChar->notify(); }
    }
    unsigned long now = millis();
    if (bleConnected && bleCtsState == CTS_DONE && blePeerBatHandle && !battery.isAvailable() && bleNextBatPollMs && now >= bleNextBatPollMs) {
      bleNextBatPollMs = now + 60000UL;
      setCts(CTS_READ_BAT_WAIT);
      ble_gattc_read(activeBleConnHandle, blePeerBatHandle, onBleBatReadCb, this);
    }
    if (bleConnected && bleCtsState != CTS_IDLE && bleCtsState != CTS_DONE && bleNextCtsProbeMs && now >= bleNextCtsProbeMs) {
      BleCtsState st = bleCtsState; uint16_t ch = activeBleConnHandle;
      if (st == CTS_WAIT_SEC && !bleGotDirectCmd) {
        if (!bleCtsSecStarted) { bleCtsSecStarted = true; NimBLEDevice::startSecurity(ch); bleNextCtsProbeMs = now + 900UL; }
        else {
          ble_gap_conn_desc d;
          if (ble_gap_conn_find(ch, &d) == 0 && d.sec_state.encrypted) { bleCtsEncrypted = true; bleCtsAttempt = 0; setCts(CTS_DISC_SVC, 350UL); }
          else if (now - bleConnAtMs >= 10000UL && bleCtsAttempt == 0) setCts(CTS_DISC_SVC, 50UL);
          else bleNextCtsProbeMs = now + 1000UL;
        }
      } else if (st == CTS_DISC_SVC && !bleGotDirectCmd) {
        static const ble_uuid16_t u = BLE_UUID16_INIT(0x1805);
        portENTER_CRITICAL(&bleMux); bleCtsState = CTS_DISC_SVC_WAIT; bleCtsSvcStart = bleCtsSvcEnd = 0; portEXIT_CRITICAL(&bleMux);
        ble_gattc_exchange_mtu(ch, nullptr, nullptr);
        if (ble_gattc_disc_svc_by_uuid(ch, &u.u, onBleCtsSvcCb, this) != 0) {
          if (bleCtsAttempt < 10) { bleCtsAttempt++; setCts(CTS_DISC_SVC, 2000UL); }
          else setCts(CTS_DISC_BAT_SVC, 60UL);
        }
      } else if (st == CTS_DISC_CHR) {
        portENTER_CRITICAL(&bleMux); bleCtsState = CTS_DISC_CHR_WAIT; bleCtsTimeHandle = bleCtsLocalHandle = 0; portEXIT_CRITICAL(&bleMux);
        if (ble_gattc_disc_all_chrs(ch, bleCtsSvcStart, bleCtsSvcEnd, onBleCtsChrCb, this) != 0) setCts(CTS_DISC_BAT_SVC, 40UL);
      } else if (st == CTS_READ_LOCAL) {
        setCts(CTS_READ_LOCAL_WAIT);
        if (ble_gattc_read(ch, bleCtsLocalHandle, onBleCtsLocalReadCb, this) != 0) setCts(CTS_READ_TIME, 40UL);
      } else if (st == CTS_READ_TIME) {
        setCts(CTS_READ_TIME_WAIT);
        if (ble_gattc_read(ch, bleCtsTimeHandle, onBleCtsTimeReadCb, this) != 0) setCts(CTS_DISC_BAT_SVC, 40UL);
      } else if (st == CTS_DISC_BAT_SVC) {
        static const ble_uuid16_t u = BLE_UUID16_INIT(0x180F);
        portENTER_CRITICAL(&bleMux); bleCtsState = CTS_DISC_BAT_SVC_WAIT; bleBatSvcStart = bleBatSvcEnd = 0; portEXIT_CRITICAL(&bleMux);
        if (ble_gattc_disc_svc_by_uuid(ch, &u.u, onBleBatSvcCb, this) != 0) setCts(CTS_DONE);
      } else if (st == CTS_DISC_BAT_CHR) {
        portENTER_CRITICAL(&bleMux); bleCtsState = CTS_DISC_BAT_CHR_WAIT; blePeerBatHandle = 0; portEXIT_CRITICAL(&bleMux);
        if (ble_gattc_disc_all_chrs(ch, bleBatSvcStart, bleBatSvcEnd, onBleBatChrCb, this) != 0) setCts(CTS_DONE);
      } else if (st == CTS_READ_BAT) {
        setCts(CTS_READ_BAT_WAIT);
        if (ble_gattc_read(ch, blePeerBatHandle, onBleBatReadCb, this) != 0) setCts(CTS_DONE);
      }
    }

    dnsServer.processNextRequest();
    server.handleClient();

#if ENABLE_WIFI_NTP
    if (WiFi.status() == WL_CONNECTED) {
      if (staState != STA_CONNECTED) {
        staState = STA_CONNECTED; staConnectedAtMs = now; lastHttpTimeTryMs = lastNtpMs = 0;
        if (!sntpStarted) { configTime(0, 0, "pool.ntp.org", "time.google.com", "time.apple.com"); sntpStarted = true; }
      }
      if (time(nullptr) > 1700000000) {
        if (!ntpSyncedOnce || (now - lastNtpMs >= 1800000UL)) {
          ntpSyncedOnce = true; lastNtpMs = now;
          rtc.applyUtcAndTz((uint32_t)time(nullptr), rtc.getTzOffsetMin(), "NTP");
        }
      } else if (!rtc.isLiveSynced() && (now - staConnectedAtMs >= 3000UL) && (!lastHttpTimeTryMs || (now - lastHttpTimeTryMs >= 15000UL))) {
        lastHttpTimeTryMs = now;
        if (syncTimeViaHttpDate()) { ntpSyncedOnce = true; lastNtpMs = now; }
      }
      return;
    }
    if (staState == STA_CONNECTED) { staState = STA_COOLDOWN; staStateMs = now; }
    if (!hasSavedWifi()) { staState = STA_IDLE; return; }
    if (staState == STA_IDLE) {
      String s, p;
      for (int i = 0; i < 3; i++) {
        if (getSlotCreds(activeSlot, s, p)) { activeSsid = s; WiFi.begin(s.c_str(), p.c_str()); staState = STA_CONNECTING; staStateMs = now; return; }
        activeSlot = (activeSlot + 1) % 3;
      }
    } else if (staState == STA_CONNECTING && (now - staStateMs >= 12000UL)) {
      WiFi.disconnect(false);
      activeSlot = (activeSlot + 1) % 3;
      staState = (activeSlot == 0) ? STA_COOLDOWN : STA_IDLE;
      staStateMs = now;
    } else if (staState == STA_COOLDOWN && (now - staStateMs >= 45000UL)) {
      activeSlot = 0; staState = STA_IDLE;
    }
#endif
  }
};
