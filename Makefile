CC := clang
CFLAGS := -O3 -march=native -Wall -Wextra
LDFLAGS := -lvulkan -lX11 -lpng -lm

TARGET := voxel.out
SRC := main.c
OBJ := $(SRC:.c=.o)

SHADER_DIR := shaders
VERT_SHADER := $(SHADER_DIR)/shader.vert
FRAG_SHADER := $(SHADER_DIR)/shader.frag
VERT_SPV := $(SHADER_DIR)/vert.spv
FRAG_SPV := $(SHADER_DIR)/frag.spv

.PHONY: all clean run shaders

all: shaders $(TARGET)

shaders: $(VERT_SPV) $(FRAG_SPV)

$(VERT_SPV): $(VERT_SHADER)
	glslc $< -o $@

$(FRAG_SPV): $(FRAG_SHADER)
	glslc $< -o $@

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: shaders $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)
	rm -f $(VERT_SPV) $(FRAG_SPV)