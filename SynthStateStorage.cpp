#include "SynthStateStorage.h"
#include "daisy_seed.h"

using namespace daisy;

#define QSPIFUNC DSY_QSPI_TEXT

namespace {
constexpr uint32_t kFlashBase   = 0x90000000; // QSPI memory-mapped base
constexpr uint32_t kSectorSize  = 4096;
constexpr uint32_t kMagic       = 0x54485359; // 'THSY'

inline uint32_t CalcCrc(uint32_t magic, int32_t engine) { return magic ^ static_cast<uint32_t>(engine); }

QSPIHandle& GetQSPI() {
    static QSPIHandle* qspi_ptr = nullptr;
    if(!qspi_ptr) {
        qspi_ptr = new QSPIHandle();
    }
    return *qspi_ptr;
}

QSPIFUNC void ConfigureQSPI(QSPIHandle::Config::Mode mode) {
    QSPIHandle::Config cfg;
    cfg.device = QSPIHandle::Config::Device::IS25LP064A;
    cfg.mode   = mode;
    cfg.pin_config.io0 = dsy_pin(DSY_GPIOF, 8);
    cfg.pin_config.io1 = dsy_pin(DSY_GPIOF, 9);
    cfg.pin_config.io2 = dsy_pin(DSY_GPIOF, 7);
    cfg.pin_config.io3 = dsy_pin(DSY_GPIOF, 6);
    cfg.pin_config.clk = dsy_pin(DSY_GPIOF,10);
    cfg.pin_config.ncs = dsy_pin(DSY_GPIOG, 6);
    auto &qspi = GetQSPI();
    qspi.DeInit();
    qspi.Init(cfg);
}
}

namespace SynthStateStorage {

QSPIFUNC bool Load(int &engine_index) {
    const SynthState *state = reinterpret_cast<const SynthState *>(kFlashBase);
    if(state->magic != kMagic) { return false; }
    if(state->crc != CalcCrc(state->magic, state->engine_index)) { return false; }
    engine_index = state->engine_index;
    return true;
}

QSPIFUNC void Save(int engine_index) {
    SynthState state{};
    state.magic = kMagic;
    state.engine_index = engine_index;
    state.crc = CalcCrc(state.magic, state.engine_index);

    ConfigureQSPI(QSPIHandle::Config::Mode::INDIRECT_POLLING);
    auto &qspi = GetQSPI();
    qspi.EraseSector(0);
    qspi.Write(0, sizeof(state), reinterpret_cast<uint8_t *>(&state));
    ConfigureQSPI(QSPIHandle::Config::Mode::MEMORY_MAPPED);
}

QSPIFUNC void InitMemoryMapped() {
    ConfigureQSPI(QSPIHandle::Config::Mode::MEMORY_MAPPED);
}

} // namespace SynthStateStorage 