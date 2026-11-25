/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>

#include "platform.h"

#ifdef USE_TARGET_CONFIG

#include "io/gps.h"
#include "io/ledstrip.h"
#include "io/serial.h"
#include "io/vtx.h"

#include "drivers/vtx_rtc6705.h"

#include "config_helper.h"

static targetSerialPortFunction_t targetSerialPortFunction[] = {
    { SERIAL_PORT_UART5, FUNCTION_GPS },
};

// Source: betaflight/config configs/NEBD/LIONBEE_V1_DEV/config.c. Runs on a config reset only.
void targetConfiguration(void)
{
    targetSerialPortFunctionConfig(targetSerialPortFunction, ARRAYLEN(targetSerialPortFunction));
#ifdef USE_LED_STRIP
    // Four GPS-status LEDs on one row.
    parseLedStripConfig(0, "0,0::G:0");
    parseLedStripConfig(1, "1,0::G:0");
    parseLedStripConfig(2, "2,0::G:0");
    parseLedStripConfig(3, "3,0::G:0");
#endif
#ifdef USE_GPS
    gpsConfigMutable()->provider = GPS_UBLOX;
    gpsConfigMutable()->gps_ublox_use_galileo = true;
#endif
#ifdef USE_VTX_RTC6705
    // Maximum power while armed, lowest power while disarmed.
    vtxSettingsConfigMutable()->power = VTX_RTC6705_POWER_COUNT - 1;
    vtxSettingsConfigMutable()->lowPowerDisarm = 1;
#endif
    // Not supported: the reference VTX table (5 bands, 8 channels). EF has no VTX table.
}
#endif
