/*
 * This file is part of EmuFlight. It is derived from Betaflight.
 *
 * This is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 * You should have received a copy of the GNU General Public
 * License along with this software.
 * If not, see <http://www.gnu.org/licenses/>.
 */

// This resource file generated using https://github.com/nerdCopter/target-convert
// Commit: d84474d + 1 file changed, 24 deletions(-)

#pragma once

#define BOARD_NAME        OMNINXT4
#define MANUFACTURER_ID   AIRB
#define TARGET_BOARD_IDENTIFIER "S405"  // generic ID
#define FC_TARGET_MCU     STM32F405     // not used in EmuF

#define USE_GYRO
#define USE_GYRO_SPI_MPU6500
#define USE_ACC
#define USE_ACC_SPI_MPU6500
#define USE_GYRO_SPI_MPU6000
#define USE_ACC_SPI_MPU6000
#define USE_BARO_SPI_LPS
#define USE_FLASH
#define USE_FLASH_W25Q128FV
#define USE_MAX7456
#define USE_BARO

#define USE_VCP
#define USE_FLASHFS
#define USE_FLASH_M25P16    // 16MB Micron M25P16 driver; drives all unless QSPI
#define USE_OSD

#define USE_LED
#define LED0_PIN             PB2
#define LED_STRIP_PIN        PA9
#define USE_BEEPER
#define BEEPER_PIN           PC13
#define BEEPER_INVERTED
#define CAMERA_CONTROL_PIN   PB15

#define USE_SPI
#define USE_SPI_DEVICE_1
#define SPI1_SCK_PIN         PA5
#define SPI1_MISO_PIN        PA6
#define SPI1_MOSI_PIN        PA7
#define USE_SPI_DEVICE_2
#define SPI2_SCK_PIN         PB13
#define SPI2_MISO_PIN        PB14
#define SPI2_MOSI_PIN        PC3
#define USE_SPI_DEVICE_3
#define SPI3_SCK_PIN         PC10
#define SPI3_MISO_PIN        PC11
#define SPI3_MOSI_PIN        PC12

#define USE_SPI_GYRO
#define USE_MPU_DATA_READY_SIGNAL

#define ACC_1_ALIGN          CW0_DEG
#define GYRO_1_ALIGN         CW0_DEG
#define GYRO_1_CS_PIN        PB12
#define GYRO_1_SPI_INSTANCE SPI1

#define USE_DUAL_GYRO

#define ACC_2_ALIGN          CW270_DEG
#define GYRO_2_ALIGN         CW270_DEG
#define GYRO_2_CS_PIN        PA8
#define GYRO_2_SPI_INSTANCE SPI1

#define USE_UART1
#define UART1_TX_PIN         PB6
#define UART1_RX_PIN         PB7
#define USE_UART2
#define UART2_TX_PIN         PA2
#define UART2_RX_PIN         PA3
#define USE_UART3
#define UART3_TX_PIN         PB10
#define UART3_RX_PIN         PB11
#define USE_UART4
#define UART4_TX_PIN         PA0
#define UART4_RX_PIN         PA1
#define USE_UART5
#define UART5_RX_PIN         PD2
#define USE_UART6
#define UART6_TX_PIN         PC6
#define UART6_RX_PIN         PC7
#define INVERTER_PIN_UART2   PC5
#define SERIAL_PORT_COUNT 7

#define BARO_CS_PIN          PA10
#define BARO_SPI_INSTANCE SPI2
#define USE_I2C
#define USE_I2C_DEVICE_1
#define I2C_DEVICE_1      (I2CDEV_1)
#define MAG_I2C_INSTANCE (I2CDEV_1)
#define DASHBOARD_I2C_INSTANCE (I2CDEV_1)
#define I2C1_SCL PB8
#define I2C1_SDA PB9

#define FLASH_CS_PIN         PC14
#define FLASH_SPI_INSTANCE SPI2
#define ENABLE_BLACKBOX_LOGGING_ON_SPIFLASH_BY_DEFAULT

#define MAX7456_SPI_CS_PIN   PA15
#define MAX7456_SPI_INSTANCE SPI2

#define USE_ADC
#define VBAT_ADC_PIN PC0
#define CURRENT_METER_ADC_PIN PC1
#define RSSI_ADC_PIN PC4
#define ADC1_DMA_OPT        1
#define ADC1_DMA_STREAM DMA2_Stream4 //# ADC 1: DMA2 Stream 4 Channel 0
#define DEFAULT_VOLTAGE_METER_SOURCE VOLTAGE_METER_ADC
#define DEFAULT_CURRENT_METER_SOURCE CURRENT_METER_ADC

#define ENABLE_DSHOT_DMAR true

#define USE_ESCSERIAL
#define ESCSERIAL_TIMER_TX_PIN PB7


#define TARGET_IO_PORTA 0xffff
#define TARGET_IO_PORTB 0xffff
#define TARGET_IO_PORTC 0xffff
#define TARGET_IO_PORTD 0xffff
// notice - masks were programmatically generated - please verify last port group for 0xffff or (BIT(2))

#define DEFAULT_FEATURES       (FEATURE_OSD | FEATURE_TELEMETRY | FEATURE_AIRMODE | FEATURE_RX_SERIAL)
#define DEFAULT_RX_FEATURE     FEATURE_RX_SERIAL

#define USABLE_TIMER_CHANNEL_COUNT 21
#define USED_TIMERS ( TIM_N(1) | TIM_N(2) | TIM_N(3) | TIM_N(4) | TIM_N(5) | TIM_N(8) | TIM_N(9) | TIM_N(10) | TIM_N(11) | TIM_N(12) )

// notice - this file was programmatically generated and may be incomplete.
