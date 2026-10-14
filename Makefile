# Builds EarComp.component (AUv2) and the offline probe with the Xcode toolchain.
# make            -> build/EarComp.component + build/probe
# make install    -> copy to ~/Library/Audio/Plug-Ins/Components and run auval
# make check      -> probe vs the measured Mimi map

export DEVELOPER_DIR := /Applications/Xcode.app/Contents/Developer
SDK := $(shell DEVELOPER_DIR=$(DEVELOPER_DIR) xcrun --sdk macosx --show-sdk-path)
CXX := xcrun clang++
AUSDK := vendor/AudioUnitSDK
CXXFLAGS := -isysroot $(SDK) -std=c++23 -O2 -Wall -Wextra -arch arm64 -mmacosx-version-min=13.0 \
            -fvisibility=hidden -I$(AUSDK)/include
AUSDK_SRC := AUBase AUBuffer AUBufferAllocator AUEffectBase AUInputElement AUOutputElement \
             AUPlugInDispatch AUScopeElement ComponentBase
SRC := src/EarCompAU.cpp src/Processor.cpp $(AUSDK_SRC:%=$(AUSDK)/src/AudioUnitSDK/%.cpp)
BUNDLE := build/EarComp.component
DEST := $(HOME)/Library/Audio/Plug-Ins/Components

all: $(BUNDLE) build/probe

src/MimiTables.h: data/mimi-map.json tools/gen_tables.py
	python3 -I tools/gen_tables.py

$(BUNDLE): $(SRC) src/Processor.hpp src/MimiTables.h Info.plist
	mkdir -p $(BUNDLE)/Contents/MacOS
	cp Info.plist $(BUNDLE)/Contents/Info.plist
	$(CXX) $(CXXFLAGS) -bundle -o $(BUNDLE)/Contents/MacOS/EarComp $(SRC) \
		-framework AudioToolbox -framework CoreAudio -framework CoreFoundation -framework Accelerate
	codesign --force --sign - $(BUNDLE)

build/probe: tools/probe.cpp src/Processor.cpp src/Processor.hpp src/MimiTables.h
	mkdir -p build
	$(CXX) -isysroot $(SDK) -std=c++20 -O2 -o $@ tools/probe.cpp src/Processor.cpp -framework Accelerate

check: build/probe
	./build/probe > build/ours.json
	python3 -I tools/compare.py build/ours.json

install: $(BUNDLE)
	mkdir -p $(DEST)
	rm -rf $(DEST)/EarComp.component
	cp -R $(BUNDLE) $(DEST)/
	auval -v aufx Ercp EarC | tail -5

clean:
	rm -rf build

.PHONY: all check install clean
