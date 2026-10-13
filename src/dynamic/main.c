/* life.c — Conway's Game of Life.
 *
 * Сборка (Linux/macOS):   cc -O2 -Wall -Wextra -o life life.c -lncurses
 * Сборка (Linux, wide):   cc -O2 -Wall -Wextra -o life life.c -lncursesw
 * Сборка (MSYS2/MinGW):   gcc -O2 -o life.exe life.c -lncursesw
 *
 * Запуск: LC_ALL=ru_RU.UTF-8 ./life [pattern.txt]
 *         (на Windows: chcp 65001 + подходящая локаль)
 */
#include <locale.h>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* useconds_t / usleep на Windows нет — оборачиваем. */
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#define SLEEP_US(us) Sleep((us) / 1000)
#else
#define SLEEP_US(us) usleep((useconds_t)(us))
#endif

/* ---------- состояние ---------- */
static int W, H;             /* логическая сетка (клеток) */
static int TW, TH;           /* терминальные ячейки: COLS × (LINES-1) */
static unsigned char **cur;  /* текущее поколение */
static unsigned char **next; /* следующее */
static unsigned char **age;  /* возраст живой клетки, 0 = мёртвая */

static int paused = 0;
static int delay_us = 80000; /* ~12 fps */
static long gen = 0;
static int cx = 0, cy = 0; /* курсор-редактор, в логических клетках */

enum render_mode { RM_ASCII = 0, RM_BLOCK, RM_QUAD };
static enum render_mode rmode = RM_QUAD;

static int cpc_x(void) { return rmode == RM_QUAD ? 2 : 1; }
static int cpc_y(void) { return rmode == RM_QUAD ? 2 : 1; }

/* ---------- цвета ---------- */
enum { CP_YOUNG = 1, CP_MID, CP_OLD };

static void init_colors(void) {
  if (!has_colors())
    return;
  start_color();
#ifdef NCURSES_VERSION
  use_default_colors(); /* есть в ncurses, нет в PDCurses */
#endif
  init_pair(CP_YOUNG, COLOR_GREEN, -1);
  init_pair(CP_MID, COLOR_CYAN, -1);
  init_pair(CP_OLD, COLOR_BLUE, -1);
}

static int age_attr(unsigned a) {
  int attr = A_NORMAL;
  if (has_colors()) {
    if (a <= 1)
      attr |= COLOR_PAIR(CP_YOUNG);
    else if (a <= 4)
      attr |= COLOR_PAIR(CP_MID);
    else
      attr |= COLOR_PAIR(CP_OLD);
  }
  if (a <= 1)
    attr |= A_BOLD;
  return attr;
}

/* ---------- память ---------- */
static unsigned char **alloc_rows(int h, int w) {
  unsigned char **m = calloc((size_t)h, sizeof(*m));
  for (int y = 0; y < h; ++y)
    m[y] = calloc((size_t)w, 1);
  return m;
}

static void free_rows(unsigned char **m, int h) {
  if (!m)
    return;
  for (int y = 0; y < h; ++y)
    free(m[y]);
  free(m);
}

/* ---------- эволюция ---------- */
static int count_neighbors(int y, int x) {
  int cnt = 0;
  for (int dy = -1; dy <= 1; ++dy)
    for (int dx = -1; dx <= 1; ++dx) {
      if (!dx && !dy)
        continue;
      int ny = (y + dy + H) % H;
      int nx = (x + dx + W) % W;
      cnt += cur[ny][nx] != 0;
    }
  return cnt;
}

static void step(void) {
  for (int y = 0; y < H; ++y) {
    for (int x = 0; x < W; ++x) {
      int n = count_neighbors(y, x);
      int alive = cur[y][x] ? (n == 2 || n == 3) : (n == 3);
      next[y][x] = (unsigned char)alive;
      if (alive) {
        unsigned a = age[y][x];
        age[y][x] = a < 250 ? a + 1 : 250;
      } else {
        age[y][x] = 0;
      }
    }
  }
  /* Обмен буферов по строкам — O(H) вместо memcpy всего поля. */
  for (int y = 0; y < H; ++y) {
    unsigned char *t = cur[y];
    cur[y] = next[y];
    next[y] = t;
    memset(next[y], 0, (size_t)W);
  }
  ++gen;
}

/* ---------- рендер ---------- */
/* Индекс = битовая маска 2×2: UL=8 UR=4 LL=2 LR=1.
 * Строки в UTF-8: "\uXXXX" в C11 разворачивается в байты. */
static const char *quad_chars[16] = {
    " ",      "\u2597", "\u2596", "\u2584", "\u259D", "\u2590",
    "\u259E", "\u259F", "\u2598", "\u259A", "\u258C", "\u2599",
    "\u2580", "\u259C", "\u259B", "\u2588",
};

static void draw(void) {
  erase();

  int tcx_cur = cx / cpc_x();
  int tcy_cur = cy / cpc_y();

  for (int tcy = 0; tcy < TH; ++tcy) {
    for (int tcx = 0; tcx < TW; ++tcx) {
      int on_cursor = (tcx == tcx_cur && tcy == tcy_cur);
      int attr = A_NORMAL;
      const char *sym = " ";

      if (rmode == RM_QUAD) {
        int gx = tcx * 2, gy = tcy * 2;
        int mask = 0;
        unsigned maxage = 0;
        if (cur[gy][gx]) {
          mask |= 8;
          if (age[gy][gx] > maxage)
            maxage = age[gy][gx];
        }
        if (cur[gy][gx + 1]) {
          mask |= 4;
          if (age[gy][gx + 1] > maxage)
            maxage = age[gy][gx + 1];
        }
        if (cur[gy + 1][gx]) {
          mask |= 2;
          if (age[gy + 1][gx] > maxage)
            maxage = age[gy + 1][gx];
        }
        if (cur[gy + 1][gx + 1]) {
          mask |= 1;
          if (age[gy + 1][gx + 1] > maxage)
            maxage = age[gy + 1][gx + 1];
        }
        if (mask)
          attr = age_attr(maxage);
        sym = quad_chars[mask];
      } else if (cur[tcy][tcx]) {
        attr = age_attr(age[tcy][tcx]);
        sym = (rmode == RM_BLOCK) ? "\u2588" : "#";
      }

      if (on_cursor)
        attr |= A_REVERSE;
      attrset(attr);
      /* addstr корректен в UTF-8 локали: сам декодирует многобайтные
       * последовательности. addch делать нельзя — он байтовый. */
      mvaddstr(tcy, tcx, sym);
    }
  }
  attrset(A_NORMAL);

  /* статусная строка */
  attron(A_REVERSE);
  move(TH, 0);
  clrtoeol();
  {
    char status[256];
    int fps = delay_us ? 1000000 / delay_us : 0;
    const char *mode_str = rmode == RM_QUAD    ? "quad "
                           : rmode == RM_BLOCK ? "block"
                                               : "ascii";
    snprintf(status, sizeof(status),
             " gen %-6ld %s  %2d fps  [%s] %dx%d   "
             "q:quit SPACE:pause n:step r:rand c:clear v:mode L:load",
             gen, paused ? "PAUSED" : "run   ", fps, mode_str, W, H);
    mvaddnstr(TH, 0, status, COLS);
  }
  attroff(A_REVERSE);

  refresh();
}

/* ---------- утилиты ---------- */
static void clear_grid(void) {
  for (int y = 0; y < H; ++y) {
    memset(cur[y], 0, (size_t)W);
    memset(age[y], 0, (size_t)W);
  }
  gen = 0;
}

static void random_fill(void) {
  for (int y = 0; y < H; ++y)
    for (int x = 0; x < W; ++x) {
      int v = (rand() % 100) < 25;
      cur[y][x] = (unsigned char)v;
      age[y][x] = (unsigned char)v;
    }
  gen = 0;
}

static int is_alive_char(char c) {
  return c == '#' || c == 'O' || c == 'o' || c == '*' || c == 'X' || c == 'x' ||
         c == '1';
}

static int load_pattern(const char *path) {
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  size_t cap = 1 << 16, len = 0;
  char *data = malloc(cap);
  if (!data) {
    fclose(f);
    return -1;
  }

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    if (line[0] == '!')
      continue; /* комментарий в стиле .cells */
    size_t ll = strlen(line);
    while (ll && (line[ll - 1] == '\n' || line[ll - 1] == '\r'))
      line[--ll] = 0;
    if (len + ll + 1 > cap) {
      cap *= 2;
      char *nd = realloc(data, cap);
      if (!nd) {
        free(data);
        fclose(f);
        return -1;
      }
      data = nd;
    }
    memcpy(data + len, line, ll);
    len += ll;
    data[len++] = '\n';
  }
  fclose(f);
  data[len] = 0;

  clear_grid();

  /* bbox живых клеток — чтобы центрировать узор. */
  int minx = 1 << 30, miny = 1 << 30, maxx = -1, maxy = -1;
  int y = 0, x = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = data[i];
    if (c == '\n') {
      ++y;
      x = 0;
      continue;
    }
    if (is_alive_char(c)) {
      if (x < minx)
        minx = x;
      if (x > maxx)
        maxx = x;
      if (y < miny)
        miny = y;
      if (y > maxy)
        maxy = y;
    }
    ++x;
  }
  if (maxx < 0) {
    free(data);
    return 0;
  }

  int pw = maxx - minx + 1, ph = maxy - miny + 1;
  int ox = (W - pw) / 2 - minx;
  int oy = (H - ph) / 2 - miny;

  y = 0;
  x = 0;
  for (size_t i = 0; i < len; ++i) {
    char c = data[i];
    if (c == '\n') {
      ++y;
      x = 0;
      continue;
    }
    if (is_alive_char(c)) {
      int py = y + oy, px = x + ox;
      if (py >= 0 && py < H && px >= 0 && px < W) {
        cur[py][px] = 1;
        age[py][px] = 1;
      }
    }
    ++x;
  }
  free(data);
  gen = 0;
  return 0;
}

/* ---------- resize / смена режима ---------- */
static void handle_resize(void) {
  int oldH = H;
  unsigned char **old_cur = cur, **old_next = next, **old_age = age;

  TW = COLS;
  TH = LINES - 1;
  if (TW < 1)
    TW = 1;
  if (TH < 1)
    TH = 1;

  int newW = TW * cpc_x();
  int newH = TH * cpc_y();

  unsigned char **n_cur = alloc_rows(newH, newW);
  unsigned char **n_next = alloc_rows(newH, newW);
  unsigned char **n_age = alloc_rows(newH, newW);

  if (old_cur) {
    int cw = W < newW ? W : newW;
    int chh = oldH < newH ? oldH : newH;
    for (int y = 0; y < chh; ++y) {
      memcpy(n_cur[y], old_cur[y], (size_t)cw);
      memcpy(n_age[y], old_age[y], (size_t)cw);
    }
    free_rows(old_cur, oldH);
    free_rows(old_next, oldH);
    free_rows(old_age, oldH);
  }

  cur = n_cur;
  next = n_next;
  age = n_age;
  W = newW;
  H = newH;

  if (cx >= W)
    cx = W - 1;
  if (cx < 0)
    cx = 0;
  if (cy >= H)
    cy = H - 1;
  if (cy < 0)
    cy = 0;

  scrollok(stdscr, FALSE);
}

/* ---------- SIGWINCH (есть везде, кроме Windows-консоли) ---------- */
static volatile sig_atomic_t resize_flag = 0;
static void on_winch(int sig) {
  (void)sig;
  resize_flag = 1;
}

/* ---------- промпт загрузки ---------- */
static void prompt_load(void) {
  timeout(-1);
  echo();
  curs_set(1);
  move(TH, 0);
  clrtoeol();
  mvaddstr(TH, 0, "Load file: ");
  refresh();

  char path[512];
  path[0] = 0;
  int rc = getnstr(path, sizeof(path) - 1);

  noecho();
  curs_set(0);
  timeout(0);

  if (rc == OK && path[0] && load_pattern(path) != 0) {
    move(TH, 0);
    clrtoeol();
    mvprintw(TH, 0, "Cannot open '%s' — press any key", path);
    refresh();
    timeout(-1);
    getch();
    timeout(0);
  }
}

/* ---------- main ---------- */
int main(int argc, char **argv) {
  setlocale(LC_ALL, "");
  srand((unsigned)time(NULL));

  initscr();
  cbreak();
  noecho();
  curs_set(0);
  keypad(stdscr, TRUE);
  nodelay(stdscr, TRUE);
  init_colors();
#ifndef _WIN32
  signal(SIGWINCH, on_winch);
#endif

  W = H = 0;
  handle_resize();

  if (argc > 1) {
    if (load_pattern(argv[1]) != 0)
      random_fill();
  } else {
    random_fill();
  }

  int running = 1;
  while (running) {
    if (resize_flag) {
      resize_flag = 0;
      handle_resize();
    }

    draw();

    int ch;
    while ((ch = getch()) != ERR) {
      switch (ch) {
      case 'q':
      case 'Q':
      case 27:
        running = 0;
        break;

      case ' ':
        paused = !paused;
        break;
      case 'n':
        if (paused)
          step();
        break;
      case 'r':
        random_fill();
        break;
      case 'c':
        clear_grid();
        break;

      case 'v':
      case 'V':
        rmode = (enum render_mode)((rmode + 1) % 3);
        handle_resize();
        break;

      case 'x':
      case '\n':
      case KEY_ENTER:
        cur[cy][cx] ^= 1;
        age[cy][cx] = cur[cy][cx];
        break;

      case 'g':
        for (int i = 0; i < 20; ++i) {
          int xx = cx + (rand() % 20) - 10;
          int yy = cy + (rand() % 10) - 5;
          if (xx >= 0 && xx < W && yy >= 0 && yy < H)
            cur[yy][xx] = age[yy][xx] = 1;
        }
        break;

      case 'h':
      case KEY_LEFT:
        if (cx > 0)
          --cx;
        break;
      case 'l':
      case KEY_RIGHT:
        if (cx < W - 1)
          ++cx;
        break;
      case 'k':
      case KEY_UP:
        if (cy > 0)
          --cy;
        break;
      case 'j':
      case KEY_DOWN:
        if (cy < H - 1)
          ++cy;
        break;

      case '[':
        delay_us += 20000;
        if (delay_us > 1000000)
          delay_us = 1000000;
        break;
      case ']':
        delay_us -= 20000;
        if (delay_us < 10000)
          delay_us = 10000;
        break;

      case 'L':
        prompt_load();
        break;

      case KEY_RESIZE:
        resize_flag = 1;
        break;
      default:
        break;
      }
    }

    if (running && !paused) {
      step();
      SLEEP_US(delay_us);
    } else {
      SLEEP_US(15000);
    }
  }

  free_rows(cur, H);
  free_rows(next, H);
  free_rows(age, H);
  endwin();
  return 0;
}