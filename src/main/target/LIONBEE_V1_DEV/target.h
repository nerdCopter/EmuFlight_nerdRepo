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

#pragma once

// Source: betaflight/config configs/NEBD/LIONBEE_V1_DEV/config.h. A line starting with // is in the reference and not supported by EF yet.

#define TARGET_MANUFACTURER_IDENTIFIER "NEBD"
#define USBD_PRODUCT_STRING "LIONBEE_V1_DEV"
#ifndef TARGET_BOARD_IDENTIFIER
#define TARGET_BOARD_IDENTIFIER "A435"
#endif

#define FC_TARGET_MCU     AT32F435G
#define BOARD_NAME        LIONBEE_V1_DEV
#define MANUFACTURER_ID   NEBD

#ifndef AT32F435
#define AT32F435
#endif

// --- AT32 MCU defaults
#define FLASH_PAGE_SIZE ((uint32_t)0x0800) // 2K sectors (AT32F435G)
#define USE_USB_DETECT
#define USE_PERSISTENT_MSC_RTC
#define USE_SPI_DMA_ENABLE_LATE
#define USE_BEEPER
#undef USE_PPM
#undef USE_PWM
#undef USE_RX_CC2500
#undef USE_SERIAL_4WAY_SK_BOOTLOADER
#undef USE_TRANSPONDER
// EF escserial and camera control drive STM32 timer registers; not ported to AT32.
#undef USE_ESCSERIAL
#undef USE_CAMERA_CONTROL
#undef USE_DSHOT_DMAR

#define TARGET_IO_PORTA 0xffff
#define TARGET_IO_PORTB 0xffff
#define TARGET_IO_PORTC 0xffff
#define TARGET_IO_PORTD 0xffff
#define TARGET_IO_PORTH 0xffff

// --- Sensors
#define USE_ACC
#define USE_ACC_SPI_ICM42688P
#define USE_GYRO
#define USE_GYRO_SPI_ICM42688P
// Not supported: ENABLE_BMI270_ALIGN_AS_ICM (rotates BMI270 axes to the ICM42688P frame). Without it the BMI270 reads rotated. No BMI270 unit tested.
// #define USE_ACCGYRO_BMI270
#define USE_BARO
#define USE_BARO_DPS310
#define USE_MAG
#define USE_MAG_QMC5883
#define USE_GPS

#define USE_SPI_GYRO
#define USE_EXTI
#define USE_GYRO_EXTI
#define USE_MPU_DATA_READY_SIGNAL

#define GYRO_1_CS_PIN        PA4
#define GYRO_1_EXTI_PIN      PC13
#define GYRO_1_SPI_BUS       SPIDEV_1
#define MPU_INT_EXTI         PC13
#define ICM42688P_CS_PIN     PA4
#define ICM42688P_SPI_BUS    SPIDEV_1

// EF-only difference: the reference has GYRO_1_ALIGN CW270_DEG and DEFAULT_ALIGN_BOARD_PITCH 180 (SMD side down). EF has no board-pitch default.
// Sensor rotation CW270 then pitch 180 equals CW90_DEG_FLIP: (x,y,z) -> (-y,x,z) -> (y,x,-z).
#define ACC_ICM42688P_ALIGN  CW90_DEG_FLIP
#define GYRO_ICM42688P_ALIGN CW90_DEG_FLIP
// Not supported: DEFAULT_ALIGN_BOARD_PITCH 180 (see above).
// #define DEFAULT_ALIGN_BOARD_PITCH 180
// Not supported: YAW_MOTORS_REVERSED 1 (reference sets it; EF has no such define).
// #define YAW_MOTORS_REVERSED 1

// --- LED, LED strip
#define LED0_PIN             PB8
#define LED_STRIP_PIN        PB1

// --- VTX: RTC6705 on shared SPI3, chip select PB2, external power code pins
// RTC6705 external power code: PA14 = bit 0, PB7 = bit 1; 00 = lowest (boot default), 01 = mid, 10 = max, 11 is undefined and unsafe.
// Schematic: PA14 and PB7 feed the OSD MCU (VTX_CTRL_A0/A1), not the RTC6705; the power path and output per code are unmeasured.
// Schematic: Q4 on VUSB pulls the VTX regulator enable low, so the VTX is probably off on USB power (unconfirmed).
#define ENABLE_RTC6705_DYNAMIC_POWER_CTRL 1
#define RTC6705_EX_POWER_1_PIN PA14
#define RTC6705_EX_POWER_2_PIN PB7
#define USE_VTX_RTC6705
#define RTC6705_CS_PIN       PB2
#define RTC6705_SPI_INSTANCE SPI3
// Unproven: taken from the reference config; its effect is not tested.
#define SPI_SHARED_MAX7456_AND_RTC6705

// --- SPI
#define USE_SPI
#define USE_SPI_DEVICE_1
#define USE_SPI_DEVICE_3
#define SPI1_SCK_PIN         PA5
#define SPI1_MISO_PIN        PA6
#define SPI1_MOSI_PIN        PA7
#define SPI3_SCK_PIN         PB12
#define SPI3_MISO_PIN        PB4
#define SPI3_MOSI_PIN        PB0

// --- Flash (BY25Q64, ID row in flash_m25p16.c) and OSD
#define USE_FLASH
#define USE_FLASH_M25P16
#define USE_FLASHFS
#define FLASH_CS_PIN         PC15
#define FLASH_SPI_INSTANCE   SPI3
#define ENABLE_BLACKBOX_LOGGING_ON_SPIFLASH_BY_DEFAULT
#define USE_MAX7456
#define MAX7456_SPI_CS_PIN   PC14
#define MAX7456_SPI_INSTANCE SPI3

// --- ExpressLRS SPI receiver (SX1280 on SPI2): EF has no SX1280 or ExpressLRS driver
// #define USE_RX_SPI
// #define USE_RX_EXPRESSLRS
// #define USE_RX_SX1280
// #define USE_SPI_DEVICE_2
// #define SPI2_SCK_PIN                    PB13
// #define SPI2_MISO_PIN                   PB14
// #define SPI2_MOSI_PIN                   PB15
// #define RX_SPI_INSTANCE                 SPI2
// #define RX_SPI_CS_PIN                   PA8
// #define RX_SPI_EXTI_PIN                 PB3
// #define RX_SPI_BIND_PIN                 PH2
// #define RX_SPI_LED_PIN                  PB9
// #define RX_SPI_LED_INVERTED
// #define RX_EXPRESSLRS_SPI_RESET_PIN     PH3
// #define RX_EXPRESSLRS_SPI_BUSY_PIN      PA15
// #define RX_EXPRESSLRS_TIMER_INSTANCE    TMR5
// #define RX_SPI_DEFAULT_PROTOCOL         RX_SPI_EXPRESSLRS
// #define DEFAULT_RX_FEATURE              FEATURE_RX_SPI
#undef USE_RX_SPI
#undef USE_RX_EXPRESSLRS

// --- I2C2: DPS310 baro, QMC5883 compass
#define USE_I2C
#define USE_I2C_DEVICE_2
#define I2C_DEVICE           (I2CDEV_2)
#define I2C2_SCL             PB10
#define I2C2_SDA             PB11
#define BARO_I2C_INSTANCE    (I2CDEV_2)
#define MAG_I2C_INSTANCE     (I2CDEV_2)

// --- Serial: VCP and UART5 (GPS)
#define USE_VCP
#define USE_UART5
#define UART5_TX_PIN         PB6
#define UART5_RX_PIN         PB5
#define SERIAL_PORT_COUNT    2
// The reference sets GPS_UART (default GPS function on UART5); EF sets it in config.c with targetSerialPortFunctionConfig.

// --- ADC
#define ADC_INSTANCE         ADC1
#define ADC1_DMA_OPT         12
#define VBAT_ADC_PIN         PA2
#define CURRENT_METER_ADC_PIN PA3
#define DEFAULT_VOLTAGE_METER_SOURCE VOLTAGE_METER_ADC
#define DEFAULT_CURRENT_METER_SOURCE CURRENT_METER_ADC
// Unproven: scale copied from the factory dump in the reference, not calibrated.
#define DEFAULT_CURRENT_METER_SCALE 447

// --- Defaults
#define DEFAULT_FEATURES     (FEATURE_GPS | FEATURE_OSD | FEATURE_LED_STRIP)
// Not supported: DEFAULT_VIDEO_SYSTEM VIDEO_SYSTEM_AUTO and DEFAULT_OSD_DISPLAYPORT_DEVICE (EF has no such defines).
// #define DEFAULT_VIDEO_SYSTEM            VIDEO_SYSTEM_AUTO
// #define DEFAULT_OSD_DISPLAYPORT_DEVICE  OSD_DISPLAYPORT_DEVICE_AUTO
// Not supported: SYSTEM_HSE_MHZ is read by STM32 system files only; the reference sets 8.
// #define SYSTEM_HSE_MHZ                  8
// config.c seeds the GPS function on UART5, the u-blox provider, Galileo and the LED strip layout.
#define USE_TARGET_CONFIG

// --- Motors and LED strip timers.
// LED strip on DMA1 Ch1 (option 0); motors on DMA2 Ch2 to Ch5 (options 8 to 11) so they do not take the LED stream.
#define TIMER_PIN_MAPPING \
    TIMER_PIN_MAP( 0, LED_STRIP_PIN , 2,  0) \
    TIMER_PIN_MAP( 1, PA1 , 1,  8) /* motor 1 */ \
    TIMER_PIN_MAP( 2, PA0 , 1,  9) /* motor 2 */ \
    TIMER_PIN_MAP( 3, PA9 , 1, 10) /* motor 3 */ \
    TIMER_PIN_MAP( 4, PA10, 1, 11) /* motor 4 */
#define MOTOR1_PIN           PA1
#define MOTOR2_PIN           PA0
#define MOTOR3_PIN           PA9
#define MOTOR4_PIN           PA10
