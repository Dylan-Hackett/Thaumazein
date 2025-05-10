#include "mpr121_daisy.h"
#include "daisy_seed.h" // For System::Delay, though ideally this cpp wouldn't know about System

// Note: Error handling (SetTransportErr) is a bit basic, just accumulates.

bool thaumazein_hal::Mpr121::Init(const thaumazein_hal::Mpr121::Config& config)
{
    // Use 8-bit address (7-bit left-shifted) to match pre-update driver
    // behaviour and the custom board routing.
    i2c_address_ = config.i2c_address << 1;  // 0x5A → 0xB4

    i2c_handle_.Init(config.i2c_config);
    transport_error_ = false; 
    WriteRegister(MPR121_SOFTRESET, 0x63);
    daisy::System::Delay(1); // Allow time for reset

    if(ReadRegister8(MPR121_CONFIG2) != 0x24) // Check for a known post-reset value
    {
        SetTransportErr(true);
        return false;
    }

    // Stop electodes while configuring
    WriteRegister(MPR121_ECR, 0x00);

    SetThresholds(8, 4);

    // Filter and global CDC/CDT configuration (matches working pre-update settings)
    WriteRegister(MPR121_MHDR, 0x01);
    WriteRegister(MPR121_NHDR, 0x03);
    WriteRegister(MPR121_NCLR, 0x10);
    WriteRegister(MPR121_FDLR, 0x20);

    WriteRegister(MPR121_MHDF, 0x01);
    WriteRegister(MPR121_NHDF, 0x03);
    WriteRegister(MPR121_NCLF, 0x10);
    WriteRegister(MPR121_FDLF, 0x20);

    WriteRegister(MPR121_NHDT, 0x01);
    WriteRegister(MPR121_NCLT, 0x05);
    WriteRegister(MPR121_FDLT, 0x00);

    WriteRegister(MPR121_DEBOUNCE, (2 << 4) | 2);
    WriteRegister(MPR121_CONFIG1, 0x3F);
    WriteRegister(MPR121_CONFIG2, 0x00);

    // Enable all electrodes in run mode, 10-bit data, 63 samples
    WriteRegister(MPR121_ECR, 0x8F);

    return !transport_error_;
}


// Helper function to read a single byte from an 8-bit register
// 			uint8_t ReadRegister(uint8_t reg_addr)
// 			{
// 				uint8_t value = 0;
// 				i2c_handle_.ReadDataAtAddress(i2c_address_, reg_addr, 1, &value, 1, kTimeout);
// 				return value;
// 			}
// 
// 			// Helper function to write a single byte to an 8-bit register
// 			void WriteRegister(uint8_t reg_addr, uint8_t value)
// 			{
// 				i2c_handle_.WriteDataAtAddress(i2c_address_, reg_addr, 1, &value, 1, kTimeout);
// 			}
// 
// 			// Helper function to read two bytes from a 16-bit register
// 			uint16_t ReadRegister16(uint8_t reg_addr) // todo - untested
// 			{
// 				uint8_t  value_buff[2];
// 				i2c_handle_.ReadDataAtAddress(i2c_address_, reg_addr, 1, value_buff, 2, kTimeout);
// 				return (uint16_t)value_buff[0] | ((uint16_t)value_buff[1] << 8) ; 
// 			}

uint16_t thaumazein_hal::Mpr121::Touched(void)
{
    return ReadRegister16(MPR121_TOUCHSTATUS_L);
}

uint16_t thaumazein_hal::Mpr121::FilteredData(uint8_t channel)
{
    if(channel > 12) // 0-11 for touch, 12 for proximity
        return 0;
    return ReadRegister16(MPR121_FILTDATA_0L + channel * 2);
}

uint8_t thaumazein_hal::Mpr121::BaselineData(uint8_t channel)
{
    if(channel > 12)
        return 0;
    uint8_t bl = ReadRegister8(MPR121_BASELINE_0 + channel);
    return bl;
}


int16_t thaumazein_hal::Mpr121::GetBaselineDeviation(uint8_t channel)
{
    if (channel > 11) return 0; // Only for touch channels 0-11
    uint16_t filtered  = FilteredData(channel);
    uint8_t  baseline  = BaselineData(channel);
    // Deviation positive when touch lowers the filtered value below baseline
    return (static_cast<int16_t>(baseline) << 2) - static_cast<int16_t>(filtered);
}


float thaumazein_hal::Mpr121::GetProximityValue(const uint16_t channelMask, float sensitivity)
{
    if (sensitivity <= 0.0f) sensitivity = 1.0f;
    int16_t total_deviation = 0;
    int active_channels = 0;

    for (uint8_t i = 0; i < 12; ++i) {
        if ((channelMask >> i) & 0x01) { // Check if this channel is in the mask
            uint16_t filtered_data  = FilteredData(i);
            uint8_t  baseline_data  = BaselineData(i);
            // Positive deviation when touch lowers filtered value
            int16_t deviation = (static_cast<int16_t>(baseline_data) << 2) - static_cast<int16_t>(filtered_data);
            
            if (deviation > 0) { // Consider only positive deviations for proximity
                total_deviation += deviation;
            }
            active_channels++;
        }
    }

    if (active_channels == 0) return 0.0f;

    float average_deviation = static_cast<float>(total_deviation) / active_channels;
    float proximity_value = (average_deviation / (1023.0f * sensitivity)); // Normalize against max possible deviation (scaled)

    return fmaxf(0.0f, fminf(1.0f, proximity_value)); // Clamp to 0.0 - 1.0
}

void thaumazein_hal::Mpr121::SetThresholds(uint8_t touch, uint8_t release)
{
    WriteRegister(MPR121_ECR, 0x00); // Stop sensor to change settings
    for(uint8_t i = 0; i < 12; i++)
    {
        WriteRegister(MPR121_TOUCHTH_0 + 2 * i, touch);
        WriteRegister(MPR121_RELEASETH_0 + 2 * i, release);
    }
    WriteRegister(MPR121_ECR, 0x8F); // Restart sensor with 12 electrodes
}


uint8_t thaumazein_hal::Mpr121::ReadRegister8(uint8_t reg)
{
    uint8_t value = 0;
    auto res = i2c_handle_.ReadDataAtAddress(i2c_address_, reg, 1, &value, 1, kTimeout);
    SetTransportErr(res != daisy::I2CHandle::Result::OK);
    return value;
}

uint16_t thaumazein_hal::Mpr121::ReadRegister16(uint8_t reg)
{
    uint8_t buffer[2];
    auto res = i2c_handle_.ReadDataAtAddress(i2c_address_, reg, 1, buffer, 2, kTimeout);
    SetTransportErr(res != daisy::I2CHandle::Result::OK);
    if (res == daisy::I2CHandle::Result::OK) {
        return (uint16_t)buffer[0] | ((uint16_t)buffer[1] << 8);
    }
    return 0; // Return 0 on error
}


void thaumazein_hal::Mpr121::WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t buffer[1] = {value};
    auto res = i2c_handle_.WriteDataAtAddress(i2c_address_, reg, 1, buffer, 1, kTimeout);
    SetTransportErr(res != daisy::I2CHandle::Result::OK);
}

bool thaumazein_hal::Mpr121::HasError() const { return transport_error_; }

void thaumazein_hal::Mpr121::ClearError() { transport_error_ = false; } 