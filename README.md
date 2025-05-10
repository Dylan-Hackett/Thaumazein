# Thaumazein Project Overview

Thaumazein is a polyphonic Eurorack voice built on the Daisy Seed.  It combines Plaits-style macro-oscillators, per-voice envelopes, an arpeggiator, and a small effects chain (delay, biquad filters, and freeverb) into a self-contained synth engine.

# Firmware build targets

* **`make flash-stub`** – build a tiny internal-flash stub that sets up QSPI XIP then jumps to the main app.
* **`make flash-app`** – build the full application for execution-in-place at `0x90040000` and flash it with DFU.
* **`make program-dfu`** – same as **flash-app** when `APP_TYPE=BOOT_QSPI` (default).

The binary produced under `build/` should begin with:

```
0x0000 0220  <-- MSP = 0x20020000 (top of DTCM)
0x9106 0490  <-- Reset _Handler = 0x90040691 (QSPI)
```

If either of these words looks wrong the bootloader will stay in DFU.

## Current Tasks

*   [ ] Resolve QSPI boot issues.
    *   [x] Ensure `APP_TYPE = BOOT_QSPI` is in Makefile.
    *   [x] Ensure `C_DEFS += -DBOOT_APP`
    *   [x] Verify linker script points to `STM32H750IB_qspi.lds`.
    *   [x] Confirm reset vector now points to QSPI.
    *   [ ] Add automated sanity-check for vector table during CI.

*   [ ] Refactor long function names to concise variants.
*   [ ] Tighten code size (try `-Os` for PLATTS DSP sources).
*   [ ] Finish voice allocation pooling.
*   [ ] Polish MPR121 tactile interface.

## Codebase Diagram

```mermaid
graph TD
    App[Main Firmware (thaumazein.cpp)] --> Audio[Audio Engine]
    App --> UI[Interface]
    Audio --> Poly[Polyphony]
    Audio --> FX[Effects]
    UI --> Ctrl[Controls & MPR121]
    UI --> Disp[LED / Debug]
```