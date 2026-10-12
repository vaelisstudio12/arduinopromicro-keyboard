Skyforce 68 — Arduino Pro Micro + PCF8574
[https://youtu.be/DPkXlkeJkJ0](https://youtube.com/shorts/uk2whxZUJHw)
Overview
- Purpose: lightweight Arduino sketch to run a 5×15 (68-key) Skyforce/Tada68-style keyboard using an Arduino Pro Micro and a single PCF8574 I2C I/O expander.
- Hardware: Pro Micro (ATmega32U4) + PCF8574 (I2C). PCF8574 provides columns 0..7; the Pro Micro provides columns 8..14 and row drives.
- Firmware: `SkyForce-Keyboard.ino` scans the matrix and uses the Arduino `Keyboard` library to send HID events. `keymap.h` holds the key layout.
![WhatsApp Image 2025-11-03 at 00 55 08(1)](https://github.com/user-attachments/assets/769a0e33-9ae9-4c59-9e37-46051d4e1944)
![WhatsApp Image 2025-11-03 at 00 55 08](https://github.com/user-attachments/assets/99a73de5-d6f1-43c2-8b42-b9f4ce5db00d)

Quick start
1. Open `SkyForce-Keyboard/SkyForce-Keyboard.ino` in Arduino IDE (or VS Code + Arduino).
2. Edit pin arrays at the top of the sketch to match your wiring:
   - `ROW_PINS[5]` — Arduino digital pins for Rows 0..4 (example in sketch: `{16, 14, 15, A0, A1}`).
   - `COL_PINS_MCU[7]` — Arduino digital pins for Columns 8..14 (example in sketch: `{4,5,6,7,8,9,10}`).
   - `PCF_ADDR` — change only if your PCF8574 uses a different I2C address.
   - `DEBUG_SERIAL` — set true to see scan/debug output on Serial (115200).
3. Wire hardware:
   - Connect Pro Micro VCC and GND to PCF8574 VCC and GND.
   - Connect SDA/SCL between Pro Micro and PCF8574 (I2C).
   - Connect PCF8574 P0..P7 to matrix columns 7..0 respectively (firmware uses mapping P7→Col0, P6→Col1, …, P0→Col7).
   - Connect Pro Micro digital pins in `COL_PINS_MCU` to matrix columns 8..14.
   - Connect row pins in `ROW_PINS` to matrix rows 0..4.
   - Diodes: wired COLUMN → ROW (anode on column, cathode on row). The firmware assumes DIODES_COL2ROW = true.
4. Select the Pro Micro (ATmega32U4) board, correct COM port, and upload. If the sketch blocks upload (USB HID interfering), use the manual double-tap reset or ask to add an upload guard (`SKIP_KEYBOARD_ON_UPLOAD`).

Pinout (example values used in the sketch)
- ROW_PINS[] = { 16, 14, 15, A0, A1 }  // Row0..Row4
- COL_PINS_MCU[] = { 4, 5, 6, 7, 8, 9, 10 }  // Columns 8..14
- PCF8574 (I2C) pins:
  - P7 -> Column 0
  - P6 -> Column 1
  - P5 -> Column 2
  - P4 -> Column 3
  - P3 -> Column 4
  - P2 -> Column 5
  - P1 -> Column 6
  - P0 -> Column 7

Notes about the PCF8574
- The PCF8574 uses quasi-bidirectional I/O. The sketch writes 0xFF before reading and retries reads to handle bus glitches.
- If your PCF isn't responding, check wiring, address, pull-ups on SDA/SCL, and run the serial pin diagnostic (enable `DEBUG_SERIAL` and open Serial Monitor at 115200).

Behavior and keymap
- `keymap.h` contains a 5×15 KC_ layout (Tada68-like). The firmware maps `KC_*` tokens to ASCII or HID using `send_key_down()` / `send_key_up()`; printable characters are sent as ASCII and modifiers/special keys use press/release semantics.

- To change keys, edit `keymap.h` and re-upload.

Troubleshooting
- Upload hanging: ensure correct board/port, try double-tap reset on Pro Micro to enter bootloader, close serial monitor before upload, try a different USB cable or port. If uploading still fails, I can add a compile-time guard that skips `Keyboard.begin()` so the device doesn't present as an HID while uploading.
- PCF reads wrong: check SDA/SCL wiring, VCC/GND, and pull-ups. Enable `DEBUG_SERIAL` and paste diagnostic output here if you want me to analyze it.

Want me to:
- Add an upload guard (`SKIP_KEYBOARD_ON_UPLOAD`) to make uploading easier? (I can add a simple #define and conditional around USB/HID init.)
- Change the example pinout to match your exact Pro Micro pad numbering? Provide how your board labels map to Arduino digital pins and I will update the example.
- Convert the wiring diagram into a KiCad/fritzing schematic or a high-resolution PNG? Tell me which format/size you want.

License / Notes
- This project is intended as a simple test firmware — not a complete QMK replacement. Use at your own risk and adapt as needed for layers and advanced features.

Path
- Source: `SkyForce-Keyboard/SkyForce-Keyboard.ino`
- Layout: `SkyForce-Keyboard/keymap.h`

If you want any wording changed or extra details added (pin labeling per Pro Micro pad, PCB mapping, or a printable one-page diagram), tell me exactly how you want it and I'll update this README.
