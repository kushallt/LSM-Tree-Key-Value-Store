CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g -Iinclude

TARGET  = main.exe
SRCS    = $(wildcard srcs/*.cpp)
OBJS    = $(patsubst srcs/%.cpp, obj/%.o, $(SRCS))
HEADERS = $(wildcard include/*.h)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

obj/%.o: srcs/%.cpp $(HEADERS)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean