CXX			= g++
CXXFLAGS	= -Wall -g -Iinclude
ASM			= gcc

# look for source files
CPP_SRCS	= $(wildcard src/*.cpp)
ASM_SRCS	= $(wildcard asm/*.S)
TEST_SRCS 	= $(shell find test -name '*.cpp')

# object files will go in build directory
CPP_OBJS	= $(patsubst src/%.cpp, build/%.o, $(CPP_SRCS))
ASM_OBJS	= $(patsubst asm/%.S, build/%.o, $(ASM_SRCS))
TEST_OBJS	= $(patsubst test/%.cpp, build/test/%.o, $(TEST_SRCS))
OBJS		= $(CPP_OBJS) $(ASM_OBJS)

# make each test file it's own executuable
TEST_BINS	= $(patsubst test/%.cpp, build/test/%, $(TEST_SRCS))

LDFLAGS = -lpthread

all: build $(OBJS)

build:
	mkdir -p build

build/test/%: build/test/%.o $(OBJS) | build
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: asm/%.S | build
	$(ASM) -c $< -o $@

build/test/%.o: test/%.cpp | build
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# to run all tests
test: build $(TEST_BINS)
	@for test in $(TEST_BINS); do \
		echo "===== RUNNING $$test ====="; \
		./$$test; \
		echo ""; \
	done

# run tests in a specific subdirectory (make test-context_swap, etc.)
test-%: build $(patsubst test/%.cpp, build/test/%, $(shell find test/$* -name '*.cpp' 2>/dev/null))
	@for t in $(patsubst test/%.cpp, build/test/%, $(shell find test/$* -name '*.cpp')); do \
		echo "===== RUNNING $$t ====="; \
		./$$t; \
		echo ""; \
	done

tsan: CXXFLAGS += -fsanitize=thread
tsan: clean test

clean:
	rm -rf build

.PRECIOUS: build/test/%.o
.PHONY: all clean test tsan
