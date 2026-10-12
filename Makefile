CC = gcc
CFLAGS = -Wextra -Werror -Wall -std=c11
OBJ = build
SRCS = src/main.c

TARGET = $(OBJ)/game_of_life


all: game_build
game_build: $(TARGET)

$(OBJ):
	@mkdir -p build


$(TARGET): $(SRCS) | $(OBJ)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -rf $(OBJ)

.PHONY: all game_build clean