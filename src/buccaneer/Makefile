# Buccaneer Language Toolchain Makefile
# Part of the Mutineer BBS system

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -I. -D_POSIX_C_SOURCE=200809L
LDFLAGS = -lm

# Debug/Release
ifdef DEBUG
    CFLAGS += -g -O0 -DDEBUG
else
    CFLAGS += -O2 -DNDEBUG
endif

# Directories
SRCDIR = .
INCDIR = include
TOOLDIR = tools
BUILDDIR = build
BINDIR = bin

# Core library sources
CORE_SRCS = \
    value.c \
    diag.c \
    lexer.c \
    ast.c \
    parser.c \
    semantic.c \
    emit.c \
    module.c \
    vm.c \
    host.c \
    package.c \
    bbs.c

# Tool sources
TOOL_SRCS = \
    $(TOOLDIR)/formatter.c \
    $(TOOLDIR)/linter.c \
    $(TOOLDIR)/simulator.c \
    $(TOOLDIR)/bucc.c

# Object files
CORE_OBJS = $(CORE_SRCS:%.c=$(BUILDDIR)/%.o)
TOOL_OBJS = $(TOOL_SRCS:%.c=$(BUILDDIR)/%.o)

# Headers
HEADERS = $(wildcard $(INCDIR)/*.h)

# Targets
LIBRARY = $(BUILDDIR)/libbuccvm.a
CLI = $(BINDIR)/bucc

.PHONY: all clean install library cli dist

all: library cli

library: $(LIBRARY)

cli: $(CLI)

$(LIBRARY): $(CORE_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^
	@echo "Built static library: $@"

$(CLI): $(TOOL_OBJS) $(LIBRARY)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -o $@ $(TOOL_OBJS) $(LIBRARY) $(LDFLAGS)
	@echo "Built CLI tool: $@"

$(BUILDDIR)/%.o: %.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILDDIR)/$(TOOLDIR)/%.o: $(TOOLDIR)/%.c $(HEADERS)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILDDIR) $(BINDIR) $(DISTDIR)

install: $(CLI)
	install -d $(DESTDIR)/usr/local/bin
	install -m 755 $(CLI) $(DESTDIR)/usr/local/bin/
	install -d $(DESTDIR)/usr/local/lib
	install -m 644 $(LIBRARY) $(DESTDIR)/usr/local/lib/
	install -d $(DESTDIR)/usr/local/include/buccaneer
	install -m 644 $(INCDIR)/*.h $(DESTDIR)/usr/local/include/buccaneer/

# Test target
test: $(CLI)
	@echo "Running basic tests..."
	@$(CLI) version
	@echo "Tests passed!"

# Distribution directory
DISTDIR = dist

dist: $(CLI) $(LIBRARY)
	@rm -rf $(DISTDIR)
	@mkdir -p $(DISTDIR)/bin
	@mkdir -p $(DISTDIR)/lib
	@mkdir -p $(DISTDIR)/include/buccaneer
	@mkdir -p $(DISTDIR)/examples
	@cp $(CLI) $(DISTDIR)/bin/
	@cp $(LIBRARY) $(DISTDIR)/lib/
	@cp $(INCDIR)/*.h $(DISTDIR)/include/buccaneer/
	@if [ -d examples ]; then cp -r examples/* $(DISTDIR)/examples/ 2>/dev/null || true; fi
	@echo "Distribution built in $(DISTDIR)/"

# Print variables for debugging
print-%:
	@echo $* = $($*)
