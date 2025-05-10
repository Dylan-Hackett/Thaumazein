# Project Name
TARGET = thaumazein

# Library Locations
LIBDAISY_DIR = lib/libdaisy
DAISYSP_DIR = lib/DaisySP
STMLIB_DIR = eurorack/stmlib
MPR121_DIR = .
EFFECTS_DIR = Effects

# Sources - Define BEFORE including core Makefile
CPP_SOURCES += Thaumazein.cpp \
              Interface.cpp \
              Arpeggiator.cpp \
              Polyphony.cpp \
              AudioProcessor.cpp \
              VoiceEnvelope.cpp \
              mpr121_daisy.cpp \
              SynthStateStorage.cpp \
              Effects/reverbsc.cpp \
              Effects/BiquadFilters.cpp \
              Effects/DelayEffect.cpp

# Add .cc sources to be handled
CC_SOURCES += $(wildcard eurorack/plaits/dsp/*.cc)
CC_SOURCES += $(wildcard eurorack/plaits/dsp/engine/*.cc)
CC_SOURCES += $(wildcard eurorack/plaits/dsp/speech/*.cc)
CC_SOURCES += $(wildcard eurorack/plaits/dsp/physical_modelling/*.cc)
CC_SOURCES += eurorack/plaits/resources.cc
CC_SOURCES += $(STMLIB_DIR)/dsp/units.cc \
              $(STMLIB_DIR)/utils/random.cc

# Define DaisySP sources *before* including core Makefile
DAISYSP_SOURCES += $(wildcard $(DAISYSP_DIR)/Source/*.cpp)
DAISYSP_SOURCES += $(wildcard $(DAISYSP_DIR)/Source/*/*.cpp)

# Define vpath for .cc sources BEFORE including core Makefile
vpath %.cc $(sort $(dir $(CC_SOURCES)))

# Define Includes BEFORE including core Makefile
C_INCLUDES += \
-I$(MPR121_DIR) \
-I$(EFFECTS_DIR) \
-I. \
-Iresources \
-Ieurorack \
-I../..

# Hardware target
HWDEFS = -DSEED

# Ensure build is treated as boot application (code executes from QSPI)
C_DEFS += -DBOOT_APP
APP_TYPE = BOOT_QSPI

# Warning suppression
C_INCLUDES += -Wno-unused-local-typedefs

# Optimization level (can be overridden)
OPT ?= -Os -s

# Set target Linker Script to QSPI (no 256k offset)
LDSCRIPT = $(LIBDAISY_DIR)/core/STM32H750IB_qspi.lds

# Override QSPI write address to match linker (no 0x40000 offset)
QSPI_ADDRESS = 0x90040000

# Add QSPI section start flags
LDFLAGS += -Wl,--gc-sections

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile # Include core makefile

# --- Additions/Overrides AFTER Core Makefile --- 

# Add .cc source files to the OBJECTS list defined by the core makefile
# Use the same pattern (notdir/vpath) as the core makefile
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))

# Add the rule for compiling .cc files
# This pattern should match the .c/.cpp rules in the core Makefile
$(BUILD_DIR)/%.o: %.cc $(MAKEFILE_LIST) | $(BUILD_DIR)
	@echo Compiling $< 
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) $(TARGET_ARCH) -c $< -o $@ $(DEPFLAGS) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst))

# Explicitly override the linker rule AFTER OBJECTS is fully populated
$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	@echo Linking $(TARGET).elf with updated OBJECTS list...
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

# No need to override other rules (all, .c, .cpp, .bin, .hex, clean, etc.)
# Let the core Makefile handle those.

# -------------------------------------------------------------
# Convenience targets for QSPI workflow
# -------------------------------------------------------------
# 1. flash-stub : Builds the project with the internal-flash linker
#    and flashes it to alt-setting 0 (0x08000000).  This becomes the
#    tiny boot stub that jumps to QSPI.
# 2. flash-app  : Normal build (QSPI linker) + flash to alt-setting 1
#    (0x90040000).
# -------------------------------------------------------------

flash-stub:
	$(MAKE) clean
	$(MAKE) program-boot

flash-app:
	$(MAKE) clean
	$(MAKE) all
	$(MAKE) program-app

# Flash application code directly to QSPI external flash via the QSPI bootloader stub (alt 0)
program-app:
	dfu-util -a 0 -s $(QSPI_ADDRESS):leave -D $(BUILD_DIR)/$(TARGET_BIN) -d ,0483:df11 -R

.PHONY: flash-stub flash-app