CC = gcc
CFLAGS = -Wextra -Werror -Wall -std=c11
OBJ = build
SRCS_STATIC = src/static/main.c
SRCS_DYNAMIC = src/dynamic/main.c

TARGET_STATIC = $(OBJ)/game_of_life_static
TARGET_DYNAMIC = $(OBJ)/game_of_life_dynamic


all: game_build
game_build: $(TARGET_STATIC) $(TARGET_DYNAMIC)

$(OBJ):
	@mkdir -p build


$(TARGET_STATIC): $(SRCS_STATIC) | $(OBJ)
	$(CC) $(CFLAGS) $(SRCS_STATIC) -o $(TARGET_STATIC)

$(TARGET_DYNAMIC): $(SRCS_DYNAMIC) | $(OBJ)
	$(CC) $(CFLAGS) $(SRCS_DYNAMIC) -o $(TARGET_DYNAMIC) -lncurses

clean:
	rm -rf $(OBJ)

.PHONY: all game_build clean