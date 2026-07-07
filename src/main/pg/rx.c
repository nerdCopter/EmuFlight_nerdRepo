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

#include "platform.h"

#if defined(USE_PWM) || defined(USE_PPM) || defined(USE_SERIAL_RX) || defined(USE_RX_MSP) || defined(USE_RX_SPI)

#include "pg/pg.h"
#include "pg/pg_ids.h"
#include "rx.h"

#include "config/config_reset.h"

#include "drivers/io.h"
#include "fc/fc_rc.h"
#include "fc/rc_controls.h"
#include "rx/rx.h"
#include "rx/rx_spi.h"

// PG bump to 5: this branch removes rc_smoothing_1euro_stage2 (the toggle field is gone —
// 1EURO/2EURO are now independent peer enum values instead), so its rxConfig_t layout is
// byte-identical to feat/2euro/feat/1euro-single/feat/1euro-double's (all at version 3). It does
// NOT share version 3 with them despite the identical layout, because enum value 3 means
// something different here (2EURO, was PT3 on the others) — sharing a version number would let
// a pilot's stored rc_smoothing_input_type=3 (PT3) silently get reinterpreted as 2EURO after
// switching branches, since pgLoad() only checks byte layout/size, not per-field semantics (see
// pg.c). Version 5 forces a full reset instead. Distinct from feat/1euro-stage2's 4 (different
// layout — that branch still has the toggle field). If this branch is ever chosen as the actual
// merge candidate, renormalize to master's-current-value+1 as the final pre-merge step.
PG_REGISTER_WITH_RESET_FN(rxConfig_t, rxConfig, PG_RX_CONFIG, 5);
void pgResetFn_rxConfig(rxConfig_t *rxConfig) {
    RESET_CONFIG_2(rxConfig_t, rxConfig,
                   .halfDuplex = 0,
                   .serialrx_provider = SERIALRX_PROVIDER,
                   .serialrx_inverted = 0,
                   .spektrum_bind_pin_override_ioTag = IO_TAG(SPEKTRUM_BIND_PIN),
                   .spektrum_bind_plug_ioTag = IO_TAG(BINDPLUG_PIN),
                   .spektrum_sat_bind = 0,
                   .spektrum_sat_bind_autoreset = 1,
                   .midrc = RX_MID_USEC,
                   .mincheck = 1050,
                   .maxcheck = 1900,
                   .rx_min_usec = RX_MIN_USEC,          // any of first 4 channels below this value will trigger rx loss detection
                   .rx_max_usec = RX_MAX_USEC,         // any of first 4 channels above this value will trigger rx loss detection
                   .rssi_src_frame_errors = false,
                   .rssi_channel = 0,
                   .rssi_scale = RSSI_SCALE_DEFAULT,
                   .rssi_offset = 0,
                   .rssi_invert = 0,
                   .rcInterpolation = RC_SMOOTHING_MANUAL,
                   .rcInterpolationChannels = INTERPOLATION_CHANNELS_RPYT,
                   .rcInterpolationInterval = 21,
                   .fpvCamAngleDegrees = 0,
                   .cinematicYaw = 0,
                   .airModeActivateThreshold = 32,
                   .max_aux_channel = DEFAULT_AUX_CHANNEL_COUNT,
                   // TESTING DEFAULT — DO NOT MERGE: rc_smoothing_type/input_type below are set for
                   // 2EURO flight validation on this design-variant branch. Mergeable defaults are
                   // rc_smoothing_type = RC_SMOOTHING_TYPE_INTERPOLATION and
                   // rc_smoothing_input_type = RC_SMOOTHING_INPUT_PT2 (see CONTEXT_2euro.md).
                   .rc_smoothing_type = RC_SMOOTHING_TYPE_FILTER,
                   .rc_smoothing_input_cutoff = 50,      // automatically calculate the cutoff by default
                   .rc_smoothing_debug_axis = ROLL,     // default to debug logging for the roll axis
                   .rc_smoothing_input_type = RC_SMOOTHING_INPUT_2EURO,
                   .showAlteredRc = 0,
                   .sbus_baud_fast = false,
                  );
#ifdef RX_CHANNELS_TAER
    parseRcChannels("TAER1234", rxConfig);
#else
    parseRcChannels("AETR1234", rxConfig);
#endif
}

#endif
