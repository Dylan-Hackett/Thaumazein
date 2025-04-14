#pragma once

#include "daisy_seed.h"
#include "daisysp.h"
#include "plaits/dsp/voice.h"
#include "mpr121_daisy.h"
#include "Effects/EchoDelay.h"
#include "VoiceEnvelope.h"
#include <cmath>

using namespace daisy;
using namespace daisysp;
using namespace stmlib;
using namespace infrasonic;


#define NUM_VOICES 4 
#define BLOCK_SIZE 16  
#define MAX_DELAY_SAMPLES 48000 
#define SAMPLE_RATE 48000  // Adding sample rate definition


void AudioCallback(AudioHandle::InterleavingInputBuffer in,
                 AudioHandle::InterleavingOutputBuffer out,
                 size_t size);
int FindVoice(float note, int max_voices);
int FindAvailableVoice(int max_voices);
void AssignMonoNote(float note);
void InitializeVoices();
void InitializeSynth();
void HandleButtonInput();
void UpdateLED();


extern DaisySeed hw;
extern Mpr121 touch_sensor;
extern EchoDelay<48000> delay;


extern plaits::Voice voices[NUM_VOICES];
extern plaits::Patch patches[NUM_VOICES];
extern plaits::Modulations modulations[NUM_VOICES];
extern plaits::Voice::Frame output_buffers[NUM_VOICES][BLOCK_SIZE];


extern bool voice_active[NUM_VOICES];
extern float voice_note[NUM_VOICES];
extern VoiceEnvelope voice_envelopes[NUM_VOICES];
extern uint16_t last_touch_state;

extern Switch button;
extern AnalogControl pitch_knob;          // ADC 0 (Pin 15) Plaits Pitch
extern AnalogControl harmonics_knob;      // ADC 1 (Pin 16) Plaits Harmonics
extern AnalogControl timbre_knob;         // ADC 2 (Pin 17) Plaits Timbre/Engine
extern AnalogControl decay_knob;          // ADC 3 (Pin 18) Plaits Decay
extern AnalogControl morph_knob;          // ADC 4 (Pin 20) Plaits Morph
extern AnalogControl delay_feedback_knob; // ADC 5 (Pin 21) Delay Feedback
extern AnalogControl delay_time_knob;     // ADC 6 (Pin 22) Delay Time
extern AnalogControl delay_lag_knob;      // ADC 7 (Pin 23) Delay Lag
extern AnalogControl delay_mix_knob;      // ADC 8 (Pin 19) Delay Wet/Dry Mix
extern AnalogControl env_attack_knob;     // ADC 9 (Pin 24) Envelope Attack
extern AnalogControl env_release_knob;    // ADC 10 (Pin 25) Envelope Release


extern const float kTouchMidiNotes[12];


extern float sample_rate;
extern volatile uint32_t avg_elapsed_us;
extern volatile bool update_display;
extern volatile float smoothed_output_level;
extern const int MAX_ENGINE_INDEX;

// Bootloader configuration
extern const uint32_t BOOTLOADER_HOLD_TIME_MS;
extern uint32_t button_held_start_time;
extern bool button_was_pressed;

// Shared buffer
extern char shared_buffer[262144]; 