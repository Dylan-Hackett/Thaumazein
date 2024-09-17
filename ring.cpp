#include "daisy_seed.h"
#include "daisysp.h"
#include "../../DaisySP/Source/PhysicalModeling/stringvoice.h" // Adjust the include path

using namespace daisy;
using namespace daisysp;

DaisySeed hw;
StringVoice string_voice;

float knob1, knob2, knob3;
float adc_vals[8];


float arabic_scale[] = {61.74, 55.00, 65.41, 43.65, 51.91, 77.78, 41.20};

void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                   AudioHandle::InterleavingOutputBuffer out,
                   size_t size) {
    float string_out;
    float brightness = 0.0f;

 
    knob1 = hw.adc.GetFloat(0);
    knob2 = hw.adc.GetFloat(1);
    knob3 = hw.adc.GetFloat(2);

    bool key_pressed = false;

    // Update ADC values for touch keys and calculate brightness
    for (int i = 0; i < 8; i++) {
        adc_vals[i] = hw.adc.GetFloat(i + 3); // Read values from ADC channels 3-10
        brightness += adc_vals[i];
        if (adc_vals[i] > 0.5f) {
            if (i < 7) {
                float base_freq = arabic_scale[i % 7]; 
                float scaled_freq = base_freq * powf(2.0f, (knob1 * 4.0f) - 2.0f);
                string_voice.SetFreq(scaled_freq);
                string_voice.SetBrightness(brightness*2.0f);
                string_voice.Trig();
            }
            key_pressed = true;
            break; // Only one key can be active at a time
        }
    }

    hw.SetLed(key_pressed);

    brightness = brightness / 1.0f; 



    string_voice.SetDamping(knob3);  
    string_voice.SetStructure(knob2 * 2.0f+.2f);

    // Fill the block with samples
    for (size_t i = 0; i < size; i += 2) {

        string_out = string_voice.Process();

        out[i] = string_out * 0.1f;
        out[i + 1] = string_out * 0.1f;
    }



}

void ArchConfig(DaisySeed& hw) {
    AdcChannelConfig adcConfig[11];
    
    for (int i = 0; i < 11; i++) {
        adcConfig[i].InitSingle(hw.GetPin(16 + i));
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
        

    }
}