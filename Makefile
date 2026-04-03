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

SRC_DIR    = src
BUILD_DIR  = build
SQL_DIR    = sql/migrations
EMBED_SCRIPT = sql/embed.sh
MIGRATIONS_SRC = $(SRC_DIR)/migrations.c

SRCS      = $(wildcard $(SRC_DIR)/*.c)
OBJS      = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
TARGET    = $(BUILD_DIR)/todoc

.PHONY: all clean install uninstall embed

all: $(TARGET)

# Generate migrations.c from sql files before compiling
embed: $(MIGRATIONS_SRC)

$(MIGRATIONS_SRC): $(wildcard $(SQL_DIR)/*.sql) $(EMBED_SCRIPT)
	sh $(EMBED_SCRIPT)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

# migrations.o depends on the generated file
$(BUILD_DIR)/migrations.o: $(MIGRATIONS_SRC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/todoc

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/todoc
