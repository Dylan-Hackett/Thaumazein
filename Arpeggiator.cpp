#include "Arpeggiator.h"

Arpeggiator::Arpeggiator()
    : rng_state_(0x12345678),
      notes_(nullptr),
      num_notes_(0),
      scale_(nullptr),
      scale_size_(0),
      octave_jump_prob_(0.0f),
      note_callback_(nullptr),
      polyrhythm_ratio_(1.0f),
      next_trigger_time_(0.0f),
      current_time_(0.0f),
      sample_rate_(48000.0f),
      current_interval_(1.0f),
      step_index_(0),
      direction_(Forward)
{}

void Arpeggiator::Init(float samplerate) {
    sample_rate_ = samplerate;
    metro_.Init(1.0f, samplerate);
    next_trigger_time_ = 0.0f;
    current_time_ = 0.0f;
    UpdateInterval();
}

void Arpeggiator::SetNotes(int* notes, int num_notes) {
    notes_ = notes;
    num_notes_ = num_notes;
}

void Arpeggiator::SetScale(float* scale, int scale_size) {
    scale_ = scale;
    scale_size_ = scale_size;
}

void Arpeggiator::SetMainTempo(float tempo) {
    if (tempo < 0.1f) tempo = 0.1f;
    metro_.SetFreq(tempo);
    UpdateInterval();
}

void Arpeggiator::SetPolyrhythmRatio(float ratio) {
    if (ratio <= 0.0f) ratio = 1.0f;
    polyrhythm_ratio_ = ratio;
    UpdateInterval();
}

void Arpeggiator::SetOctaveJumpProbability(float probability) {
    octave_jump_prob_ = probability;
}

void Arpeggiator::SetNoteTriggerCallback(std::function<void(int)> cb) {
    note_callback_ = cb;
}

void Arpeggiator::SetDirection(Direction dir) {
    direction_ = dir;
}

bool Arpeggiator::IsActive() const {
    return num_notes_ >= 1;
}

float Arpeggiator::GetMetroRate() {
    return metro_.GetFreq();
}

float Arpeggiator::GetCurrentInterval() const {
    return current_interval_;
}

void Arpeggiator::Process(size_t frames) {
    if (num_notes_ < 1 || !note_callback_) return;
    for (size_t i = 0; i < frames; ++i) {
        current_time_ += 1.0f / sample_rate_;
        if (current_time_ >= next_trigger_time_) {
            TriggerNote();
            next_trigger_time_ += current_interval_;
        }
    }
}

void Arpeggiator::TriggerNote() {
    if (num_notes_ == 0) return;
    // Debug: confirm ARP TriggerNote call
    {
        extern DaisySeed hw; // global hardware
        char buf[32];
        sprintf(buf, "[DEBUG] ARP idx %d", step_index_);
        hw.PrintLine(buf);
    }
    int idx;
    if (direction_ == Random) {
        uint32_t rnd = Xorshift32();
        idx = rnd % num_notes_;
    } else { // Forward sequential
        idx = step_index_ % num_notes_;
        ++step_index_;
    }
    if (note_callback_) {
        note_callback_(notes_[idx]);
    }
}

void Arpeggiator::UpdateInterval() {
    float main_interval = 1.0f / metro_.GetFreq();
    current_interval_ = main_interval / polyrhythm_ratio_;
}

uint32_t Arpeggiator::Xorshift32() {
    uint32_t x = rng_state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state_ = x;
    return x;
} 