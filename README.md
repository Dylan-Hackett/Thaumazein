# Thaumazein

## Description

Thaumazein is a polyphonic and percussive synthesizer for the Daisy Seed platform, based on Mutable Instruments Plaits. The project extends the original Plaits source code with:

- Polyphony (up to 4 voices) for Virtual Analog engines
- Echo delay effect with feedback, time, lag, and mix controls
- Touch-based interface using MPR121 capacitive touch sensor
- Separate attack and release envelope controls with punchy response and pulse-like release capability (down to 0.1ms)
- Capacitive touch position and pressure sensing for continuous control of synth parameters

## Project Structure

The codebase has been refactored from a single monolithic file to a more modular design:

* **Thaumazein.h**: Main header defining project constants, external variables, and function declarations
* **Thaumazein.cpp**: Main program entry point and UI handling
* **Polyphony.cpp**: Handles voice allocation, polyphony management, and voice initialization
* **Interface.cpp**: Manages hardware interface, controls, and system initialization
* **AudioProcessor.cpp**: Implements the audio processing callback and voice rendering
* **VoiceEnvelope.h/cpp**: Custom envelope generator for polyphonic voices with attack/release phases

## Codebase Architecture Diagram

```
┌───────────────────────┐
│ External Libraries    │
│                       │
│ - libDaisy            │
│ - DaisySP             │
│ - Plaits engine       │
│ - MPR121 touch        │
└────────────┬──────────┘
             │
             ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                               Thaumazein.h                                │
│                                                                         │
│ - Constants (NUM_VOICES, BLOCK_SIZE)                                    │
│ - External declarations (variables, functions)                          │
│ - Forward declarations                                                  │
└───────────┬──────────────┬────────────────┬────────────────┬────────────┘
            │              │                │                │
            ▼              ▼                ▼                ▼
┌────────────────┐ ┌──────────────┐ ┌────────────────┐ ┌──────────────────┐
│ Thaumazein.cpp │ │ Interface.cpp│ │ Polyphony.cpp  │ │AudioProcessor.cpp│
│                │ │              │ │                │ │                  │
│ - Main()       │ │ - Hardware   │ │ - Voice data   │ │ - AudioCallback()│
│ - Display      │ │   setup      │ │ - Voice alloc  │ │ - Real-time      │
│                │ │ - ADC config │ │ - MIDI mapping │ │   audio rendering│
│                │ │ - Controls   │ │ - Voice init   │ │ - Control reading│
│                │ │ - Button     │ │                │ │ - Effect params  │
│                │ │ - LED control│ │                │ │                  │
└────────────────┘ └──────────────┘ └────────────────┘ └──────────────────┘
        │                 │                                     │
        └─────────────────┼─────────────────────────────────────┘
                          │                
                          ▼                
                 ┌──────────────────┐
                 │ VoiceEnvelope.h  │
                 │ VoiceEnvelope.cpp│
                 │                  │
                 │ - Envelope       │
                 │   processing     │
                 └──────────────────┘
```

**Data Flow:**
1. **Thaumazein.cpp** contains `main()` - the entry point that initializes everything via `InitializeSynth()` and handles display updates
2. **Interface.cpp** manages hardware setup, ADC mapping, button handling, LED control, and peripherals
3. **Polyphony.cpp** handles voice allocation and initialization
4. **AudioProcessor.cpp** contains the real-time audio callback that runs continuously
5. **VoiceEnvelope.h/cpp** provides envelope processing for the audio system

## Controls

The synthesizer uses the following knobs and touch pads connected to the Daisy Seed ADC pins:

- D15 / A0 (Pin 30): Delay Time
- D16 / A1 (Pin 31): Delay Mix & Feedback
- D17 / A2 (Pin 32): Envelope Release
- D18 / A3 (Pin 33): Envelope Attack
- D19 / A4 (Pin 34): Plaits Timbre
- D20 / A5 (Pin 35): Plaits Harmonics
- D21 / A6 (Pin 36): Plaits Morph
- D22 / A7 (Pin 37): Plaits Pitch Offset
- D23 / A8 (Pin 38): Engine Select Decrement Pad (Touch)
- D24 / A9 (Pin 39): Engine Select Increment Pad (Touch)

(ADC pins A10/A11 remain currently unused and are available for future expansion.)

Additional controls:
- ADC 8 (Arpeggiator Toggle Pad), ADC 9 (Model Select Previous Pad), and ADC 10 (Model Select Next Pad): Press all three pads simultaneously to enter bootloader at any time during operation
- LED: Indicates active voices, blinks when idle
- Twelve GPIO-controlled touch LEDs on pins D14–D1, mapped pad 11→D14 … pad 0→D1; light while pads are touched
- When the arpeggiator is active, LEDs blink in time with their corresponding sequenced notes

## Engine Behavior

- Engines 0-3: Operate in poly mode with custom envelope processing (4-voice polyphony)
- Engines 4-12: Operate in mono mode with direct trigger processing (monophonic)

## Daisy Seed Pinout Reference

```
////////////// SIMPLE X DAISY PINOUT CHEATSHEET ///////////////

// 3v3           29  |       |   20    AGND
// D15 / A0      30  |       |   19    OUT 01
// D16 / A1      31  |       |   18    OUT 00
// D17 / A2      32  |       |   17    IN 01
// D18 / A3      33  |       |   16    IN 00
// D19 / A4      34  |       |   15    D14
// D20 / A5      35  |       |   14    D13
// D21 / A6      36  |       |   13    D12
// D22 / A7      37  |       |   12    D11
// D23 / A8      38  |       |   11    D10
// D24 / A9      39  |       |   10    D9
// D25 / A10     40  |       |   09    D8
// D26           41  |       |   08    D7
// D27           42  |       |   07    D6
// D28 / A11     43  |       |   06    D5
// D29           44  |       |   05    D4
// D30           45  |       |   04    D3
// 3v3 Digital   46  |       |   03    D2
// VIN           47  |       |   02    D1
// DGND          48  |       |   01    D0
```

## Hardware Setup

This project requires the following hardware:
- Daisy Seed
- **Adafruit MPR121 Capacitive Touch Breakout:** Connected to Daisy Seed's I2C1 pins (D11/Pin 12=SCL, D10/Pin 11=SDA) and 3.3V/GND.
- 11 potentiometers connected to the pins listed in the Controls section.
- Audio outputs connected to Daisy Seed's audio outputs.

**Touch Functionality:**
- The 12 touch pads on the MPR121 act as a keyboard, playing notes in an E Phrygian scale.
- Touch pads provide dual functionality: note triggering and continuous control.
- The sensor is polled in the main loop at ~200Hz to read touch state and calculate a continuous control value.
- The average capacitance deviation (pressure) across all currently touched pads is calculated, smoothed, and used to generate a control signal.
- This control signal blends (50/50) with the knob values to modulate the Morph and Delay Feedback parameters.
- The exact note mapping is defined in the `kTouchMidiNotes` array.
- Multiple pads can be touched simultaneously in poly mode (engines 0-3).

## Building

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

## Development

When adding features or making changes, follow the modular structure:
1. Add new class definitions to appropriate headers
2. Implement voice-related code in Polyphony.cpp
3. Implement hardware interface code in Interface.cpp  
4. Add audio processing code to AudioProcessor.cpp
5. Add UI elements to Thaumazein.cpp

## TO DO:

- Integrate basic Arpeggiator: use ADC8 to toggle on/off, map held touch pads to notes, tempo mapped to ADC0, triggers Plaits voices.
- Implement an Arpeggiator preset mode, which stores each parameter per step, including the engine parameter and then cycles through them rhytmically. Monophonic implementation to save CPU

- Improve onboard effects with either reverb or more full delay network. Possibly port code from clouds instead of current delay/reverb

- Improve and potentially redesign how the touch control for each keyboard key will work. Currently its one parameter for the whole keyboard being modulated by all keys being touched. Will definitely change which parameter is being modulated depending on engine. Potentially will change to modulate multiple params with different keys/parts of the keyboard as well.

- Some "magic numbers" (sensitivity = 150.0f, smoothing bounds) could be pulled into named constants or config

- Unit/integration tests are largely absent—consider adding automated tests for the core DSP and polyphony logic

- Build system could automate the multi‑library build steps via a top‑level Makefile or CMake wrapper

- Scale Mode chosen with the button combo and keyboard. pick 12 scales.

- Improve Attack/Release response (pulse-like release implemented)
- - Debounce engine prev/next pad buttons to prevent multiple jumps (implemented)

- Finetune Touch Sensitivty 

- Implement a random assignment of Voice assignments for polymode

## License

This project contains code adapted from Mutable Instruments Plaits, which is released under the MIT License.

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

