CXX ?= clang++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isdk

BOT_SRCS := $(wildcard bots/*.cpp)
BOT_BINS := $(patsubst bots/%.cpp,build/bots/%,$(BOT_SRCS))

.PHONY: all bots test serve clean

all: build/arena bots

bots: $(BOT_BINS)

build:
	mkdir -p build

build/bots:
	mkdir -p build/bots

build/arena: engine/arena.cpp sdk/rps.h | build
	$(CXX) $(CXXFLAGS) engine/arena.cpp -o $@

build/bots/%: bots/%.cpp sdk/rps.h | build/bots
	$(CXX) $(CXXFLAGS) $< -o $@

build/test_rules: tests/test_rules.cpp sdk/rps.h | build
	$(CXX) $(CXXFLAGS) tests/test_rules.cpp -o $@

test: build/test_rules
	./build/test_rules

serve: all
	python3 server.py --open

clean:
	rm -rf build
