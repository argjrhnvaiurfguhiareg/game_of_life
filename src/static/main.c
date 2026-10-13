#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define WIDTH 80
#define HEIGHT 25

static unsigned char cur[HEIGHT][WIDTH];
static unsigned char next[HEIGHT][WIDTH];

static int count_neighbors(int y, int x) {
  int cnt = 0;
  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      if (dx == 0 && dy == 0)
        continue;
      int ny = (y + dy + HEIGHT) % HEIGHT;
      int nx = (x + dx + WIDTH) % WIDTH;
      cnt += cur[ny][nx];
    }
  }
  return cnt;
}

static void step(void) {
  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x) {
      int n = count_neighbors(y, x);
      if (cur[y][x])
        next[y][x] = (n == 2 || n == 3);
      else
        next[y][x] = (n == 3);
    }
  }
  memcpy(cur, next, sizeof(cur));
}

static void draw(void) {
  static char buf[HEIGHT * (WIDTH + 16)];
  size_t pos = 0;
  pos += sprintf(buf + pos, "\x1b[?25l\x1b[H");
  for (int y = 0; y < HEIGHT; ++y) {
    for (int x = 0; x < WIDTH; ++x)
      buf[pos++] = cur[y][x] ? '#' : ' ';
    buf[pos++] = '\n';
  }
  fwrite(buf, 1, pos, stdout);
  fflush(stdout);
}

static void random_fill(void) {
  for (int y = 0; y < HEIGHT; ++y)
    for (int x = 0; x < WIDTH; ++x)
      cur[y][x] = (rand() % 100) < 25;
}

static void put(int x, int y) {
  if (x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT)
    cur[y][x] = 1;
}

static void glider(int x, int y) {
  put(x + 1, y);
  put(x + 2, y + 1);
  put(x, y + 2);
  put(x + 1, y + 2);
  put(x + 2, y + 2);
}

/* ---- Загрузка узора из текстового файла ------------------------------ */
static int is_alive_char(char c) {
  return c == '#' || c == 'O' || c == 'o' || c == '*' || c == 'X' || c == 'x' ||
         c == '1';
}

/* Возвращает 0 при успехе, -1 если файл не открылся */
static int load_pattern(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  memset(cur, 0, sizeof(cur));

  char line[1024];
  int y = 0;
  while (y < HEIGHT && fgets(line, sizeof(line), f)) {
    size_t len = strlen(line);
    /* срезаем \n и \r (CRLF-файлы) */
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
      line[--len] = '\0';

    /* строки-комментарии пропускаем */
    if (len > 0 && line[0] == '!')
      continue;

    for (int x = 0; x < WIDTH && x < (int)len; ++x)
      cur[y][x] = is_alive_char(line[x]);

    ++y;
  }

  fclose(f);
  return 0;
}
/* ---------------------------------------------------------------------- */

int main(int argc, char **argv) {
  srand((unsigned)time(NULL));

  int loaded = 0;
  if (argc > 1) {
    /* сначала пробуем как файл — самый интуитивный вариант */
    if (load_pattern(argv[1]) == 0) {
      loaded = 1;
    }
  }

  if (!loaded) {
    if (argc > 1 && strcmp(argv[1], "g") == 0) {
      memset(cur, 0, sizeof(cur));
      glider(5, 5);
      glider(40, 10);
      glider(20, 18);
    } else {
      if (argc > 1)
        srand((unsigned)atoi(argv[1]));
      random_fill();
    }
  }

  printf("\x1b[2J");

  for (;;) {
    draw();
    usleep(100000);
    step();
  }

  printf("\x1b[?25h");
  return 0;
}