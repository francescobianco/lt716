# LT716 battery monitor: a micro firmware with no Bluetooth that shows the battery and charger
# voltages on the screen, then goes back to the vendor updater by itself. st7735, font and
# battery are copies of the MorseOS libraries (the build mounts only this folder).
# Built inside the FitPro-LT715-TLSR8232 tree, see ../build.sh.
TARGET  = lt716_battmon
SRC     = \
	main.c \
	st7735.c \
	font.c \
	battery.c
SDK_SRC = \
	drivers/5316/flash.c \
	drivers/5316/spi.c \
	drivers/5316/timer.c
DEFINES = -D__TL_LIB_5316__ -D__PROJECT_5316_BLE_SAMPLE__ -Os  # -Os: smallest image, fastest OTA
include ../../sdk/tlsr8232.mk
