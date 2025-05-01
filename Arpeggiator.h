#ifndef ARPEGGIATOR_H
#define ARPEGGIATOR_H

#include "daisy_seed.h"
#include "daisysp.h"
#include <functional>

using namespace daisy;
using namespace daisysp;

class Arpeggiator {
public:
    Arpeggiator();
    void Init(float samplerate);
    void SetNotes(int* notes, int num_notes);
    void SetScale(float* scale, int scale_size);
    void SetMainTempo(float tempo);             // Main tempo in Hz
    void SetPolyrhythmRatio(float ratio);       // Ratio for polyrhythm
    void SetOctaveJumpProbability(float probability); // 0.0f to 1.0f
    void Process(size_t frames);                // Call each block for scheduling

    void SetNoteTriggerCallback(std::function<void(int)> cb);

    bool IsActive() const;
    float GetMetroRate();
    float GetCurrentInterval() const;

    enum Direction { Forward, Random };
    void SetDirection(Direction dir);

private:
    Metro metro_;
    uint32_t rng_state_;
    int* notes_;
    int num_notes_;
    float* scale_;
    int scale_size_;
    float octave_jump_prob_;
    std::function<void(int)> note_callback_;

    uint32_t Xorshift32();
    void TriggerNote();

    float polyrhythm_ratio_;
    float next_trigger_time_;
    float current_time_;
    float sample_rate_;
    float current_interval_;
    int step_index_;
    Direction direction_;

    void UpdateInterval();
};

#endif // ARPEGGIATOR_H 