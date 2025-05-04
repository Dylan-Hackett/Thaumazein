#include "Thaumazein.h"

// --- Polyphony Setup ---
plaits::Voice voices[NUM_VOICES];
plaits::Patch patches[NUM_VOICES];
plaits::Modulations modulations[NUM_VOICES];

// Place the shared buffer in SDRAM using the DSY_SDRAM_BSS attribute
// Increase buffer size for 4 voices (256KB)
DSY_SDRAM_BSS char shared_buffer[262144]; 

plaits::Voice::Frame output_buffers[NUM_VOICES][BLOCK_SIZE]; 

bool voice_active[NUM_VOICES] = {false};
float voice_note[NUM_VOICES] = {0.0f};
float voice_values[NUM_VOICES] = {0.0f}; // For envelope output values
uint16_t last_touch_state = 0;
VoiceEnvelope voice_envelopes[NUM_VOICES]; 

// Map touch pads (0-11) to MIDI notes (E Phrygian scale starting at E2)
const float kTouchMidiNotes[12] = {
    40.0f, 41.0f, 43.0f, 45.0f, 47.0f, 48.0f, // E2, F2, G2, A2, B2, C3
    50.0f, 52.0f, 53.0f, 55.0f, 57.0f, 59.0f  // D3, E3, F3, G3, A3, B3
};

// Engine configuration
// Allow all engines, polyphony determined dynamically
// Total registered engines in plaits::Voice (see Voice::Init)
const int MAX_ENGINE_INDEX = 12; // Valid indices: 0-12

// --- Voice management functions ---
int FindVoice(float note, int max_voices) {
    for (int v = 0; v < max_voices; ++v) {
        if (voice_active[v] && fabsf(voice_note[v] - note) < 0.1f) {
            return v;
        }
    }
    return -1; // Not found
}

int FindAvailableVoice(int max_voices) {
    for (int v = 0; v < max_voices; ++v) {
        if (!voice_active[v]) {
            return v;
        }
    }
    return -1; // All allowed voices active
}

void AssignMonoNote(float note) {
    if (voice_active[0] && fabsf(voice_note[0] - note) > 0.1f) {
        voice_active[0] = false; // Mark old note for release
    }
    voice_note[0] = note;
    voice_active[0] = true;
    modulations[0].trigger = 1.0f; // Set trigger high for the new note
}

void InitializeVoices() {
    // Initialize the allocator with the SDRAM buffer
    stmlib::BufferAllocator allocator(shared_buffer, sizeof(shared_buffer));

    // Initialize Plaits Voices 
    for (int i = 0; i < NUM_VOICES; ++i) { // Initialize all 4 voices
        voices[i].Init(&allocator);
        patches[i].engine = 0;      
        modulations[i].engine = 0; 
        modulations[i].trigger = 0.0f;
        modulations[i].level_patched = false; // Initialize level patched flag
        voice_active[i] = false;
        voice_note[i] = 0.0f;
        
        // Initialize envelopes with proper settings
        voice_envelopes[i].Init(SAMPLE_RATE);
        voice_envelopes[i].SetMode(VoiceEnvelope::MODE_AR);  // Use AR mode (no sustain) for snappier release
        voice_envelopes[i].SetShape(0.5f); // Start with middle curve
        voice_values[i] = 0.0f;
    }
} 