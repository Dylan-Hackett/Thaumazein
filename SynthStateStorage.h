#pragma once
#include "daisy_seed.h"

struct SynthState {
    uint32_t magic;
    int32_t engine_index;
    uint32_t crc;
};

namespace SynthStateStorage {
    bool Load(int &engine_index);
    void Save(int engine_index);
    void InitMemoryMapped();
} 