/*
*****************************************************************************
**
**  File        : stm32_flash_h7.ld
**
**  Abstract    : Linker script for STM32H750VBTx Device with
**                2048KByte FLASH, 1024KByte RAM
**
*****************************************************************************
*/

/* Entry Point */
ENTRY(Reset_Handler)

/*
0x08000000 to 0x081FFFFF  2M full flash
0x08000000 to 0x08003FFF   16K ISR vector
0x08004000 to 0x08007FFF   16K config
0x08008000 to 0x081FFFFF  1984K firmware
*/

/* Specify the memory areas */
MEMORY
{
    ITCM_RAM (rx)           : ORIGIN = 0x00000000, LENGTH = 64K

    ITCM_FLASH (rx)         : ORIGIN = 0x00200000, LENGTH = 16K
    /* config occupies the entire flash sector 1 for the ease of erasure, 16K on H750 */
    ITCM_FLASH_CONFIG (r)   : ORIGIN = 0x00204000, LENGTH = 16K
    ITCM_FLASH1 (rx)        : ORIGIN = 0x00208000, LENGTH = 1984K

    AXIM_FLASH (rx)         : ORIGIN = 0x08000000, LENGTH = 16K
    AXIM_FLASH_CONFIG (r)   : ORIGIN = 0x08004000, LENGTH = 16K
    AXIM_FLASH1 (rx)        : ORIGIN = 0x08008000, LENGTH = 1984K

    DTCM_RAM (rwx)          : ORIGIN = 0x20000000, LENGTH = 128K
    SRAM1 (rwx)             : ORIGIN = 0x24000000, LENGTH = 512K
    SRAM2 (rwx)             : ORIGIN = 0x24080000, LENGTH = 288K
    SRAM3 (rwx)             : ORIGIN = 0x240C8000, LENGTH = 64K
    SRAM4 (rwx)             : ORIGIN = 0x240D8000, LENGTH = 32K
    MEMORY_B1 (rx)          : ORIGIN = 0x60000000, LENGTH = 0K
}

REGION_ALIAS("FLASH", AXIM_FLASH)
REGION_ALIAS("FLASH_CONFIG", AXIM_FLASH_CONFIG)
REGION_ALIAS("FLASH1", AXIM_FLASH1)

REGION_ALIAS("STACKRAM", DTCM_RAM)
REGION_ALIAS("FASTRAM", DTCM_RAM)
REGION_ALIAS("RAM", SRAM1)

INCLUDE "stm32_flash_h7_split.ld"