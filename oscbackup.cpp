#include "daisy_seed.h"
#include "daisysp.h"
#include "../../DaisySP/Source/PhysicalModeling/stringvoice.h" // Adjust the include path

using namespace daisy;
using namespace daisysp;

DaisySeed hw;
StringVoice string_voice;

float knob1, knob2, knob3;
float adc_vals[8];
float brightness = 0.0f;
float arabic_scale[] = {61.74, 55.00, 65.41, 43.65, 51.91, 77.78, 41.20};

// Function to apply a very extreme nonlinear scaling
float Scaling(float input) {
    if (input < 0.3f) {
        return input * (1.0f / 0.3f) * 0.99f; // Scale 0-0.3 to 0-0.99 range
    } else {
        return 0.99f + ((input - 0.3f) * (0.01f / 0.7f)); // Scale 0.3-1 to 0.99-1 range
    }
}

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size) {
    float string_out;
    brightness = 0.0f;

    knob1 = hw.adc.GetFloat(0);
    knob2 = hw.adc.GetFloat(1);
    knob3 = Scaling(hw.adc.GetFloat(2));

    bool key_pressed = false;

    // Update ADC values for touch keys and calculate brightnessy
    for (int i = 0; i < 8; i++) {
        adc_vals[i] = hw.adc.GetFloat(i + 3); // Read values from ADC channels 3-10
        brightness += adc_vals[i];
        brightness /= 4.0f;

        if (adc_vals[i] > 0.5f) {
            if (i < 7) {
                float base_freq = arabic_scale[i % 7]; 
                float scaled_freq = base_freq * powf(2.0f, (knob1 * 4.0f) - 1.0f); // Knob1 controls frequency scaling
                string_voice.SetFreq(scaled_freq);
                 // Knob2 controls structure
                
                string_voice.Trig();
            }
            key_pressed = true;
            break; // Only one key can be active at a time
        }
    }

    hw.SetLed(key_pressed);
    string_voice.SetBrightness(knob2); 
    string_voice.SetDamping(knob3);  
    string_voice.SetStructure(brightness * 2.0f+.2f);

    // Fill the block with samples
    for (size_t i = 0; i < size; i += 2) {
        string_out = string_voice.Process();
        out[i] = string_out * 0.6f;
        out[i + 1] = string_out * 0.6f;
    }
}

void ArchConfig(DaisySeed& hw) {
    AdcChannelConfig adcConfig[11];

    for (int pin = 16, idx = 0; pin < 27; pin++, idx++) {
        adcConfig[idx].InitSingle(hw.GetPin(pin));
    }

    hw.adc.Init(adcConfig, 11);
    hw.adc.Start();
}

int main(void) {
    hw.Init();
    ArchConfig(hw);

    float samplerate = hw.AudioSampleRate();
    string_voice.Init(samplerate);

    hw.SetAudioBlockSize(4); // Use a larger block size for stability
    hw.StartAudio(AudioCallback);
    hw.StartLog(true);

    while (1) {
        // Main loop
        int bright_int = static_cast<int>(brightness * 100); // Scale knob1 to [0, 100]
        int knob2_int = static_cast<int>(knob2 * 100); // Scale knob2 to [0, 100]
        int knob3_int = static_cast<int>(knob3 * 100); // Scale knob3 to [0, 100]
        
        hw.PrintLine("Brightness: .%d, Knob2: .%d, Knob3: .%d", bright_int, knob2_int, knob3_int);
        hw.DelayMs(3000);
    }
}