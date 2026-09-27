#include <Arduino.h>
#include <ArduinoOTA.h>
//#include <M5Unified.hpp>

//#include "LGFX_TDongleS3-v1.h"
#include "SD_MMC.h"
#include "WiFi.h"
#include <WiFiMulti.h>
#include <FastLED.h>

#include "config.h"
#include "display.h"
#include "midi_bridge.h"
#include "spark_ble.h"
#include "spark_state.h"
#include "tuner.h"

namespace {

#define BOOT_PIN       0
#define LED_DI_PIN     40
#define LED_CI_PIN     39

//CRGB leds;

const char* connectionStateText(spark_ble::ConnectionState state) {
  switch (state) {
    case spark_ble::ConnectionState::kDisconnected:
      return "Disconnected";
    case spark_ble::ConnectionState::kScanning:
      return "Scanning...";
    case spark_ble::ConnectionState::kConnecting:
      return "Connecting...";
    case spark_ble::ConnectionState::kConnected:
      return "Connected";
  }
  return "?";
}

uint32_t g_lastBootButtonMs = 0;
bool g_wasTunerOn = false;

constexpr uint32_t kBootSplashHoldMs = 1200;

//Preferences preferences;
//void preset_store();

// Renders the pre-connection/connection-lost diagnostic view right now, from
// whatever spark_ble/spark_state currently report. Called from loop() every
// iteration - see spark_ble::loop()'s own comments for why its scan/connect
// attempts are now split across loop() iterations (kScanning/kConnecting are
// each set one iteration before the matching blocking call actually runs):
// that's what lets this ordinary top-level redraw show "Scanning..." and
// "Connecting..." at all, without needing to draw from inside spark_ble's
// own call chain or callback (an earlier attempt at the latter was
// suspected of interfering with the scan itself on real hardware).
void renderStatusView() {
  int patch0Based = spark_state::activePatch0Based();
  // rx/sub/cccd are diagnostics (see spark_ble::rawNotificationCount/
  // lastSubscribeOk/cccdFound) - only useful while still trying to connect.
  String connectionLine = String(connectionStateText(spark_ble::state())) + " rx:" +
                           spark_ble::rawNotificationCount() +
                           (spark_ble::lastSubscribeOk() ? " sub:Y" : " sub:N") +
                           (spark_ble::cccdFound() ? " cccd:Y" : " cccd:N");
  display::showStatus(connectionLine, patch0Based >= 0 ? patch0Based + 1 : -1,
                       midi_bridge::lastEventText());
}

// Serial goes out over the TX/RX pins (GPIO43/44), not the main USB-A plug
// (that one's dedicated to USB-MIDI) - so seeing these needs an external
// USB-serial adapter wired to those pins, not just the same USB cable used
// to flash. See README.md. Deliberately Serial-only, no display calls here -
// see renderStatusView()'s comment.
void handleConnectionStateChanged(spark_ble::ConnectionState state) {
  Serial.print("[BLE] ");
  Serial.println(connectionStateText(state));

  // Drop the cached patch/effect state on disconnect rather than letting it
  // silently survive until the next connection's preset read overwrites it -
  // otherwise a stale patch number/name could briefly show as current right
  // after reconnecting, before that fresh read completes.
  if (state == spark_ble::ConnectionState::kDisconnected) {
    spark_state::reset();
  }
}

}  // namespace

Preferences preferences;
void preset_store();

void setup() {
  setCpuFrequencyMhz(160);
  Serial.begin(115200);
  //while(!Serial) {
  //  delay(10); 
  //}
  delay(1000);
  Serial.println("--- spark-go-utils-TDS3 Start ---");
  
  pinMode(BOOT_PIN, INPUT);
  //FastLED.addLeds<APA102, LED_DI_PIN, LED_CI_PIN, BGR>(&leds, 1);
  //FastLED.setBrightness(100);

  // One checkpoint per init step, both on Serial and on the screen itself
  // (showStatus() doubles as a generic "one status line + one detail line"
  // display, reused here before the BLE/MIDI machinery it's normally fed by
  // even exists yet). If the board hangs or crashes during setup(), whatever
  // is still on screen (or the last Serial line) says exactly where -
  // useful without needing a USB-serial adapter at all, since the display is
  // already confirmed working on its own.
  display::begin();
  display::showBootSplash();
  delay(kBootSplashHoldMs);
  display::showStatus("Booting", -1, "display OK");
  Serial.println("display::begin() done");

  // Preset Store: if Storeing do not need, The Next sentens should be comentout. 
  preset_store();
  //

  midi_bridge::begin();
  display::showStatus("Booting", -1, "USB-MIDI OK");
  Serial.println("midi_bridge::begin() done");

  tuner::begin();
  display::showStatus("Booting", -1, "tuner OK");
  Serial.println("tuner::begin() done");

  spark_ble::begin();
  display::showStatus("Booting", -1, "BLE init OK");
  Serial.println("spark_ble::begin() done");

  pinMode(config::kBootButtonPin, INPUT_PULLUP);

  // spark_state is the source of truth for effect-toggle slot state; keep it
  // fed from whatever the device tells us. Display refresh itself happens
  // unconditionally every loop() iteration (see below), not from these
  // callbacks - simpler than trying to catch every path that changes what's
  // on screen.
  // setActivePatch() shows the patch *number* as soon as it's known, rather
  // than waiting on the slower multi-chunk preset read (applyPreset()) that
  // also carries the patch *name* - decouples the two, since the preset read
  // occasionally not completing (e.g. right after connecting) shouldn't
  // leave the number blank too.
  spark_ble::onPatchConfirmed(
      [](uint8_t patch0Based) { spark_state::setActivePatch(patch0Based); });
  spark_ble::onPreset(
      [](const spark_protocol::PresetData& preset) { spark_state::applyPreset(preset); });
  spark_ble::onEffectState(
      [](const spark_protocol::EffectStateEvent& event) { spark_state::applyEffectState(event); });
  spark_ble::onTunerFrame(tuner::handleFrame);
  spark_ble::onConnectionStateChanged(handleConnectionStateChanged);

  display::showStatus("Booting", -1, "setup() done");
  ChocolateBle::init();
  Serial.println("setup() complete, entering loop()");
  Serial.println("[BLE] Looking for Spark GO...");
}

void loop() {
  spark_ble::loop();
  midi_bridge::loop();

  bool tunerOn = tuner::isOn();
  if (tunerOn) {
    tuner::Reading reading = tuner::currentReading();
    display::showTuner(reading.noteName, reading.cents, reading.hasSignal);
  } else {
    if (g_wasTunerOn) display::invalidate();  // force a redraw after leaving the tuner view
    if (spark_ble::isConnected()) {
      // Once connected, the pre-connection diagnostics aren't interesting
      // anymore - show the actual patch number/name instead. Falls straight
      // back to the diagnostic view below on its own if the connection
      // drops (isConnected() becomes false again), no extra handling needed.
      int patch0Based = spark_state::activePatch0Based();
      display::showConnected(patch0Based >= 0 ? patch0Based + 1 : -1, spark_state::activePatchName(),
                             midi_bridge::lastEventText());
    } else {
      renderStatusView();
    }
  }
  g_wasTunerOn = tunerOn;

  // BOOT button (GPIO0) = manual "force BLE reconnect", debounced.
  if (digitalRead(config::kBootButtonPin) == LOW) {
    uint32_t now = millis();
    if (now - g_lastBootButtonMs > 500) {
      g_lastBootButtonMs = now;
      spark_ble::forceReconnect();
      delay(100);
      ESP.restart();
      
    }
  }
}

// ============================================================================
void hexToBytes(const char* hexStr, uint8_t* byteArr, size_t maxLen) {
  size_t len = strlen(hexStr);
  for (size_t i = 0; i < len && (i / 2) < maxLen; i += 2) {
    char s[3] = { hexStr[i], hexStr[i+1], '\0' };
    byteArr[i / 2] = (uint8_t)strtol(s, NULL, 16);
  }
}

void restoreSingleSlot(int slotNum, const char* hexData) {
  if (strlen(hexData) == 0) {
    Serial.printf("[Skip] Slot %d is empty.\n", slotNum);
    return;
  }

  char slotKey[10];
  snprintf(slotKey, sizeof(slotKey), "slot_%d", slotNum);

  SparkPreset tempPreset;
  hexToBytes(hexData, (uint8_t*)&tempPreset, sizeof(SparkPreset));

  // T-Dongle-S3Preferences
  size_t written = preferences.putBytes(slotKey, &tempPreset, sizeof(SparkPreset));

  if (written == sizeof(SparkPreset)) {
    Serial.printf("[Success] Slot %d safely restored (%d bytes).\n", slotNum, written);
  } else {
    Serial.printf("[Error] Slot %d write fail. Size mismatch.\n", slotNum);
  }
}

void preset_store() {

  Serial.println("\n=== START PRESET RESTORE PROCESS ===");
  preferences.begin("presets", false);

  restoreSingleSlot(1, PROG_SLOT_1);
  restoreSingleSlot(2, PROG_SLOT_2);
  restoreSingleSlot(3, PROG_SLOT_3);
  restoreSingleSlot(4, PROG_SLOT_4);
  restoreSingleSlot(5, PROG_SLOT_5);
  restoreSingleSlot(6, PROG_SLOT_6);
  restoreSingleSlot(7, PROG_SLOT_7);
  restoreSingleSlot(8, PROG_SLOT_8);
  restoreSingleSlot(9, PROG_SLOT_9);
  restoreSingleSlot(10, PROG_SLOT_10);
  restoreSingleSlot(11, PROG_SLOT_11);
  restoreSingleSlot(12, PROG_SLOT_12);
  restoreSingleSlot(13, PROG_SLOT_13);
  restoreSingleSlot(14, PROG_SLOT_14);
  restoreSingleSlot(15, PROG_SLOT_15);
  restoreSingleSlot(16, PROG_SLOT_16);

  preferences.end();
  Serial.println("=== RESTORE PROCESS COMPLETED ===");
}
