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
# display wiring: [hardware] band in bundle.ini, or make BAND=0|1|2 (0 = detect at boot)
BAND   ?= $(MOS_BAND)
DEFINES += -DLT716_BAND=$(BAND)
include ../../sdk/tlsr8232.mk
# optimisation of the MorseOS sources only (the SDK drivers stay as they were tested, -O0):
# make APP_OPT=-Os
APP_OPT ?=
$(BUILD_DIR)/app/%.o: CCFLAGS += $(APP_OPT)
# measurement only, not used: the SDK drivers optimised too (make SDK_OPT=-Os)
SDK_OPT ?=
$(BUILD_DIR)/sdk/%.o: CCFLAGS += $(SDK_OPT)
