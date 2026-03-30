CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -march=native -pedantic -g
INCLUDES := -I include

FH_BIN  := build/feedhandler
FH_SRCS := src/main.cpp \
            src/feed/kalshi_ws.cpp
FH_OBJS := $(FH_SRCS:.cpp=.o)
FH_LIBS := -lpthread -lwebsockets -lssl -lcrypto

SANDBOX_BINS  := build/SPSCRingBuffer_benchmark \
				 build/AltRingBuffer_benchmark
SANDBOX_SRCS  := sandbox/SPSCRingBuffer_benchmark.cpp \
				 sandbox/AltRingBuffer_benchmark.cpp
SANDBOX_OBJS  := $(SANDBOX_SRCS:.cpp=.o)
SANDBOX_LIBS  := -lpthread

ALL_OBJS := $(FH_OBJS) $(SANDBOX_OBJS)

.PHONY: all clean dirs

all: dirs $(FH_BIN) $(SANDBOX_BINS)

dirs:
	@mkdir -p build src/feed sandbox

$(FH_BIN): $(FH_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(FH_LIBS)

build/SPSCRingBuffer_benchmark: sandbox/SPSCRingBuffer_benchmark.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(SANDBOX_LIBS)

build/AltRingBuffer_benchmark: sandbox/AltRingBuffer_benchmark.o
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(SANDBOX_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(ALL_OBJS) $(FH_BIN) $(SANDBOX_BIN)
	rm -rf build
