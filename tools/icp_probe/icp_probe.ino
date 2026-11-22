// ICP power-cycle probe for Nuvoton MS51 / N76E003.
// Holds nRESET low and repeatedly sends the ICP entry bits, so the chip is
// caught in ICP mode right after its VDD is (re)applied. This works even when
// CONFIG0 has the reset pin turned into a GPIO.
//
// Wiring (Uno): D11 -> ICPDA, D12 -> ICPCK, D13 -> nRESET, GND -> VSS.
// Procedure: open serial @115200, disconnect the chip's VDD, then reconnect it.
// Read-only: it never erases or writes anything.
#include <Arduino.h>
extern "C" {
#include "n51_icp.h"
#include "n51_pgm.h"
}

static uint32_t tries = 0;
static bool done = false;

static void hex2(uint8_t b) {
  if (b < 0x10) Serial.print('0');
  Serial.print(b, HEX);
}

void setup() {
  Serial.begin(115200);
  N51ICP_init();
  N51PGM_set_rst(0);  // hold the chip in reset the whole time
  Serial.println(F("\nICP probe ready. Holding RST low."));
  Serial.println(F("Now disconnect the chip's VDD, wait a second, then reconnect it."));
}

void loop() {
  if (done) return;

  N51ICP_send_entry_bits();
  delayMicroseconds(10);
  uint32_t devid = N51ICP_read_device_id() & 0xFFFF;

  if (devid != 0x0000 && devid != 0xFFFF) {
    done = true;
    uint8_t cid = N51ICP_read_cid();
    uint8_t cfg[5];
    N51ICP_read_flash(0x30000, sizeof(cfg), cfg);
    uint8_t aprom[16];
    N51ICP_read_flash(0x0000, sizeof(aprom), aprom);

    Serial.print(F("\nFOUND after ")); Serial.print(tries); Serial.println(F(" tries"));
    Serial.print(F("DEVICE_ID 0x")); Serial.println(devid, HEX);
    Serial.print(F("CID 0x")); hex2(cid); Serial.println();
    Serial.print(F("CONFIG"));
    for (uint8_t i = 0; i < sizeof(cfg); i++) { Serial.print(' '); hex2(cfg[i]); }
    Serial.println();
    Serial.print(F("APROM[0:16]"));
    for (uint8_t i = 0; i < sizeof(aprom); i++) { Serial.print(' '); hex2(aprom[i]); }
    Serial.println();
    Serial.println(F("DONE (chip left in ICP mode, RST held low)"));
    return;
  }

  tries++;
  if (tries % 200 == 0) Serial.print('.');  // heartbeat, ~every couple of seconds
  delay(10);
}
