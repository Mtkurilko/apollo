# Apollo — incremental build and install.
#
#   make              build ./build/apollo
#   make -j8          parallel build
#   make install      install `apollo` onto your PATH (may need sudo)
#   make uninstall    remove it again
#   make run          build and launch in place
#   make doctor       build, then check the installation
#   make clean

VERSION  := 0.2.0

CXX      ?= c++
SFML_DIR ?= /usr/local
BUILD    ?= build
BIN      := apollo
TARGET   := $(BUILD)/$(BIN)
PREFIX   ?= /usr/local
DESTDIR  ?=

BINDIR   := $(DESTDIR)$(PREFIX)/bin
SHAREDIR := $(DESTDIR)$(PREFIX)/share/apollo

SRCS := $(wildcard *.cpp)
OBJS := $(patsubst %.cpp,$(BUILD)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# -O2 matters: with no -O flag at all the compiler defaults to -O0.
# -MMD -MP emits the header dependencies that make incremental rebuilds correct.
CXXFLAGS ?= -std=c++17 -O2 -DNDEBUG -Wall -Wextra -Wno-unused-parameter \
            -MMD -MP -I$(SFML_DIR)/include \
            -DAPOLLO_VERSION='"$(VERSION)"' \
            -DAPOLLO_DEFAULT_HOME='"$(CURDIR)"'

LDFLAGS  ?= -L$(SFML_DIR)/lib -Wl,-rpath,$(SFML_DIR)/lib
LDLIBS   ?= -lsfml-graphics -lsfml-window -lsfml-system

.PHONY: all clean install uninstall run doctor

all: $(TARGET)

$(TARGET): $(OBJS)
	@$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)
	@echo "built $@ ($(VERSION))"

$(BUILD)/%.o: %.cpp | $(BUILD)
	@echo "  CXX $<"
	@$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD):
	@mkdir -p $(BUILD)

# Installs the binary onto PATH and the assets beside it, so `apollo` works from
# any directory with no environment variables set. AppPaths finds the assets by
# walking from the executable to ../share/apollo.
install: $(TARGET)
	@mkdir -p "$(BINDIR)" "$(SHAREDIR)"
	@cp -f "$(TARGET)" "$(BINDIR)/$(BIN)"
	@rm -rf "$(SHAREDIR)/assets"
	@cp -R assets "$(SHAREDIR)/assets"
	@echo "installed $(BINDIR)/$(BIN)"
	@echo "          $(SHAREDIR)/assets"
	@case ":$$PATH:" in \
	  *":$(PREFIX)/bin:"*) echo "run: apollo" ;; \
	  *) echo "" ; echo "WARNING: $(PREFIX)/bin is not on your PATH." ; \
	     echo "Add this to your ~/.zshrc:" ; \
	     echo "  export PATH=\"$(PREFIX)/bin:\$$PATH\"" ;; \
	esac

uninstall:
	@rm -f "$(BINDIR)/$(BIN)"
	@rm -rf "$(SHAREDIR)"
	@echo "removed $(BINDIR)/$(BIN) and $(SHAREDIR)"
	@echo "(your settings in ~/.apollo were left alone)"

run: $(TARGET)
	@APOLLO_HOME=$(CURDIR) $(TARGET)

doctor: $(TARGET)
	@APOLLO_HOME=$(CURDIR) $(TARGET) doctor

clean:
	@rm -rf $(BUILD)

-include $(DEPS)
