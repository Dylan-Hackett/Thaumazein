#pragma once

#include <cstddef> // For size_t

// Assuming 'daisy.h' or a similar header included by 'Thaumazein.h'
// makes 'daisy::AudioHandle' available. For self-containment of this header,
// and to ensure Daisy types are known:
#include "daisy.h"

namespace DelayEffect {

    /**
     * @brief Configures the delay parameters like feedback and time.
     */
    void UpdateDelay();

    /**
     * @brief Applies the delay effect to the input audio signal and writes to the output buffer.
     * 
     * @param mixed_signal_input Pointer to the input buffer containing the dry audio signal (mono).
     * @param out The interleaved output buffer to write the processed audio to.
     * @param size The number of samples in the output buffer (for all channels).
     * @param dry_level The proportion of the dry signal in the final mix (0.0 to 1.0).
     */
    void ApplyDelayToOutput(const float* mixed_signal_input,
                            daisy::AudioHandle::InterleavingOutputBuffer out,
                            size_t size,
                            float dry_level);

} // namespace DelayEffect 