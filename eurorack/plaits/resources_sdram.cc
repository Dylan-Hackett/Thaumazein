#include "plaits/resources.h"
#include <cstring>

namespace plaits {

// Allocate uninitialized copy of the integrated wavetable data in external SDRAM.
DSY_SDRAM_BSS int16_t wav_integrated_waves[WAV_INTEGRATED_WAVES_SIZE];

// Forward declaration of the flash-resident wavetable we renamed in resources.cc
extern const int16_t wav_integrated_waves_flash[WAV_INTEGRATED_WAVES_SIZE];

// Copy the wavetable from flash to SDRAM – must be called once after SDRAM is initialised.
void PlaitsResourcesInit() {
    memcpy(wav_integrated_waves, wav_integrated_waves_flash, sizeof(wav_integrated_waves));
}

} // namespace plaits

extern "C" {
void PlaitsResourcesInit_C() {
    plaits::PlaitsResourcesInit();
}
} 