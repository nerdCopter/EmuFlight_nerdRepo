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

#define FC_TARGET_MCU                   STM32F745  // not used in EmuF
#define TARGET_BOARD_IDENTIFIER        "S745"      // generic ID
#define TARGET_MANUFACTURER_IDENTIFIER "HBRO"

//#define USE_TARGET_CONFIG
#if defined(KAKUTEF7MINIV2)
#define USBD_PRODUCT_STRING "KAKUTEF7MINIV2"
#elif defined(KAKUTEF7V2)
#define USBD_PRODUCT_STRING "KAKUTEF7V2"
#elif defined(KAKUTEF7V15)
#define USBD_PRODUCT_STRING "KAKUTEF7V15"
#else
#define USBD_PRODUCT_STRING "KAKUTEF7HDV"
#endif

#define LED0_PIN                PA2

#define USE_BEEPER
#define BEEPER_PIN              PD15
#define BEEPER_INVERTED

#define USE_ACC
#define USE_GYRO

//ICM20689
#define USE_ACC_SPI_ICM20689
#define USE_GYRO_SPI_ICM20689
#define ACC_ICM20689_ALIGN         CW270_DEG
#define GYRO_ICM20689_ALIGN        CW270_DEG
#define ICM20689_CS_PIN            PE4
#define ICM20689_SPI_INSTANCE      SPI4

// MPU6000
#define USE_ACC_SPI_MPU6000
#define USE_GYRO_SPI_MPU6000
#define GYRO_MPU6000_ALIGN      CW270_DEG
#define ACC_MPU6000_ALIGN       CW270_DEG
#define MPU_INT_EXTI               PE1

#define MPU6000_CS_PIN          SPI4_NSS_PIN
#define MPU6000_SPI_INSTANCE    SPI4
#define GYRO_1_CS_PIN            MPU6000_CS_PIN
#define GYRO_1_SPI_INSTANCE      MPU6000_SPI_INSTANCE

#define ACC_1_ALIGN              ACC_MPU6000_ALIGN
#define GYRO_1_ALIGN             GYRO_MPU6000_ALIGN

#define USE_MPU_DATA_READY_SIGNAL
#define USE_EXTI

#define USE_VCP
#define USE_USB_DETECT
#define USB_DETECT_PIN          PA8

#define USE_UART1
#define UART1_TX_PIN            PA9
#define UART1_RX_PIN            PA10

#define USE_UART2
#define UART2_TX_PIN            PD5
#define UART2_RX_PIN            PD6

#define USE_UART3
#define UART3_TX_PIN            PB10
#define UART3_RX_PIN            PB11

#define USE_UART4
#define UART4_TX_PIN            PA0
#define UART4_RX_PIN            PA1

#define USE_UART6
#define UART6_TX_PIN            PC6
#define UART6_RX_PIN            PC7

#define USE_UART7
#define UART7_TX_PIN            NONE
#define UART7_RX_PIN            PE7


#define USE_SOFTSERIAL1
#define USE_SOFTSERIAL2

#define SERIAL_PORT_COUNT 9 //VCP,UART1,UART2,UART3,UAER4,UART6,UART7

#define USE_ESCSERIAL
#define ESCSERIAL_TIMER_TX_PIN  PE13 // ( Hardware=0)

#define USE_SPI
#define USE_SPI_DEVICE_1   //SD Card
#define USE_SPI_DEVICE_2   //OSD
#define USE_SPI_DEVICE_4   //MPU6000

#define SPI1_NSS_PIN            PA4
#define SPI1_SCK_PIN            PA5
#define SPI1_MISO_PIN           PA6
#define SPI1_MOSI_PIN           PA7

#define SPI2_NSS_PIN            PB12
#define SPI2_SCK_PIN            PB13
#define SPI2_MISO_PIN           PB14
#define SPI2_MOSI_PIN           PB15

#define SPI4_NSS_PIN            PE4
#define SPI4_SCK_PIN            PE2
#define SPI4_MISO_PIN           PE5
#define SPI4_MOSI_PIN           PE6

#if defined(KAKUTEF7V15) || defined(KAKUTEF7V2) ||defined(KAKUTEF7MINIV2)
#define CAMERA_CONTROL_PIN      PB3

#define USE_MAX7456
#define MAX7456_SPI_INSTANCE    SPI2
#define MAX7456_SPI_CS_PIN      SPI2_NSS_PIN
#define MAX7456_SPI_CLK         (SPI_CLOCK_STANDARD) // 10MHz
#define MAX7456_RESTORE_CLK     (SPI_CLOCK_FAST)
#endif
#if defined(KAKUTEF7MINIV2)
#define ENABLE_BLACKBOX_LOGGING_ON_SPIFLASH_BY_DEFAULT
#define USE_FLASHFS
#define USE_FLASH_M25P16
#define FLASH_CS_PIN            SPI1_NSS_PIN
#define FLASH_SPI_INSTANCE      SPI1
#else
#define USE_SDCARD
#define SDCARD_DETECT_INVERTED
#define SDCARD_DETECT_PIN                   PD8

#define SDCARD_SPI_INSTANCE                 SPI1
#define SDCARD_SPI_CS_PIN                   SPI1_NSS_PIN

#define SDCARD_SPI_INITIALIZATION_CLOCK_DIVIDER 256 // 422kHz

#define SDCARD_SPI_FULL_SPEED_CLOCK_DIVIDER      8 // 27MHz

#define SDCARD_DMA_STREAM_TX_FULL             DMA2_Stream5
#define SDCARD_DMA_CHANNEL                    3
#define ENABLE_BLACKBOX_LOGGING_ON_SDCARD_BY_DEFAULT
#endif

#define USE_I2C
#define USE_I2C_DEVICE_1
#define I2C_DEVICE              (I2CDEV_1)
#define I2C1_SCL                PB6
#define I2C1_SDA                PB7

#define USE_BARO
#define USE_BARO_BMP280
#define BARO_I2C_INSTANCE     I2C_DEVICE

#define USE_MAG
#define USE_MAG_HMC5883
#define USE_MAG_QMC5883
#define MAG_I2C_INSTANCE      I2C_DEVICE

#define SENSORS_SET (SENSOR_ACC | SENSOR_BARO)

#define DEFAULT_VOLTAGE_METER_SOURCE VOLTAGE_METER_ADC
#define DEFAULT_CURRENT_METER_SOURCE CURRENT_METER_ADC

#define USE_ADC
#define CURRENT_METER_ADC_PIN   PC2
#define VBAT_ADC_PIN            PC3
#define RSSI_ADC_PIN            PC5



#define DEFAULT_FEATURES        (FEATURE_OSD)
#define DEFAULT_RX_FEATURE      FEATURE_RX_SERIAL
#define SERIALRX_UART           SERIAL_PORT_USART6
#define SERIALRX_PROVIDER       SERIALRX_SBUS
#define ESC_SENSOR_UART         SERIAL_PORT_USART7

#define TARGET_IO_PORTA 0xffff
#define TARGET_IO_PORTB 0xffff
#define TARGET_IO_PORTC 0xffff
#define TARGET_IO_PORTD 0xffff
#define TARGET_IO_PORTE 0xffff

#define USABLE_TIMER_CHANNEL_COUNT 8

#define USED_TIMERS  ( TIM_N(1) | TIM_N(5) | TIM_N(3) | TIM_N(4) | TIM_N(8) )
