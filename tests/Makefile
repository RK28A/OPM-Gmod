# Host-side unit tests for the portable parts of the SDK.
#
# The module itself is MSVC/Windows/DirectX only, but the headers under
# GMod-SDK/core/, plus notify_core.h and AngleMath.h, are deliberately free of
# those dependencies so the logic the review flagged can be compiled, sanitised
# and tested anywhere.
#
# Anything a test can reach is logic that was deliberately pulled out of the
# Windows-only translation unit; growing this list is how coverage grows.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
            -Wformat=2 -Wnon-virtual-dtor -Wold-style-cast -Wunused
LDFLAGS  ?= -pthread

SANITIZERS ?= -fsanitize=address,undefined -fno-omit-frame-pointer

SOURCES := main.cpp notify_tests.cpp anglemath_tests.cpp keystate_tests.cpp \
           pathsanitize_tests.cpp stringutil_tests.cpp luastack_tests.cpp
BINARY  := run_tests

HEADERS := test_harness.h \
           ../GMod-SDK/ImGui/imgui-notify/notify_core.h \
           ../GMod-SDK/mathlib/AngleMath.h \
           ../GMod-SDK/core/KeyState.h \
           ../GMod-SDK/core/PathSanitize.h \
           ../GMod-SDK/core/StringUtil.h \
           ../GMod-SDK/core/LuaStack.h

.PHONY: all test asan clean

all: $(BINARY)

$(BINARY): $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDFLAGS)

test: $(BINARY)
	./$(BINARY)

# Same suite under ASan + UBSan.
asan: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SANITIZERS) $(SOURCES) -o $(BINARY)_asan $(LDFLAGS)
	./$(BINARY)_asan

clean:
	rm -f $(BINARY) $(BINARY)_asan
