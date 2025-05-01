# Project Name
TARGET = thaumazein

# Library Locations
LIBDAISY_DIR = lib/libdaisy
DAISYSP_DIR = lib/DaisySP
STMLIB_DIR = eurorack/stmlib
MPR121_DIR = .
EFFECTS_DIR = Effects

# Sources
CPP_SOURCES = Thaumazein.cpp \
              Interface.cpp \
              Arpeggiator.cpp \
              Polyphony.cpp \
              AudioProcessor.cpp \
              VoiceEnvelope.cpp \
              mpr121_daisy.cpp \
              Effects/reverbsc.cpp \
              Effects/BiquadFilters.cpp

CC_SOURCES += $(wildcard eurorack/plaits/dsp/*.cc)
CC_SOURCES += $(wildcard eurorack/plaits/dsp/engine/*.cc)
CC_SOURCES += $(wildcard eurorack/plaits/dsp/speech/*.cc)
CC_SOURCES += $(wildcard eurorack/plaits/dsp/physical_modelling/*.cc)
CC_SOURCES += eurorack/plaits/resources.cc

CC_SOURCES += $(STMLIB_DIR)/dsp/units.cc \
$(STMLIB_DIR)/utils/random.cc
# cv_scaler.cc

# Define DaisySP sources *before* including core Makefile
DAISYSP_SOURCES += $(wildcard $(DAISYSP_DIR)/Source/*.cpp)
DAISYSP_SOURCES += $(wildcard $(DAISYSP_DIR)/Source/*/*.cpp)

C_INCLUDES += \
-I$(MPR121_DIR) \
-I$(EFFECTS_DIR) \
-I. \
-Iresources \
-Ieurorack \
-I../.. \
-IEffects

# Hardware target
# Set to SEED for Daisy Seed
HWDEFS = -DSEED

# Can't actually add to CFLAGS.. due to libDaisy stuff
# silences warning from stmlib JOIN macros
C_INCLUDES += -Wno-unused-local-typedefs
OPT = -Os -s

# Core location, and generic makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile # Include core makefile AFTER defining DAISYSP_SOURCES


### Need to override all to get support for .cc files
all: $(BUILD_DIR)/$(TARGET).elf $(BUILD_DIR)/$(TARGET).hex $(BUILD_DIR)/$(TARGET).bin

#######################################
# build the application
#######################################
# list of objects
OBJECTS = $(addprefix $(BUILD_DIR)/,$(notdir $(C_SOURCES:.c=.o)))
vpath %.c $(sort $(dir $(C_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CPP_SOURCES:.cpp=.o)))
vpath %.cpp $(sort $(dir $(CPP_SOURCES)))
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(CC_SOURCES:.cc=.o)))
vpath %.cc $(sort $(dir $(CC_SOURCES)))
# list of ASM program objects
OBJECTS += $(addprefix $(BUILD_DIR)/,$(notdir $(ASM_SOURCES:.s=.o)))
vpath %.s $(sort $(dir $(ASM_SOURCES)))

$(BUILD_DIR)/%.o: %.c Makefile | $(BUILD_DIR)
	$(CC) -c $(CFLAGS) $(C_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.c=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.cpp Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cpp=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.cc Makefile | $(BUILD_DIR)
	$(CXX) -c $(CPPFLAGS) $(CPP_STANDARD) -Wa,-a,-ad,-alms=$(BUILD_DIR)/$(notdir $(<:.cc=.lst)) $< -o $@

$(BUILD_DIR)/%.o: %.s Makefile | $(BUILD_DIR)
	$(AS) -c $(CFLAGS) $< -o $@

$(BUILD_DIR)/$(TARGET).elf: $(OBJECTS) Makefile
	$(CXX) $(OBJECTS) $(LDFLAGS) -o $@
	$(SZ) $@

$(BUILD_DIR)/%.hex: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(HEX) $< $@

$(BUILD_DIR)/%.bin: $(BUILD_DIR)/%.elf | $(BUILD_DIR)
	$(BIN) $< $@

$(BUILD_DIR):
	mkdir $@