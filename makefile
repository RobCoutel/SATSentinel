EXEC = SATSentinel
LIB_NAME = SATSentinel
MAIN = main.cpp
TARGET_LIB ?= $(LIB_NAME).a

CC := g++

# GUI opt-in. Default GUI=0 builds the classic CLI-only tool with zero new
# dependencies. `make all GUI=1` additionally compiles the vendored Dear
# ImGui sources and the src/gui/ frontend, and links against the system
# GLFW + OpenGL libraries (apt: libglfw3-dev libgl1-mesa-dev).
GUI ?= 0
IMGUI_DIR ?= ./third_party/imgui

BUILD_DIR ?= ./build
SRC_DIRS ?= ./src
TEST_DIRS ?= ./tests

SRCS := $(shell find $(SRC_DIRS) -name "*.cpp" -not -path "$(SRC_DIRS)/gui/*")
TEST_SRCS := $(shell find $(TEST_DIRS) -name "*.cpp")

ifeq ($(GUI),1)
  SRCS += $(IMGUI_DIR)/imgui.cpp \
          $(IMGUI_DIR)/imgui_draw.cpp \
          $(IMGUI_DIR)/imgui_tables.cpp \
          $(IMGUI_DIR)/imgui_widgets.cpp \
          $(IMGUI_DIR)/backends/imgui_impl_glfw.cpp \
          $(IMGUI_DIR)/backends/imgui_impl_opengl3.cpp \
          $(shell find $(SRC_DIRS)/gui -name "*.cpp")
endif

OBJS := $(SRCS:%.cpp=$(BUILD_DIR)/%.o)
TEST_OBJS := $(TEST_SRCS:%.cpp=$(BUILD_DIR)/%.o)
MAIN_OBJ := $(BUILD_DIR)/$(MAIN:.cpp=.o)

HEAD := $(shell find $(SRC_DIRS) -name "*.hpp")
TEST_HEAD := $(shell find $(TEST_DIRS) -name *.hpp)

DEPS := $(OBJS:.o=.d)
MODULES_DIR ?= ..
MODULES :=

INC_DIRS += ./include/ ./src/ $(foreach D, $(MODULES), $(MODULES_DIR)/$(D)/include/)
ifeq ($(GUI),1)
  INC_DIRS += $(IMGUI_DIR) $(IMGUI_DIR)/backends
endif
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
LINK_FLAGS :=
TEST_LINK_FLAGS := -lCatch2Main -lCatch2
ifeq ($(GUI),1)
  LINK_FLAGS += -lglfw -lGL -ldl -lpthread
endif

CFLAGS ?= $(INC_FLAGS) -MMD -MP -fPIC -std=c++17 -Wall --pedantic
ifeq ($(GUI),1)
  CFLAGS += -DSENTINEL_GUI_ENABLED
endif
REL_FLAGS ?= -O3 -DNDEBUG
DBG_FLAGS ?= -O0 -g -g3 -gdwarf-2 -ftrapv

BUILD_MODE ?= release

ifeq ($(BUILD_MODE),debug)
  REL_FLAGS := $(DBG_FLAGS)
else
  REL_FLAGS := $(REL_FLAGS)
endif

# Fingerprint of the flags that affect compilation (GUI, optimization/debug
# flags, defines, ...). Made a prerequisite of every .o below so that
# toggling GUI=0/1 or BUILD_MODE forces a rebuild of the affected objects
# instead of silently reusing stale ones compiled with different flags.
BUILD_FLAGS := GUI=$(GUI) REL_FLAGS=$(REL_FLAGS) CFLAGS=$(CFLAGS)
FLAGS_FILE := $(BUILD_DIR)/.build-flags

# c source
$(BUILD_DIR)/%.o: %.cpp $(HEAD) $(FLAGS_FILE)
	$(MKDIR_P) $(dir $@)
	$(CC) -c $< -o $@ $(REL_FLAGS) $(CFLAGS)

$(FLAGS_FILE): FORCE
	@$(MKDIR_P) $(dir $@)
	@echo '$(BUILD_FLAGS)' | cmp -s - $@ || echo '$(BUILD_FLAGS)' > $@

.PHONY: FORCE
FORCE:

# release
$(BUILD_DIR)/$(EXEC): $(OBJS) $(MAIN_OBJ)
	$(CC) $^ -o $@ $(CFLAGS) $(REL_FLAGS) $(LINK_FLAGS)

# library
# The archive is removed first because `ar rcs` only replaces/adds members
# for the objects listed in $^; it never drops members that are no longer
# part of the build (e.g. imgui/gui objects left over from a GUI=1 build),
# which used to make GUI=0 builds silently link stale GUI code.
$(BUILD_DIR)/$(TARGET_LIB): $(OBJS)
	$(RM) $@
	ar rcs $@ $^

# tests
tests: REL_FLAGS = $(DBG_FLAGS) $(TEST_LINK_FLAGS)
tests: $(OBJS) $(TEST_OBJS)
	$(CC) -o $(BUILD_DIR)/SATSentinel-tests $(OBJS) $(TEST_OBJS) $(CFLAGS) $(DBG_FLAGS) $(LINK_FLAGS) $(TEST_LINK_FLAGS)

.PHONY: debug

lib: $(BUILD_DIR)/$(TARGET_LIB)

all: $(BUILD_DIR)/$(TARGET_LIB)
all: $(BUILD_DIR)/$(EXEC)

debug: REL_FLAGS = $(DBG_FLAGS)
debug: $(BUILD_DIR)/$(TARGET_LIB)
debug: $(BUILD_DIR)/$(EXEC)

.PHONY: install install-gui install-test
install-gui:
	sudo apt-get install -y libglfw3-dev libgl1-mesa-dev
	git submodule update --init third_party/imgui

install-test:
	sudo apt-get install catch2

.PHONY: install-hooks
install-hooks:
	git config core.hooksPath .githooks
	@echo "Git hooks installed. Pre-commit hook will run tests before each commit."

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)

MKDIR_P ?= mkdir -p
