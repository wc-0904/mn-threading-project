CXX			= g++
CXXFLAGS	= -Wall -g -Iinclude
ASM			= gcc

# look for source files
CPP_SRCS	= $(wildcard src/*.cpp)
ASM_SRCS	= $(wildcard asm/*.S)
TEST_SRCS	= $(wildcard test/*.cpp)

# object files will go in build directory
CPP_OBJS	= $(patsubst src/%.cpp, build/%.o, $(CPP_SRCS))
ASM_OBJS	= $(patsubst asm/%.S, build/%.o, $(ASM_SRCS))
TEST_OBJS	= $(patsubst test/%.cpp, build/test_%.o, $(TEST_SRCS))
OBJS		= $(CPP_OBJS) $(ASM_OBJS)

# make each test file it's own executuable
TEST_BINS	= $(patsubst test/%.cpp, build/%, $(TEST_SRCS))

all: build $(OBJS)

build:
	mkdir -p build

build/%: build/test_%.o $(OBJS) | build
	$(CXX) $(CXXFLAGS) $^ -o $@

build/%.o: src/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/%.o: asm/%.S | build
	$(ASM) -c $< -o $@

build/test_%.o: test/%.cpp | build
	$(CXX) $(CXXFLAGS) -c $< -o $@

# to run all tests
test: build $(TEST_BINS)
	@for test in $(TEST_BINS); do \
		echo "===== RUNNING $$test ====="; \
		./$$test; \
		echo ""; \
	done

tsan: CXXFLAGS += -fsanitize=thread
tsan: clean test

clean:
	rm -rf build

.PHONY: all clean test tsan
