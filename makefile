EXEC = SATSentinel
LIB_NAME = SATSentinel
MAIN = main.cpp
TARGET_LIB ?= $(LIB_NAME).a

CC := g++

BUILD_DIR ?= ./build
SRC_DIRS ?= ./src
TEST_DIRS ?= ./tests

SRCS := $(shell find $(SRC_DIRS) -name "*.cpp")
TEST_SRCS := $(shell find $(TEST_DIRS) -name "*.cpp")

OBJS := $(SRCS:%.cpp=$(BUILD_DIR)/%.o)
TEST_OBJS := $(TEST_SRCS:%.cpp=$(BUILD_DIR)/%.o)
MAIN_OBJ := $(BUILD_DIR)/$(MAIN:.cpp=.o)

HEAD := $(shell find $(SRC_DIRS) -name "*.hpp")
TEST_HEAD := $(shell find $(TEST_DIRS) -name *.hpp)

DEPS := $(OBJS:.o=.d)
MODULES_DIR ?= ..
MODULES :=

INC_DIRS += ./include/ ./src/ $(foreach D, $(MODULES), $(MODULES_DIR)/$(D)/include/)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))
LINK_FLAGS := -llzma
TEST_LINK_FLAGS := -lCatch2Main -lCatch2

CFLAGS ?= $(INC_FLAGS) -MMD -MP -fPIC -std=c++17 -Wall --pedantic
REL_FLAGS ?= -O3 -DNDEBUG
DBG_FLAGS ?= -O0 -g -g3 -gdwarf-2 -ftrapv

BUILD_MODE ?= release

ifeq ($(BUILD_MODE),debug)
  REL_FLAGS := $(DBG_FLAGS)
else
  REL_FLAGS := $(REL_FLAGS)
endif

# c source
$(BUILD_DIR)/%.o: %.cpp $(HEAD)
	$(MKDIR_P) $(dir $@)
	$(CC) -c $< -o $@ $(REL_FLAGS) $(CFLAGS)

# release
$(BUILD_DIR)/$(EXEC): $(OBJS) $(MAIN_OBJ)
	$(CC) $^ -o $@ $(CFLAGS) $(REL_FLAGS) $(LINK_FLAGS)

# library
$(BUILD_DIR)/$(TARGET_LIB): $(OBJS)
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

.PHONY: install
install-test:
	sudo apt-get install catch2

.PHONY: clean

clean:
	$(RM) -r $(BUILD_DIR)

-include $(DEPS)

MKDIR_P ?= mkdir -p
