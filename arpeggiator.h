// arpeggiator.h

#ifndef ARPEGGIATOR_H
#define ARPEGGIATOR_H

#include "daisy_seed.h"
#include "daisysp.h"
#include "../../DaisySP/Source/PhysicalModeling/stringvoice.h"

using namespace daisy;
using namespace daisysp;

class Arpeggiator {
public:
    Arpeggiator();
    void Init(float samplerate);
    void SetNotes(int* notes, int num_notes);
    void SetScale(float* scale, int scale_size);
    void SetTempo(float tempo); // Tempo in Hz
    void SetOctaveJumpProbability(float probability); // 0.0f to 1.0f
    void UpdateParameters(float brightness, float damping, float structure, float knob1);
    void Process(float* out, size_t size);

    void SetSynth(StringVoice* synth);
    void SetHardware(DaisySeed* hardware);

    bool IsActive() const;
    float GetMetroRate();

private:
    Metro metro_;
    uint32_t rng_state_;
    int* notes_;
    int num_notes_;
    float* scale_;
    int scale_size_;
    float octave_jump_prob_;
    StringVoice* synth_;
    DaisySeed* hw_;
    float brightness_;
    float damping_;
    float structure_;
    float knob1_;

    // Random number generator
    uint32_t Xorshift32();

    // Helper functions
    void TriggerNote();
};

#endif // ARPEGGIATOR_H
