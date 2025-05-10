#ifndef THAUMAZEIN_H_
#define THAUMAZEIN_H_

#pragma once

#include "daisy_seed.h"
#include "daisysp.h"
#include "plaits/dsp/voice.h"
#include "mpr121_daisy.h"
#include "Effects/EchoDelay.h"
#include "VoiceEnvelope.h"
#include "Effects/reverbsc.h"
#include "Effects/BiquadFilters.h"
#include "util/CpuLoadMeter.h"
#include "stmlib/utils/buffer_allocator.h"
#include <cmath>
#include "Arpeggiator.h"
#include "Polyphony.h"
#include "SynthStateStorage.h"

using namespace daisy;
using namespace daisysp;
using namespace stmlib;
using namespace infrasonic;


// Global Constants
// Moved to Polyphony.h as they are primarily used there
// #define NUM_VOICES 4 
// #define BLOCK_SIZE 16
#define MAX_DELAY_SAMPLES 48000 
#define SAMPLE_RATE sample_rate  // Use runtime sample_rate from hardware audio sampler

const float MASTER_VOLUME = 0.7f; // Master output level scaler


void AudioCallback(AudioHandle::InterleavingInputBuffer in, AudioHandle::InterleavingOutputBuffer out, size_t size);
void InitializeSynth();
void Bootload();
void UpdateLED();
void PollTouchSensor();
void ProcessControls();
void ReadKnobValues();
void UpdateEngineSelection();
void UpdateArpeggiatorToggle();


extern DaisySeed hw;
extern thaumazein_hal::Mpr121 touch_sensor;
extern EchoDelay<48000> delay;
extern CpuLoadMeter cpu_meter;


extern AnalogControl delay_time_knob;        // ADC 0 (Pin 15) Delay Time
extern AnalogControl delay_mix_feedback_knob; // ADC 1 (Pin 16) Delay Mix & Feedback
extern AnalogControl env_release_knob;       // ADC 2 (Pin 17) Envelope Release
extern AnalogControl env_attack_knob;        // ADC 3 (Pin 18) Envelope Attack
extern AnalogControl timbre_knob;            // ADC 4 (Pin 20) Plaits Timbre/Engine
extern AnalogControl harmonics_knob;         // ADC 5 (Pin 21) Plaits Harmonics
extern AnalogControl morph_knob;             // ADC 6 (Pin 22) Plaits Morph
extern AnalogControl pitch_knob;             // ADC 7 (Pin 23) Plaits Pitch
extern AnalogControl arp_pad;               // ADC 8 (Pin 23) Arpeggiator Toggle Pad
extern AnalogControl model_prev_pad;        // ADC 9 (Pin 24) Model Select Previous Pad
extern AnalogControl model_next_pad;        // ADC 10 (Pin 25) Model Select Next Pad
extern AnalogControl mod_wheel;             // ADC 11 (Pin 28) Mod Wheel Control


extern Arpeggiator arp;
extern volatile bool arp_enabled;


extern float sample_rate;
extern volatile bool update_display;
extern volatile float smoothed_output_level;
extern const int MAX_ENGINE_INDEX;

// Shared buffer
extern char shared_buffer[262144];

// Shared touch sensor data (polled in main loop)
extern volatile uint16_t current_touch_state; 
extern volatile float touch_cv_value; 

extern volatile int current_engine_index;
extern volatile bool engine_changed_flag;

extern volatile float adc_raw_values[12]; // Array to hold raw values for all 12 ADCs

// Extern declarations for global knob values
extern float pitch_val, harm_knob_val, timbre_knob_val, morph_knob_val;
extern float delay_time_val, delay_mix_feedback_val, delay_mix_val, delay_feedback_val;
extern float env_attack_val, env_release_val;

// Add extern declaration for touch pad LED GPIOs
extern daisy::GPIO touch_leds[12];

// Add extern declarations for ARP LED blink timestamps and duration
extern volatile uint32_t arp_led_timestamps[12];
extern const uint32_t ARP_LED_DURATION_MS;

extern bool touch_sensor_present;

extern PolyphonyEngine poly_engine;

#endif // THAUMAZEIN_H_ 