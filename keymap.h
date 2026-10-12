#pragma once

// Keymap exported from skyforce.json (5 rows x 15 cols)
// Each entry is a KC_... string. The Arduino sketch uses keymap_get(row,col) to read.

// Tada68-like ANSI QWERTY layout (5x15) — printable keys use KC_... tokens the
// firmware maps to ASCII when possible.
static const char *KEYMAP[5][15] = {
  { "KC_ESC","KC_1","KC_2","KC_3","KC_4","KC_5","KC_6","KC_7","KC_8","KC_9","KC_0","KC_MINS","KC_EQL","KC_BSPC","KC_GRV" },
  { "KC_TAB","KC_Q","KC_W","KC_E","KC_R","KC_T","KC_Y","KC_U","KC_I","KC_O","KC_P","KC_LBRC","KC_RBRC","KC_BSLS","KC_DEL" },
  { "KC_CAPS","KC_A","KC_S","KC_D","KC_F","KC_G","KC_H","KC_J","KC_K","KC_L","KC_SCLN","KC_QUOT","KC_ENT","KC_ENT","KC_PGUP" },
  { "KC_LSFT","KC_Z","KC_X","KC_C","KC_V","KC_B","KC_N","KC_M","KC_COMM","KC_DOT","KC_SLSH","KC_NO","KC_RSFT","KC_UP","KC_PGDN" },
  { "KC_LCTL","KC_LGUI","KC_LALT","KC_NO","KC_NO","KC_SPC","KC_NO","KC_NO","KC_NO","KC_RALT","KC_RCTL","KC_NO","KC_LEFT","KC_DOWN","KC_RGHT" }
};

inline const char *keymap_get(uint8_t row, uint8_t col) {
  if (row >= 5 || col >= 15) return "KC_NO";
  return KEYMAP[row][col];
}
