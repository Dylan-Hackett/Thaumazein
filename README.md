# ARCHEIN Prototype
\

## Description  
This project is a custom audio synthesizer for the **Daisy Seed** platform that utilizes physical modeling synthesis. It features touch-sensitive key input with support for note arpeggiation. The synthesizer uses a string voice model for sound generation, and an **arpeggiator** is activated when multiple keys are pressed.

## Features  
- **Touch-sensitive keys**: Up to 8 touch-sensitive keys for note input.  
- **Arabic scale**: Fixed scale based on predefined Arabic frequencies.  
- **Knob-controlled parameters**:  
  - Knob 1: Frequency scaling  
  - Knob 2: Brightness (timbre control)  
  - Knob 3: Damping (decay characteristics)  
- **Arpeggiator mode**: Activated when two or more keys are pressed, cycling through notes at an adjustable tempo.  
- **Single-note mode**: Normal playback with a single key press.  

## Code Structure  
- **`ring.cpp`**: Core implementation of the touch input and string synthesis logic.  
- **`oscbackup.cpp`**: Backup implementation with alternate parameter scaling for testing.  
- **`oscarpbackup.cpp`**: Extended version with arpeggiator integration.  
- **`arpeggiator.cpp` / `arpeggiator.h`**: Implements the arpeggiator logic, controlling note progression and timing.  
- **`randomrange.h`**: Provides a utility for generating random values within a specified range.  
- **`Osc.cpp`**: Alternate implementation focusing on modular arpeggiator testing.  

## Hardware Requirements  
- **Daisy Seed microcontroller**  
- Capacitive touch input pads (configured for 8 pads)  
- Three potentiometers (mapped to knobs 1–3)  
- LED for visual feedback  

## Build Instructions  
1. Install the **Daisy Toolchain** following the instructions from [Electro-Smith Daisy GitHub](https://github.com/electro-smith/DaisyExamples).  
2. Clone this repository or add the relevant `.cpp` and `.h` files to your project folder.  
3. Ensure paths to the **DaisySP** library are correctly configured (update include paths if necessary).  
4. Run `make clean`, `make`, and `make program` to compile and flash the firmware.  

## Usage  
- Press a **single key** to play a note at a frequency corresponding to the Arabic scale.  
- Adjust **Knob 1** for frequency scaling (octave range).  
- Adjust **Knob 2** to modify the brightness/timbre.  
- Adjust **Knob 3** to control damping (affecting the decay).  
- When **two or more keys** are pressed, the arpeggiator activates and cycles through the pressed notes based on the tempo set by **Knob 2**.  

## Dependencies  
- **DaisySP Library**: Provides the `StringVoice` physical model for sound generation.  
- **Daisy Seed Hardware Library**: Handles audio processing, ADC configuration, and hardware interaction.  

## Future Enhancements  
- Add additional scales or make the scale configurable via firmware.  
- Implement dynamic LED feedback based on tempo or key presses.  
- Explore MIDI input/output compatibility for integration with external devices.

## Acknowledgments  
Special thanks to **Shensley** for the initial oscillator project framework that inspired this synthesizer's development.  
