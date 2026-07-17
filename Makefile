# Apollo — incremental build.
#
#   make          build ./build/launchApollo
#   make -j8      parallel build
#   make install  copy to /usr/local/bin/launchApollo
#   make run      build and launch
#   make clean

CXX      ?= g++
SFML_DIR ?= /usr/local
BUILD    ?= build
TARGET   := $(BUILD)/launchApollo
PREFIX   ?= /usr/local

SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# -O2 matters here: the previous script passed no -O flag at all, so every
# build was -O0. -MMD -MP generates the header dependencies that make incremental
# rebuilds correct.
CXXFLAGS ?= -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wno-unused-parameter \
            -MMD -MP -I$(SFML_DIR)/include \
            -DAPOLLO_DEFAULT_HOME='"$(CURDIR)"'

LDFLAGS  ?= -L$(SFML_DIR)/lib -Wl,-rpath,$(SFML_DIR)/lib
LDLIBS   ?= -lsfml-graphics -lsfml-window -lsfml-system

.PHONY: all clean install run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)
	@echo "Built $@"

$(BUILD)/%.o: %.cpp | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

install: $(TARGET)
	cp $(TARGET) $(PREFIX)/bin/launchApollo
	@echo "Installed to $(PREFIX)/bin/launchApollo"
	@echo "Note: set APOLLO_HOME=$(CURDIR) if you move the source tree."

run: $(TARGET)
	APOLLO_HOME=$(CURDIR) $(TARGET)

clean:
	rm -rf $(BUILD)

-include $(DEPS)
