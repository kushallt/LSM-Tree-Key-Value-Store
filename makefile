CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -g

TARGET  = main.exe
SRCS    = $(wildcard *.cpp)
OBJS    = $(SRCS:.cpp=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp memtable.h
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean