F405_TARGETS   += $(TARGET)
FEATURES       += VCP ONBOARDFLASH

TARGET_SRC = \
drivers/accgyro/accgyro_mpu.c \
drivers/accgyro/accgyro_spi_bmi270.c \
drivers/light_led.h \
drivers/light_ws2811strip.c \
drivers/max7456.c \

# This resource file generated using https://github.com/nerdCopter/target-convert
# Commit: bacae61
