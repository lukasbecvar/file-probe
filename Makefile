# compiler settings
CXX            = g++
PKG_CONFIG    ?= pkg-config
FFMPEG_CFLAGS := $(shell $(PKG_CONFIG) --cflags libavformat 2>/dev/null)
FFMPEG_LIBS   := $(shell $(PKG_CONFIG) --libs libavformat 2>/dev/null)

ifeq ($(FFMPEG_LIBS),)
FFMPEG_LIBS = -lavformat -lavcodec -lavutil -lswresample -lswscale
endif

CXXFLAGS = -std=c++17 -O2 -Wall $(FFMPEG_CFLAGS)
LIBS     = $(FFMPEG_LIBS) -lm
TARGET   = file-probe
SRC      = file-probe.cpp

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
