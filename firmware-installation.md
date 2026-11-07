# Firmware and book installation

## Requirements

- Arduino IDE 2.x.
- Arduino-Pico board package by Earle F. Philhower, III.
- Raspberry Pi Pico (RP2040) selected as the board; select the matching port.
- U8g2 library installed through Library Manager.
- `Firmware/RSVP_v13/RSVP_v13.ino`.

The firmware uses Arduino `Wire`, `LittleFS`, and U8g2. Install the Arduino-Pico core following its official instructions: https://arduino-pico.readthedocs.io/en/latest/install.html

## Upload the sketch

1. Open `RSVP_v13.ino` in Arduino IDE. If the IDE requires a matching folder name, put the sketch in a folder named `RSVP_v13`.
2. Select the Raspberry Pi Pico board and its connected port.
3. Confirm the selected flash/filesystem layout suits the firmware and leaves enough space for your books.
4. Compile, then upload the sketch over USB.

## Add books with LittleFS

1. Create a `data` directory beside the sketch file.
2. Put plain-text `.txt` files directly in `data` (avoid nested folders). Use books you have rights to store.
3. Install the Arduino IDE 2.x LittleFS uploader from https://github.com/earlephilhower/arduino-littlefs-upload following its current instructions.
4. Close Serial Monitor, select the correct Pico board/port, then run the uploader command from the IDE command palette.
5. Reboot the device and confirm the book appears in the list.

**Important:** A filesystem upload writes a filesystem image. It may replace existing books and the device's `/.system` progress/configuration files. Back up source books and expect saved reading positions to reset. The Arduino-Pico documentation explains filesystem layout and uploader behavior: https://arduino-pico.readthedocs.io/en/latest/fs.html

## Troubleshooting

- **Compile error for U8g2:** Install the U8g2 library and restart the IDE.
- **OLED blank:** Check module voltage, SDA/SCL mapping, connector order, and address; the supplied sketch relies on default `Wire` pins.
- **No books listed:** Confirm `.txt` files are directly in the `data` folder and re-upload the filesystem image.
- **Progress reset:** A LittleFS image upload can replace saved system files; restore books and resume from the start.
