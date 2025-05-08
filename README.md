# Thaumazein Synthesizer Firmware

## License

This project contains code adapted from Mutable Instruments Plaits, which is released under the MIT License.

```
Copyright 2014-2019 Emilie Gillet.
```

## Project Description

Thaumazein is a Daisy Seed based polyphonic synthesizer with a capacitive touch interface, arpeggiator, and delay effect. This firmware aims to provide a flexible and expressive musical instrument.

## Project Overview

The project is structured around the Daisy Seed platform, utilizing its audio processing capabilities and hardware peripherals. Key components include:

*   **Audio Engine (`AudioProcessor.cpp`, `Polyphony.cpp`, `Plaits/`, `stmlib/`)**: Handles voice generation (based on Mutable Instruments Plaits), polyphony, and envelope processing. The `PolyphonyEngine` class encapsulates voice management.
*   **Effects (`DelayEffect.cpp`, `DelayEffect.h`)**: Implements a tempo-syncable stereo delay. Its parameters (like feedback and time) are configured by reading global knob values and updating the delay unit's settings.
*   **User Interface (`Interface.cpp`, `Interface.h`, `Mpr121/`)**: Manages hardware controls (knobs, button), MPR121 capacitive touch sensor input, and LED feedback.
*   **Arpeggiator (`Arpeggiator.cpp`, `Arpeggiator.h`)**: Provides arpeggiation functionality with various modes and tempo control.
*   **Main Application (`Thaumazein.cpp`, `Thaumazein.h`)**: Initializes all modules, manages the main loop, and handles inter-module communication (e.g., display updates, touch data).
*   **Utilities (`VoiceEnvelope.h`, `CpuLoadMeter.h`)**: Helper classes for tasks like ADSR envelopes and CPU load monitoring.

## Tasks

### Current Focus
*   **Code Hygiene & Readability**: Ongoing efforts to refine comments, remove dead code, and improve overall code clarity across the project.

### Recently Completed
*   **THM-108: Add Knob Control for Arpeggiator Tempo** - *Completed*
    *   Mapped Arpeggiator tempo control to ADC 0 (`delay_time_knob`).
    *   Calls `arp.SetMainTempoFromKnob()` in `AudioProcessor.cpp` based on the knob value.
    *   Removed fixed 8 Hz tempo setting from initialization.
    *   Tempo now ranges exponentially from 1 Hz to 30 Hz.
*   **THM-107: Fix Arpeggiator Active LED Indication** - *Completed*
    *   Restored the update of `arp_led_timestamps` within the arpeggiator's note trigger callback in `Interface.cpp`.
    *   This ensures that LEDs blink correctly when the arpeggiator is active, reflecting sequenced notes.
*   **THM-106: Fix Arpeggiator Call and Initialization** - *Completed*
    *   Corrected `Arpeggiator::Process` call in `AudioProcessor.cpp` to use `Arpeggiator::UpdateHeldNotes` first and then call `Process` with only the frame count.
    *   Ensured `Arpeggiator::SetNoteTriggerCallback` is called in `Interface.cpp` after `arp.Init()` in `UpdateArpeggiatorToggle` to correctly link arpeggiator triggers to `PolyphonyEngine`.
*   **THM‑105: Fix Mono Percussive Trigger (ARP OFF)** - *Completed (Needs HW Verification)*
    *   Moved trigger reset logic from `PrepareVoiceParametersInternal` to the start of `UpdateMonoNonArpVoiceTriggerInternal`.
    *   Ensures trigger stays high for the full `Render` block.
*   **Comment Cleanup (Various Files)**: Removed redundant and overly verbose comments from `AudioProcessor.cpp`, `Interface.cpp`, `Polyphony.h`, and `DelayEffect.h` to improve readability.
*   **THM‑104: Resolve Linker Errors in PolyphonyEngine** - *Completed*
    *   Added missing definitions for `ClearAllVoicesForEngineSwitch`, `TransferPolyToMonoVoice`, `UpdateLastTouchState`, `GetLastTouchState` in `Polyphony.cpp`.
    *   Added missing definitions for `FindAvailableVoiceInternal`, `AssignMonoNoteInternal`, `FindVoiceForNote` in `Polyphony.cpp`.
    *   Corrected `ClearAllVoicesForEngineSwitch` to not call non-existent `plaits::Voice::Silence()`.
    *   Added missing definitions for `TriggerArpCallbackVoice`, `IsAnyVoiceActive`, `IsVoiceActive` in `Polyphony.cpp`.
*   **Refactor voice management into `PolyphonyEngine` class.**
*   **Refactor delay processing into `DelayEffect` namespace/module.**
*   **Initial port of Plaits code.**
*   **Basic UI and control handling.**

### Backlog / Future Enhancements
*   **Verification & Testing:**
    *   Verify ARP playback (Forward, Random, AsPlayed) on hardware after THM-103 changes.
    *   Verify Mono Percussive Trigger fix (THM-105) on hardware.
    *   Unit/integration tests are largely absent—consider adding automated tests for the core DSP and polyphony logic.
*   **Bug Fixing & Stability:**
    *   Investigate and resolve `dfu-util` errors (Error 74: `get_status`, Error 74: `No DFU capable USB device available`) during flashing.
*   **Feature Development:**
    *   Implement voice stealing in `PolyphonyEngine`.
    *   Implement a random assignment of Voice assignments for poly mode (currently sequential).
    *   Expand display information.
    *   Refine touch sensitivity and CV generation.
    *   Explore additional synthesis engines or parameters.
    *   Consider increasing max Arpeggiator tempo (currently 30 Hz via `SetMainTempoFromKnob`).
    *   Add more effects (e.g., reverb, chorus). Possibly port code from Clouds instead of current delay/reverb.
    *   Improve and potentially redesign how the touch control for each keyboard key will work. Currently its one parameter for the whole keyboard being modulated by all keys being touched. Will definitely change which parameter is being modulated depending on engine. Potentially will change to modulate multiple params with different keys/parts of the keyboard as well.
    *   Implement Scale Mode chosen with a button combo and keyboard (select from ~12 scales).
*   **Optimization:**
    *   Further investigate and optimize CPU usage.
    *   Investigate FLASH memory usage (currently high) and explore optimization techniques if necessary (e.g., compiler flags, code size reduction).
*   **Code Quality & Refinements:**
    *   Some "magic numbers" (e.g., `sensitivity = 150.0f`, smoothing bounds in `Thaumazein.cpp::PollTouchSensor`) could be pulled into named constants or config.
    *   Build system could automate the multi‑library build steps via a top‑level Makefile or CMake wrapper.
    *   Fine-tune Touch Sensitivity.

## Codebase Diagram

```mermaid
graph TD
    subgraph MainLoop [Thaumazein.cpp]
        direction LR
        ML_Init[InitializeSynth] --> ML_Loop
        ML_Loop --> ML_PollTouch[PollTouchSensor]
        ML_Loop --> ML_UpdateDisplay[UpdateDisplay]
        ML_Loop --> ML_UpdateLED[UpdateLED]
        ML_Loop --> ML_Bootload[Bootload Check]
    end

    subgraph AudioProcessing [AudioCallback in AudioProcessor.cpp]
        direction LR
        AC_Start[Audio In/Out] --> AC_ProcessUI[ProcessUIAndControls]
        AC_ProcessUI --> AC_UpdateArp[UpdateArpState]
        AC_UpdateArp --> AC_RenderVoices[RenderVoices]
        AC_RenderVoices --> AC_ApplyEffects[ApplyEffectsAndOutput]
        AC_ApplyEffects --> AC_UpdateMonitors[UpdatePerformanceMonitors]
    end

    subgraph UI [Interface.cpp]
        direction TB
        UI_Init[InitializeHardware, Controls, Touch, Delay, LEDs]
        UI_ProcessCtrl[ProcessControls] --> UI_ReadKnobs[ReadKnobValues]
        UI_ProcessCtrl --> UI_UpdateEngine[UpdateEngineSelection]
        UI_ProcessCtrl --> UI_UpdateArpToggle[UpdateArpeggiatorToggle]
        UI_Touch[Mpr121 Touch Sensor] --> ML_PollTouch
        UI_Knobs[Analog Controls] --> UI_ReadKnobs
        UI_Button[Button] --> UI_ProcessCtrl
        UI_TouchStateGlobal["current_touch_state (Global in AudioProcessor context via Thaumazein.h)"]
    end

    subgraph Polyphony [PolyphonyEngine in Polyphony.cpp]
        direction TB
        PE_Init[Init]
        PE_HandleTouch[HandleTouchInput]
        PE_Render[RenderBlock]
        PE_Reset[ResetVoices]
        PE_ArpTrigger[TriggerArpCallbackVoice]
        PE_GetLastTouch[GetLastTouchState]
    end

    subgraph Arpeggiator [Arpeggiator.cpp]
        direction TB
        Arp_Init[Init]
        Arp_SetCB[SetNoteTriggerCallback]
        Arp_UpdateNotes[UpdateHeldNotes]
        Arp_Process[Process(frames)]
        Arp_SetTempo[SetMainTempoFromKnob]
    end

    subgraph Effects [DelayEffect.cpp]
        direction TB
        DE_Update[UpdateDelay]
        DE_Apply[ApplyDelayToOutput]
    end

    subgraph Plaits [Plaits Code]
        direction TB
        Plaits_Voice[plaits::Voice]
        Plaits_Patch[plaits::Patch]
        Plaits_Mod[plaits::Modulations]
    end

    %% Connections
    ML_Init --> UI_Init
    ML_Init --> PE_Init
    ML_Init --> Arp_Init
    ML_Init --> Arp_SetCB
    UI_UpdateArpToggle --> Arp_Init
    UI_UpdateArpToggle --> Arp_SetCB

    ML_PollTouch --> AC_ProcessUI
    AC_ProcessUI --> UI_ProcessCtrl
    AC_ProcessUI --> UI_ReadKnobs
    
    AC_UpdateArp --> PE_GetLastTouch
    PE_GetLastTouch --> Arp_UpdateNotes
    UI_TouchStateGlobal --> Arp_UpdateNotes
    AC_UpdateArp --> Arp_UpdateNotes
    Arp_UpdateNotes --> Arp_Process
    
    AC_UpdateArp --> PE_HandleTouch
    AC_RenderVoices --> PE_Render
    PE_Render --> Plaits_Voice
    Arp_Process -.-> PE_ArpTrigger
    AC_ApplyEffects --> DE_Update
    AC_ApplyEffects --> DE_Apply

    classDef main fill:#f9f,stroke:#333,stroke-width:2px;
    classDef audio fill:#9cf,stroke:#333,stroke-width:2px;
    classDef ui fill:#9f9,stroke:#333,stroke-width:2px;
    classDef dsp fill:#fc9,stroke:#333,stroke-width:2px;

    class MainLoop main;
    class AudioProcessing audio;
    class UI ui;
    class Polyphony,Plaits,Arpeggiator,Effects dsp;
```

*(Diagram needs to be rendered with a Mermaid plugin or tool. The diagram provides a high-level overview of module interactions and is updated as the project evolves.)*

## Controls
The synthesizer uses the following knobs and touch pads connected to the Daisy Seed ADC pins:

- D15 / A0 (Pin 30): Arpeggiator Tempo (1-30 Hz, exponential) / Delay Time (when Arp is off)
- D16 / A1 (Pin 31): Delay Mix & Feedback
- D17 / A2 (Pin 32): Envelope Release
- D18 / A3 (Pin 33): Envelope Attack
- D19 / A4 (Pin 34): Plaits Timbre
- D20 / A5 (Pin 35): Plaits Harmonics
- D21 / A6 (Pin 36): Plaits Morph
- D22 / A7 (Pin 37): Plaits Pitch Offset
- D23 / A8 (Pin 38): Arpeggiator Toggle Pad
- D24 / A9 (Pin 39): Model Select Previous Pad
- D25 / A10 (Pin 40): Model Select Next Pad
- D28 / A11 (Pin 43): Mod Wheel Control


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
- Potentiometers connected to the ADC pins listed in the Controls section.
- Audio outputs connected to Daisy Seed's audio outputs.

**Touch Functionality:**
- The 12 touch pads on the MPR121 act as a keyboard, playing notes in an E Phrygian scale.
- Touch pads provide dual functionality: note triggering and continuous control.
- The sensor is polled in the main loop at ~200Hz to read touch state and calculate a continuous control value.
- The average capacitance deviation (pressure) across all currently touched pads is calculated, smoothed, and used to generate a control signal.
- This control signal blends (50/50) with the knob values to modulate the Morph and Delay Feedback parameters.
- The exact note mapping is defined in `PolyphonyEngine::kTouchMidiNotes_`.
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
1. Add new class definitions to appropriate headers.
2. Implement voice-related code in `Polyphony.cpp` (within `PolyphonyEngine`).
3. Implement hardware interface code (including control processing) in `Interface.cpp`.
4. Add audio processing orchestration code to `AudioProcessor.cpp`.
5. Add UI elements (display, main loop logic) to `Thaumazein.cpp`.


### Completed

* THM-104 – Decoupled engine-switch UI from voice state changes:
    * Added `PolyphonyEngine::OnEngineChange` to handle voice migration/reset.
    * `Interface.cpp::UpdateEngineSelection` now only debounces pads, updates `current_engine_index`, and sets `engine_changed_flag`.
    * `AudioProcessor.cpp` listens for `engine_changed_flag` each block and invokes `OnEngineChange` for safe DSP-side migration.

### Remaining TO-DO

**Refactoring Cleanup:**
    - Confirm and remove dead code:
        - Commented-out "Polyphony Helper Function Implementations" block in `Polyphony.cpp`.
        - `PrepareVoiceParameters` and `ProcessVoiceEnvelopes` function definitions and declarations in `AudioProcessor.cpp` - *Completed THM-105 cleanup.*
