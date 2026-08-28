# Linux build -- 64-bit only. Modern CS:S on Linux is x86_64 (cstrike_linux64),
# and a 32-bit .so can't preload into a 64-bit game process anyway.
#
# We statically link libstdc++/libgcc so the .so loads cleanly inside Steam
# Linux Runtime's older libstdc++ ABI without symbol-version mismatches.
# That makes hiding our symbols mandatory: an LD_PRELOAD object sits first
# in the global symbol scope, so exporting the static libstdc++/libsupc++
# copies of operator new/delete and the __cxa_* exception ABI would
# interpose them on the whole game process.

CXX      ?= g++
CXXFLAGS ?= -O2 -g -Wall -Wextra -Wno-unused-parameter
CXXFLAGS += -std=c++14 -fno-strict-aliasing

SO_NAME  := librawinput2_linux.so
EXE_NAME := RawInput2BunnyhopAPE

SO_FLAGS := -shared -fPIC -m64 \
            -fno-stack-protector \
            -fvisibility=hidden -fvisibility-inlines-hidden \
            -static-libstdc++ -static-libgcc \
            -Wl,--no-undefined \
            -Wl,--exclude-libs,ALL \
            -Wl,--version-script=preload.ver \
            -ldl -lpthread

EXE_FLAGS := -m64 \
             -static-libstdc++ -static-libgcc

.PHONY: all clean

all: $(SO_NAME) $(EXE_NAME)

$(SO_NAME): preload.cpp utils.h sigs.h preload.ver
	$(CXX) $(CXXFLAGS) -fPIC preload.cpp -o $@ $(SO_FLAGS)

$(EXE_NAME): launcher.cpp
	$(CXX) $(CXXFLAGS) launcher.cpp -o $@ $(EXE_FLAGS)

clean:
	rm -f $(SO_NAME) $(EXE_NAME)
