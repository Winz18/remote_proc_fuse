# Compiler and flags
CC = gcc
CFLAGS = -Wall -g `pkg-config fuse3 --cflags` `pkg-config libssh2 --cflags` -D_FILE_OFFSET_BITS=64
LDFLAGS = `pkg-config fuse3 --libs` `pkg-config libssh2 --libs`

# Directories
SRCDIR = src
OBJDIR = obj
BINDIR = bin

# Source files and object files for main remotefs
MAIN_SOURCES = $(SRCDIR)/main.c $(SRCDIR)/remote_proc_fuse.c $(SRCDIR)/ssh_sftp_client.c $(SRCDIR)/mount_config.c
MAIN_OBJECTS = $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(MAIN_SOURCES))

# Utility programs
CP_TARGET = $(BINDIR)/remote-cp
MV_TARGET = $(BINDIR)/remote-mv

# Executable name
TARGET = $(BINDIR)/remotefs

# Default target
all: $(TARGET) $(CP_TARGET) $(MV_TARGET)

# Rule to link the main executable
$(TARGET): $(MAIN_OBJECTS)
	@mkdir -p $(BINDIR)
	$(CC) $(MAIN_OBJECTS) -o $(TARGET) $(LDFLAGS)
	@echo "Linked executable: $(TARGET)"

# Rule to compile source files into object files
$(OBJDIR)/%.o: $(SRCDIR)/%.c
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@
	@echo "Compiled object: $@"

# Rule to compile and link the cp utility
$(CP_TARGET): $(OBJDIR)/cp.o $(OBJDIR)/ssh_sftp_client.o $(OBJDIR)/mount_config.o
	@mkdir -p $(BINDIR)
	$(CC) $^ -o $(CP_TARGET) $(LDFLAGS)
	@echo "Linked executable: $(CP_TARGET)"

# Rule to compile and link the mv utility
$(MV_TARGET): $(OBJDIR)/mv.o $(OBJDIR)/ssh_sftp_client.o $(OBJDIR)/mount_config.o
	@mkdir -p $(BINDIR)
	$(CC) $^ -o $(MV_TARGET) $(LDFLAGS)
	@echo "Linked executable: $(MV_TARGET)"

# Clean target
clean:
	@rm -rf $(OBJDIR) $(BINDIR)
	@echo "Cleaned object and binary directories."

# Install target
install: $(TARGET) $(CP_TARGET) $(MV_TARGET)
	@mkdir -p /usr/local/bin
	@cp $(TARGET) /usr/local/bin/
	@cp $(CP_TARGET) /usr/local/bin/
	@cp $(MV_TARGET) /usr/local/bin/
	@echo "Installed remotefs, remote-cp and remote-mv to /usr/local/bin/"

# Phony targets
.PHONY: all clean install