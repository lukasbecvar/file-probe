# compiler settings
CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Wall
LIBS     = -lavformat -lavutil -lm
TARGET   = file-probe
SRC      = main.cpp

# default target: build the binary
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LIBS)

# Install target: install the binary do /usr/local/bin
install: $(TARGET)
	install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)

# clean target: for deleting the binary
clean:
	rm -f $(TARGET)
