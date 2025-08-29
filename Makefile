CXX := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -O2 -Iinclude
LDFLAGS := -lreadline

SRC := $(wildcard src/*.cpp)
BIN := bin/minishell

all: $(BIN)

$(BIN): $(SRC)
	mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $(SRC) $(LDFLAGS)

run: all
	./bin/minishell

clean:
	rm -rf bin

