#pragma once
#ifndef MPR121_DAISY_H
#define MPR121_DAISY_H

#include "daisy_seed.h"
#include "per/i2c.h"

// Add a namespace to avoid collision with daisy::Mpr121
namespace thaumazein_hal {

using namespace daisy; // Keep daisy namespace accessible within thaumazein_hal for convenience

/// mp

// Register map
#define MPR121_TOUCHSTATUS_L 0x00
#define MPR121_TOUCHSTATUS_H 0x01
#define MPR121_FILTDATA_0L 0x04
#define MPR121_FILTDATA_0H 0x05
#define MPR121_BASELINE_0 0x1E
#define MPR121_MHDR 0x2B
#define MPR121_NHDR 0x2C
#define MPR121_NCLR 0x2D
#define MPR121_FDLR 0x2E
#define MPR121_MHDF 0x2F
#define MPR121_NHDF 0x30
#define MPR121_NCLF 0x31
#define MPR121_FDLF 0x32
#define MPR121_NHDT 0x33
#define MPR121_NCLT 0x34
#define MPR121_FDLT 0x35

#define MPR121_TOUCHTH_0 0x41
#define MPR121_RELEASETH_0 0x42
#define MPR121_DEBOUNCE 0x5B
#define MPR121_CONFIG1 0x5C
#define MPR121_CONFIG2 0x5D
#define MPR121_CHARGECURR_0 0x5F
#define MPR121_CHARGETIME_1 0x6C
#define MPR121_ECR 0x5E
#define MPR121_AUTOCONFIG0 0x7B
#define MPR121_AUTOCONFIG1 0x7C
#define MPR121_UPLIMIT 0x7D
#define MPR121_LOWLIMIT 0x7E
#define MPR121_TARGETLIMIT 0x7F

#define MPR121_GPIODIR 0x76
#define MPR121_GPIOEN 0x77
#define MPR121_GPIOSET 0x78
#define MPR121_GPIOCLR 0x79
#define MPR121_GPIOTOGGLE 0x7A

#define MPR121_SOFTRESET 0x80
#define MPR121_I2CADDR_DEFAULT 0x5A

class Mpr121
{
  public:
    Mpr121() {}
    ~Mpr121() {}

    struct Config
    {
        I2CHandle::Config i2c_config;
        uint8_t          i2c_address;

        void Defaults()
        {
            i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
            i2c_config.speed  = I2CHandle::Config::Speed::I2C_400KHZ;
            i2c_config.mode   = I2CHandle::Config::Mode::I2C_MASTER;
            i2c_config.pin_config.scl = Pin(daisy::GPIOPort::PORTB, 8);
            i2c_config.pin_config.sda = Pin(daisy::GPIOPort::PORTB, 9);
            i2c_address               = MPR121_I2CADDR_DEFAULT;
        }
    };

    bool     Init(const Config& config);
    uint16_t Touched();
    uint16_t FilteredData(uint8_t channel);
    uint8_t  BaselineData(uint8_t channel);
    int16_t  GetBaselineDeviation(uint8_t channel);
    float    GetProximityValue(uint16_t channelMask = 0x0FFF,
                               float     sensitivity  = 1.0f);
    void     SetThresholds(uint8_t touch, uint8_t release);

    // Add error handling accessors
    bool HasError() const;     // true if any I2C transaction failed since last ClearError()
    void ClearError();         // reset internal error flag

  private:
    I2CHandle              i2c_handle_;
    uint8_t                i2c_address_;
    bool transport_error_ = false; // accumulates I2C errors
    void SetTransportErr(bool err) { transport_error_ |= err; }
    static constexpr uint32_t kTimeout = 100;

    uint8_t  ReadRegister8(uint8_t reg);
    uint16_t ReadRegister16(uint8_t reg);
    void     WriteRegister(uint8_t reg, uint8_t value);
};

} // namespace thaumazein_hal

#endif 