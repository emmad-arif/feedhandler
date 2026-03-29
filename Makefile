CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Werror -O2 -march=native -pedantic
INCLUDES := -I include

FH_BIN  := build/feedhandler
FH_SRCS := src/main.cpp \
            src/feed/kalshi_ws.cpp
FH_OBJS := $(FH_SRCS:.cpp=.o)
FH_LIBS := -lpthread -lwebsockets -lssl -lcrypto

SANDBOX_BIN  := build/SPSCRingBuffer_benchmark
SANDBOX_SRCS := sandbox/SPSCRingBuffer_benchmark.cpp
SANDBOX_OBJS := $(SANDBOX_SRCS:.cpp=.o)
SANDBOX_LIBS := -lpthread

ALL_OBJS := $(FH_OBJS) $(SANDBOX_OBJS)

.PHONY: all clean dirs

all: dirs $(FH_BIN) $(SANDBOX_BIN)

dirs:
	@mkdir -p build src/feed sandbox

$(FH_BIN): $(FH_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(FH_LIBS)

$(SANDBOX_BIN): $(SANDBOX_OBJS)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^ $(SANDBOX_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

clean:
	rm -f $(ALL_OBJS) $(FH_BIN) $(SANDBOX_BIN)
	rm -rf build
