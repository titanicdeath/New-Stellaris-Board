# Build with: make
# Test with: make test

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic
LDFLAGS  ?=

SRC = src/pdx_node.cpp src/json_writer.cpp src/resolver.cpp src/extractor.cpp
OBJ = $(SRC:.cpp=.o)
BIN = build/stellaris_extract

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJ) $(BIN)

test: $(BIN)
	./$(BIN) test/synthetic_gamestate.txt build/synthetic_out.json
	@echo "--- output preview ---"
	@head -80 build/synthetic_out.json
