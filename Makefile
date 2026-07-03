CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++2a
OBJS = main.o
TARGET = infetch

PHONY: clean all
all: $(TARGET)
clean:
	rm -f $(OBJS) $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
