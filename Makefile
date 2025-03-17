#------------------------------------------------------------------------------
#
#            CPS1/CPS2/NEOGEO/NEOGEO CDZ Emulator for PSP Makefile
#
#------------------------------------------------------------------------------

#------------------------------------------------------------------------------
# Configuration
#------------------------------------------------------------------------------

BUILD_CPS1 = 0
BUILD_CPS2 = 0
BUILD_MVS = 0
BUILD_NCDZ = 0

LARGE_MEMORY = 0
KERNEL_MODE = 0
COMMAND_LIST = 0
ADHOC = 0
NO_GUI = 0
SAVE_STATE = 0
UI_32BPP = 1
RELEASE = 0
SYSTEM_BUTTONS = 0
DEBUG = 0

#------------------------------------------------------------------------------
# Version
#------------------------------------------------------------------------------

VERSION_MAJOR = 2
VERSION_MINOR = 4
VERSION_BUILD = 0


#------------------------------------------------------------------------------
# Defines
#------------------------------------------------------------------------------

OS = psp

# You need to select at least one target, show error if none is selected
ifeq ($(BUILD_CPS1), 0)
ifeq ($(BUILD_CPS2), 0)
ifeq ($(BUILD_MVS), 0)
ifeq ($(BUILD_NCDZ), 0)
$(error No target selected, you need to enable at least one target. Available targets are CPS1, CPS2, MVS and NCDZ)
endif
endif
endif
endif

ifeq ($(BUILD_CPS1), 1)
TARGET = CPS1
PSP_EBOOT_ICON = data/cps1.png
endif

ifeq ($(BUILD_CPS2), 1) 
TARGET = CPS2
PSP_EBOOT_ICON = data/cps2.png
endif

ifeq ($(BUILD_MVS), 1)
TARGET = MVS
PSP_EBOOT_ICON = data/mvs.png
endif

ifeq ($(BUILD_NCDZ), 1)
TARGET = NCDZ
ADHOC = 0
PSP_EBOOT_ICON = data/ncdz.png
endif

ifeq ($(ADHOC), 1)
SAVE_STATE = 1
endif

ifeq ($(VERSION_BUILD), 0)
VERSION_STR = $(VERSION_MAJOR).$(VERSION_MINOR)
else
VERSION_STR = $(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_BUILD)
endif

EXTRA_TARGETS = EBOOT.PBP
ifeq ($(LARGE_MEMORY), 1)
PSP_EBOOT_TITLE = $(TARGET) $(VERSION_STR) for PSP Slim
else
PSP_EBOOT_TITLE = $(TARGET) $(VERSION_STR) for PSP
endif

#------------------------------------------------------------------------------
# Utilities
#------------------------------------------------------------------------------

MD = -mkdir
RM = -rm

#------------------------------------------------------------------------------
# File include path
#------------------------------------------------------------------------------

INCDIR = \
	src \
	src/zip \


#------------------------------------------------------------------------------
# Object Files (common)
#------------------------------------------------------------------------------

MAINOBJS = \
	emumain.o \
	zip/zfile.o \
	zip/unzip.o \
	sound/sndintrf.o \
	common/cache.o \
	common/filer.o \
	common/loadrom.o \
	common/thread_driver.o \
	common/audio_driver.o \
	common/power_driver.o \
	common/ticker_driver.o \
	common/input_driver.o \
	common/video_driver.o \
	common/ui_text_driver.o \
	common/platform_driver.o \
	common/sound.o \

ifeq ($(ADHOC), 1)
MAINOBJS += common/adhoc.o
endif

ifeq ($(COMMAND_LIST), 1)
MAINOBJS += common/cmdlist.o
endif

ifeq ($(SAVE_STATE), 1)
MAINOBJS += common/state.o
endif

ifeq ($(NO_GUI), 1)
OSOBJS += $(OS)/psp_no_gui.o
else
FONTOBJS = \
	$(OS)/font/graphic.o \
	$(OS)/font/ascii_14p.o \
	$(OS)/font/font_s.o \
	$(OS)/font/bshadow.o \
	$(OS)/font/command.o \
	$(OS)/font/ascii_14.o \
	$(OS)/font/latin1_14.o \
	$(OS)/font/gbk_s14.o \
	$(OS)/font/gbk_tbl.o

OSOBJS += \
	$(OS)/config.o \
	$(OS)/filer.o \
	$(OS)/ui.o \
	$(OS)/ui_draw.o \
	$(OS)/ui_menu.o \
	$(OS)/png.o \

endif

OSOBJS += \
	$(OS)/$(OS)_platform.o \
	$(OS)/$(OS)_thread.o \
	$(OS)/$(OS)_audio.o \
	$(OS)/$(OS)_power.o \
	$(OS)/$(OS)_ticker.o \
	$(OS)/$(OS)_input.o \
	$(OS)/$(OS)_video.o \
	$(OS)/$(OS)_ui_text.o \

ifeq ($(ADHOC), 1)
OSOBJS += $(OS)/adhoc.o
endif

ifeq ($(SYSTEM_BUTTONS), 1)
OSOBJS += $(OS)/SystemButtons.o
endif

ifeq ($(UI_32BPP), 1)
ifeq ($(NO_GUI), 0)
OSOBJS += $(OS)/wallpaper.o
endif
endif

#------------------------------------------------------------------------------
# Include makefiles
#------------------------------------------------------------------------------

include src/makefiles/$(TARGET).mak


#------------------------------------------------------------------------------
# Compiler Flags
#------------------------------------------------------------------------------

CFLAGS = \
	-Wno-unused-but-set-variable \
	-Wno-unused-function \
	-Werror \

ifeq ($(DEBUG), 1)
CFLAGS += -O0 -g
else
CFLAGS += -O3
endif

#------------------------------------------------------------------------------
# Compiler Defines
#------------------------------------------------------------------------------

CDEFS = \
	-DBUILD_$(TARGET)=1 \
	-DTARGET_STR='"$(TARGET)"' \
	-DVERSION_STR='"$(VERSION_STR)"' \
	-DVERSION_MAJOR=$(VERSION_MAJOR) \
	-DVERSION_MINOR=$(VERSION_MINOR) \
	-DVERSION_BUILD=$(VERSION_BUILD) \
	-DPSP

ifeq ($(LARGE_MEMORY), 1)
CDEFS += -DLARGE_MEMORY=1
endif

ifeq ($(KERNEL_MODE), 1)
CDEFS += -DKERNEL_MODE=1
endif

ifeq ($(ADHOC), 1)
CDEFS += -DADHOC=1
endif

ifeq ($(SAVE_STATE), 1)
CDEFS += -DSAVE_STATE=1
endif

ifeq ($(COMMAND_LIST), 1)
CFLAGS += -DCOMMAND_LIST=1
endif

ifeq ($(UI_32BPP), 1)
CFLAGS += -DVIDEO_32BPP=1
else
CFLAGS += -DVIDEO_32BPP=0
endif

ifeq ($(RELEASE), 1)
CDEFS += -DRELEASE=1
else
CDEFS += -DRELEASE=0
endif

CFLAGS += $(CDEFS)

#------------------------------------------------------------------------------
# Linker Flags
#------------------------------------------------------------------------------

LIBDIR =
LDFLAGS = -L$(shell psp-config --psp-prefix)


#------------------------------------------------------------------------------
# Library
#------------------------------------------------------------------------------

LIBS = -lpspaudio -lpspgu -lpsppower -lz

ifeq ($(LARGE_MEMORY), 1)
LIBS += -lpspkubridge
endif

ifeq ($(ADHOC), 1)
LIBS += -lpspwlan -lpspnet_adhoc -lpspnet_adhocctl -lpspnet_adhocmatching
endif

ifeq ($(BUILD_NCDZ), 1)
LIBS += -lmad
endif

#------------------------------------------------------------------------------
# Rules to make libraries
#------------------------------------------------------------------------------

ALLOBJS = $(MAINOBJS) $(COREOBJS) $(OSOBJS) $(FONTOBJS) $(ICONOBJS)
OBJS = $(ALLOBJS:%=src/%)

PSPSDK=$(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
