#include "daisy_seed.h"
#include "daisysp.h"
#include "../../DaisySP/Source/PhysicalModeling/stringvoice.h"
#include "arpeggiator.h"

using namespace daisy;
using namespace daisysp;

DaisySeed hw;
StringVoice string_voice;
Arpeggiator arpeggiator;

float knob1, knob2, knob3;
float adc_vals[8];
float brightness = 0.0f;
float arabic_scale[] = {61.74f, 55.00f, 65.41f, 43.65f, 51.91f, 77.78f, 41.20f};

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

    // Read knob values
    knob1 = hw.adc.GetFloat(0); // Frequency scaling
    knob2 = hw.adc.GetFloat(1); // Brightness
    knob3 = Scaling(hw.adc.GetFloat(2)); // Damping

    bool key_pressed = false;
    int num_keys_pressed = 0;
    int key_indices[8];

    // Update ADC values for touch keys and calculate brightness
    for (int i = 0; i < 8; i++) {
        adc_vals[i] = hw.adc.GetFloat(i + 3);
        brightness += adc_vals[i];

        if (adc_vals[i] > 0.5f) {
            key_indices[num_keys_pressed] = i;
            num_keys_pressed++;
        }
    }
    brightness /= 8.8f;

    float structure_value = brightness * 2.0f + 0.2f;

    // Update parameters for single notes
    string_voice.SetBrightness(knob2);
    string_voice.SetDamping(knob3);
    string_voice.SetStructure(structure_value);

    // Update parameters for the arpeggiator
    arpeggiator.UpdateParameters(knob2, knob3, structure_value, knob1);

    arpeggiator.UpdateParameters(knob2, knob3, brightness * 2.0f + 0.2f, knob1);
    arpeggiator.SetOctaveJumpProbability(0.0f); // Adjust as needed

    if (num_keys_pressed == 1) {
        // One key pressed, trigger the synth as usual
        int i = key_indices[0];
        if (i < 7) {
            float base_freq = arabic_scale[i % 7];
            float scaled_freq = base_freq * powf(2.0f, (knob1 * 4.0f) - 1.0f);

            // Set parameters before triggering
            string_voice.SetBrightness(knob2);
            string_voice.SetDamping(knob3);
            string_voice.SetStructure(brightness * 2.0f + 0.2f);

            string_voice.SetFreq(scaled_freq);
            string_voice.Trig();
        }
        key_pressed = true;
        hw.SetLed(true);
    } else if (num_keys_pressed >= 2) {
        // Two or more keys pressed, activate arpeggiator
        key_pressed = true;


        // Set notes and tempo in the arpeggiator
        arpeggiator.SetNotes(key_indices, num_keys_pressed);

        float min_arpeggio_rate = 1.0f;
        float max_arpeggio_rate = 10.0f;
        float new_tempo = min_arpeggio_rate + (knob2 * (max_arpeggio_rate - min_arpeggio_rate));
        arpeggiator.SetTempo(new_tempo);

        // Process the arpeggiator
        arpeggiator.Process((float*)out, size);
        return; // Prevent further processing
    } else {
        // No keys pressed
        key_pressed = false;
        hw.SetLed(false);
        arpeggiator.SetNotes(nullptr, 0);
    }

    // Process audio when 0 or 1 key is pressed
    for (size_t i = 0; i < size; i += 2) {
        string_out = string_voice.Process();

        // Reduce output gain
        float output = string_out * 0.25f;

        out[i] = output;
        out[i + 1] = output;
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

    // Initialize arpeggiator
    arpeggiator.Init(samplerate);
    arpeggiator.SetScale(arabic_scale, 7);
    arpeggiator.SetSynth(&string_voice);
    arpeggiator.SetHardware(&hw);

    hw.SetAudioBlockSize(4); // Use a smaller block size for lower latency
    hw.StartAudio(AudioCallback);
    hw.StartLog(true);

    while (1) {
        // Main loop
        int bright_int = static_cast<int>(brightness * 100); // Scale brightness to [0, 100]
        int knob2_int = static_cast<int>(knob2 * 100);       // Scale knob2 to [0, 100]
        int knob3_int = static_cast<int>(knob3 * 100);       // Scale knob3 to [0, 100]

        hw.PrintLine("Brightness: %d, Knob2: %d, Knob3: %d", bright_int, knob2_int, knob3_int);
        hw.DelayMs(3000);
    }
}