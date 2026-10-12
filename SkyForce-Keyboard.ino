  /*
  Skyforce 68 - Arduino Pro Micro + PCF8574

  This sketch scans a 5x15 matrix where columns 0..7 are wired to a single PCF8574 I/O
  expander on I2C address 0x20 and columns 8..14 are wired directly to MCU pins.

  Configure the ROW_PINS and COL_PINS_MCU arrays below to match your Pro Micro wiring
  (use Arduino digital pin numbers). The PCF8574 address can be changed with PCF_ADDR.

  Two modes:
   - If DEBUG_SERIAL is true the sketch will print matrix changes to Serial for testing.
   - Otherwise it will use Keyboard.* to send HID events.

  Note: Update the pin arrays to Arduino pin numbers before uploading.
*/

#include <Wire.h>
#include <Keyboard.h>
#include "keymap.h"

// ====== CONFIG ======
// Replace these with Arduino digital pin numbers for your Pro Micro wiring.
// If you know which Pro Micro pads you want to use, update these arrays.
// I provide a sensible example mapping below (common pins used on Pro Micro)
// — change them to match your PCB or wiring.
//
// Mapping assumptions:
// - Columns 0..7 are on the PCF8574 P0..P7 (I2C). The sketch reads those from the
//   expander; do NOT include them in COL_PINS_MCU.
// - COL_PINS_MCU holds the Arduino digital pin numbers for matrix columns 8..14
//   (7 pins). ROW_PINS holds Arduino pin numbers used as row drivers.

// ===== Example mapping (edit as needed) =====
const uint8_t ROW_PINS[5] = { 16, 14, 15, A0, A1 }; // example: 5 row pins

// MCU pins for columns 8..14 (7 pins). These are Arduino digital pin numbers.
// Requested mapping from user:
// - Column 9 (1-based) -> Arduino pin 4  -> (column index 8 -> pin 4)
// - Column 15 (1-based) -> Arduino pin 10 -> (column index 14 -> pin 10)
// We'll map the middle columns sequentially so columns 8..14 map to pins 4..10.
const uint8_t COL_PINS_MCU[7] = { 4, 5, 6, 7, 8, 9, 10 };

// If these don't match your wiring, replace with the pins you want to use.

const uint8_t MATRIX_ROWS = 5;
const uint8_t MATRIX_COLS = 15;

const uint8_t PCF_ADDR = 0x20; // default PCF8574

// Diode orientation: set true if diodes are connected from COLUMN -> ROW (anode on column,
// cathode on row). You wrote "colrown şeklinde diyotlar bağlandı" which I interpret as
// column->row diodes. The scanning logic below will treat a LOW on a column input as a
// key press when this is true.
const bool DIODES_COL2ROW = true;

// Cached PCF data for the currently selected row to avoid repeated I2C reads per bit.
uint8_t pcf_row_data = 0xFF;
bool pcf_row_data_valid = false;
// flag set when last pcf_read succeeded
bool pcf_ok = false;
// consecutive PCF failure counter; only force-release after threshold
uint8_t pcf_fail_count = 0;
const uint8_t PCF_FAIL_THRESHOLD = 3;

// Debounce settings
const uint16_t SCAN_INTERVAL_MS = 10; // scan interval
// Increased debounce to reduce chatter / stuck key symptoms. You can lower if response
// feels sluggish.
const uint16_t DEBOUNCE_MS = 20;

// Set true to print matrix events to Serial instead of sending HID
const bool DEBUG_SERIAL = true;

// runtime state
// use 16-bit masks because the matrix has 15 columns (needs >8 bits)
uint16_t prev_matrix[5];
uint32_t last_change_ts[5][15];
// logical pressed state we reported to the host. This prevents duplicate press
// events and helps ensure releases are sent even when scans are noisy.
bool reported_pressed[5][15];
// timestamps when we reported a key down to the host (ms)
uint32_t reported_ts[5][15];
// if a key remains reported as pressed longer than this without physical press,
// force a release (ms)
const uint32_t STUCK_RELEASE_MS = 2000;

void setup_pins() {
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    pinMode(ROW_PINS[r], INPUT_PULLUP);
  }
  for (uint8_t i = 0; i < 7; i++) {
    pinMode(COL_PINS_MCU[i], INPUT_PULLUP);
  }
}

uint8_t pcf_read() {
  // Try a few times to robustly read the PCF8574. Write 0xFF to ensure
  // quasi-bidirectional pins are pulled high, then request one byte.
  for (uint8_t attempt = 0; attempt < 3; attempt++) {
    Wire.beginTransmission(PCF_ADDR);
    Wire.write(0xFF);
    uint8_t status = Wire.endTransmission();
    if (status != 0) {
      // NACK or bus error — retry
      delay(2);
      continue;
    }
    Wire.requestFrom(PCF_ADDR, (uint8_t)1);
    if (Wire.available()) {
      uint8_t val = Wire.read();
      pcf_ok = true;
      return val;
    }
    delay(2);
  }
  // failed to read
  pcf_ok = false;
  return 0xFF;
}

void select_row(uint8_t row) {
  // set selected row low, others hi-z pullup
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    if (r == row) {
      pinMode(ROW_PINS[r], OUTPUT);
      digitalWrite(ROW_PINS[r], LOW);
    } else {
      pinMode(ROW_PINS[r], INPUT_PULLUP);
    }
  }
}

void release_row(uint8_t row) {
  pinMode(ROW_PINS[row], INPUT_PULLUP);
}

bool read_matrix_row_bit(uint8_t row, uint8_t col) {
  // returns true if released (no press), false if pressed -- matches PCF pattern
  if (col < 8) {
    // read from PCF; prefer cached per-row value
    uint8_t data = pcf_row_data_valid ? pcf_row_data : pcf_read();
    // User requested mapping: matrix column 0 -> PCF P7, column 1 -> P6, ... column7 -> P0
    // That means PCF bit index = 7 - col
    uint8_t bit = 7 - col;
    bool bit_high = (data & (1 << bit)) != 0;
    // If diodes are Column->Row, a high bit means released (pull-ups), low means pressed.
    return DIODES_COL2ROW ? bit_high : !bit_high;
  } else {
    uint8_t idx = col - 8;
    bool val = digitalRead(COL_PINS_MCU[idx]) == HIGH;
    return DIODES_COL2ROW ? val : !val;
  }
}

void send_key_down(const char *kc) {
  // map keycode string like "KC_A" to HID
  if (strcmp(kc, "KC_NO") == 0) return;

  if (strcmp(kc, "KC_SPC") == 0 || strcmp(kc, "KC_SPACE") == 0) {
    Keyboard.press(' ');
    return;
  }

  // letters
  if (kc[3] >= 'A' && kc[3] <= 'Z' && strlen(kc) == 4) {
    char c = kc[3] - 'A' + 'a';
    Keyboard.press(c);
    return;
  }

  // numbers KC_1..KC_0
  if (strcmp(kc, "KC_1") == 0) { Keyboard.press('1'); return; }
  if (strcmp(kc, "KC_2") == 0) { Keyboard.press('2'); return; }
  if (strcmp(kc, "KC_3") == 0) { Keyboard.press('3'); return; }
  if (strcmp(kc, "KC_4") == 0) { Keyboard.press('4'); return; }
  if (strcmp(kc, "KC_5") == 0) { Keyboard.press('5'); return; }
  if (strcmp(kc, "KC_6") == 0) { Keyboard.press('6'); return; }
  if (strcmp(kc, "KC_7") == 0) { Keyboard.press('7'); return; }
  if (strcmp(kc, "KC_8") == 0) { Keyboard.press('8'); return; }
  if (strcmp(kc, "KC_9") == 0) { Keyboard.press('9'); return; }
  if (strcmp(kc, "KC_0") == 0) { Keyboard.press('0'); return; }

  // common symbols
  if (strcmp(kc, "KC_MINS") == 0) { Keyboard.press('-'); return; }
  if (strcmp(kc, "KC_EQL") == 0) { Keyboard.press('='); return; }
  if (strcmp(kc, "KC_BSPC") == 0) { Keyboard.press(KEY_BACKSPACE); return; }
  if (strcmp(kc, "KC_ENT") == 0) { Keyboard.press(10); return; }
  if (strcmp(kc, "KC_TAB") == 0) { Keyboard.press('\t'); return; }
  if (strcmp(kc, "KC_ESC") == 0) { Keyboard.press(KEY_ESC); return; }
  if (strcmp(kc, "KC_DEL") == 0) { Keyboard.press(KEY_DELETE); return; }
  if (strcmp(kc, "KC_COMM") == 0) { Keyboard.press(','); return; }
  if (strcmp(kc, "KC_DOT") == 0) { Keyboard.press('.'); return; }
  if (strcmp(kc, "KC_SLSH") == 0) { Keyboard.press('/'); return; }
  if (strcmp(kc, "KC_SCLN") == 0) { Keyboard.press(';'); return; }
  if (strcmp(kc, "KC_QUOT") == 0) { Keyboard.press('\''); return; }
  if (strcmp(kc, "KC_LBRC") == 0) { Keyboard.press('['); return; }
  if (strcmp(kc, "KC_RBRC") == 0) { Keyboard.press(']'); return; }
  if (strcmp(kc, "KC_BSLS") == 0) { Keyboard.press('\\'); return; }
  if (strcmp(kc, "KC_GRV") == 0) { Keyboard.press('`'); return; }

  // arrows and modifiers
  if (strcmp(kc, "KC_UP") == 0) { Keyboard.press(KEY_UP_ARROW); return; }
  if (strcmp(kc, "KC_DOWN") == 0) { Keyboard.press(KEY_DOWN_ARROW); return; }
  if (strcmp(kc, "KC_LEFT") == 0) { Keyboard.press(KEY_LEFT_ARROW); return; }
  if (strcmp(kc, "KC_RGHT") == 0) { Keyboard.press(KEY_RIGHT_ARROW); return; }
  // Modifiers: use left/right specific constants per Arduino Keyboard API
  if (strcmp(kc, "KC_LSFT") == 0) { Keyboard.press(KEY_LEFT_SHIFT); return; }
  if (strcmp(kc, "KC_RSFT") == 0) { Keyboard.press(KEY_RIGHT_SHIFT); return; }
  if (strcmp(kc, "KC_LCTL") == 0) { Keyboard.press(KEY_LEFT_CTRL); return; }
  if (strcmp(kc, "KC_RCTL") == 0) { Keyboard.press(KEY_RIGHT_CTRL); return; }
  if (strcmp(kc, "KC_LALT") == 0) { Keyboard.press(KEY_LEFT_ALT); return; }
  if (strcmp(kc, "KC_RALT") == 0) { Keyboard.press(KEY_RIGHT_ALT); return; }
  if (strcmp(kc, "KC_LGUI") == 0) { Keyboard.press(KEY_LEFT_GUI); return; }
  if (strcmp(kc, "KC_RGUI") == 0) { Keyboard.press(KEY_RIGHT_GUI); return; }
  if (strcmp(kc, "KC_APP") == 0) { Keyboard.press(KEY_MENU); return; }

  // fallback: if KC_xxx unknown, ignore
}

void send_key_up(const char *kc) {
  if (strcmp(kc, "KC_NO") == 0) return;

  if (strcmp(kc, "KC_SPC") == 0 || strcmp(kc, "KC_SPACE") == 0) { Keyboard.release(' '); return; }
  if (kc[3] >= 'A' && kc[3] <= 'Z' && strlen(kc) == 4) { char c = kc[3] - 'A' + 'a'; Keyboard.release(c); return; }
  if (strcmp(kc, "KC_1") == 0) { Keyboard.release('1'); return; }
  if (strcmp(kc, "KC_2") == 0) { Keyboard.release('2'); return; }
  if (strcmp(kc, "KC_3") == 0) { Keyboard.release('3'); return; }
  if (strcmp(kc, "KC_4") == 0) { Keyboard.release('4'); return; }
  if (strcmp(kc, "KC_5") == 0) { Keyboard.release('5'); return; }
  if (strcmp(kc, "KC_6") == 0) { Keyboard.release('6'); return; }
  if (strcmp(kc, "KC_7") == 0) { Keyboard.release('7'); return; }
  if (strcmp(kc, "KC_8") == 0) { Keyboard.release('8'); return; }
  if (strcmp(kc, "KC_9") == 0) { Keyboard.release('9'); return; }
  if (strcmp(kc, "KC_0") == 0) { Keyboard.release('0'); return; }
  if (strcmp(kc, "KC_MINS") == 0) { Keyboard.release('-'); return; }
  if (strcmp(kc, "KC_EQL") == 0) { Keyboard.release('='); return; }
  if (strcmp(kc, "KC_BSPC") == 0) { Keyboard.release(KEY_BACKSPACE); return; }
  if (strcmp(kc, "KC_ENT") == 0) { Keyboard.release(10); return; }
  if (strcmp(kc, "KC_TAB") == 0) { Keyboard.release('\t'); return; }
  if (strcmp(kc, "KC_ESC") == 0) { Keyboard.release(KEY_ESC); return; }
  if (strcmp(kc, "KC_DEL") == 0) { Keyboard.release(KEY_DELETE); return; }
  if (strcmp(kc, "KC_COMM") == 0) { Keyboard.release(','); return; }
  if (strcmp(kc, "KC_DOT") == 0) { Keyboard.release('.'); return; }
  if (strcmp(kc, "KC_SLSH") == 0) { Keyboard.release('/'); return; }
  if (strcmp(kc, "KC_SCLN") == 0) { Keyboard.release(';'); return; }
  if (strcmp(kc, "KC_QUOT") == 0) { Keyboard.release('\''); return; }
  if (strcmp(kc, "KC_LBRC") == 0) { Keyboard.release('['); return; }
  if (strcmp(kc, "KC_RBRC") == 0) { Keyboard.release(']'); return; }
  if (strcmp(kc, "KC_BSLS") == 0) { Keyboard.release('\\'); return; }
  if (strcmp(kc, "KC_GRV") == 0) { Keyboard.release('`'); return; }

  if (strcmp(kc, "KC_UP") == 0) { Keyboard.release(KEY_UP_ARROW); return; }
  if (strcmp(kc, "KC_DOWN") == 0) { Keyboard.release(KEY_DOWN_ARROW); return; }
  if (strcmp(kc, "KC_LEFT") == 0) { Keyboard.release(KEY_LEFT_ARROW); return; }
  if (strcmp(kc, "KC_RGHT") == 0) { Keyboard.release(KEY_RIGHT_ARROW); return; }
  // Modifiers: release left/right specific constants per Arduino Keyboard API
  if (strcmp(kc, "KC_LSFT") == 0) { Keyboard.release(KEY_LEFT_SHIFT); return; }
  if (strcmp(kc, "KC_RSFT") == 0) { Keyboard.release(KEY_RIGHT_SHIFT); return; }
  if (strcmp(kc, "KC_LCTL") == 0) { Keyboard.release(KEY_LEFT_CTRL); return; }
  if (strcmp(kc, "KC_RCTL") == 0) { Keyboard.release(KEY_RIGHT_CTRL); return; }
  if (strcmp(kc, "KC_LALT") == 0) { Keyboard.release(KEY_LEFT_ALT); return; }
  if (strcmp(kc, "KC_RALT") == 0) { Keyboard.release(KEY_RIGHT_ALT); return; }
  if (strcmp(kc, "KC_LGUI") == 0) { Keyboard.release(KEY_LEFT_GUI); return; }
  if (strcmp(kc, "KC_RGUI") == 0) { Keyboard.release(KEY_RIGHT_GUI); return; }
  if (strcmp(kc, "KC_APP") == 0) { Keyboard.release(KEY_LEFT_GUI); return; }
}

void setup() {
  Wire.begin();
  if (DEBUG_SERIAL) Serial.begin(115200);
  setup_pins();

  // init prev matrix to released (all ones) and zero debounce timestamps
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) prev_matrix[r] = 0xFFFF;
  memset(last_change_ts, 0, sizeof(last_change_ts));
  // init reported_pressed to false
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) for (uint8_t c = 0; c < MATRIX_COLS; c++) {
    reported_pressed[r][c] = false;
    reported_ts[r][c] = 0;
  }

  Keyboard.begin();
  // ensure no stuck HID state at startup
  Keyboard.releaseAll();

  // If debug enabled, run a quick diagnostic of row/col pins and PCF
  if (DEBUG_SERIAL) {
    delay(50);
    Serial.println("=== RUNNING PIN DIAGNOSTIC ===");
    // print configured pins
    Serial.print("ROW_PINS: ");
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
      Serial.print(ROW_PINS[r]); Serial.print(' ');
    }
    Serial.println();
    Serial.print("COL_PINS_MCU: ");
    for (uint8_t i = 0; i < 7; i++) { Serial.print(COL_PINS_MCU[i]); Serial.print(' '); }
    Serial.println();

    // test each row: drive then read back states and PCF
    for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
      Serial.print("Testing row "); Serial.println(r);
      select_row(r);
      delay(20);
      Serial.print("  ROW reads: ");
      for (uint8_t rr = 0; rr < MATRIX_ROWS; rr++) {
        Serial.print(digitalRead(ROW_PINS[rr])); Serial.print(' ');
      }
      Serial.println();
      uint8_t p = pcf_read();
      Serial.print("  PCF read: 0x"); Serial.println(p, HEX);
      Serial.print("  MCU cols: ");
      for (uint8_t i = 0; i < 7; i++) { Serial.print(digitalRead(COL_PINS_MCU[i])); Serial.print(' '); }
      Serial.println();
      release_row(r);
      delay(20);
    }
    Serial.println("=== DIAGNOSTIC COMPLETE ===");
  }
}

void loop() {
  static uint32_t last_scan = 0;
  uint32_t now = millis();
  if (now - last_scan < SCAN_INTERVAL_MS) return;
  last_scan = now;

  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    select_row(r);
    delayMicroseconds(30);
    // cache PCF reading for this row (only if expander present)
    pcf_row_data = pcf_read();
    // consider PCF read successful if we get something different than the default
    // 0xFF only isn't a certain indicator (could be all released), but we can
    // probe availability by attempting the read above; if Wire had no data pcf_read
    // still returns 0xFF. We'll use a quick availability check: attempt a second
    // small read by checking Wire.available is not directly possible here, so
    // instead infer failure when the expander doesn't ACK the transmission —
    // unfortunately Wire doesn't expose ACK here. As a pragmatic step we'll
    // treat returned 0xFF as potentially valid, but if the user reports PCF not
    // responding we'll explicitly check and force releases below.
    pcf_row_data_valid = true;

    // If PCF read failed, increment failure counter; only after reaching
    // PCF_FAIL_THRESHOLD we force-release any previously reported presses on
    // PCF columns to avoid accidental releases when the bus has a transient
    // hiccup. If a read succeeds we reset the failure counter.
    if (!pcf_ok) {
      pcf_fail_count++;
      if (pcf_fail_count >= PCF_FAIL_THRESHOLD) {
        for (uint8_t c = 0; c < 8; c++) {
          if (reported_pressed[r][c]) {
            const char *kc = keymap_get(r, c);
            if (DEBUG_SERIAL) {
              Serial.print("Forced release (PCF down) R"); Serial.print(r);
              Serial.print("C"); Serial.print(c);
              Serial.print(" "); Serial.println(kc);
            }
            send_key_up(kc);
            reported_pressed[r][c] = false;
          }
        }
      }
    } else {
      pcf_fail_count = 0;
    }

  // 16-bit mask to hold up to 15 columns
  uint16_t rowbits = 0;
    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
      bool released = read_matrix_row_bit(r, c);
      bool pressed = !released;
      if (pressed) rowbits |= (1 << c);
    }

  uint16_t changed = rowbits ^ prev_matrix[r];
    if (changed) {
      if (DEBUG_SERIAL) {
        Serial.print("ROW "); Serial.print(r);
        Serial.print(" PCF=0x"); Serial.print(pcf_row_data, HEX);
        Serial.print(" ROWBITS=0x"); Serial.print(rowbits, HEX);
        Serial.print(" PREV=0x"); Serial.print(prev_matrix[r], HEX);
        Serial.print(" CHG=0x"); Serial.println(changed, HEX);
        // print MCU pin raw reads for columns 8..14
        Serial.print("MCU cols:");
        for (uint8_t i = 0; i < 7; i++) {
          Serial.print(' ');
          Serial.print(digitalRead(COL_PINS_MCU[i]));
        }
        Serial.println();
      }
      for (uint8_t c = 0; c < MATRIX_COLS; c++) {
        if (changed & (1 << c)) {
          // debounce simple: require stable for DEBOUNCE_MS
          uint32_t &ts = last_change_ts[r][c];
          if (ts == 0) { ts = now; }
          if (now - ts < DEBOUNCE_MS) continue; // wait
          ts = 0;

          bool is_pressed = (rowbits & (1 << c));
          const char *kc = keymap_get(r, c);
          // Use reported_pressed to avoid duplicate HID events and ensure we only
          // send transitions we haven't already reported.
          if (is_pressed && !reported_pressed[r][c]) {
            // newly pressed
            if (DEBUG_SERIAL) {
              Serial.print("R"); Serial.print(r);
              Serial.print("C"); Serial.print(c);
              Serial.print(" DOWN ");
              Serial.println(kc);
            }
            send_key_down(kc);
            reported_pressed[r][c] = true;
            reported_ts[r][c] = now;
          } else if (!is_pressed && reported_pressed[r][c]) {
            // newly released
            if (DEBUG_SERIAL) {
              Serial.print("R"); Serial.print(r);
              Serial.print("C"); Serial.print(c);
              Serial.print(" UP ");
              Serial.println(kc);
            }
            send_key_up(kc);
            reported_pressed[r][c] = false;
            reported_ts[r][c] = 0;
          }
        }
      }
      prev_matrix[r] = rowbits;
    }

    release_row(r);
    // invalidate cached PCF data after releasing the row
    pcf_row_data_valid = false;
  }
  // After scanning all rows, ensure we don't have any reported (sent) presses
  // that are no longer physically pressed. This guarantees that when no key
  // is pressed we release any stuck reported state immediately.
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
      if (reported_pressed[r][c]) {
        // if this key is not currently pressed in prev_matrix, release it
        if ((prev_matrix[r] & (1 << c)) == 0) {
          const char *kc = keymap_get(r, c);
          if (DEBUG_SERIAL) {
            Serial.print("Cleanup release R"); Serial.print(r);
            Serial.print("C"); Serial.print(c);
            Serial.print(" "); Serial.println(kc);
          }
          send_key_up(kc);
          reported_pressed[r][c] = false;
          reported_ts[r][c] = 0;
        }
      }
    }
  }
  // Also release any reported keys that have been held for too long (stuck)
  uint32_t now_ms = millis();
  for (uint8_t r = 0; r < MATRIX_ROWS; r++) {
    for (uint8_t c = 0; c < MATRIX_COLS; c++) {
      if (reported_pressed[r][c] && reported_ts[r][c] != 0) {
        if (now_ms - reported_ts[r][c] > STUCK_RELEASE_MS) {
          const char *kc = keymap_get(r, c);
          if (DEBUG_SERIAL) {
            Serial.print("Timeout release R"); Serial.print(r);
            Serial.print("C"); Serial.print(c);
            Serial.print(" "); Serial.println(kc);
          }
          send_key_up(kc);
          reported_pressed[r][c] = false;
          reported_ts[r][c] = 0;
        }
      }
    }
  }
}
