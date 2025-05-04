#pragma once
#include <algorithm>

// Simple envelope for each polyphonic voice
class VoiceEnvelope {
public:
    enum EnvState {
        ENV_IDLE,
        ENV_ATTACK,
        ENV_SUSTAIN,
        ENV_DECAY,
        ENV_RESET
    };

    enum EnvMode {
        MODE_AR,   // Attack-Release
        MODE_ASR   // Attack-Sustain-Release
    };

    VoiceEnvelope() :
        current_state(ENV_IDLE),
        current_value(0.0f),
        mode(MODE_ASR),
        attack_curve_coefficient(0.5f),
        release_curve_coefficient(0.5f),
        phase(0),
        decay_start_level(0.0f)
    {}

    void Init(float sample_rate) {
        time_range_2x = 2.0f * 4.0f * sample_rate;  // 4s max time
        min_attack_time = 0.0002f * sample_rate;    // 0.2ms min attack (much faster for punchiness)
        min_decay_time_a = 0.4f * sample_rate;      // 400ms min decay A
        min_decay_time_b = 0.005f * sample_rate;    //  5ms min decay B (tighter release)
        reset_time = 0.008f * sample_rate;          // 8ms reset time
        reset_coefficient = 1.0f / reset_time;
        
        // Default curves
        SetAttackCurve(0.5f);
        SetReleaseCurve(0.5f);
    }

    void SetMode(EnvMode new_mode) {
        mode = new_mode;
    }

    // Original combined shape method for backward compatibility
    void SetShape(float value) {
        float curve;
        if (value < 0.5f) {
            attack_time = min_attack_time;
            decay_time = static_cast<size_t>(time_range_2x * value * value) + min_decay_time_a;
            curve = value * 0.15f;
        } else {
            float norm_val = 2.0f * value - 1.0f;
            attack_time = static_cast<size_t>(time_range_2x * norm_val * norm_val) + min_attack_time;
            decay_time = static_cast<size_t>(time_range_2x * (1.0f - value * value)) + min_decay_time_b;
            curve = 0.5f;
        }
        attack_coefficient = 1.0f / attack_time;
        decay_coefficient = 1.0f / decay_time;
        SetAttackCurve(curve);
        SetReleaseCurve(curve);
    }
    
    // New methods for separate attack and release control
    void SetAttackTime(float value) {
        // Map 0-1 value to attack time (exponential scaling)
        // value³ for finer control at shorter settings
        float value_cubed = value * value * value;
        
        // Special handling for super punchy attacks at low values
        if (value < 0.1f) {
            // Extra fast attacks at the beginning of the range
            attack_time = static_cast<size_t>(min_attack_time + 
                                           (min_attack_time * 9.0f * value / 0.1f));
        } else {
            // Normal range from 2ms to 4 seconds
            attack_time = static_cast<size_t>(0.002f * time_range_2x/8.0f + 
                                           (time_range_2x * 0.5f * value_cubed));
        }
        
        attack_coefficient = 1.0f / attack_time;
        
        // Make curve more exponential for faster attacks
        if (value < 0.3f) {
            // More exponential curve for punchy attacks
            SetAttackCurve(0.7f + (0.3f - value)); // 0.7 to 1.0 range for punchier curve
        } else {
            // Normal curve
            SetAttackCurve(0.5f);
        }
    }
    
    void SetReleaseTime(float value) {
        // Map 0-1 value to release time (exponential scaling)
        // value³ for finer control at shorter settings
        float value_cubed = value * value * value;
        
        // Release time ranges from min_decay_time_b to 8 seconds
        decay_time = static_cast<size_t>(min_decay_time_b + 
                                        (time_range_2x * value_cubed));
        decay_coefficient = 1.0f / decay_time;
        
        // Use consistent release curve regardless of release time
        SetReleaseCurve(0.5f);
    }
    
    void SetAttackCurve(float value) {
        float cu = value - 0.5f;
        attack_curve_coefficient = 128.0f * cu * cu;
    }
    
    void SetReleaseCurve(float value) {
        float cu = value - 0.5f;
        release_curve_coefficient = 128.0f * cu * cu;
    }

    void Trigger() {
        switch (current_state) {
            case ENV_IDLE:
                current_state = ENV_ATTACK;
                phase = 0;
                break;
            case ENV_DECAY:
                current_state = ENV_ATTACK;
                phase = CalculateAttackPhase(current_value);
                break;
            default:
                break;
        }
    }

    void Release() {
        switch (current_state) {
            case ENV_ATTACK:
                // Preserve the attack curve character when transitioning to release
                // This decouples release time from affecting attack punchiness
                current_value = CalculateAttackAmplitude(static_cast<float>(phase) * attack_coefficient);
                phase = 0; // Always start release from beginning, preserving the current level
                current_state = ENV_DECAY;
                break;
            case ENV_SUSTAIN:
                phase = 0;
                current_state = ENV_DECAY;
                break;
            default:
                break;
        }
    }

    float Process() {
        float ph;
        switch (current_state) {
            case ENV_IDLE:
                current_value = 0.0f;
                break;

            case ENV_ATTACK:
                ph = static_cast<float>(phase) * attack_coefficient;
                current_value = CalculateAttackAmplitude(ph);
                if (phase >= attack_time) {
                    current_state = (mode == MODE_AR) ? ENV_DECAY : ENV_SUSTAIN;
                    phase = 0;
                } else {
                    phase++;
                }
                break;

            case ENV_SUSTAIN:
                current_value = 1.0f;
                break;

            case ENV_DECAY:
                // Use current_value as starting point for decay
                // This preserves the correct level from attack phase
                ph = static_cast<float>(phase) * decay_coefficient;
                if (phase == 0) {
                    // On first frame, use current value
                    decay_start_level = current_value;
                }
                current_value = decay_start_level * (1.0f - ph) / (1.0f + release_curve_coefficient * ph);
                
                if (phase >= decay_time) {
                    current_state = ENV_IDLE;
                    phase = 0;
                } else {
                    phase++;
                }
                break;

            case ENV_RESET:
                current_value = reset_target - static_cast<float>(phase) * reset_coefficient;
                if (phase >= reset_time) {
                    current_state = ENV_IDLE;
                    phase = 0;
                } else {
                    phase++;
                }
                break;
        }
        return std::min(std::max(current_value, 0.0f), 1.0f);
    }

    void Reset() {
        if (current_state == ENV_IDLE) return;
        current_state = ENV_RESET;
        reset_target = current_value;
        phase = 0;
    }

    bool IsActive() const {
        return current_state != ENV_IDLE;
    }

private:
    float CalculateAttackAmplitude(float ph) {
        return ph / (1.0f + attack_curve_coefficient * (1.0f - ph));
    }

    float CalculateDecayAmplitude(float ph) {
        return (1.0f - ph) / (1.0f + release_curve_coefficient * ph);
    }

    size_t CalculateAttackPhase(float amp) {
        return static_cast<size_t>(roundf(attack_time * amp * (1.0f + attack_curve_coefficient) / (1.0f + amp * attack_curve_coefficient)));
    }

    size_t CalculateDecayPhase(float amp) {
        return static_cast<size_t>(roundf(decay_time * (1.0f - amp) / (amp * release_curve_coefficient + 1.0f)));
    }

    EnvState current_state;
    EnvMode mode;
    float current_value;
    float attack_curve_coefficient;
    float release_curve_coefficient;
    float time_range_2x;
    float min_attack_time;
    float min_decay_time_a;
    float min_decay_time_b;
    float reset_time;
    float reset_coefficient;
    float reset_target;
    float attack_coefficient;
    float decay_coefficient;
    size_t attack_time;
    size_t decay_time;
    size_t phase;
    float decay_start_level;
}; 