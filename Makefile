TARGET      ?= file-probe
CXX         ?= g++
PKG_CONFIG  ?= pkg-config
BUILD_DIR   ?= build
SRC_DIR     := src

FFMPEG_CFLAGS := $(shell $(PKG_CONFIG) --cflags libavformat libavcodec libavutil 2>/dev/null)
FFMPEG_LIBS   := $(shell $(PKG_CONFIG) --libs libavformat libavcodec libavutil 2>/dev/null)

ifeq ($(strip $(FFMPEG_LIBS)),)
FFMPEG_LIBS = -lavformat -lavcodec -lavutil -lswresample -lswscale
endif

SOURCES := $(wildcard $(SRC_DIR)/*.cpp)
OBJECTS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))
DEPFILES := $(OBJECTS:.o=.d)

CXXFLAGS += -std=c++17 -Wall -Wextra -Wpedantic -Iinclude -I. $(FFMPEG_CFLAGS)
DEPFLAGS ?= -MMD -MP

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(FFMPEG_LIBS) -lm

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(DEPFLAGS) -c $< -o $@

install: $(TARGET)
	install -Dm755 $(TARGET) /usr/local/bin/$(TARGET)

uninstall:
	rm -f /usr/local/bin/$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

-include $(DEPFILES)
