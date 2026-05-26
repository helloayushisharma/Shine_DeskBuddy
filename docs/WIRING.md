# Wiring reference

The display ships with documentation written for Raspberry Pi. This table translates the pin mapping to the ESP32 V2 Feather's named breakout labels.

## Pin map: ILI9486 LCD on ESP32 V2 Feather

| LCD Pin | LCD Function | ESP32 V2 Pin |
|---|---|---|
| 19 | MOSI (SPI data out) | MO |
| 21 | MISO (SPI data in) | MI |
| 23 | SCK (SPI clock) | SCK |
| 24 | LCD_CS (display chip select) | 15 |
| 18 | DC (data / command select) | 27 |
| 22 | RST (display reset) | 33 |
| 26 | TP_CS (touch chip select) | 14 |
| 1, 17 | Logic power | 3V |
| 6 | Ground | GND |
| Backlight | LED+ (5V rail) | USB / VBUS |

## Servo

| Servo wire | ESP32 V2 Pin |
|---|---|
| Signal (orange/yellow) | GPIO 32 |
| Power (red) | USB / VBUS (5V) |
| Ground (brown/black) | GND |

## Two non-obvious things to know

**The backlight runs on 5V.** Logic and touch run on 3.3V. The backlight LED runs on 5V and pulls more current than the 3.3V regulator on the ESP32 can supply. Route the backlight to USB VBUS, not the 3V pin, or the screen will stay dark while everything else works.

**LCD_CS and TP_CS are different chip selects.** The display and the touch controller share MOSI, MISO, and SCK, but listen on different chip-select lines. Mixing them up gives you a working display with no touch input, or working touch with a blank display.
