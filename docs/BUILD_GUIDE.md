# Building Shi+ne

Three flows: getting the electronics to work, printing the body, and bringing the two together. Allow a few hours for the first, several hours of print time for the second, and roughly an evening for the third.

## What is Shi+ne?

Shi+ne is a small connected object that lives on your desk and reads your Notion to-do list. When you complete a task, it lifts a sunflower from its head, animates a celebration on its LCD face, and returns to a soft idle state. Tap it once and it acknowledges the touch with a tickled expression. Tap it five times and it gets visibly annoyed.

There are other features. You can play Flappy Bird on it. You can watch it react to tasks being added or unchecked in Notion.

---

## Part 1 · Technical flow

The goal of this part is a fully working Shi+ne on a breadboard, connected to your Notion to-do list, before you commit to any soldering or 3D printing.

### Step 1 · Gather the parts

```
1× Adafruit ESP32 V2 Feather
1× 3.5" ILI9486 SPI touch LCD (480×320, resistive touch)
1× Smraza SG90 (or equivalent) micro servo
1× USB-C cable and 5V wall adapter
1× breadboard, jumper wires, multimeter
1× soldering iron, solder, perfboard (for the final assembly)
1× M2 fastener set
3D printing services or equivalent
PLA filament (white and dark green recommended)
```

### Step 2 · Set up the development environment

Install the Arduino IDE.

Add the ESP32 board package via Boards Manager. Install these libraries through the Library Manager:

```
TFT_eSPI      — display driver
ESP32Servo    — non-blocking servo control
ArduinoJson   — Notion API response parsing
HTTPClient    — comes with the ESP32 package
```

Open `TFT_eSPI/User_Setup.h` in your Arduino libraries folder and configure it for the ILI9486 board. The key defines are:

```cpp
#define ILI9486_DRIVER
#define TFT_MISO  MISO
#define TFT_MOSI  MOSI
#define TFT_SCLK  SCK
#define TFT_CS    15
#define TFT_DC    27
#define TFT_RST   33
#define TOUCH_CS  14

#define SPI_FREQUENCY        27000000
#define SPI_READ_FREQUENCY   10000000
#define SPI_TOUCH_FREQUENCY  2500000
```

Keep `RPI_DISPLAY_TYPE` enabled for this panel. Do not define `TFT_WR` (the standard fast `pushBlock` path is what Shi+ne is tuned for).

### Step 3 · Wire the display on a breadboard

The display ships with documentation written for Raspberry Pi. Translating to the ESP32 V2 took most of a week the first time around. Use this table.

| LCD Pin | LCD Function | ESP32 V2 Pin |
|---|---|---|
| 19 | MOSI | MO |
| 21 | MISO | MI |
| 23 | SCK | SCK |
| 24 | LCD_CS | 15 |
| 18 | DC | 27 |
| 22 | RST | 33 |
| 26 | TP_CS | 14 |
| 1, 17 | Logic power | 3V |
| 6 | Ground | GND |
| Backlight | LED+ | USB / VBUS |

Two things you absolutely have to know up front. **The LCD has two independent power systems.** Logic and touch run on 3.3V, the backlight runs on 5V. Route the backlight to USB VBUS, not the 3V pin, or your screen will be alive and invisible. Second, **LCD_CS and TP_CS are separate chip-select lines on the same SPI bus.** Confusing them will give you a display that draws but ignores touch, or vice versa.

### Step 4 · First display test

Flash a simple color-fill sketch from the `TFT_eSPI` examples folder. If the screen is bright and showing colors, the wiring is correct. If the screen is dark, check the backlight wire is on VBUS. If the screen is bright white or random pixels, check the chip-select wires.

### Step 5 · Touch test

Run the `Touch_calibrate` example from the `TFT_eSPI` library. Touch the four corners as prompted. Note the five calibration values it prints to the Serial Monitor. You will paste them into the main firmware in Step 8. The values Shi+ne ships with are `{300, 3600, 300, 3600, 1}`, but every panel is slightly different.

### Step 6 · Wire the servo

```
Servo signal → GPIO 32
Servo power  → USB / VBUS (5V)
Servo ground → GND
```

Flash a simple servo sweep sketch to confirm motion. If the servo jitters or fails to hold position, your 3V regulator is probably trying to power it. Move the power wire to VBUS.

### Step 7 · Set up Notion

This step has more onboarding friction than the soldering. Three substeps.

**Create an integration.** Go to `notion.so/my-integrations`, click New Integration, name it Shi+ne, set it to Internal. Copy the integration token that appears. It starts with `secret_`.

**Create the to-do database.** In Notion, create a new database with two properties: a Title property called `Name`, and a Checkbox property called `Done`. Add a few test tasks.

**Share the database with the integration.** Open the database, click the three dots in the top right, choose Connections, and add the Shi+ne integration you just made. Without this step the API will return an empty result and you will lose an evening debugging the wrong thing.

**Find your database ID.** Open the database in a browser. The URL looks like `notion.so/yourname/336ea5e0a8d04...?v=...`. The 32-character string between the last slash and the question mark is your database ID.

### Step 8 · Configure the firmware

Open `firmware/shine/shine.ino`. Fill in the four constants at the top:

```cpp
const char* WIFI_SSID     = "your network name";
const char* WIFI_PASSWORD = "your network password";
const char* NOTION_TOKEN  = "secret_...";
const char* DATABASE_ID   = "336ea5e0...";
```

Paste your touch calibration values into the `setup()` function:

```cpp
uint16_t calData[5] = {300, 3600, 300, 3600, 1};  // your values from Step 5
```

_Do not commit these credentials to GitHub._ The repo's `.gitignore` already excludes the common local-config patterns, but if you change the structure, double-check before pushing.

### Step 9 · Flash and verify end to end

Connect the ESP32 over USB-C, select the board and port, and click Upload. Watch the Serial Monitor at 115200 baud during boot. You should see, in order:

1. The display boot animation (eyes opening, looking around, settling)
2. WiFi connection within ten seconds
3. A Notion poll log line showing the count of tasks fetched
4. The idle face with the task counter at the bottom

Mark a task complete in Notion. Within three seconds Shi+ne should run the celebration sequence and raise the flower. Tap the screen once for the tickled face, five times rapidly for the agitated face.

If any of this fails, the Serial Monitor will tell you which step broke. WiFi errors, Notion 404s, and JSON parse failures are all logged clearly.

### Step 10 · Solder to perfboard

Once everything works on the breadboard, transfer the wiring to perfboard with permanent soldered joints. Fourteen connections in total. Check each one with a multimeter on continuity and voltage modes. Two of the joints on our first build were cold solder joints and only revealed themselves when the display froze ninety seconds into a session, so do this carefully.

---

## Part 2 · Form flow

The goal of this part is a clean printed body, ready to receive the electronics.

### Step 1 · The CAD files

The `cad/` directory of the repo contains print-ready STL files:

```
cad/shine_body.stl       Main body shell
cad/front_1.stl          Front face panel
cad/front_curve.stl      Front curve detail
cad/back_1.stl           Back panel
cad/back_curve.stl       Back curve detail
cad/legs.stl             Base legs
cad/servo_topper.stl     Servo mount and flower-stem holder
```

If you want to modify dimensions, the STLs can be imported into Fusion 360, Blender, or any mesh editor. The Fusion 360 source files are not included in this release.

### Step 2 · Understand the four-part structure

The body is split into multiple printable parts that bolt together:

```
1. Main body         holds the LCD and the internal electronics
2. Front / back panels   close the cavity around the screen
3. Legs              the base with the feet
4. Servo topper      the arm that rises through the slot for the flower
```

The split exists because earlier single-piece prints failed at an internal corner where the LCD bezel met the body. PLA does not cooperate with sharp internal edges in long unsupported spans. Splitting the body into parts let each surface print in its preferred orientation. Do not try to print this as one piece. The split is the design.

### Step 3 · Slicer settings

These are the settings the final prints used. Adjust for your printer.

```
Material        PLA (white for body, dark green for base)
Layer height    0.16 mm
Infill          20%
Wall thickness  3 perimeters (1.2 mm)
Print speed     50 mm/s
Top/bottom      6 layers
Supports        Tree, only where the slicer flags overhangs over 50°
Bed adhesion    Brim, 5 mm
```

Print the body parts in white. Print the legs in dark green. Print the servo topper in white.

### Step 4 · Print order and orientation

Print in this order so you can dry-fit as you go:

1. **Legs first.** Simplest geometry. If they print clean, your slicer settings are dialed in.
2. **Back and front panels next.** Lets you check fit between panels before printing the more time-consuming parts.
3. **Main body third.** This is the longest print. Orient with the LCD bezel facing up.
4. **Servo topper last.** Short print. Print two in case the first servo attachment fails.

### Step 5 · Post-process

Remove supports gently. The LCD bezel area is the most fragile, work slowly there. Sand any visible seams with 400-grit paper. Dry-fit the LCD into the body bezel before assembly. The fit should be snug. If it is loose, add a small strip of double-sided foam tape to the inside lip. If it is tight, sand the inside of the bezel by a few tenths of a millimeter.

### Step 6 · The flower

The sunflower itself is a separate print, or store-bought, your call. If you don't have yellow filament, print in white and paint with yellow. Glue the petals and center together with super glue. Glue the assembled flower to the top of the servo topper.

### Step 7 · Test the LCD bezel fit before going further

Before any electronics go in, slot the unwired LCD into the body bezel. It should sit flush, with the screen facing forward. If anything is off, fix the print or the model before continuing. Once the wiring is in, going back is painful.

---

## Part 3 · Final assembly flow

The goal of this part is a working Shi+ne on your desk.

### Step 1 · Mount the LCD

Slot the LCD into the body bezel from inside. Secure with the small clip features the model includes, or with two dabs of hot glue if the friction fit needs help. The screen face should sit flush with the outer surface of the bezel.

### Step 2 · Route the wiring

The 14-wire ribbon between the LCD and the ESP32 should pass through the channel on the inside of the body, down into the lower cavity. Leave roughly 3 cm of slack so you can lift the upper body without disconnecting anything.

### Step 3 · Mount the ESP32

The ESP32 sits inside the body cavity on the small mounting posts. Secure with M2 screws. The USB-C port should align with the cutout on the back of the body so you can plug in power without disassembling anything.

### Step 4 · Mount the servo and attach the flower stem

The servo sits in the dedicated bay inside the body, with its output shaft pointing up through the flower-stem slot. Secure with two M2 screws. Slide the servo topper onto the servo's output shaft. The topper includes a friction-fit socket sized for the SG90 spline.

### Step 5 · Servo calibration

Power on the device. The flower should sit at the down position (flush with or just below the top surface of the body). If it sits too high or too low, adjust the `FLAG_DOWN` constant in the firmware. If the flower at full rise extends too far above the body, lower `FLAG_UP`. Defaults are 0 and 90 degrees respectively.

The first build broke a flower stem at this step because the servo slammed against the mechanical stop at full speed. The firmware now uses a critically-damped spring simulation on the descent, but it is still worth dry-running the full motion with the flower detached the first time, then attaching once you confirm the angles are right.

### Step 6 · Assemble the panels

Bolt the front and back panels to the main body. Slot the assembled body into the legs base. The base holds the body by friction, no fasteners needed.

### Step 7 · Final boot test on the desk

Plug Shi+ne in. Watch the boot animation. Confirm:

1. Eyes open, look around, settle
2. WiFi connects within ten seconds
3. The task counter shows your current Notion state
4. Idle blink fires every four to seven seconds
5. A single tap on the screen produces the tickled face
6. Marking a task complete in Notion raises the flower within three seconds
7. The flower descends smoothly without slamming

### Step 8 · Hold the screen for five seconds

Five seconds of held touch on the Shi+ne face opens the Win10-style tile launcher. From there, switch into Flappy Bird. Hold for five seconds in either mode to return.

### Step 9 · Put it on your desk

If you got this far, your Shi+ne is real. Place it somewhere you will see it during work. Mark a task complete. Watch the flower rise. The first time it works on your actual desk, in front of your actual to-do list, is the moment the project becomes the thing you designed.

---

## What you have at the end

A working desk companion that watches your Notion database, raises a flower when you finish something, and pretends to be ticklish if you tap its screen. The full source, STL files, and this guide are open. Fork it, modify it, change the personality, swap the flower for something else, port the integration to a different service.

If you build one, open an issue on this repo with a photo. We would love to see it on your desk.
