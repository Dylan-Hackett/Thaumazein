#include "mpr121_daisy.h"
#include "sys/system.h"
#include "daisysp.h"

bool Mpr121::Init(const Config& config)
{
    i2c_address_ = config.i2c_address << 1;
    i2c_handle_.Init(config.i2c_config);

    WriteRegister(MPR121_SOFTRESET, 0x63);
    System::Delay(1);

    if(ReadRegister8(MPR121_CONFIG2) != 0x24)
    {
        return false;
    }

    WriteRegister(MPR121_ECR, 0x00);
    SetThresholds(8, 4);

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

    WriteRegister(MPR121_ECR, 0x8F);

    return true;
}

uint16_t Mpr121::Touched(void)
{
    return ReadRegister16(MPR121_TOUCHSTATUS_L);
}

uint16_t Mpr121::FilteredData(uint8_t channel)
{
    if(channel > 11)
        return 0;
    return ReadRegister16(MPR121_FILTDATA_0L + channel * 2);
}

uint8_t Mpr121::BaselineData(uint8_t channel)
{
    if(channel > 11)
        return 0;
    uint8_t bl = ReadRegister8(MPR121_BASELINE_0 + channel);
    return (bl << 2);
}

int16_t Mpr121::GetBaselineDeviation(uint8_t channel)
{
    if(channel > 11)
        return 0;

    uint16_t baseline = BaselineData(channel);
    uint16_t filtered = FilteredData(channel);
    return static_cast<int16_t>(baseline - filtered);
}

float Mpr121::GetProximityValue(const uint16_t channelMask, float sensitivity)
{
    int32_t totalDeviation = 0;
    int     numChannels    = 0;
    int16_t maxDeviation   = 0;

    for(uint8_t i = 0; i < 12; i++)
    {
        if(channelMask & (1 << i))
        {
            int16_t dev = GetBaselineDeviation(i);
            if(dev > maxDeviation)
                maxDeviation = dev;
            totalDeviation += dev;
            numChannels++;
        }
    }

    if(numChannels == 0)
        return 0.0f;

    float proximityValue = static_cast<float>(maxDeviation) * sensitivity;
    proximityValue = daisysp::fmax(0.0f,
                                   daisysp::fmin(1.0f, proximityValue / 100.0f));
    return proximityValue;
}

void Mpr121::SetThresholds(uint8_t touch, uint8_t release)
{
    WriteRegister(MPR121_ECR, 0x00);
    for(uint8_t i = 0; i < 12; i++)
    {
        WriteRegister(MPR121_TOUCHTH_0 + 2 * i, touch);
        WriteRegister(MPR121_RELEASETH_0 + 2 * i, release);
    }
    WriteRegister(MPR121_ECR, 0x8F);
}

uint8_t Mpr121::ReadRegister8(uint8_t reg)
{
    uint8_t value = 0;
    auto res = i2c_handle_.ReadDataAtAddress(i2c_address_, reg, 1, &value, 1, kTimeout);
    SetTransportErr(res != I2CHandle::Result::OK);
    return value;
}

uint16_t Mpr121::ReadRegister16(uint8_t reg)
{
    uint8_t buffer[2] = {0, 0};
    auto res = i2c_handle_.ReadDataAtAddress(i2c_address_, reg, 1, buffer, 2, kTimeout);
    SetTransportErr(res != I2CHandle::Result::OK);
    uint16_t value = buffer[1];
    value <<= 8;
    value |= buffer[0];
    return value;
}

void Mpr121::WriteRegister(uint8_t reg, uint8_t value)
{
    uint8_t buffer[1] = {value};
    auto res = i2c_handle_.WriteDataAtAddress(i2c_address_, reg, 1, buffer, 1, kTimeout);
    SetTransportErr(res != I2CHandle::Result::OK);
}

bool Mpr121::HasError() const { return transport_error_; }

void Mpr121::ClearError() { transport_error_ = false; } 