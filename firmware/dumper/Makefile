# LT716 flash dumper. Built inside the FitPro-LT715-TLSR8232 tree (for its SDK and make
# rules), see ../build.sh.
TARGET  = lt716_dumper
SRC     = \
	main.c \
	app_att.c
SDK_SRC = \
	drivers/5316/flash.c \
	drivers/5316/spi.c \
	drivers/5316/timer.c \
	common/string.c \
	common/utility.c \
	vendor/common/blt_common.c
DEFINES = -D__TL_LIB_5316__ -D__PROJECT_5316_BLE_SAMPLE__
include ../../sdk/tlsr8232.mk
