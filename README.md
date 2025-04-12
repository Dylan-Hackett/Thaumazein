# Plaits

## Author

Emilie Gillet, port by Ewan Hemingway, adapted for Daisy Seed by [Your Name]

## Description

Port of Mutable Instruments Plaits to Daisy Seed.

## Controls

| Pin Function | Daisy Pin | Hardware Connection | Comment |
| --- | --- | --- | --- |
| Pitch Knob | Pin 15 | Potentiometer | Coarse Tuning |
| Harmonics Knob | Pin 16 | Potentiometer | Adjusts harmonics parameter |
| Timbre Knob (Engine Selection) | Pin 17 | Potentiometer | Selects Plaits synthesis engine |
| Decay Knob | Pin 18 | Potentiometer | Adjusts ADSR Decay time |
| V/OCT Input | Pin 19 | CV Input | 1V/Octave pitch CV |
| Harmonics CV | Pin 20 | CV Input | Harmonics CV modulation |
| Morph CV | Pin 21 | CV Input | Morph CV modulation |
| Trigger Input | Pin 25 | Gate/Trigger Input | Triggers envelope/excitation |
| Model Button | Pin 27 | Button | Cycles through engine models |
| Trigger Mode Toggle | Pin 28 | Switch | Active state enables trigger input |
| Audio Out L | Audio Out L | Output Jack | Main output |
| Audio Out R | Audio Out R | Output Jack | Aux output |

## Hardware Setup

This port requires the following hardware:
- Daisy Seed
- **Adafruit MPR121 Capacitive Touch Breakout:** Connected to Daisy Seed's I2C1 pins (Pin 11/PB8=SCL, Pin 12/PB9=SDA) and 3.3V/GND.
- 4 potentiometers connected to pins 15-18 for parameter control.
- 3 CV inputs connected to pins 19-21 for modulation.
- 1 gate input connected to pin 25 for external triggering.
- 2 buttons/switches connected to pins 27-28 for model/mode switching.
- Audio outputs connected to Daisy Seed's audio outputs.

**Touch Functionality:**
- The 12 touch pads on the MPR121 act as a keyboard, playing notes from C4 upwards (see `kTouchMidiNotes` in `Plaits.cpp` for the exact mapping).
- Touching a pad overrides the V/OCT CV input for pitch control.
- The highest numbered pad touched takes priority.
- Touching any pad generates a trigger signal for the Plaits engine.

The onboard LED blinks based on the selected engine and flashes differently when a touch pad is active.

### Building

1. Ensure you have the Daisy toolchain installed
2. Build both libraries: 
   ```
   cd lib/DaisySP && make
   cd ../libdaisy && make
   cd ../..
   ```
3. Build the project:
   ```
   make
   ```
4. Flash to your Daisy Seed using:
   ```
   make program-dfu
   ```

### License

This program is free software: you can redistribute it and/or modify it under the terms of the [GNU General Public License](https://www.gnu.org/licenses/gpl-3.0.en.html) as published by the [Free Software Foundation](https://www.fsf.org/), either version 3 of the License, or (at your option) any later version.

**Dependencies** included in the `eurorack` submodule (by Emilie Gillet) are licensed as follows:

```
Copyright 2014-2019 Emilie Gillet.

Author: Emilie Gillet (emilie.o.gillet@gmail.com)

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

See http://creativecommons.org/licenses/MIT/ for more information.
```

