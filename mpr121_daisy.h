#pragma once
#ifndef MPR121_DAISY_H
#define MPR121_DAISY_H

#include "daisy_seed.h"
#include "per/i2c.h"

using namespace daisy;

// MPR121 Register Defines
#define MPR121_TOUCHSTATUS_L 0x00
#define MPR121_TOUCHSTATUS_H 0x01
#define MPR121_FILTDATA_0L  0x04
#define MPR121_FILTDATA_0H  0x05
#define MPR121_BASELINE_0   0x1E
#define MPR121_MHDR         0x2B
#define MPR121_NHDR         0x2C
#define MPR121_NCLR         0x2D
#define MPR121_FDLR         0x2E
#define MPR121_MHDF         0x2F
#define MPR121_NHDF         0x30
#define MPR121_NCLF         0x31
#define MPR121_FDLF         0x32
#define MPR121_NHDT         0x33
#define MPR121_NCLT         0x34
#define MPR121_FDLT         0x35

#define MPR121_TOUCHTH_0    0x41
#define MPR121_RELEASETH_0  0x42
#define MPR121_DEBOUNCE     0x5B
#define MPR121_CONFIG1      0x5C
#define MPR121_CONFIG2      0x5D
#define MPR121_CHARGECURR_0 0x5F
#define MPR121_CHARGETIME_1 0x6C
#define MPR121_ECR          0x5E // Electrode Configuration Register
#define MPR121_AUTOCONFIG0  0x7B
#define MPR121_AUTOCONFIG1  0x7C
#define MPR121_UPLIMIT      0x7D
#define MPR121_LOWLIMIT     0x7E
#define MPR121_TARGETLIMIT  0x7F

#define MPR121_GPIODIR      0x76
#define MPR121_GPIOEN       0x77
#define MPR121_GPIOSET      0x78
#define MPR121_GPIOCLR      0x79
#define MPR121_GPIOTOGGLE   0x7A

#define MPR121_SOFTRESET    0x80

// Default I2C address
#define MPR121_I2CADDR_DEFAULT 0x5A


class Mpr121 {
public:
    Mpr121() {}
    ~Mpr121() {}

    struct Config {
        I2CHandle::Config i2c_config;
        uint8_t i2c_address;

        void Defaults() {
            i2c_config.periph = I2CHandle::Config::Peripheral::I2C_1;
            i2c_config.speed = I2CHandle::Config::Speed::I2C_400KHZ; // Or I2C_100KHZ
            i2c_config.mode = I2CHandle::Config::Mode::I2C_MASTER;
            // Default pins for I2C1 on Daisy Seed
            i2c_config.pin_config.scl = {DSY_GPIOB, 8}; 
            i2c_config.pin_config.sda = {DSY_GPIOB, 9};
            i2c_address = MPR121_I2CADDR_DEFAULT;
        }
    };

    /** Initializes the sensor with the given configuration.
     *  Returns true on success, false otherwise.
     */
    bool Init(const Config& config);

    /** Reads the touch status of all 12 channels.
     *  Returns a 16-bit value where the lower 12 bits represent the touch status.
     */
    uint16_t Touched(void);

    /** Reads the filtered data for a specific channel (0-11).
     *  Returns the 10-bit filtered data value.
     */
    uint16_t FilteredData(uint8_t channel);

    /** Reads the baseline data for a specific channel (0-11).
     *  Returns the 8-bit baseline data value.
     */
    uint8_t BaselineData(uint8_t channel);

    /** Gets the difference between baseline and filtered data
     *  This is useful for proximity/continuous control
     *  Returns the difference value (higher = closer)
     */
    int16_t GetBaselineDeviation(uint8_t channel);
    
    /** Gets a cumulative proximity value from all or selected sensors
     *  This averages readings from specified channels for proximity sensing
     *  Returns a 0.0f-1.0f value representing proximity
     */
    float GetProximityValue(const uint16_t channelMask = 0x0FFF, float sensitivity = 1.0f);

    /** Sets the touch and release thresholds for all channels.
     *  touch: 0-255
     *  release: 0-255
     */
    void SetThresholds(uint8_t touch, uint8_t release);

private:
    I2CHandle i2c_handle_;
    uint8_t i2c_address_;
    static constexpr uint32_t kTimeout = 100; // I2C timeout in ms

    uint8_t ReadRegister8(uint8_t reg);
    uint16_t ReadRegister16(uint8_t reg);
    void WriteRegister(uint8_t reg, uint8_t value);
};

#endif // MPR121_DAISY_H 