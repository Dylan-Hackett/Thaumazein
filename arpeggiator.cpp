// arpeggiator.cpp

#include "arpeggiator.h"

Arpeggiator::Arpeggiator()
    : rng_state_(0x12345678),
      notes_(nullptr),
      num_notes_(0),
      scale_(nullptr),
      scale_size_(0),
      octave_jump_prob_(0.0f),
      synth_(nullptr),
      hw_(nullptr),
      brightness_(0.5f),
      damping_(0.5f),
      structure_(0.5f),
      knob1_(0.5f) {
}

void Arpeggiator::Init(float samplerate) {
    metro_.Init(2.0f, samplerate); // Default tempo
}

void Arpeggiator::SetNotes(int* notes, int num_notes) {
    notes_ = notes;
    num_notes_ = num_notes;
}

void Arpeggiator::SetScale(float* scale, int scale_size) {
    scale_ = scale;
    scale_size_ = scale_size;
}

void Arpeggiator::SetTempo(float tempo) {
    metro_.SetFreq(tempo);
}

void Arpeggiator::SetOctaveJumpProbability(float probability) {
    octave_jump_prob_ = probability;
}

void Arpeggiator::UpdateParameters(float brightness, float damping, float structure, float knob1) {
    brightness_ = brightness;
    damping_ = damping;
    structure_ = structure;
    knob1_ = knob1;
}

void Arpeggiator::Process(float* out, size_t size) {
    if (num_notes_ < 2 || !synth_ || !hw_) return;

    for (size_t i = 0; i < size; i += 2) {
        if (metro_.Process()) {
            TriggerNote();
        }
        // Process audio
        float string_out = synth_->Process();

        // Reduce output gain
        float output = string_out * 0.25f;

        out[i] = output;
        out[i + 1] = output;
    }
}

bool Arpeggiator::IsActive() const {
    return num_notes_ >= 2;
}

uint32_t Arpeggiator::Xorshift32() {
    // Xorshift algorithm
    uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return x;
}
float Arpeggiator::GetMetroRate() {
    return metro_.GetFreq();  // Return the current metro rate (in Hz)
}

void Arpeggiator::TriggerNote() {
    // Randomly select the next note
    uint32_t rnd = Xorshift32();
    int random_index = rnd % num_notes_;
    int note_index = notes_[random_index];

    if (note_index < scale_size_) {
        float base_freq = scale_[note_index % scale_size_];
        float scaled_freq = base_freq * powf(2.0f, (knob1_ * 4.0f) - 1.0f);

        // Randomly decide whether to shift the note up an octave
        rnd = Xorshift32();
        float rand_float = static_cast<float>(rnd) / static_cast<float>(UINT32_MAX);
        if (rand_float < octave_jump_prob_) {
            scaled_freq *= 2.0f; // Raise by one octave
        }

        // Set parameters before triggering
        synth_->SetBrightness(brightness_);
        synth_->SetDamping(damping_);
        synth_->SetStructure(structure_);

        synth_->SetFreq(scaled_freq);
        synth_->Trig();

        // Optional: Toggle LED state for visual feedback
        static bool led_state = false;
        led_state = !led_state;
        hw_->SetLed(led_state);
    }
}

void Arpeggiator::SetSynth(StringVoice* synth) {
    synth_ = synth;
}

void Arpeggiator::SetHardware(DaisySeed* hardware) {
    hw_ = hardware;
}
