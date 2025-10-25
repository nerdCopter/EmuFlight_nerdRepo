/*
 * This file is part of Cleanflight, Betaflight and INAV.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * Alternatively, the contents of this file may be used under the terms
 * of the GNU General Public License Version 3, as described below:
 *
 * This file is free software: you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see http://www.gnu.org/licenses/.
 *
 * Copyright: INAVFLIGHT OU
 */

// See datasheet at https://www.infineon.com/dgdl/Infineon-DPS310-DataSheet-v01_02-EN.pdf?fileId=5546d462576f34750157750826c42242

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#include "build/build_config.h"
#include "build/debug.h"
#include "common/utils.h"
#include "common/maths.h"

#include "drivers/io.h"
#include "drivers/bus.h"
#include "drivers/bus_spi.h"
#include "drivers/time.h"
#include "drivers/barometer/barometer.h"
#include "drivers/barometer/barometer_dps310.h"
#include "drivers/resource.h"

// 10 MHz max SPI frequency
#define DPS310_MAX_SPI_CLK_HZ 10000000

#if defined(USE_BARO) && (defined(USE_BARO_DPS310) || defined(USE_BARO_SPI_DPS310))

#define DPS310_I2C_ADDR             0x76

#define DPS310_REG_PSR_B2           0x00
#define DPS310_REG_PSR_B1           0x01
#define DPS310_REG_PSR_B0           0x02
#define DPS310_REG_TMP_B2           0x03
#define DPS310_REG_TMP_B1           0x04
#define DPS310_REG_TMP_B0           0x05
#define DPS310_REG_PRS_CFG          0x06
#define DPS310_REG_TMP_CFG          0x07
#define DPS310_REG_MEAS_CFG         0x08
#define DPS310_REG_CFG_REG          0x09

#define DPS310_REG_RESET            0x0C
#define DPS310_REG_ID               0x0D

#define DPS310_REG_COEF             0x10
#define DPS310_REG_COEF_SRCE        0x28

#define DPS310_ID_REV_AND_PROD_ID       (0x10)  // Infineon DPS310
#define SPL07_003_CHIP_ID               (0x11)  // SPL07_003

#define DPS310_RESET_BIT_SOFT_RST       (0x09)    // 0b1001

#define DPS310_MEAS_CFG_COEF_RDY        (1 << 7)
#define DPS310_MEAS_CFG_SENSOR_RDY      (1 << 6)
#define DPS310_MEAS_CFG_TMP_RDY         (1 << 5)
#define DPS310_MEAS_CFG_PRS_RDY         (1 << 4)
#define DPS310_MEAS_CFG_MEAS_CTRL_CONT  (0x7)

#define DPS310_PRS_CFG_BIT_PM_RATE_32HZ (0x50)      //  101 - 32 measurements pr. sec.
#define DPS310_PRS_CFG_BIT_PM_PRC_16    (0x04)      // 0100 - 16 times (Standard).

#define DPS310_TMP_CFG_BIT_TMP_EXT          (0x80)  //
#define DPS310_TMP_CFG_BIT_TMP_RATE_32HZ    (0x50)  //  101 - 32 measurements pr. sec.
#define DPS310_TMP_CFG_BIT_TMP_PRC_16       (0x04)  // 0100 - 16 times (Standard).

#define DPS310_CFG_REG_BIT_P_SHIFT          (0x04)
#define DPS310_CFG_REG_BIT_T_SHIFT          (0x08)

#define DPS310_COEF_SRCE_BIT_TMP_COEF_SRCE  (0x80)

typedef struct {
    int16_t c0;     // 12bit
    int16_t c1;     // 12bit
    int32_t c00;    // 20bit
    int32_t c10;    // 20bit
    int16_t c01;    // 16bit
    int16_t c11;    // 16bit
    int16_t c20;    // 16bit
    int16_t c21;    // 16bit
    int16_t c30;    // 16bit
    int16_t c31;    // 12bit
    int16_t c40;    // 12bit
} calibrationCoefficients_t;

typedef struct {
    calibrationCoefficients_t   calib;
    float                       pressure;       // Pa
    float                       temperature;    // DegC
} baroState_t;

static baroState_t  baroState;

#define busReadBuf busReadRegisterBuffer
#define busWrite   busWriteRegister

static uint8_t buf[6];
static uint8_t chipId[1];

// Helper functions
static uint8_t registerRead(const busDevice_t *dev, uint8_t reg)
{
    return busReadRegister(dev, reg);
}

static void registerWrite(const busDevice_t *dev, uint8_t reg, uint8_t value)
{
    busWriteRegister(dev, reg, value);
}

static void registerSetBits(const busDevice_t *dev, uint8_t reg, uint8_t setbits)
{
    uint8_t val = registerRead(dev, reg);

    if ((val & setbits) != setbits) {
        val |= setbits;
        registerWrite(dev, reg, val);
    }
}

static int32_t getTwosComplement(uint32_t raw, uint8_t length)
{
    if (raw & ((int)1 << (length - 1))) {
        return ((int32_t)raw) - ((int32_t)1 << length);
    }
    else {
        return raw;
    }
}





static void dps310GetUP(baroDev_t *baro)
{
    busDevice_t *dev = &baro->busdev;
    uint8_t buf[3];
    if (!busReadRegisterBuffer(dev, DPS310_REG_PSR_B2, buf, 3)) {
        return;
    }
    baro->up = (int32_t)buf[0] << 16 | (int32_t)buf[1] << 8 | (int32_t)buf[2];
}

static void dps310Calculate(int32_t *pressure, int32_t *temperature)
{
    // Implement calculation logic here
    // This will involve using the raw pressure and temperature values read from the sensor
    // and applying the calibration coefficients.
    // For now, I'll just set them to dummy values.
    *pressure = 0;
    *temperature = 0;
}

// DPS310 driver functions



#define DETECTION_MAX_RETRY_COUNT   5
static bool deviceDetect(const busDevice_t *dev)
{
    for (int retry = 0; retry < DETECTION_MAX_RETRY_COUNT; retry++) {
        delay(100);

        bool ack = busReadRegisterBuffer(dev, DPS310_REG_ID, chipId, 1);

        if (ack && (chipId[0] == DPS310_ID_REV_AND_PROD_ID || chipId[0] == SPL07_003_CHIP_ID)) {
            return true;
        }
    };

    return false;
}

static void dps310StartUT(baroDev_t *baro)
{
    busDevice_t *dev = &baro->busdev;
    registerSetBits(dev, DPS310_REG_MEAS_CFG, DPS310_MEAS_CFG_TMP_RDY);
}



static void dps310GetUT(baroDev_t *baro)
{
    busDevice_t *dev = &baro->busdev;
    uint8_t buf[3];
    if (!busReadRegisterBuffer(dev, DPS310_REG_TMP_B2, buf, 3)) {
        return;
    }
    baro->ut = (int32_t)buf[0] << 16 | (int32_t)buf[1] << 8 | (int32_t)buf[2];
}

static void dps310StartUP(baroDev_t *baro)
{
    busDevice_t *dev = &baro->busdev;
    registerSetBits(dev, DPS310_REG_MEAS_CFG, DPS310_MEAS_CFG_PRS_RDY);
}

static void deviceInit(const busDevice_t *dev, resourceOwner_e owner)
{
#ifdef USE_BARO_SPI_DPS310
    if (dev->bustype == BUSTYPE_SPI) {
        IOHi(dev->busdev_u.spi.csnPin); // Disable
        IOInit(dev->busdev_u.spi.csnPin, owner, 0);
        IOConfigGPIO(dev->busdev_u.spi.csnPin, IOCFG_OUT_PP);
        spiSetDivisor(dev->busdev_u.spi.instance, spiCalculateDivider(DPS310_MAX_SPI_CLK_HZ));
    }
#else
    UNUSED(dev);
    UNUSED(owner);
#endif
}

static void deviceDeInit(const busDevice_t *dev)
{
#ifdef USE_BARO_SPI_DPS310
    if (dev->bustype == BUSTYPE_SPI) {
        IOConfigGPIO(dev->busdev_u.spi.csnPin, IOCFG_IPU);
    }
#else
    UNUSED(dev);
#endif
}

bool baroDPS310Detect(baroDev_t *baro)
{
    busDevice_t *dev = &baro->busdev;

    deviceInit(dev, OWNER_BARO_CS);

    if ((dev->bustype == BUSTYPE_I2C) && (dev->busdev_u.i2c.address == 0)) {
        dev->busdev_u.i2c.address = DPS310_I2C_ADDR;
    }

    if (!deviceDetect(dev)) {
        deviceDeInit(dev);
        return false;
    }

    // busDeviceRegister(dev); // This function is not present in EmuFlight's bus.h

    baro->start_ut = dps310StartUT;
    baro->get_ut = dps310GetUT;

    baro->start_up = dps310StartUP;
    baro->get_up = dps310GetUP;

    baro->calculate = dps310Calculate;

    return true;
}

#endif
