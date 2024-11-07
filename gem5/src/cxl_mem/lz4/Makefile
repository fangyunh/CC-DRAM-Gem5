# Makefile for compiling basic LZ4

# Compiler
CC = gcc

# Compiler flags
CFLAGS = -O3 -Wall

# Source and object files
SRC = lz4.c
OBJ = lz4.o
LIB = liblz4.a

# Target to generate both .o and .a files
all: $(OBJ) $(LIB)

# Compile the object file
$(OBJ): $(SRC)
	$(CC) $(CFLAGS) -c $< -o $@

# Create the static library
$(LIB): $(OBJ)
	ar rcs $@ $^

# Clean up generated files
clean:
	rm -f $(OBJ) $(LIB)

.PHONY: all clean
