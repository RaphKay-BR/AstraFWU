## Makefile for Multiple Astra Mini Pro
## Target G++ (MinGW-W64)
## (C) 2024 Raphael Kim @ Bear Robotics

GCC = $(PREFIX)gcc
GXX = $(PREFIX)g++

SRC_PATH = src
INC_PATH = inc
BIN_PATH = bin
OBJ_PATH = obj

TARGET = astrafwu

LIBORBBECSDK_PATH=../OrbbecSDK_v1.10.11/SDK

CXXFLAGS += -std=c++17 -mconsole
CXXFLAGS += -fexceptions -O3 -s -static
#CXXFLAGS += -g3

#CXXFLAGS += -DDEBUG
#CXXFLAGS += -DDEBUG_LIBUSB

CXXFLAGS += -I$(LIBORBBECSDK_PATH)/include

# Windows has different naming of OrbbecSDK.
LFLAGS += -lpthread
LFLAGS += $(LIBORBBECSDK_PATH)/lib/OrbbecSDK.dll

CPPSRCS = $(wildcard $(SRC_PATH)/*.cpp)
CPPOBJS = $(CPPSRCS:$(SRC_PATH)/%.cpp=$(OBJ_PATH)/%.o)

.PHONY: prepare all clean

all: prepare $(BIN_PATH)/$(TARGET)

prepare:
	@mkdir -p bin
	@mkdir -p obj

clean:
	@rm -rf $(BIN_PATH)/$(TARGET)
	@rm -rf $(OBJ_PATH)/*.o

$(CPPOBJS): $(OBJ_PATH)/%.o: $(SRC_PATH)/%.cpp
	@echo "Compiling CXX $< to $@ ..."
	@$(GXX) $(CXXFLAGS) -c $< -o $@

$(BIN_PATH)/$(TARGET): $(CPPOBJS)
	@echo "Linking $@ ..."
	@$(GXX) $(CXXFLAGS) $^ $(LFLAGS) -o $@
