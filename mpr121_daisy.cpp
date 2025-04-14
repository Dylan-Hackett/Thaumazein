#include "mpr121_daisy.h"
#include "sys/system.h"

bool Mpr121::Init(const Config& config) {
    i2c_address_ = config.i2c_address << 1; // Shift address for HAL library
    i2c_handle_.Init(config.i2c_config);

    // Soft reset
    WriteRegister(MPR121_SOFTRESET, 0x63);
    System::Delay(1); // Allow time for reset

    // Check if reset was successful by reading a register that should have a default value
    if (ReadRegister8(MPR121_CONFIG2) != 0x24) {
        return false; 
    }

    // Stop mode to configure
    WriteRegister(MPR121_ECR, 0x00);

    // More sensitive touch/release thresholds
    SetThresholds(8, 4); // Lower thresholds (was 12,6)

    // First Filter (Rising)
    WriteRegister(MPR121_MHDR, 0x01);  // Maximum half delta
    WriteRegister(MPR121_NHDR, 0x03);  // Noise half delta (increased)
    WriteRegister(MPR121_NCLR, 0x10);  // Noise count limit (increased)
    WriteRegister(MPR121_FDLR, 0x20);  // Filter delay count limit (increased)

    // Second Filter (Falling)
    WriteRegister(MPR121_MHDF, 0x01);  // Maximum half delta
    WriteRegister(MPR121_NHDF, 0x03);  // Noise half delta (increased)
    WriteRegister(MPR121_NCLF, 0x10);  // Noise count limit (increased)
    WriteRegister(MPR121_FDLF, 0x20);  // Filter delay count limit (increased)

    // Touch Filter
    WriteRegister(MPR121_NHDT, 0x01);  // Noise half delta for touch
    WriteRegister(MPR121_NCLT, 0x05);  // Noise count limit for touch
    WriteRegister(MPR121_FDLT, 0x00);  // Filter delay for touch

    // Debounce settings
    WriteRegister(MPR121_DEBOUNCE, (2 << 4) | 2); // 2 samples for both touch and release

    // Charge current and timing settings
    WriteRegister(MPR121_CONFIG1, 0x3F); // 63uA charge current (increased from 16uA)
    WriteRegister(MPR121_CONFIG2, 0x00); // 0.5ms sample period (fastest)

    // Enable all 12 electrodes with baseline tracking
    WriteRegister(MPR121_ECR, 0x8F);

    return true;
}

uint16_t Mpr121::Touched(void) {
    return ReadRegister16(MPR121_TOUCHSTATUS_L);
}

uint16_t Mpr121::FilteredData(uint8_t channel) {
    if (channel > 11) return 0;
    return ReadRegister16(MPR121_FILTDATA_0L + channel * 2);
}

uint8_t Mpr121::BaselineData(uint8_t channel) {
    if (channel > 11) return 0;
    uint8_t bl = ReadRegister8(MPR121_BASELINE_0 + channel);
    return (bl << 2); // Datasheet says baseline is high 8 bits of 10-bit value
}

void Mpr121::SetThresholds(uint8_t touch, uint8_t release) {
    WriteRegister(MPR121_ECR, 0x00); // Stop mode
    for (uint8_t i = 0; i < 12; i++) {
        WriteRegister(MPR121_TOUCHTH_0 + 2 * i, touch);   // TOUCH THRESHOLD
        WriteRegister(MPR121_RELEASETH_0 + 2 * i, release); // RELEASE THRESHOLD
    }
    WriteRegister(MPR121_ECR, 0x8F); // Restart
}

// --- Private I2C Helper Functions ---

uint8_t Mpr121::ReadRegister8(uint8_t reg) {
    uint8_t value = 0;
    i2c_handle_.ReadDataAtAddress(i2c_address_, reg, 1, &value, 1, kTimeout);
    // Add error handling based on return value if needed
    return value;
}

uint16_t Mpr121::ReadRegister16(uint8_t reg) {
    uint8_t buffer[2];
    i2c_handle_.ReadDataAtAddress(i2c_address_, reg, 1, buffer, 2, kTimeout);
    // Add error handling based on return value if needed
    uint16_t value = buffer[1]; // High byte first
    value <<= 8;
    value |= buffer[0]; // Low byte second
    return value;
}

void Mpr121::WriteRegister(uint8_t reg, uint8_t value) {
    uint8_t buffer[1] = {value};
    i2c_handle_.WriteDataAtAddress(i2c_address_, reg, 1, buffer, 1, kTimeout);
    // Add error handling based on return value if needed
} 