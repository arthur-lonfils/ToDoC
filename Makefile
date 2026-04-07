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
CHANGELOG_EMBED_SCRIPT = scripts/embed_changelog.sh
CHANGELOG_SRC = $(SRC_DIR)/changelog_data.c

# SRCS is evaluated lazily, but generated sources must exist first.
SRCS      = $(wildcard $(SRC_DIR)/*.c) $(MIGRATIONS_SRC) $(CHANGELOG_SRC)
OBJS      = $(sort $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS)))
TARGET    = $(BUILD_DIR)/todoc

.PHONY: all clean install uninstall embed test test-valgrind setup format format-check quality release

all: $(TARGET)

# ── Build ────────────────────────────────────────────────────────

embed: $(MIGRATIONS_SRC) $(CHANGELOG_SRC)

$(MIGRATIONS_SRC): $(wildcard $(SQL_DIR)/*.sql) $(EMBED_SCRIPT)
	sh $(EMBED_SCRIPT)

$(CHANGELOG_SRC): CHANGELOG.md $(CHANGELOG_EMBED_SCRIPT)
	sh $(CHANGELOG_EMBED_SCRIPT)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD_DIR)/migrations.o: $(MIGRATIONS_SRC)
$(BUILD_DIR)/changelog_data.o: $(CHANGELOG_SRC)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

# ── Install / Uninstall ─────────────────────────────────────────

install: $(TARGET)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 $(TARGET) $(DESTDIR)$(BINDIR)/todoc

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/todoc

# ── Dev setup (git hooks) ───────────────────────────────────────

setup:
	@echo "Installing git hooks..."
	@cp hooks/commit-msg .git/hooks/commit-msg
	@cp hooks/pre-commit .git/hooks/pre-commit
	@chmod +x .git/hooks/commit-msg .git/hooks/pre-commit
	@echo "Done. Git hooks installed."

# ── Code quality ─────────────────────────────────────────────────

format:
	@clang-format -i $(SRC_DIR)/*.c $(SRC_DIR)/*.h
	@echo "Formatted all source files."

format-check:
	@clang-format --dry-run --Werror $(SRC_DIR)/*.c $(SRC_DIR)/*.h
	@echo "All files correctly formatted."

quality: format-check
	@echo "Quality checks passed."

# ── Testing ──────────────────────────────────────────────────────

test: $(TARGET)
	./tests/run.sh

test-valgrind: $(TARGET)
	./tests/run.sh --valgrind

# ── Release ──────────────────────────────────────────────────────

release:
	./scripts/release.sh
