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

// Source: betaflight/config configs/NEBD/LIONBEE_V1_DEV/config.c. Runs on a config reset only.
void targetConfiguration(void)
{
#ifdef USE_LED_STRIP
    // Four GPS-status LEDs on one row.
    parseLedStripConfig(0, "0,0::G:0");
    parseLedStripConfig(1, "1,0::G:0");
    parseLedStripConfig(2, "2,0::G:0");
    parseLedStripConfig(3, "3,0::G:0");
#endif
#ifdef USE_GPS
    gpsConfigMutable()->gps_ublox_use_galileo = true;
#endif
    // Not supported: the reference seeds a VTX table (5 bands, 8 channels, power values 0, 1, 2 = RTC6705 external codes 00, 01, 10)
    // and sets vtxSettingsConfigMutable()->power = 3 and lowPowerDisarm = VTX_LOW_POWER_DISARM_ALWAYS. EF has no VTX table.
}
#endif
