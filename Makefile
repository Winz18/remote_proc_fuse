# Compiler and flags
CC = gcc
CFLAGS = -Wall -g `pkg-config fuse3 --cflags` `pkg-config libssh2 --cflags` -D_FILE_OFFSET_BITS=64
LDFLAGS = `pkg-config fuse3 --libs` `pkg-config libssh2 --libs`

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Source files and object files
SOURCES = $(wildcard $(SRCDIR)/*.c)
OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(SOURCES))

# Executable name
TARGET = $(BINDIR)/remote_proc_fuse

# Default target
all: $(TARGET)

# Rule to link the executable
$(TARGET): $(OBJECTS)
	@mkdir -p $(BINDIR)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Linked executable: $(TARGET)"

# Rule to compile source files into object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/*.h $(SRCDIR)/common.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "Compiled object: $@"

# Clean target
clean:
	@rm -rf $(OBJDIR) $(BINDIR)
	@echo "Cleaned object and binary directories."

# Phony targets
.PHONY: all clean