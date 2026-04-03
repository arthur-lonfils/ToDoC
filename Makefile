CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -Wpedantic -Werror -Wshadow -Wstrict-prototypes
CFLAGS  += -D_POSIX_C_SOURCE=200809L
LDFLAGS  =
LDLIBS   = -lsqlite3

ifdef DEBUG
  CFLAGS  += -g -O0 -DDEBUG -fsanitize=address,undefined
  LDFLAGS += -fsanitize=address,undefined
else
  CFLAGS  += -O2 -DNDEBUG
endif

PREFIX  ?= /usr/local
BINDIR   = $(PREFIX)/bin

SRC_DIR   = src
BUILD_DIR = build
SRCS      = $(wildcard $(SRC_DIR)/*.c)
OBJS      = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET    = $(BUILD_DIR)/todoc

.PHONY: all clean install uninstall

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/todoc

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/todoc
