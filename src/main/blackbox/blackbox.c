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
#include <string.h>
#include <math.h>

#include "platform.h"

#ifdef USE_BLACKBOX

#include "blackbox.h"
#include "blackbox_encoding.h"
#include "blackbox_fielddefs.h"
#include "blackbox_io.h"

#include "build/build_config.h"
#include "build/debug.h"
#include "build/version.h"

#include "common/axis.h"
#include "common/encoding.h"
#include "common/maths.h"
#include "common/time.h"
#include "common/utils.h"

#include "config/feature.h"
#include "pg/pg.h"
#include "pg/pg_ids.h"
#include "pg/rx.h"

#include "drivers/compass/compass.h"
#include "drivers/sensor.h"
#include "drivers/time.h"

#include "fc/config.h"
#include "fc/controlrate_profile.h"
#include "fc/fc_rc.h"
#include "fc/rc_controls.h"
#include "fc/rc_modes.h"
#include "fc/runtime_config.h"

#include "flight/failsafe.h"
#include "flight/mixer.h"
#include "flight/pid.h"
#include "flight/servos.h"

#include "io/beeper.h"
#include "io/gps.h"
#include "io/serial.h"

#include "rx/rx.h"

#include "sensors/acceleration.h"
#include "sensors/barometer.h"
#include "sensors/battery.h"
#include "sensors/compass.h"
#include "sensors/gyro.h"
#include "sensors/rangefinder.h"

#ifdef USE_GYRO_IMUF9001
#include "drivers/accgyro/accgyro_imuf9001.h"
#endif

#if defined(ENABLE_BLACKBOX_LOGGING_ON_SPIFLASH_BY_DEFAULT)
#define DEFAULT_BLACKBOX_DEVICE     BLACKBOX_DEVICE_FLASH
#elif defined(ENABLE_BLACKBOX_LOGGING_ON_SDCARD_BY_DEFAULT)
#define DEFAULT_BLACKBOX_DEVICE     BLACKBOX_DEVICE_SDCARD
#else
#define DEFAULT_BLACKBOX_DEVICE     BLACKBOX_DEVICE_SERIAL
#endif

PG_REGISTER_WITH_RESET_TEMPLATE(blackboxConfig_t, blackboxConfig, PG_BLACKBOX_CONFIG, 1);

PG_RESET_TEMPLATE(blackboxConfig_t, blackboxConfig,
                  .p_ratio = 48,
                  .device = DEFAULT_BLACKBOX_DEVICE,
                  .record_acc = 1,
                  .mode = BLACKBOX_MODE_NORMAL
                 );

#define BLACKBOX_SHUTDOWN_TIMEOUT_MILLIS 200

// Some macros to make writing FLIGHT_LOG_FIELD_* constants shorter:

#define PREDICT(x) CONCAT(FLIGHT_LOG_FIELD_PREDICTOR_, x)
#define ENCODING(x) CONCAT(FLIGHT_LOG_FIELD_ENCODING_, x)
#define CONDITION(x) CONCAT(FLIGHT_LOG_FIELD_CONDITION_, x)
#define UNSIGNED FLIGHT_LOG_FIELD_UNSIGNED
#define SIGNED FLIGHT_LOG_FIELD_SIGNED

static const char blackboxHeader[] =
    "H Product:Blackbox flight data recorder by Nicholas Sherlock\n"
    "H Data version:2\n";

static const char* const blackboxFieldHeaderNames[] = {
    "name",
    "signed",
    "predictor",
    "encoding",
    "predictor",
    "encoding"
};

/* All field definition structs should look like this (but with longer arrs): */
typedef struct blackboxFieldDefinition_s {
    const char *name;
    // If the field name has a number to be included in square brackets [1] afterwards, set it here, or -1 for no brackets:
    int8_t fieldNameIndex;

    // Each member of this array will be the value to print for this field for the given header index
    uint8_t arr[1];
} blackboxFieldDefinition_t;

#define BLACKBOX_DELTA_FIELD_HEADER_COUNT       ARRAYLEN(blackboxFieldHeaderNames)
#define BLACKBOX_SIMPLE_FIELD_HEADER_COUNT      (BLACKBOX_DELTA_FIELD_HEADER_COUNT - 2)
#define BLACKBOX_CONDITIONAL_FIELD_HEADER_COUNT (BLACKBOX_DELTA_FIELD_HEADER_COUNT - 2)

typedef struct blackboxSimpleFieldDefinition_s {
    const char *name;
    int8_t fieldNameIndex;

    uint8_t isSigned;
    uint8_t predict;
    uint8_t encode;
} blackboxSimpleFieldDefinition_t;

typedef struct blackboxConditionalFieldDefinition_s {
    const char *name;
    int8_t fieldNameIndex;

    uint8_t isSigned;
    uint8_t predict;
    uint8_t encode;
    uint8_t condition; // Decide whether this field should appear in the log
} blackboxConditionalFieldDefinition_t;

typedef struct blackboxDeltaFieldDefinition_s {
    const char *name;
    int8_t fieldNameIndex;

    uint8_t isSigned;
    uint8_t Ipredict;
    uint8_t Iencode;
    uint8_t Ppredict;
    uint8_t Pencode;
    uint8_t condition; // Decide whether this field should appear in the log
} blackboxDeltaFieldDefinition_t;

/**
 * Description of the blackbox fields we are writing in our main intra (I) and inter (P) frames. This description is
 * written into the flight log header so the log can be properly interpreted (but these definitions don't actually cause
 * the encoding to happen, we have to encode the flight log ourselves in write{Inter|Intra}frame() in a way that matches
 * the encoding we've promised here).
 */
static const blackboxDeltaFieldDefinition_t blackboxMainFields[] = {
    /* loopIteration doesn't appear in P frames since it always increments */
    {"loopIteration", -1, UNSIGNED, .Ipredict = PREDICT(0),     .Iencode = ENCODING(UNSIGNED_VB), .Ppredict = PREDICT(INC),           .Pencode = FLIGHT_LOG_FIELD_ENCODING_NULL, CONDITION(ALWAYS)},
    /* Time advances pretty steadily so the P-frame prediction is a straight line */
    {"time",       -1, UNSIGNED, .Ipredict = PREDICT(0),       .Iencode = ENCODING(UNSIGNED_VB), .Ppredict = PREDICT(STRAIGHT_LINE), .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"axisP",       0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"axisP",       1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"axisP",       2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    /* I terms get special packed encoding in P frames: */
    {"axisI",       0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG2_3S32), CONDITION(ALWAYS)},
    {"axisI",       1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG2_3S32), CONDITION(ALWAYS)},
    {"axisI",       2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG2_3S32), CONDITION(ALWAYS)},
    {"axisD",       0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(NONZERO_PID_D_0)},
    {"axisD",       1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(NONZERO_PID_D_1)},
    {"axisD",       2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(NONZERO_PID_D_2)},
    {"axisF",       0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"axisF",       1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"axisF",       2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    /* rcCommands are encoded together as a group in P-frames: */
    {"rcCommand",   0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},
    {"rcCommand",   1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},
    {"rcCommand",   2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},
    /* Throttle is always in the range [minthrottle..maxthrottle]: */
    {"rcCommand",   3, UNSIGNED, .Ipredict = PREDICT(MINTHROTTLE), .Iencode = ENCODING(UNSIGNED_VB), .Ppredict = PREDICT(PREVIOUS),  .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},

    // setpoint - define 4 fields like rcCommand to use the same encoding. setpoint[4] contains the mixer throttle
    {"setpoint",    0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},
    {"setpoint",    1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},
    {"setpoint",    2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},
    {"setpoint",    3, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_4S16), CONDITION(ALWAYS)},

    {"vbatLatest",    -1, UNSIGNED, .Ipredict = PREDICT(VBATREF),  .Iencode = ENCODING(NEG_14BIT),   .Ppredict = PREDICT(PREVIOUS),  .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_VBAT},
    {"amperageLatest", -1, SIGNED,   .Ipredict = PREDICT(0),        .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),  .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_AMPERAGE_ADC},

#ifdef USE_MAG
    {"magADC",      0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_MAG},
    {"magADC",      1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_MAG},
    {"magADC",      2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_MAG},
#endif
#ifdef USE_BARO
    {"BaroAlt",    -1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_BARO},
#endif
#ifdef USE_RANGEFINDER
    {"surfaceRaw",   -1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_RANGEFINDER},
#endif
    {"rssi",       -1, UNSIGNED, .Ipredict = PREDICT(0),       .Iencode = ENCODING(UNSIGNED_VB), .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(TAG8_8SVB), FLIGHT_LOG_FIELD_CONDITION_RSSI},

    /* Gyros and accelerometers base their P-predictions on the average of the previous 2 frames to reduce noise impact */
    {"gyroADC",     0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"gyroADC",     1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"gyroADC",     2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(ALWAYS)},
    {"accSmooth",   0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), FLIGHT_LOG_FIELD_CONDITION_ACC},
    {"accSmooth",   1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), FLIGHT_LOG_FIELD_CONDITION_ACC},
    {"accSmooth",   2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), FLIGHT_LOG_FIELD_CONDITION_ACC},
    {"debug",       0, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), FLIGHT_LOG_FIELD_CONDITION_DEBUG},
    {"debug",       1, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), FLIGHT_LOG_FIELD_CONDITION_DEBUG},
    {"debug",       2, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), FLIGHT_LOG_FIELD_CONDITION_DEBUG},
    {"debug",       3, SIGNED,   .Ipredict = PREDICT(0),       .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), FLIGHT_LOG_FIELD_CONDITION_DEBUG},
    /* Motors only rarely drops under minthrottle (when stick falls below mincommand), so predict minthrottle for it and use *unsigned* encoding (which is large for negative numbers but more compact for positive ones): */
    {"motor",       0, UNSIGNED, .Ipredict = PREDICT(MINMOTOR), .Iencode = ENCODING(UNSIGNED_VB), .Ppredict = PREDICT(AVERAGE_2), .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_1)},
    /* Subsequent motors base their I-frame values on the first one, P-frame values on the average of last two frames: */
    {"motor",       1, UNSIGNED, .Ipredict = PREDICT(MOTOR_0), .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_2)},
    {"motor",       2, UNSIGNED, .Ipredict = PREDICT(MOTOR_0), .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_3)},
    {"motor",       3, UNSIGNED, .Ipredict = PREDICT(MOTOR_0), .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_4)},
    {"motor",       4, UNSIGNED, .Ipredict = PREDICT(MOTOR_0), .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_5)},
    {"motor",       5, UNSIGNED, .Ipredict = PREDICT(MOTOR_0), .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_6)},
    {"motor",       6, UNSIGNED, .Ipredict = PREDICT(MOTOR_0), .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_7)},
    {"motor",       7, UNSIGNED, .Ipredict = PREDICT(MOTOR_0), .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(AVERAGE_2),     .Pencode = ENCODING(SIGNED_VB), CONDITION(AT_LEAST_MOTORS_8)},

    /* Tricopter tail servo */
    {"servo",       5, UNSIGNED, .Ipredict = PREDICT(1500),    .Iencode = ENCODING(SIGNED_VB),   .Ppredict = PREDICT(PREVIOUS),      .Pencode = ENCODING(SIGNED_VB), CONDITION(TRICOPTER)}
};

#ifdef USE_GPS
// GPS position/vel frame
static const blackboxConditionalFieldDefinition_t blackboxGpsGFields[] = {
    {"time",              -1, UNSIGNED, PREDICT(LAST_MAIN_FRAME_TIME), ENCODING(UNSIGNED_VB), CONDITION(NOT_LOGGING_EVERY_FRAME)},
    {"GPS_numSat",        -1, UNSIGNED, PREDICT(0),          ENCODING(UNSIGNED_VB), CONDITION(ALWAYS)},
    {"GPS_coord",          0, SIGNED,   PREDICT(HOME_COORD), ENCODING(SIGNED_VB),   CONDITION(ALWAYS)},
    {"GPS_coord",          1, SIGNED,   PREDICT(HOME_COORD), ENCODING(SIGNED_VB),   CONDITION(ALWAYS)},
    {"GPS_altitude",      -1, UNSIGNED, PREDICT(0),          ENCODING(UNSIGNED_VB), CONDITION(ALWAYS)},
    {"GPS_speed",         -1, UNSIGNED, PREDICT(0),          ENCODING(UNSIGNED_VB), CONDITION(ALWAYS)},
    {"GPS_ground_course", -1, UNSIGNED, PREDICT(0),          ENCODING(UNSIGNED_VB), CONDITION(ALWAYS)}
};

// GPS home frame
static const blackboxSimpleFieldDefinition_t blackboxGpsHFields[] = {
    {"GPS_home",           0, SIGNED,   PREDICT(0),          ENCODING(SIGNED_VB)},
    {"GPS_home",           1, SIGNED,   PREDICT(0),          ENCODING(SIGNED_VB)}
};
#endif

// Rarely-updated fields
static const blackboxSimpleFieldDefinition_t blackboxSlowFields[] = {
    {"flightModeFlags",       -1, UNSIGNED, PREDICT(0),      ENCODING(UNSIGNED_VB)},
    {"stateFlags",            -1, UNSIGNED, PREDICT(0),      ENCODING(UNSIGNED_VB)},

    {"failsafePhase",         -1, UNSIGNED, PREDICT(0),      ENCODING(TAG2_3S32)},
    {"rxSignalReceived",      -1, UNSIGNED, PREDICT(0),      ENCODING(TAG2_3S32)},
    {"rxFlightChannelsValid", -1, UNSIGNED, PREDICT(0),      ENCODING(TAG2_3S32)}
};

typedef enum BlackboxState {
    BLACKBOX_STATE_DISABLED = 0,
    BLACKBOX_STATE_STOPPED,
    BLACKBOX_STATE_PREPARE_LOG_FILE,
    BLACKBOX_STATE_SEND_HEADER,
    BLACKBOX_STATE_SEND_MAIN_FIELD_HEADER,
    BLACKBOX_STATE_SEND_GPS_H_HEADER,
    BLACKBOX_STATE_SEND_GPS_G_HEADER,
    BLACKBOX_STATE_SEND_SLOW_HEADER,
    BLACKBOX_STATE_SEND_SYSINFO,
    BLACKBOX_STATE_PAUSED,
    BLACKBOX_STATE_RUNNING,
    BLACKBOX_STATE_SHUTTING_DOWN,
    BLACKBOX_STATE_START_ERASE,
    BLACKBOX_STATE_ERASING,
    BLACKBOX_STATE_ERASED
} BlackboxState;


typedef struct blackboxMainState_s {
    uint32_t time;

    int32_t axisPID_P[XYZ_AXIS_COUNT];
    int32_t axisPID_I[XYZ_AXIS_COUNT];
    int32_t axisPID_D[XYZ_AXIS_COUNT];
    int32_t axisPID_F[XYZ_AXIS_COUNT];

    int16_t rcCommand[4];
    int16_t setpoint[4];
    int16_t gyroADC[XYZ_AXIS_COUNT];
    int16_t accADC[XYZ_AXIS_COUNT];
    int16_t debug[DEBUG16_VALUE_COUNT];
    int16_t motor[MAX_SUPPORTED_MOTORS];
    int16_t servo[MAX_SUPPORTED_SERVOS];

    uint16_t vbatLatest;
    int32_t amperageLatest;

#ifdef USE_BARO
    int32_t BaroAlt;
#endif
#ifdef USE_MAG
    int16_t magADC[XYZ_AXIS_COUNT];
#endif
#ifdef USE_RANGEFINDER
    int32_t surfaceRaw;
#endif
    uint16_t rssi;
} blackboxMainState_t;

typedef struct blackboxGpsState_s {
    int32_t GPS_home[2];
    int32_t GPS_coord[2];
    uint8_t GPS_numSat;
} blackboxGpsState_t;

// This data is updated really infrequently:
typedef struct blackboxSlowState_s {
    uint32_t flightModeFlags; // extend this data size (from uint16_t)
    uint8_t stateFlags;
    uint8_t failsafePhase;
    bool rxSignalReceived;
    bool rxFlightChannelsValid;
} __attribute__((__packed__)) blackboxSlowState_t; // We pack this struct so that padding doesn't interfere with memcmp()

//From rc_controls.c
extern boxBitmask_t rcModeActivationMask;

static BlackboxState blackboxState = BLACKBOX_STATE_DISABLED;

static uint32_t blackboxLastArmingBeep = 0;
static uint32_t blackboxLastFlightModeFlags = 0; // New event tracking of flight modes

static struct {
    uint32_t headerIndex;

    /* Since these fields are used during different blackbox states (never simultaneously) we can
     * overlap them to save on RAM
     */
    union {
        int fieldIndex;
        uint32_t startTime;
    } u;
} xmitState;

// Cache for FLIGHT_LOG_FIELD_CONDITION_* test results:
static uint32_t blackboxConditionCache;

STATIC_ASSERT((sizeof(blackboxConditionCache) * 8) >= FLIGHT_LOG_FIELD_CONDITION_LAST, too_many_flight_log_conditions);

static uint32_t blackboxIteration;
static uint16_t blackboxLoopIndex;
static uint16_t blackboxPFrameIndex;
static uint16_t blackboxIFrameIndex;
// number of flight loop iterations before logging I-frame
// typically 32 for 1kHz loop, 64 for 2kHz loop etc
STATIC_UNIT_TESTED int16_t blackboxIInterval = 0;
// number of flight loop iterations before logging P-frame
STATIC_UNIT_TESTED int16_t blackboxPInterval = 0;
STATIC_UNIT_TESTED int32_t blackboxSInterval = 0;
STATIC_UNIT_TESTED int32_t blackboxSlowFrameIterationTimer;
static bool blackboxLoggedAnyFrames;

/*
 * We store voltages in I-frames relative to this, which was the voltage when the blackbox was activated.
 * This helps out since the voltage is only expected to fall from that point and we can reduce our diffs
 * to encode:
 */
static uint16_t vbatReference;

static blackboxGpsState_t gpsHistory;
static blackboxSlowState_t slowHistory;

// Keep a history of length 2, plus a buffer for MW to store the new values into
static blackboxMainState_t blackboxHistoryRing[3];

// These point into blackboxHistoryRing, use them to know where to store history of a given age (0, 1 or 2 generations old)
static blackboxMainState_t* blackboxHistory[3];

static bool blackboxModeActivationConditionPresent = false;

/**
 * Return true if it is safe to edit the Blackbox configuration.
 */
bool blackboxMayEditConfig(void) {
    return blackboxState <= BLACKBOX_STATE_STOPPED;
}

static bool blackboxIsOnlyLoggingIntraframes(void) {
    return blackboxConfig()->p_ratio == 0;
}

static bool testBlackboxConditionUncached(FlightLogFieldCondition condition) {
    switch (condition) {
    case FLIGHT_LOG_FIELD_CONDITION_ALWAYS:
        return true;
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_1:
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_2:
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_3:
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_4:
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_5:
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_6:
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_7:
    case FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_8:
        return getMotorCount() >= condition - FLIGHT_LOG_FIELD_CONDITION_AT_LEAST_MOTORS_1 + 1;
    case FLIGHT_LOG_FIELD_CONDITION_TRICOPTER:
        return mixerConfig()->mixerMode == MIXER_TRI || mixerConfig()->mixerMode == MIXER_CUSTOM_TRI;
    case FLIGHT_LOG_FIELD_CONDITION_NONZERO_PID_D_0:
    case FLIGHT_LOG_FIELD_CONDITION_NONZERO_PID_D_1:
    case FLIGHT_LOG_FIELD_CONDITION_NONZERO_PID_D_2:
        return currentPidProfile->pid[condition - FLIGHT_LOG_FIELD_CONDITION_NONZERO_PID_D_0].D != 0;
    case FLIGHT_LOG_FIELD_CONDITION_MAG:
#ifdef USE_MAG
        return sensors(SENSOR_MAG);
#else
        return false;
#endif
    case FLIGHT_LOG_FIELD_CONDITION_BARO:
#ifdef USE_BARO
        return sensors(SENSOR_BARO);
#else
        return false;
#endif
    case FLIGHT_LOG_FIELD_CONDITION_VBAT:
        return batteryConfig()->voltageMeterSource != VOLTAGE_METER_NONE;
    case FLIGHT_LOG_FIELD_CONDITION_AMPERAGE_ADC:
        return (batteryConfig()->currentMeterSource != CURRENT_METER_NONE) && (batteryConfig()->currentMeterSource != CURRENT_METER_VIRTUAL);
    case FLIGHT_LOG_FIELD_CONDITION_RANGEFINDER:
#ifdef USE_RANGEFINDER
        return sensors(SENSOR_RANGEFINDER);
#else
        return false;
#endif
    case FLIGHT_LOG_FIELD_CONDITION_RSSI:
        return isRssiConfigured();
    case FLIGHT_LOG_FIELD_CONDITION_NOT_LOGGING_EVERY_FRAME:
        return blackboxConfig()->p_ratio != 1;
    case FLIGHT_LOG_FIELD_CONDITION_ACC:
        return sensors(SENSOR_ACC) && blackboxConfig()->record_acc;
    case FLIGHT_LOG_FIELD_CONDITION_DEBUG:
        return debugMode != DEBUG_NONE;
    case FLIGHT_LOG_FIELD_CONDITION_NEVER:
        return false;
    default:
        return false;
    }
}

static void blackboxBuildConditionCache(void) {
    blackboxConditionCache = 0;
    for (FlightLogFieldCondition cond = FLIGHT_LOG_FIELD_CONDITION_FIRST; cond <= FLIGHT_LOG_FIELD_CONDITION_LAST; cond++) {
        if (testBlackboxConditionUncached(cond)) {
            blackboxConditionCache |= 1 << cond;
        }
    }
}

static bool testBlackboxCondition(FlightLogFieldCondition condition) {
    return (blackboxConditionCache & (1 << condition)) != 0;
}

static void blackboxSetState(BlackboxState newState) {
    //Perform initial setup required for the new state
    switch (newState) {
    case BLACKBOX_STATE_PREPARE_LOG_FILE:
        blackboxLoggedAnyFrames = false;
        break;
    case BLACKBOX_STATE_SEND_HEADER:
        blackboxHeaderBudget = 0;
        xmitState.headerIndex = 0;
        xmitState.u.startTime = millis();
        break;
    case BLACKBOX_STATE_SEND_MAIN_FIELD_HEADER:
    case BLACKBOX_STATE_SEND_GPS_G_HEADER:
    case BLACKBOX_STATE_SEND_GPS_H_HEADER:
    case BLACKBOX_STATE_SEND_SLOW_HEADER:
        xmitState.headerIndex = 0;
        xmitState.u.fieldIndex = -1;
        break;
    case BLACKBOX_STATE_SEND_SYSINFO:
        xmitState.headerIndex = 0;
        break;
    case BLACKBOX_STATE_RUNNING:
        blackboxSlowFrameIterationTimer = blackboxSInterval; //Force a slow frame to be written on the first iteration
        break;
    case BLACKBOX_STATE_SHUTTING_DOWN:
        xmitState.u.startTime = millis();
        break;
    default:
        ;
    }
    blackboxState = newState;
}

static void writeIntraframe(void) {
    blackboxMainState_t *blackboxCurrent = blackboxHistory[0];
    blackboxWrite('I');
    blackboxWriteUnsignedVB(blackboxIteration);
    blackboxWriteUnsignedVB(blackboxCurrent->time);
    blackboxWriteSignedVBArray(blackboxCurrent->axisPID_P, XYZ_AXIS_COUNT);
    blackboxWriteSignedVBArray(blackboxCurrent->axisPID_I, XYZ_AXIS_COUNT);
    // Don't bother writing the current D term if the corresponding PID setting is zero
    for (int x = 0; x < XYZ_AXIS_COUNT; x++) {
        if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_NONZERO_PID_D_0 + x)) {
            blackboxWriteSignedVB(blackboxCurrent->axisPID_D[x]);
        }
    }
    blackboxWriteSignedVBArray(blackboxCurrent->axisPID_F, XYZ_AXIS_COUNT);
    // Write roll, pitch and yaw first:
    blackboxWriteSigned16VBArray(blackboxCurrent->rcCommand, 3);
    /*
     * Write the throttle separately from the rest of the RC data so we can apply a predictor to it.
     * Throttle lies in range [minthrottle..maxthrottle]:
     */
    blackboxWriteUnsignedVB(blackboxCurrent->rcCommand[THROTTLE] - motorConfig()->minthrottle);
    // Write setpoint roll, pitch, yaw, and throttle
    blackboxWriteSigned16VBArray(blackboxCurrent->setpoint, 4);
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_VBAT)) {
        /*
         * Our voltage is expected to decrease over the course of the flight, so store our difference from
         * the reference:
         *
         * Write 14 bits even if the number is negative (which would otherwise result in 32 bits)
         */
        blackboxWriteUnsignedVB((vbatReference - blackboxCurrent->vbatLatest) & 0x3FFF);
    }
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_AMPERAGE_ADC)) {
        // 12bit value directly from ADC
        blackboxWriteSignedVB(blackboxCurrent->amperageLatest);
    }
#ifdef USE_MAG
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_MAG)) {
        blackboxWriteSigned16VBArray(blackboxCurrent->magADC, XYZ_AXIS_COUNT);
    }
#endif
#ifdef USE_BARO
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_BARO)) {
        blackboxWriteSignedVB(blackboxCurrent->BaroAlt);
    }
#endif
#ifdef USE_RANGEFINDER
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_RANGEFINDER)) {
        blackboxWriteSignedVB(blackboxCurrent->surfaceRaw);
    }
#endif
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_RSSI)) {
        blackboxWriteUnsignedVB(blackboxCurrent->rssi);
    }
    blackboxWriteSigned16VBArray(blackboxCurrent->gyroADC, XYZ_AXIS_COUNT);
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_ACC)) {
        blackboxWriteSigned16VBArray(blackboxCurrent->accADC, XYZ_AXIS_COUNT);
    }
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_DEBUG)) {
        blackboxWriteSigned16VBArray(blackboxCurrent->debug, DEBUG16_VALUE_COUNT);
    }
    //Motors can be below minimum output when disarmed, but that doesn't happen much
    blackboxWriteUnsignedVB(blackboxCurrent->motor[0] - motorOutputLow);
    //Motors tend to be similar to each other so use the first motor's value as a predictor of the others
    const int motorCount = getMotorCount();
    for (int x = 1; x < motorCount; x++) {
        blackboxWriteSignedVB(blackboxCurrent->motor[x] - blackboxCurrent->motor[0]);
    }
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_TRICOPTER)) {
        //Assume the tail spends most of its time around the center
        blackboxWriteSignedVB(blackboxCurrent->servo[5] - 1500);
    }
    //Rotate our history buffers:
    //The current state becomes the new "before" state
    blackboxHistory[1] = blackboxHistory[0];
    //And since we have no other history, we also use it for the "before, before" state
    blackboxHistory[2] = blackboxHistory[0];
    //And advance the current state over to a blank space ready to be filled
    blackboxHistory[0] = ((blackboxHistory[0] - blackboxHistoryRing + 1) % 3) + blackboxHistoryRing;
    blackboxLoggedAnyFrames = true;
}

static void blackboxWriteMainStateArrayUsingAveragePredictor(int arrOffsetInHistory, int count) {
    int16_t *curr  = (int16_t*) ((char*) (blackboxHistory[0]) + arrOffsetInHistory);
    int16_t *prev1 = (int16_t*) ((char*) (blackboxHistory[1]) + arrOffsetInHistory);
    int16_t *prev2 = (int16_t*) ((char*) (blackboxHistory[2]) + arrOffsetInHistory);
    for (int i = 0; i < count; i++) {
        // Predictor is the average of the previous two history states
        int32_t predictor = (prev1[i] + prev2[i]) / 2;
        blackboxWriteSignedVB(curr[i] - predictor);
    }
}

static void writeInterframe(void) {
    blackboxMainState_t *blackboxCurrent = blackboxHistory[0];
    blackboxMainState_t *blackboxLast = blackboxHistory[1];
    blackboxWrite('P');
    //No need to store iteration count since its delta is always 1
    /*
     * Since the difference between the difference between successive times will be nearly zero (due to consistent
     * looptime spacing), use second-order differences.
     */
    blackboxWriteSignedVB((int32_t) (blackboxHistory[0]->time - 2 * blackboxHistory[1]->time + blackboxHistory[2]->time));
    int32_t deltas[8];
    int32_t setpointDeltas[4];
    arraySubInt32(deltas, blackboxCurrent->axisPID_P, blackboxLast->axisPID_P, XYZ_AXIS_COUNT);
    blackboxWriteSignedVBArray(deltas, XYZ_AXIS_COUNT);
    /*
     * The PID I field changes very slowly, most of the time +-2, so use an encoding
     * that can pack all three fields into one byte in that situation.
     */
    arraySubInt32(deltas, blackboxCurrent->axisPID_I, blackboxLast->axisPID_I, XYZ_AXIS_COUNT);
    blackboxWriteTag2_3S32(deltas);
    /*
     * The PID D term is frequently set to zero for yaw, which makes the result from the calculation
     * always zero. So don't bother recording D results when PID D terms are zero.
     */
    for (int x = 0; x < XYZ_AXIS_COUNT; x++) {
        if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_NONZERO_PID_D_0 + x)) {
            blackboxWriteSignedVB(blackboxCurrent->axisPID_D[x] - blackboxLast->axisPID_D[x]);
        }
    }
    arraySubInt32(deltas, blackboxCurrent->axisPID_F, blackboxLast->axisPID_F, XYZ_AXIS_COUNT);
    blackboxWriteSignedVBArray(deltas, XYZ_AXIS_COUNT);
    /*
     * RC tends to stay the same or fairly small for many frames at a time, so use an encoding that
     * can pack multiple values per byte:
     */
    for (int x = 0; x < 4; x++) {
        deltas[x] = blackboxCurrent->rcCommand[x] - blackboxLast->rcCommand[x];
        setpointDeltas[x] = blackboxCurrent->setpoint[x] - blackboxLast->setpoint[x];
    }
    blackboxWriteTag8_4S16(deltas);
    blackboxWriteTag8_4S16(setpointDeltas);
    //Check for sensors that are updated periodically (so deltas are normally zero)
    int optionalFieldCount = 0;
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_VBAT)) {
        deltas[optionalFieldCount++] = (int32_t) blackboxCurrent->vbatLatest - blackboxLast->vbatLatest;
    }
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_AMPERAGE_ADC)) {
        deltas[optionalFieldCount++] = blackboxCurrent->amperageLatest - blackboxLast->amperageLatest;
    }
#ifdef USE_MAG
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_MAG)) {
        for (int x = 0; x < XYZ_AXIS_COUNT; x++) {
            deltas[optionalFieldCount++] = blackboxCurrent->magADC[x] - blackboxLast->magADC[x];
        }
    }
#endif
#ifdef USE_BARO
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_BARO)) {
        deltas[optionalFieldCount++] = blackboxCurrent->BaroAlt - blackboxLast->BaroAlt;
    }
#endif
#ifdef USE_RANGEFINDER
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_RANGEFINDER)) {
        deltas[optionalFieldCount++] = blackboxCurrent->surfaceRaw - blackboxLast->surfaceRaw;
    }
#endif
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_RSSI)) {
        deltas[optionalFieldCount++] = (int32_t) blackboxCurrent->rssi - blackboxLast->rssi;
    }
    blackboxWriteTag8_8SVB(deltas, optionalFieldCount);
    //Since gyros, accs and motors are noisy, base their predictions on the average of the history:
    blackboxWriteMainStateArrayUsingAveragePredictor(offsetof(blackboxMainState_t, gyroADC),   XYZ_AXIS_COUNT);
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_ACC)) {
        blackboxWriteMainStateArrayUsingAveragePredictor(offsetof(blackboxMainState_t, accADC), XYZ_AXIS_COUNT);
    }
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_DEBUG)) {
        blackboxWriteMainStateArrayUsingAveragePredictor(offsetof(blackboxMainState_t, debug), DEBUG16_VALUE_COUNT);
    }
    blackboxWriteMainStateArrayUsingAveragePredictor(offsetof(blackboxMainState_t, motor),     getMotorCount());
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_TRICOPTER)) {
        blackboxWriteSignedVB(blackboxCurrent->servo[5] - blackboxLast->servo[5]);
    }
    //Rotate our history buffers
    blackboxHistory[2] = blackboxHistory[1];
    blackboxHistory[1] = blackboxHistory[0];
    blackboxHistory[0] = ((blackboxHistory[0] - blackboxHistoryRing + 1) % 3) + blackboxHistoryRing;
    blackboxLoggedAnyFrames = true;
}

/* Write the contents of the global "slowHistory" to the log as an "S" frame. Because this data is logged so
 * infrequently, delta updates are not reasonable, so we log independent frames. */
static void writeSlowFrame(void) {
    int32_t values[3];
    blackboxWrite('S');
    blackboxWriteUnsignedVB(slowHistory.flightModeFlags);
    blackboxWriteUnsignedVB(slowHistory.stateFlags);
    /*
     * Most of the time these three values will be able to pack into one byte for us:
     */
    values[0] = slowHistory.failsafePhase;
    values[1] = slowHistory.rxSignalReceived ? 1 : 0;
    values[2] = slowHistory.rxFlightChannelsValid ? 1 : 0;
    blackboxWriteTag2_3S32(values);
    blackboxSlowFrameIterationTimer = 0;
}

/**
 * Load rarely-changing values from the FC into the given structure
 */
static void loadSlowState(blackboxSlowState_t *slow) {
    memcpy(&slow->flightModeFlags, &rcModeActivationMask, sizeof(slow->flightModeFlags)); //was flightModeFlags;
    slow->stateFlags = stateFlags;
    slow->failsafePhase = failsafePhase();
    slow->rxSignalReceived = rxIsReceivingSignal();
    slow->rxFlightChannelsValid = rxAreFlightChannelsValid();
}

/**
 * If the data in the slow frame has changed, log a slow frame.
 *
 * If allowPeriodicWrite is true, the frame is also logged if it has been more than blackboxSInterval logging iterations
 * since the field was last logged.
 */
STATIC_UNIT_TESTED bool writeSlowFrameIfNeeded(void) {
    // Write the slow frame peridocially so it can be recovered if we ever lose sync
    bool shouldWrite = blackboxSlowFrameIterationTimer >= blackboxSInterval;
    if (shouldWrite) {
        loadSlowState(&slowHistory);
    } else {
        blackboxSlowState_t newSlowState;
        loadSlowState(&newSlowState);
        // Only write a slow frame if it was different from the previous state
        if (memcmp(&newSlowState, &slowHistory, sizeof(slowHistory)) != 0) {
            // Use the new state as our new history
            memcpy(&slowHistory, &newSlowState, sizeof(slowHistory));
            shouldWrite = true;
        }
    }
    if (shouldWrite) {
        writeSlowFrame();
    }
    return shouldWrite;
}

void blackboxValidateConfig(void) {
    // If we've chosen an unsupported device, change the device to serial
    switch (blackboxConfig()->device) {
#ifdef USE_FLASHFS
    case BLACKBOX_DEVICE_FLASH:
#endif
#ifdef USE_SDCARD
    case BLACKBOX_DEVICE_SDCARD:
#endif
    case BLACKBOX_DEVICE_SERIAL:
        // Device supported, leave the setting alone
        break;
    default:
        blackboxConfigMutable()->device = BLACKBOX_DEVICE_SERIAL;
    }
}

static void blackboxResetIterationTimers(void) {
    blackboxIteration = 0;
    blackboxLoopIndex = 0;
    blackboxIFrameIndex = 0;
    blackboxPFrameIndex = 0;
    blackboxSlowFrameIterationTimer = 0;
}

/**
 * Start Blackbox logging if it is not already running. Intended to be called upon arming.
 */
static void blackboxStart(void) {
    blackboxValidateConfig();
    if (!blackboxDeviceOpen()) {
        blackboxSetState(BLACKBOX_STATE_DISABLED);
        return;
    }
    memset(&gpsHistory, 0, sizeof(gpsHistory));
    blackboxHistory[0] = &blackboxHistoryRing[0];
    blackboxHistory[1] = &blackboxHistoryRing[1];
    blackboxHistory[2] = &blackboxHistoryRing[2];
    vbatReference = getBatteryVoltageLatest();
    //No need to clear the content of blackboxHistoryRing since our first frame will be an intra which overwrites it
    /*
     * We use conditional tests to decide whether or not certain fields should be logged. Since our headers
     * must always agree with the logged data, the results of these tests must not change during logging. So
     * cache those now.
     */
    blackboxBuildConditionCache();
    blackboxModeActivationConditionPresent = isModeActivationConditionPresent(BOXBLACKBOX);
    blackboxResetIterationTimers();
    /*
     * Record the beeper's current idea of the last arming beep time, so that we can detect it changing when
     * it finally plays the beep for this arming event.
     */
    blackboxLastArmingBeep = getArmingBeepTimeMicros();
    memcpy(&blackboxLastFlightModeFlags, &rcModeActivationMask, sizeof(blackboxLastFlightModeFlags)); // record startup status
    blackboxSetState(BLACKBOX_STATE_PREPARE_LOG_FILE);
}

/**
 * Begin Blackbox shutdown.
 */
void blackboxFinish(void) {
    switch (blackboxState) {
    case BLACKBOX_STATE_DISABLED:
    case BLACKBOX_STATE_STOPPED:
    case BLACKBOX_STATE_SHUTTING_DOWN:
        // We're already stopped/shutting down
        break;
    case BLACKBOX_STATE_RUNNING:
    case BLACKBOX_STATE_PAUSED:
        blackboxLogEvent(FLIGHT_LOG_EVENT_LOG_END, NULL);
        FALLTHROUGH;
    default:
        blackboxSetState(BLACKBOX_STATE_SHUTTING_DOWN);
    }
}

/**
 * Test Motors Blackbox Logging
 */
static bool startedLoggingInTestMode = false;

static void startInTestMode(void) {
    if (!startedLoggingInTestMode) {
        if (blackboxConfig()->device == BLACKBOX_DEVICE_SERIAL) {
            serialPort_t *sharedBlackboxAndMspPort = findSharedSerialPort(FUNCTION_BLACKBOX, FUNCTION_MSP);
            if (sharedBlackboxAndMspPort) {
                return; // When in test mode, we cannot share the MSP and serial logger port!
            }
        }
        blackboxStart();
        startedLoggingInTestMode = true;
    }
}

static void stopInTestMode(void) {
    if (startedLoggingInTestMode) {
        blackboxFinish();
        startedLoggingInTestMode = false;
    }
}
/**
 * We are going to monitor the MSP_SET_MOTOR target variables motor_disarmed[] for values other than minthrottle
 * on reading a value (i.e. the user is testing the motors), then we enable test mode logging;
 * we monitor when the values return to minthrottle and start a delay timer (5 seconds); if
 * the test motors are left at minimum throttle for this delay timer, then we assume we are done testing and
 * shutdown the logger.
 *
 * Of course, after the 5 seconds and shutdown of the logger, the system will be re-enabled to allow the
 * test mode to trigger again; its just that the data will be in a second, third, fourth etc log file.
 */
static bool inMotorTestMode(void) {
    static uint32_t resetTime = 0;
    if (!ARMING_FLAG(ARMED) && areMotorsRunning()) {
        resetTime = millis() + 5000; // add 5 seconds
        return true;
    } else {
        // Monitor the duration at minimum
        return (millis() < resetTime);
    }
    return false;
}

#ifdef USE_GPS
static void writeGPSHomeFrame(void) {
    blackboxWrite('H');
    blackboxWriteSignedVB(GPS_home[0]);
    blackboxWriteSignedVB(GPS_home[1]);
    //TODO it'd be great if we could grab the GPS current time and write that too
    gpsHistory.GPS_home[0] = GPS_home[0];
    gpsHistory.GPS_home[1] = GPS_home[1];
}

static void writeGPSFrame(timeUs_t currentTimeUs) {
    blackboxWrite('G');
    /*
     * If we're logging every frame, then a GPS frame always appears just after a frame with the
     * currentTime timestamp in the log, so the reader can just use that timestamp for the GPS frame.
     *
     * If we're not logging every frame, we need to store the time of this GPS frame.
     */
    if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_NOT_LOGGING_EVERY_FRAME)) {
        // Predict the time of the last frame in the main log
        blackboxWriteUnsignedVB(currentTimeUs - blackboxHistory[1]->time);
    }
    blackboxWriteUnsignedVB(gpsSol.numSat);
    blackboxWriteSignedVB(gpsSol.llh.lat - gpsHistory.GPS_home[LAT]);
    blackboxWriteSignedVB(gpsSol.llh.lon - gpsHistory.GPS_home[LON]);
    blackboxWriteUnsignedVB(gpsSol.llh.alt);
    blackboxWriteUnsignedVB(gpsSol.groundSpeed);
    blackboxWriteUnsignedVB(gpsSol.groundCourse);
    gpsHistory.GPS_numSat = gpsSol.numSat;
    gpsHistory.GPS_coord[LAT] = gpsSol.llh.lat;
    gpsHistory.GPS_coord[LON] = gpsSol.llh.lon;
}
#endif

/**
 * Fill the current state of the blackbox using values read from the flight controller
 */
static void loadMainState(timeUs_t currentTimeUs) {
#ifndef UNIT_TEST
    blackboxMainState_t *blackboxCurrent = blackboxHistory[0];
    blackboxCurrent->time = currentTimeUs;
    for (int i = 0; i < XYZ_AXIS_COUNT; i++) {
        blackboxCurrent->axisPID_P[i] = pidData[i].P;
        blackboxCurrent->axisPID_I[i] = pidData[i].I;
        blackboxCurrent->axisPID_D[i] = pidData[i].D;
        blackboxCurrent->gyroADC[i] = lrintf(gyro.gyroADCf[i]);
#if defined(USE_ACC)
        blackboxCurrent->accADC[i] = lrintf(acc.accADC[i]);
#endif
#ifdef USE_MAG
        blackboxCurrent->magADC[i] = mag.magADC[i];
#endif
    }
    for (int i = 0; i < 4; i++) {
        blackboxCurrent->rcCommand[i] = rcCommand[i];
    }
    // log the currentPidSetpoint values applied to the PID controller
    for (int i = 0; i < XYZ_AXIS_COUNT; i++) {
        blackboxCurrent->setpoint[i] = lrintf(pidGetPreviousSetpoint(i));
    }
    // log the final throttle value used in the mixer
    blackboxCurrent->setpoint[3] = lrintf(mixerGetLoggingThrottle() * 1000);
    for (int i = 0; i < DEBUG16_VALUE_COUNT; i++) {
        blackboxCurrent->debug[i] = debug[i];
    }
    const int motorCount = getMotorCount();
    for (int i = 0; i < motorCount; i++) {
        blackboxCurrent->motor[i] = motor[i];
    }
    blackboxCurrent->vbatLatest = getBatteryVoltageLatest();
    blackboxCurrent->amperageLatest = getAmperageLatest();
#ifdef USE_BARO
    blackboxCurrent->BaroAlt = baro.BaroAlt;
#endif
#ifdef USE_RANGEFINDER
    // Store the raw sonar value without applying tilt correction
    blackboxCurrent->surfaceRaw = rangefinderGetLatestAltitude();
#endif
    blackboxCurrent->rssi = getRssi();
#ifdef USE_SERVOS
    //Tail servo for tricopters
    blackboxCurrent->servo[5] = servo[5];
#endif
#else
    UNUSED(currentTimeUs);
#endif // UNIT_TEST
}

/**
 * Transmit the header information for the given field definitions. Transmitted header lines look like:
 *
 * H Field I name:a,b,c
 * H Field I predictor:0,1,2
 *
 * For all header types, provide a "mainFrameChar" which is the name for the field and will be used to refer to it in the
 * header (e.g. P, I etc). For blackboxDeltaField_t fields, also provide deltaFrameChar, otherwise set this to zero.
 *
 * Provide an array 'conditions' of FlightLogFieldCondition enums if you want these conditions to decide whether a field
 * should be included or not. Otherwise provide NULL for this parameter and NULL for secondCondition.
 *
 * Set xmitState.headerIndex to 0 and xmitState.u.fieldIndex to -1 before calling for the first time.
 *
 * secondFieldDefinition and secondCondition element pointers need to be provided in order to compute the stride of the
 * fieldDefinition and secondCondition arrays.
 *
 * Returns true if there is still header left to transmit (so call again to continue transmission).
 */
static bool sendFieldDefinition(char mainFrameChar, char deltaFrameChar, const void *fieldDefinitions,
                                const void *secondFieldDefinition, int fieldCount, const uint8_t *conditions, const uint8_t *secondCondition) {
    const blackboxFieldDefinition_t *def;
    unsigned int headerCount;
    static bool needComma = false;
    size_t definitionStride = (char*) secondFieldDefinition - (char*) fieldDefinitions;
    size_t conditionsStride = (char*) secondCondition - (char*) conditions;
    if (deltaFrameChar) {
        headerCount = BLACKBOX_DELTA_FIELD_HEADER_COUNT;
    } else {
        headerCount = BLACKBOX_SIMPLE_FIELD_HEADER_COUNT;
    }
    /*
     * We're chunking up the header data so we don't exceed our datarate. So we'll be called multiple times to transmit
     * the whole header.
     */
    // On our first call we need to print the name of the header and a colon
    if (xmitState.u.fieldIndex == -1) {
        if (xmitState.headerIndex >= headerCount) {
            return false; //Someone probably called us again after we had already completed transmission
        }
        uint32_t charsToBeWritten = strlen("H Field x :") + strlen(blackboxFieldHeaderNames[xmitState.headerIndex]);
        if (blackboxDeviceReserveBufferSpace(charsToBeWritten) != BLACKBOX_RESERVE_SUCCESS) {
            return true; // Try again later
        }
        blackboxHeaderBudget -= blackboxPrintf("H Field %c %s:", xmitState.headerIndex >= BLACKBOX_SIMPLE_FIELD_HEADER_COUNT ? deltaFrameChar : mainFrameChar, blackboxFieldHeaderNames[xmitState.headerIndex]);
        xmitState.u.fieldIndex++;
        needComma = false;
    }
    // The longest we expect an integer to be as a string:
    const uint32_t LONGEST_INTEGER_STRLEN = 2;
    for (; xmitState.u.fieldIndex < fieldCount; xmitState.u.fieldIndex++) {
        def = (const blackboxFieldDefinition_t*) ((const char*)fieldDefinitions + definitionStride * xmitState.u.fieldIndex);
        if (!conditions || testBlackboxCondition(conditions[conditionsStride * xmitState.u.fieldIndex])) {
            // First (over)estimate the length of the string we want to print
            int32_t bytesToWrite = 1; // Leading comma
            // The first header is a field name
            if (xmitState.headerIndex == 0) {
                bytesToWrite += strlen(def->name) + strlen("[]") + LONGEST_INTEGER_STRLEN;
            } else {
                //The other headers are integers
                bytesToWrite += LONGEST_INTEGER_STRLEN;
            }
            // Now perform the write if the buffer is large enough
            if (blackboxDeviceReserveBufferSpace(bytesToWrite) != BLACKBOX_RESERVE_SUCCESS) {
                // Ran out of space!
                return true;
            }
            blackboxHeaderBudget -= bytesToWrite;
            if (needComma) {
                blackboxWrite(',');
            } else {
                needComma = true;
            }
            // The first header is a field name
            if (xmitState.headerIndex == 0) {
                blackboxWriteString(def->name);
                // Do we need to print an index in brackets after the name?
                if (def->fieldNameIndex != -1) {
                    blackboxPrintf("[%d]", def->fieldNameIndex);
                }
            } else {
                //The other headers are integers
                blackboxPrintf("%d", def->arr[xmitState.headerIndex - 1]);
            }
        }
    }
    // Did we complete this line?
    if (xmitState.u.fieldIndex == fieldCount && blackboxDeviceReserveBufferSpace(1) == BLACKBOX_RESERVE_SUCCESS) {
        blackboxHeaderBudget--;
        blackboxWrite('\n');
        xmitState.headerIndex++;
        xmitState.u.fieldIndex = -1;
    }
    return xmitState.headerIndex < headerCount;
}

// Buf must be at least FORMATTED_DATE_TIME_BUFSIZE
STATIC_UNIT_TESTED char *blackboxGetStartDateTime(char *buf) {
#ifdef USE_RTC_TIME
    dateTime_t dt;
    // rtcGetDateTime will fill dt with 0000-01-01T00:00:00
    // when time is not known.
    rtcGetDateTime(&dt);
    dateTimeFormatLocal(buf, &dt);
#else
    buf = "0000-01-01T00:00:00.000";
#endif
    return buf;
}

#ifndef BLACKBOX_PRINT_HEADER_LINE
#define BLACKBOX_PRINT_HEADER_LINE(name, format, ...) case __COUNTER__: \
                                                blackboxPrintfHeaderLine(name, format, __VA_ARGS__); \
                                                break;
#define BLACKBOX_PRINT_HEADER_LINE_CUSTOM(...) case __COUNTER__: \
                                                    {__VA_ARGS__}; \
                                               break;
#endif

/**
 * Transmit a portion of the system information headers. Call the first time with xmitState.headerIndex == 0. Returns
 * true iff transmission is complete, otherwise call again later to continue transmission.
 */
static bool blackboxWriteSysinfo(void) {
#ifndef UNIT_TEST
    const uint16_t motorOutputLowInt = lrintf(motorOutputLow);
    const uint16_t motorOutputHighInt = lrintf(motorOutputHigh);
    // Make sure we have enough room in the buffer for our longest line (as of this writing, the "Firmware date" line)
    if (blackboxDeviceReserveBufferSpace(64) != BLACKBOX_RESERVE_SUCCESS) {
        return false;
    }
    char buf[FORMATTED_DATE_TIME_BUFSIZE];
    const controlRateConfig_t *currentControlRateProfile = controlRateProfiles(systemConfig()->activeRateProfile);
    switch (xmitState.headerIndex) {
        BLACKBOX_PRINT_HEADER_LINE("Firmware type", "%s",                   "Cleanflight");
        BLACKBOX_PRINT_HEADER_LINE("Firmware revision", "%s %s (%s) %s",    FC_FIRMWARE_NAME, FC_VERSION_STRING, shortGitRevision, targetName);
        BLACKBOX_PRINT_HEADER_LINE("Target", "%s",                          targetName);
        BLACKBOX_PRINT_HEADER_LINE("Firmware date", "%s %s",                buildDate, buildTime);
        BLACKBOX_PRINT_HEADER_LINE("Log start datetime", "%s",              blackboxGetStartDateTime(buf));
        BLACKBOX_PRINT_HEADER_LINE("Craft name", "%s",                      pilotConfig()->name);
        BLACKBOX_PRINT_HEADER_LINE("I interval", "%d",                      blackboxIInterval);
        BLACKBOX_PRINT_HEADER_LINE("P interval", "%d",                      blackboxPInterval);
        BLACKBOX_PRINT_HEADER_LINE("P ratio", "%d",                         blackboxConfig()->p_ratio);
        BLACKBOX_PRINT_HEADER_LINE("minthrottle", "%d",                     motorConfig()->minthrottle);
        BLACKBOX_PRINT_HEADER_LINE("maxthrottle", "%d",                     motorConfig()->maxthrottle);
        BLACKBOX_PRINT_HEADER_LINE("gyro_scale", "0x%x",                     castFloatBytesToInt(1.0f));
        BLACKBOX_PRINT_HEADER_LINE("motorOutput", "%d,%d",                  motorOutputLowInt, motorOutputHighInt);
#if defined(USE_ACC)
        BLACKBOX_PRINT_HEADER_LINE("acc_1G", "%u",                      acc.dev.acc_1G);
#endif
        BLACKBOX_PRINT_HEADER_LINE_CUSTOM(
        if (testBlackboxCondition(FLIGHT_LOG_FIELD_CONDITION_VBAT)) {
        blackboxPrintfHeaderLine("vbat_scale", "%u", voltageSensorADCConfig(VOLTAGE_SENSOR_ADC_VBAT)->vbatscale);
        } else {
            xmitState.headerIndex += 2; // Skip the next two vbat fields too
        }
        );
        BLACKBOX_PRINT_HEADER_LINE("vbatcellvoltage", "%u,%u,%u",           batteryConfig()->vbatmincellvoltage,
                                   batteryConfig()->vbatwarningcellvoltage,
                                   batteryConfig()->vbatmaxcellvoltage);
        BLACKBOX_PRINT_HEADER_LINE("vbatref", "%u",                         vbatReference);
        BLACKBOX_PRINT_HEADER_LINE_CUSTOM(
        if (batteryConfig()->currentMeterSource == CURRENT_METER_ADC) {
        blackboxPrintfHeaderLine("currentSensor", "%d,%d", currentSensorADCConfig()->offset, currentSensorADCConfig()->scale);
        }
        );
        BLACKBOX_PRINT_HEADER_LINE("looptime", "%d",                        gyro.targetLooptime);
        BLACKBOX_PRINT_HEADER_LINE("gyro_sync_denom", "%d",                 gyroConfig()->gyro_sync_denom);
        BLACKBOX_PRINT_HEADER_LINE("pid_process_denom", "%d",               pidConfig()->pid_process_denom);
        BLACKBOX_PRINT_HEADER_LINE("thr_mid", "%d",                         currentControlRateProfile->thrMid8);
        BLACKBOX_PRINT_HEADER_LINE("thr_expo", "%d",                        currentControlRateProfile->thrExpo8);
        BLACKBOX_PRINT_HEADER_LINE("tpa_rate_p", "%d",                      currentControlRateProfile->dynThrP);
        BLACKBOX_PRINT_HEADER_LINE("tpa_rate_i", "%d",                      currentControlRateProfile->dynThrI);
        BLACKBOX_PRINT_HEADER_LINE("tpa_rate_d", "%d",                      currentControlRateProfile->dynThrD);
        BLACKBOX_PRINT_HEADER_LINE("tpa_breakpoint", "%d",                  currentControlRateProfile->tpa_breakpoint);
        BLACKBOX_PRINT_HEADER_LINE("spa_roll_p", "%d",                      currentPidProfile->setPointPTransition[ROLL]);
        BLACKBOX_PRINT_HEADER_LINE("spa_roll_i", "%d",                      currentPidProfile->setPointITransition[ROLL]);
        BLACKBOX_PRINT_HEADER_LINE("spa_roll_d", "%d",                      currentPidProfile->setPointDTransition[ROLL]);
        BLACKBOX_PRINT_HEADER_LINE("spa_pitch_p", "%d",                     currentPidProfile->setPointPTransition[PITCH]);
        BLACKBOX_PRINT_HEADER_LINE("spa_pitch_i", "%d",                     currentPidProfile->setPointITransition[PITCH]);
        BLACKBOX_PRINT_HEADER_LINE("spa_pitch_d", "%d",                     currentPidProfile->setPointDTransition[PITCH]);
        BLACKBOX_PRINT_HEADER_LINE("spa_yaw_p", "%d",                       currentPidProfile->setPointPTransition[YAW]);
        BLACKBOX_PRINT_HEADER_LINE("spa_yaw_i", "%d",                       currentPidProfile->setPointITransition[YAW]);
        BLACKBOX_PRINT_HEADER_LINE("spa_yaw_d", "%d",                       currentPidProfile->setPointDTransition[YAW]);
        BLACKBOX_PRINT_HEADER_LINE("rates_type", "%d",                      currentControlRateProfile->rates_type);
        BLACKBOX_PRINT_HEADER_LINE("rc_rates", "%d,%d,%d",                  currentControlRateProfile->rcRates[ROLL],
                                   currentControlRateProfile->rcRates[PITCH],
                                   currentControlRateProfile->rcRates[YAW]);
        BLACKBOX_PRINT_HEADER_LINE("rc_expo", "%d,%d,%d",                   currentControlRateProfile->rcExpo[ROLL],
                                   currentControlRateProfile->rcExpo[PITCH],
                                   currentControlRateProfile->rcExpo[YAW]);
        BLACKBOX_PRINT_HEADER_LINE("rates", "%d,%d,%d",                     currentControlRateProfile->rates[ROLL],
                                   currentControlRateProfile->rates[PITCH],
                                   currentControlRateProfile->rates[YAW]);
        BLACKBOX_PRINT_HEADER_LINE("rate_sensitivity", "%d,%d",             currentControlRateProfile->rateDynamics.rateSensCenter,
                                   currentControlRateProfile->rateDynamics.rateSensEnd);
        BLACKBOX_PRINT_HEADER_LINE("rate_correction", "%d,%d",              currentControlRateProfile->rateDynamics.rateCorrectionCenter,
                                   currentControlRateProfile->rateDynamics.rateCorrectionEnd);
        BLACKBOX_PRINT_HEADER_LINE("rate_weight", "%d,%d",                  currentControlRateProfile->rateDynamics.rateWeightCenter,
                                   currentControlRateProfile->rateDynamics.rateWeightEnd);
        BLACKBOX_PRINT_HEADER_LINE("rollPID", "%d,%d,%d",                   currentPidProfile->pid[PID_ROLL].P,
                                   currentPidProfile->pid[PID_ROLL].I,
                                   currentPidProfile->pid[PID_ROLL].D);
        BLACKBOX_PRINT_HEADER_LINE("pitchPID", "%d,%d,%d",                  currentPidProfile->pid[PID_PITCH].P,
                                   currentPidProfile->pid[PID_PITCH].I,
                                   currentPidProfile->pid[PID_PITCH].D);
        BLACKBOX_PRINT_HEADER_LINE("yawPID", "%d,%d,%d",                    currentPidProfile->pid[PID_YAW].P,
                                   currentPidProfile->pid[PID_YAW].I,
                                   currentPidProfile->pid[PID_YAW].D);
        BLACKBOX_PRINT_HEADER_LINE("levelPIDLOW", "%d,%d,%d",               currentPidProfile->pid[PID_LEVEL_LOW].P,
                                   currentPidProfile->pid[PID_LEVEL_LOW].D,
                                   currentPidProfile->pid[PID_LEVEL_LOW].F);
        BLACKBOX_PRINT_HEADER_LINE("levelPIDHIGH", "%d,%d",                 currentPidProfile->pid[PID_LEVEL_HIGH].P,
                                   currentPidProfile->pid[PID_LEVEL_HIGH].D);
        BLACKBOX_PRINT_HEADER_LINE("magPID", "%d",                          currentPidProfile->pid[PID_MAG].P);
        BLACKBOX_PRINT_HEADER_LINE("df_yaw", "%d",                          currentPidProfile->directFF_yaw);
        BLACKBOX_PRINT_HEADER_LINE("df_angle_low", "%d",                    currentPidProfile->pid[PID_LEVEL_LOW].I);
        BLACKBOX_PRINT_HEADER_LINE("df_angle_high", "%d",                   currentPidProfile->pid[PID_LEVEL_HIGH].I);
        BLACKBOX_PRINT_HEADER_LINE("dterm_filter_type", "%d",               currentPidProfile->dterm_filter_type);
        BLACKBOX_PRINT_HEADER_LINE("dterm_lowpass_hz_roll", "%d",           currentPidProfile->dFilter[ROLL].dLpf);
        BLACKBOX_PRINT_HEADER_LINE("dterm_lowpass_hz_pitch", "%d",          currentPidProfile->dFilter[PITCH].dLpf);
        BLACKBOX_PRINT_HEADER_LINE("dterm_lowpass_hz_yaw", "%d",            currentPidProfile->dFilter[YAW].dLpf);
        BLACKBOX_PRINT_HEADER_LINE("dterm_filter2_type", "%d",              currentPidProfile->dterm_filter2_type);
        BLACKBOX_PRINT_HEADER_LINE("dterm_lowpass2_hz_roll", "%d",          currentPidProfile->dFilter[ROLL].dLpf2);
        BLACKBOX_PRINT_HEADER_LINE("dterm_lowpass2_hz_pitch", "%d",         currentPidProfile->dFilter[PITCH].dLpf2);
        BLACKBOX_PRINT_HEADER_LINE("dterm_lowpass2_hz_yaw", "%d",           currentPidProfile->dFilter[YAW].dLpf2);
#ifdef USE_GYRO_DATA_ANALYSE
        BLACKBOX_PRINT_HEADER_LINE("dterm_dyn_notch_enable", "%d",          currentPidProfile->dtermDynNotch);
        BLACKBOX_PRINT_HEADER_LINE("dterm_dyn_notch_q", "%d",               currentPidProfile->dterm_dyn_notch_q);
#endif
        BLACKBOX_PRINT_HEADER_LINE("dterm_ABG_alpha", "%d",                 currentPidProfile->dterm_ABG_alpha);
        BLACKBOX_PRINT_HEADER_LINE("dterm_ABG_boost", "%d",                 currentPidProfile->dterm_ABG_boost);
        BLACKBOX_PRINT_HEADER_LINE("dterm_ABG_half_life", "%d",             currentPidProfile->dterm_ABG_half_life);
        BLACKBOX_PRINT_HEADER_LINE("iterm_windup", "%d",                    currentPidProfile->itermWindupPointPercent);
#if defined(USE_ITERM_RELAX)
        BLACKBOX_PRINT_HEADER_LINE("iterm_relax_cutoff", "%d",          currentPidProfile->iterm_relax_cutoff);
        BLACKBOX_PRINT_HEADER_LINE("iterm_relax_cutoff_yaw", "%d",      currentPidProfile->iterm_relax_cutoff_yaw);
        BLACKBOX_PRINT_HEADER_LINE("iterm_relax_threshold", "%d",       currentPidProfile->iterm_relax_threshold);
        BLACKBOX_PRINT_HEADER_LINE("iterm_relax_threshold_yaw", "%d",   currentPidProfile->iterm_relax_threshold_yaw);
#endif
        BLACKBOX_PRINT_HEADER_LINE("i_decay", "%d",                         currentPidProfile->i_decay);
        BLACKBOX_PRINT_HEADER_LINE("i_decay_cutoff", "%d",                  currentPidProfile->i_decay_cutoff);
        BLACKBOX_PRINT_HEADER_LINE("pidAtMinThrottle", "%d",                currentPidProfile->pidAtMinThrottle);
        // EmuFlight PID controller parameters
        BLACKBOX_PRINT_HEADER_LINE("feathered_pids", "%d",                  currentPidProfile->feathered_pids);
        BLACKBOX_PRINT_HEADER_LINE("emu_boost", "%d",                       currentPidProfile->errorBoost);
        BLACKBOX_PRINT_HEADER_LINE("emu_boost_limit", "%d",                 currentPidProfile->errorBoostLimit);
        BLACKBOX_PRINT_HEADER_LINE("emu_boost_yaw", "%d",                   currentPidProfile->errorBoostYaw);
        BLACKBOX_PRINT_HEADER_LINE("emu_boost_limit_yaw", "%d",             currentPidProfile->errorBoostLimitYaw);
        BLACKBOX_PRINT_HEADER_LINE("dterm_boost", "%d",                     currentPidProfile->dtermBoost);
        BLACKBOX_PRINT_HEADER_LINE("dterm_boost_limit", "%d",               currentPidProfile->dtermBoostLimit);
        BLACKBOX_PRINT_HEADER_LINE("emu_gravity", "%d",                     currentPidProfile->emuGravityGain);
        BLACKBOX_PRINT_HEADER_LINE("acc_limit_yaw", "%d",                   currentPidProfile->yawRateAccelLimit);
        BLACKBOX_PRINT_HEADER_LINE("acc_limit", "%d",                       currentPidProfile->rateAccelLimit);
        BLACKBOX_PRINT_HEADER_LINE("pidsum_limit", "%d",                    currentPidProfile->pidSumLimit);
        BLACKBOX_PRINT_HEADER_LINE("pidsum_limit_yaw", "%d",                currentPidProfile->pidSumLimitYaw);
        BLACKBOX_PRINT_HEADER_LINE("iterm_rotation", "%d",                  currentPidProfile->iterm_rotation);
        BLACKBOX_PRINT_HEADER_LINE("throttle_boost", "%d",                  currentPidProfile->throttle_boost);
        BLACKBOX_PRINT_HEADER_LINE("linear_thrust_low_output",  "%d",       currentPidProfile->linear_thrust_low_output);
        BLACKBOX_PRINT_HEADER_LINE("linear_thrust_high_output", "%d",       currentPidProfile->linear_thrust_high_output);
        BLACKBOX_PRINT_HEADER_LINE("linear_throttle", "%d",                 currentPidProfile->linear_throttle);
        BLACKBOX_PRINT_HEADER_LINE("mixer_impl", "%d",                      currentPidProfile->mixer_impl);
        BLACKBOX_PRINT_HEADER_LINE("mixer_laziness", "%d",                  currentPidProfile->mixer_laziness);
        BLACKBOX_PRINT_HEADER_LINE("mixer_yaw_throttle_comp", "%d",         currentPidProfile->mixer_yaw_throttle_comp);
        // End of EmuFlight controller parameters
        BLACKBOX_PRINT_HEADER_LINE("deadband", "%d",                        rcControlsConfig()->deadband);
        BLACKBOX_PRINT_HEADER_LINE("yaw_deadband", "%d",                    rcControlsConfig()->yaw_deadband);
        BLACKBOX_PRINT_HEADER_LINE("gyro_hardware_lpf", "%d",               gyroConfig()->gyro_hardware_lpf);
#ifdef USE_32K_CAPABLE_GYRO
        BLACKBOX_PRINT_HEADER_LINE("gyro_32khz_hardware_lpf", "%d",     gyroConfig()->gyro_32khz_hardware_lpf);
#endif
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass_type", "%d",               gyroConfig()->gyro_lowpass_type);
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass_hz_roll", "%d",            gyroConfig()->gyro_lowpass_hz[ROLL]);
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass_hz_pitch", "%d",           gyroConfig()->gyro_lowpass_hz[PITCH]);
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass_hz_yaw", "%d",             gyroConfig()->gyro_lowpass_hz[YAW]);
#ifdef USE_GYRO_LPF2
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass2_type", "%d",              gyroConfig()->gyro_lowpass2_type);
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass2_hz_roll", "%d",           gyroConfig()->gyro_lowpass2_hz[ROLL]);
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass2_hz_pitch", "%d",          gyroConfig()->gyro_lowpass2_hz[PITCH]);
        BLACKBOX_PRINT_HEADER_LINE("gyro_lowpass2_hz_yaw", "%d",            gyroConfig()->gyro_lowpass2_hz[YAW]);
#endif
        BLACKBOX_PRINT_HEADER_LINE("gyro_notch_hz", "%d,%d",                gyroConfig()->gyro_soft_notch_hz_1,
                                   gyroConfig()->gyro_soft_notch_hz_2);
        BLACKBOX_PRINT_HEADER_LINE("gyro_notch_cutoff", "%d,%d",            gyroConfig()->gyro_soft_notch_cutoff_1,
                                   gyroConfig()->gyro_soft_notch_cutoff_2);
#if defined(USE_GYRO_DATA_ANALYSE)
        BLACKBOX_PRINT_HEADER_LINE("dynamic_gyro_notch_q", "%d",            gyroConfig()->dyn_notch_q);
        BLACKBOX_PRINT_HEADER_LINE("dynamic_gyro_notch_count", "%d",        gyroConfig()->dyn_notch_count);
        BLACKBOX_PRINT_HEADER_LINE("dynamic_gyro_notch_min_hz", "%d",       gyroConfig()->dyn_notch_min_hz);
        BLACKBOX_PRINT_HEADER_LINE("dynamic_gyro_notch_max_hz", "%d",       gyroConfig()->dyn_notch_max_hz);
        BLACKBOX_PRINT_HEADER_LINE("dynamic_gyro_notch_axis", "%d",       gyroConfig()->dyn_notch_axis);
#endif
        BLACKBOX_PRINT_HEADER_LINE("gyro_ABG_alpha", "%d",                  gyroConfig()->gyro_ABG_alpha);
        BLACKBOX_PRINT_HEADER_LINE("gyro_ABG_boost", "%d",                  gyroConfig()->gyro_ABG_boost);
        BLACKBOX_PRINT_HEADER_LINE("gyro_ABG_half_life", "%d",              gyroConfig()->gyro_ABG_half_life);
        BLACKBOX_PRINT_HEADER_LINE("smith_predict_enabled", "%d",              gyroConfig()->smithPredictorEnabled);
        BLACKBOX_PRINT_HEADER_LINE("smith_predict_str", "%d",               gyroConfig()->smithPredictorStrength);
        BLACKBOX_PRINT_HEADER_LINE("smith_predict_delay", "%d",             gyroConfig()->smithPredictorDelay);
        BLACKBOX_PRINT_HEADER_LINE("smith_predict_filt_hz", "%d",           gyroConfig()->smithPredictorFilterHz);
#if defined(USE_ACC)
        BLACKBOX_PRINT_HEADER_LINE("acc_lpf_hz", "%d",                  (int)(accelerometerConfig()->acc_lpf_hz * 100.0f));
        BLACKBOX_PRINT_HEADER_LINE("acc_hardware", "%d",                accelerometerConfig()->acc_hardware);
#endif
#ifdef USE_BARO
        BLACKBOX_PRINT_HEADER_LINE("baro_hardware", "%d",               barometerConfig()->baro_hardware);
#endif
#ifdef USE_MAG
        BLACKBOX_PRINT_HEADER_LINE("mag_hardware", "%d",                compassConfig()->mag_hardware);
#endif
        BLACKBOX_PRINT_HEADER_LINE("gyro_cal_on_first_arm", "%d",           armingConfig()->gyro_cal_on_first_arm);
        BLACKBOX_PRINT_HEADER_LINE("rc_interpolation", "%d",                rxConfig()->rcInterpolation);
        BLACKBOX_PRINT_HEADER_LINE("rc_interpolation_interval", "%d",       rxConfig()->rcInterpolationInterval);
        BLACKBOX_PRINT_HEADER_LINE("airmode_activate_throttle", "%d",       rxConfig()->airModeActivateThreshold);
        BLACKBOX_PRINT_HEADER_LINE("serialrx_provider", "%d",               rxConfig()->serialrx_provider);
        BLACKBOX_PRINT_HEADER_LINE("use_unsynced_pwm", "%d",                motorConfig()->dev.useUnsyncedPwm);
        BLACKBOX_PRINT_HEADER_LINE("motor_pwm_protocol", "%d",              motorConfig()->dev.motorPwmProtocol);
        BLACKBOX_PRINT_HEADER_LINE("motor_pwm_rate", "%d",                  motorConfig()->dev.motorPwmRate);
        BLACKBOX_PRINT_HEADER_LINE("dshot_idle_value", "%d",                motorConfig()->digitalIdleOffsetValue);
        BLACKBOX_PRINT_HEADER_LINE("motor_poles", "%d",                     motorConfig()->motorPoleCount);
        BLACKBOX_PRINT_HEADER_LINE("debug_mode", "%d",                      systemConfig()->debug_mode);
        BLACKBOX_PRINT_HEADER_LINE("features", "%d",                        featureConfig()->enabledFeatures);
#ifdef USE_RC_SMOOTHING_FILTER
        BLACKBOX_PRINT_HEADER_LINE("rc_smoothing_type", "%d",               rxConfig()->rc_smoothing_type);
        BLACKBOX_PRINT_HEADER_LINE("rc_smoothing_debug_axis", "%d",         rxConfig()->rc_smoothing_debug_axis);
        BLACKBOX_PRINT_HEADER_LINE("rc_smoothing_cutoff", "%d",             rxConfig()->rc_smoothing_input_cutoff);
        BLACKBOX_PRINT_HEADER_LINE("rc_smoothing_filter", "%d",             rxConfig()->rc_smoothing_input_type);
        BLACKBOX_PRINT_HEADER_LINE("rc_smoothing_active_cutoff", "%d",      rcSmoothingGetValue(RC_SMOOTHING_VALUE_INPUT_ACTIVE));
        BLACKBOX_PRINT_HEADER_LINE("rc_smoothing_rx_average", "%d",         rcSmoothingGetValue(RC_SMOOTHING_VALUE_AVERAGE_FRAME));
#endif // USE_RC_SMOOTHING_FILTER
#ifdef USE_GYRO_IMUF9001
        BLACKBOX_PRINT_HEADER_LINE("IMUF_revision", "%d",              imufCurrentVersion);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_lowpass_roll", "%d",          gyroConfig()->imuf_roll_lpf_cutoff_hz);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_lowpass_pitch", "%d",         gyroConfig()->imuf_pitch_lpf_cutoff_hz);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_lowpass_yaw", "%d",           gyroConfig()->imuf_yaw_lpf_cutoff_hz);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_acc_lpf_cutoff", "%d",        gyroConfig()->imuf_acc_lpf_cutoff_hz);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_ptn_order", "%d",             gyroConfig()->imuf_ptn_order);
#endif
        BLACKBOX_PRINT_HEADER_LINE("IMUF_roll_q", "%d",                gyroConfig()->imuf_roll_q);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_pitch_q", "%d",               gyroConfig()->imuf_pitch_q);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_yaw_q", "%d",                 gyroConfig()->imuf_yaw_q);
        BLACKBOX_PRINT_HEADER_LINE("IMUF_w", "%d",                     gyroConfig()->imuf_w);
        
    default:
        return true;
    }
    xmitState.headerIndex++;
#endif // UNIT_TEST
    return false;
}

/**
 * Write the given event to the log immediately
 */
void blackboxLogEvent(FlightLogEvent event, flightLogEventData_t *data) {
    // Only allow events to be logged after headers have been written
    if (!(blackboxState == BLACKBOX_STATE_RUNNING || blackboxState == BLACKBOX_STATE_PAUSED)) {
        return;
    }
    //Shared header for event frames
    blackboxWrite('E');
    blackboxWrite(event);
    //Now serialize the data for this specific frame type
    switch (event) {
    case FLIGHT_LOG_EVENT_SYNC_BEEP:
        blackboxWriteUnsignedVB(data->syncBeep.time);
        break;
    case FLIGHT_LOG_EVENT_FLIGHTMODE: // New flightmode flags write
        blackboxWriteUnsignedVB(data->flightMode.flags);
        blackboxWriteUnsignedVB(data->flightMode.lastFlags);
        break;
    case FLIGHT_LOG_EVENT_INFLIGHT_ADJUSTMENT:
        if (data->inflightAdjustment.floatFlag) {
            blackboxWrite(data->inflightAdjustment.adjustmentFunction + FLIGHT_LOG_EVENT_INFLIGHT_ADJUSTMENT_FUNCTION_FLOAT_VALUE_FLAG);
            blackboxWriteFloat(data->inflightAdjustment.newFloatValue);
        } else {
            blackboxWrite(data->inflightAdjustment.adjustmentFunction);
            blackboxWriteSignedVB(data->inflightAdjustment.newValue);
        }
        break;
    case FLIGHT_LOG_EVENT_LOGGING_RESUME:
        blackboxWriteUnsignedVB(data->loggingResume.logIteration);
        blackboxWriteUnsignedVB(data->loggingResume.currentTime);
        break;
    case FLIGHT_LOG_EVENT_LOG_END:
        blackboxWriteString("End of log");
        blackboxWrite(0);
        break;
    }
}

/* If an arming beep has played since it was last logged, write the time of the arming beep to the log as a synchronization point */
static void blackboxCheckAndLogArmingBeep(void) {
    // Use != so that we can still detect a change if the counter wraps
    if (getArmingBeepTimeMicros() != blackboxLastArmingBeep) {
        blackboxLastArmingBeep = getArmingBeepTimeMicros();
        flightLogEvent_syncBeep_t eventData;
        eventData.time = blackboxLastArmingBeep;
        blackboxLogEvent(FLIGHT_LOG_EVENT_SYNC_BEEP, (flightLogEventData_t *)&eventData);
    }
}

/* monitor the flight mode event status and trigger an event record if the state changes */
static void blackboxCheckAndLogFlightMode(void) {
    // Use != so that we can still detect a change if the counter wraps
    if (memcmp(&rcModeActivationMask, &blackboxLastFlightModeFlags, sizeof(blackboxLastFlightModeFlags))) {
        flightLogEvent_flightMode_t eventData; // Add new data for current flight mode flags
        eventData.lastFlags = blackboxLastFlightModeFlags;
        memcpy(&blackboxLastFlightModeFlags, &rcModeActivationMask, sizeof(blackboxLastFlightModeFlags));
        memcpy(&eventData.flags, &rcModeActivationMask, sizeof(eventData.flags));
        blackboxLogEvent(FLIGHT_LOG_EVENT_FLIGHTMODE, (flightLogEventData_t *)&eventData);
    }
}

STATIC_UNIT_TESTED bool blackboxShouldLogPFrame(void) {
    return blackboxPFrameIndex == 0 && blackboxConfig()->p_ratio != 0;
}

STATIC_UNIT_TESTED bool blackboxShouldLogIFrame(void) {
    return blackboxLoopIndex == 0;
}

/*
 * If the GPS home point has been updated, or every 128 I-frames (~10 seconds), write the
 * GPS home position.
 *
 * We write it periodically so that if one Home Frame goes missing, the GPS coordinates can
 * still be interpreted correctly.
 */
#ifdef USE_GPS
STATIC_UNIT_TESTED bool blackboxShouldLogGpsHomeFrame(void) {
    if (GPS_home[0] != gpsHistory.GPS_home[0] || GPS_home[1] != gpsHistory.GPS_home[1]
            || (blackboxPFrameIndex == blackboxIInterval / 2 && blackboxIFrameIndex % 128 == 0)) {
        return true;
    }
    return false;
}
#endif // GPS

// Called once every FC loop in order to keep track of how many FC loop iterations have passed
STATIC_UNIT_TESTED void blackboxAdvanceIterationTimers(void) {
    ++blackboxSlowFrameIterationTimer;
    ++blackboxIteration;
    if (++blackboxLoopIndex >= blackboxIInterval) {
        blackboxLoopIndex = 0;
        blackboxIFrameIndex++;
        blackboxPFrameIndex = 0;
    } else if (++blackboxPFrameIndex >= blackboxPInterval) {
        blackboxPFrameIndex = 0;
    }
}

// Called once every FC loop in order to log the current state
STATIC_UNIT_TESTED void blackboxLogIteration(timeUs_t currentTimeUs) {
    // Write a keyframe every blackboxIInterval frames so we can resynchronise upon missing frames
    if (blackboxShouldLogIFrame()) {
        /*
         * Don't log a slow frame if the slow data didn't change ("I" frames are already large enough without adding
         * an additional item to write at the same time). Unless we're *only* logging "I" frames, then we have no choice.
         */
        if (blackboxIsOnlyLoggingIntraframes()) {
            writeSlowFrameIfNeeded();
        }
        loadMainState(currentTimeUs);
        writeIntraframe();
    } else {
        blackboxCheckAndLogArmingBeep();
        blackboxCheckAndLogFlightMode(); // Check for FlightMode status change event
        if (blackboxShouldLogPFrame()) {
            /*
             * We assume that slow frames are only interesting in that they aid the interpretation of the main data stream.
             * So only log slow frames during loop iterations where we log a main frame.
             */
            writeSlowFrameIfNeeded();
            loadMainState(currentTimeUs);
            writeInterframe();
        }
#ifdef USE_GPS
        if (feature(FEATURE_GPS)) {
            if (blackboxShouldLogGpsHomeFrame()) {
                writeGPSHomeFrame();
                writeGPSFrame(currentTimeUs);
            } else if (gpsSol.numSat != gpsHistory.GPS_numSat
                       || gpsSol.llh.lat != gpsHistory.GPS_coord[LAT]
                       || gpsSol.llh.lon != gpsHistory.GPS_coord[LON]) {
                //We could check for velocity changes as well but I doubt it changes independent of position
                writeGPSFrame(currentTimeUs);
            }
        }
#endif
    }
    //Flush every iteration so that our runtime variance is minimized
    blackboxDeviceFlush();
}

/**
 * Call each flight loop iteration to perform blackbox logging.
 */
void blackboxUpdate(timeUs_t currentTimeUs) {
    switch (blackboxState) {
    case BLACKBOX_STATE_STOPPED:
        if (ARMING_FLAG(ARMED)) {
            blackboxOpen();
            blackboxStart();
        }
#ifdef USE_FLASHFS
        if (IS_RC_MODE_ACTIVE(BOXBLACKBOXERASE)) {
            blackboxSetState(BLACKBOX_STATE_START_ERASE);
        }
#endif
        break;
    case BLACKBOX_STATE_PREPARE_LOG_FILE:
        if (blackboxDeviceBeginLog()) {
            blackboxSetState(BLACKBOX_STATE_SEND_HEADER);
        }
        break;
    case BLACKBOX_STATE_SEND_HEADER:
        blackboxReplenishHeaderBudget();
        //On entry of this state, xmitState.headerIndex is 0 and startTime is intialised
        /*
         * Once the UART has had time to init, transmit the header in chunks so we don't overflow its transmit
         * buffer, overflow the OpenLog's buffer, or keep the main loop busy for too long.
         */
        if (millis() > xmitState.u.startTime + 100) {
            if (blackboxDeviceReserveBufferSpace(BLACKBOX_TARGET_HEADER_BUDGET_PER_ITERATION) == BLACKBOX_RESERVE_SUCCESS) {
                for (int i = 0; i < BLACKBOX_TARGET_HEADER_BUDGET_PER_ITERATION && blackboxHeader[xmitState.headerIndex] != '\0'; i++, xmitState.headerIndex++) {
                    blackboxWrite(blackboxHeader[xmitState.headerIndex]);
                    blackboxHeaderBudget--;
                }
                if (blackboxHeader[xmitState.headerIndex] == '\0') {
                    blackboxSetState(BLACKBOX_STATE_SEND_MAIN_FIELD_HEADER);
                }
            }
        }
        break;
    case BLACKBOX_STATE_SEND_MAIN_FIELD_HEADER:
        blackboxReplenishHeaderBudget();
        //On entry of this state, xmitState.headerIndex is 0 and xmitState.u.fieldIndex is -1
        if (!sendFieldDefinition('I', 'P', blackboxMainFields, blackboxMainFields + 1, ARRAYLEN(blackboxMainFields),
                                 &blackboxMainFields[0].condition, &blackboxMainFields[1].condition)) {
#ifdef USE_GPS
            if (feature(FEATURE_GPS)) {
                blackboxSetState(BLACKBOX_STATE_SEND_GPS_H_HEADER);
            } else
#endif
                blackboxSetState(BLACKBOX_STATE_SEND_SLOW_HEADER);
        }
        break;
#ifdef USE_GPS
    case BLACKBOX_STATE_SEND_GPS_H_HEADER:
        blackboxReplenishHeaderBudget();
        //On entry of this state, xmitState.headerIndex is 0 and xmitState.u.fieldIndex is -1
        if (!sendFieldDefinition('H', 0, blackboxGpsHFields, blackboxGpsHFields + 1, ARRAYLEN(blackboxGpsHFields),
                                 NULL, NULL)) {
            blackboxSetState(BLACKBOX_STATE_SEND_GPS_G_HEADER);
        }
        break;
    case BLACKBOX_STATE_SEND_GPS_G_HEADER:
        blackboxReplenishHeaderBudget();
        //On entry of this state, xmitState.headerIndex is 0 and xmitState.u.fieldIndex is -1
        if (!sendFieldDefinition('G', 0, blackboxGpsGFields, blackboxGpsGFields + 1, ARRAYLEN(blackboxGpsGFields),
                                 &blackboxGpsGFields[0].condition, &blackboxGpsGFields[1].condition)) {
            blackboxSetState(BLACKBOX_STATE_SEND_SLOW_HEADER);
        }
        break;
#endif
    case BLACKBOX_STATE_SEND_SLOW_HEADER:
        blackboxReplenishHeaderBudget();
        //On entry of this state, xmitState.headerIndex is 0 and xmitState.u.fieldIndex is -1
        if (!sendFieldDefinition('S', 0, blackboxSlowFields, blackboxSlowFields + 1, ARRAYLEN(blackboxSlowFields),
                                 NULL, NULL)) {
            blackboxSetState(BLACKBOX_STATE_SEND_SYSINFO);
        }
        break;
    case BLACKBOX_STATE_SEND_SYSINFO:
        blackboxReplenishHeaderBudget();
        //On entry of this state, xmitState.headerIndex is 0
        //Keep writing chunks of the system info headers until it returns true to signal completion
        if (blackboxWriteSysinfo()) {
            /*
             * Wait for header buffers to drain completely before data logging begins to ensure reliable header delivery
             * (overflowing circular buffers causes all data to be discarded, so the first few logged iterations
             * could wipe out the end of the header if we weren't careful)
             */
            if (blackboxDeviceFlushForce()) {
                blackboxSetState(BLACKBOX_STATE_RUNNING);
            }
        }
        break;
    case BLACKBOX_STATE_PAUSED:
        // Only allow resume to occur during an I-frame iteration, so that we have an "I" base to work from
        if (IS_RC_MODE_ACTIVE(BOXBLACKBOX) && blackboxShouldLogIFrame()) {
            // Write a log entry so the decoder is aware that our large time/iteration skip is intended
            flightLogEvent_loggingResume_t resume;
            resume.logIteration = blackboxIteration;
            resume.currentTime = currentTimeUs;
            blackboxLogEvent(FLIGHT_LOG_EVENT_LOGGING_RESUME, (flightLogEventData_t *) &resume);
            blackboxSetState(BLACKBOX_STATE_RUNNING);
            blackboxLogIteration(currentTimeUs);
        }
        // Keep the logging timers ticking so our log iteration continues to advance
        blackboxAdvanceIterationTimers();
        break;
    case BLACKBOX_STATE_RUNNING:
        // On entry to this state, blackboxIteration, blackboxPFrameIndex and blackboxIFrameIndex are reset to 0
        // Prevent the Pausing of the log on the mode switch if in Motor Test Mode
        if (blackboxModeActivationConditionPresent && !IS_RC_MODE_ACTIVE(BOXBLACKBOX) && !startedLoggingInTestMode) {
            blackboxSetState(BLACKBOX_STATE_PAUSED);
        } else {
            blackboxLogIteration(currentTimeUs);
        }
        blackboxAdvanceIterationTimers();
        break;
    case BLACKBOX_STATE_SHUTTING_DOWN:
        //On entry of this state, startTime is set
        /*
         * Wait for the log we've transmitted to make its way to the logger before we release the serial port,
         * since releasing the port clears the Tx buffer.
         *
         * Don't wait longer than it could possibly take if something funky happens.
         */
        if (blackboxDeviceEndLog(blackboxLoggedAnyFrames) && (millis() > xmitState.u.startTime + BLACKBOX_SHUTDOWN_TIMEOUT_MILLIS || blackboxDeviceFlushForce())) {
            blackboxDeviceClose();
            blackboxSetState(BLACKBOX_STATE_STOPPED);
        }
        break;
#ifdef USE_FLASHFS
    case BLACKBOX_STATE_START_ERASE:
        blackboxEraseAll();
        blackboxSetState(BLACKBOX_STATE_ERASING);
        beeper(BEEPER_BLACKBOX_ERASE);
        break;
    case BLACKBOX_STATE_ERASING:
        if (isBlackboxErased()) {
            //Done erasing
            blackboxSetState(BLACKBOX_STATE_ERASED);
            beeper(BEEPER_BLACKBOX_ERASE);
        }
        break;
    case BLACKBOX_STATE_ERASED:
        if (!IS_RC_MODE_ACTIVE(BOXBLACKBOXERASE)) {
            blackboxSetState(BLACKBOX_STATE_STOPPED);
        }
        break;
#endif
    default:
        break;
    }
    // Did we run out of room on the device? Stop!
    if (isBlackboxDeviceFull()) {
#ifdef USE_FLASHFS
        if (blackboxState != BLACKBOX_STATE_ERASING
                && blackboxState != BLACKBOX_STATE_START_ERASE
                && blackboxState != BLACKBOX_STATE_ERASED)
#endif
        {
            blackboxSetState(BLACKBOX_STATE_STOPPED);
            // ensure we reset the test mode flag if we stop due to full memory card
            if (startedLoggingInTestMode) {
                startedLoggingInTestMode = false;
            }
        }
    } else { // Only log in test mode if there is room!
        switch (blackboxConfig()->mode) {
        case BLACKBOX_MODE_MOTOR_TEST:
            // Handle Motor Test Mode
            if (inMotorTestMode()) {
                if (blackboxState == BLACKBOX_STATE_STOPPED) {
                    startInTestMode();
                }
            } else {
                if (blackboxState != BLACKBOX_STATE_STOPPED) {
                    stopInTestMode();
                }
            }
            break;
        case BLACKBOX_MODE_ALWAYS_ON:
            if (blackboxState == BLACKBOX_STATE_STOPPED) {
                startInTestMode();
            }
            break;
        case BLACKBOX_MODE_NORMAL:
        default:
            break;
        }
    }
}

int blackboxCalculatePDenom(int rateNum, int rateDenom) {
    return blackboxIInterval * rateNum / rateDenom;
}

uint8_t blackboxGetRateDenom(void) {
    return blackboxPInterval;
}

/**
 * Call during system startup to initialize the blackbox.
 */
void blackboxInit(void) {
    blackboxResetIterationTimers();
    // an I-frame is written every 32ms
    // blackboxUpdate() is run in synchronisation with the PID loop
    // targetPidLooptime is 1000 for 1kHz loop, 500 for 2kHz loop etc, targetPidLooptime is rounded for short looptimes
    if (targetPidLooptime == 31) { // rounded from 31.25us
        blackboxIInterval = 1024;
    } else if (targetPidLooptime == 63) { // rounded from 62.5us
        blackboxIInterval = 512;
    } else {
        blackboxIInterval = (uint16_t)(32000 / targetPidLooptime);
    }
    // by default p_ratio is 32 and a P-frame is written every 1ms
    // if p_ratio is zero then no P-frames are logged
    if (blackboxConfig()->p_ratio == 0) {
        blackboxPInterval = 0; // blackboxPInterval not used when p_ratio is zero, so just set it to zero
    } else if (blackboxConfig()->p_ratio > blackboxIInterval && blackboxIInterval >= 32) {
        blackboxPInterval = 1;
    } else {
        blackboxPInterval = blackboxIInterval /  blackboxConfig()->p_ratio;
    }
    if (blackboxConfig()->device) {
        blackboxSetState(BLACKBOX_STATE_STOPPED);
    } else {
        blackboxSetState(BLACKBOX_STATE_DISABLED);
    }
    blackboxSInterval = blackboxIInterval * 256; // S-frame is written every 256*32 = 8192ms, approx every 8 seconds
}
#endif
