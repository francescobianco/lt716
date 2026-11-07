# MorseOS for the LT716. The list of sources comes from bundle.ini through tools/bundle.py
# (run by ../build.sh before make, output in _gen/). Built inside the FitPro-LT715-TLSR8232
# tree for its SDK and make rules.
TARGET  = morseos
include _gen/bundle.mk
SRC     = $(MOS_SRC) _gen/apps.c
SDK_SRC = \
	drivers/5316/flash.c \
	drivers/5316/spi.c \
	drivers/5316/timer.c \
	common/string.c \
	common/utility.c \
	vendor/common/blt_common.c \
	$(MOS_SDK_SRC)
DEFINES = -D__TL_LIB_5316__ -D__PROJECT_5316_BLE_SAMPLE__ -I_gen $(addprefix -I,$(MOS_INC))
include ../../sdk/tlsr8232.mk
