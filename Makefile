# Alternative build with g++ (requires pugixml and libcurl installed system-wide)
# Primary build: cmake -B build && cmake --build build

CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++20
OBJS = main.o seed_parser.o downloader.o assembler.o sha256.o
TARGET = infetch

.PHONY: clean all
all: $(TARGET)
clean:
	rm -f $(OBJS) $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@
