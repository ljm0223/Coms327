#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include "heap.h"
#include <stdbool.h>

#include "heap.h"

#define malloc(size) ({          \
  void *_tmp;                    \
  assert((_tmp = malloc(size))); \
  _tmp;                          \
})

typedef struct path {
  heap_node_t *hn;
  uint8_t pos[2];
  uint8_t from[2];
  int32_t cost;
} path_t;

typedef enum dim {
  dim_x,
  dim_y,
  num_dims
} dim_t;

typedef int16_t pair_t[num_dims];

#define MAP_X              80
#define MAP_Y              21
#define MIN_TREES          10
#define MIN_BOULDERS       10
#define TREE_PROB          95
#define BOULDER_PROB       95
#define WORLD_SIZE         401

#define MOUNTAIN_SYMBOL       '%'
#define BOULDER_SYMBOL        '0'
#define TREE_SYMBOL           '4'
#define FOREST_SYMBOL         '^'
#define GATE_SYMBOL           '#'
#define PATH_SYMBOL           '#'
#define POKEMART_SYMBOL       'M'
#define POKEMON_CENTER_SYMBOL 'C'
#define TALL_GRASS_SYMBOL     ':'
#define SHORT_GRASS_SYMBOL    '.'
#define WATER_SYMBOL          '~'
#define ERROR_SYMBOL          '&'

#define mappair(pair) (m->map[pair[dim_y]][pair[dim_x]])
#define mapxy(x, y) (m->map[y][x])
#define heightpair(pair) (m->height[pair[dim_y]][pair[dim_x]])
#define heightxy(x, y) (m->height[y][x])

typedef enum __attribute__ ((__packed__)) terrain_type {
  ter_boulder,
  ter_tree,
  ter_path,
  ter_mart,
  ter_center,
  ter_grass,
  ter_clearing,
  ter_mountain,
  ter_forest,
  ter_water, // +
  ter_gate, // +
  num_terrain_types,
  ter_debug // +
} terrain_type_t;

typedef enum __attribute__ ((__packed__)) character_type {
  char_pc,
  char_hiker,
  char_rival,
  char_swimmer,
  char_pacer, // +
  char_wanderer, // +
  char_sentry, // +
  char_explorer, // +
  char_other,
  num_character_types
} character_type_t;

typedef struct pc {
  pair_t pos;
} pc_t;

typedef struct trainer{
  heap_node_t *hn;
  int pos[2];
  int next_pos[2];
  character_type_t type;
  char dir;
  terrain_type_t ter;
  int cost;
  bool display;
} trainer_t;

typedef struct map {
  terrain_type_t map[MAP_Y][MAP_X];
  uint8_t height[MAP_Y][MAP_X];
  int8_t n, s, e, w;
} map_t;

typedef struct queue_node {
  int x, y;
  struct queue_node *next;
} queue_node_t;

typedef struct world {
  map_t *world[WORLD_SIZE][WORLD_SIZE];
  pair_t cur_idx;
  map_t *cur_map;
  /* Place distance maps in world, not map, since *
   * we only need one pair at any given time.     */
  int hiker_dist[MAP_Y][MAP_X];
  int rival_dist[MAP_Y][MAP_X];
  trainer_t trainers[MAP_Y][MAP_X];
  pc_t pc;
} world_t;

/* Even unallocated, a WORLD_SIZE x WORLD_SIZE array of pointers is a very *
 * large thing to put on the stack.  To avoid that, world is a global.     */
world_t world;

#define rand_range(min, max) ((rand() % ((max + 1) - min)) + (min)) //FIXME: made change

/* Just to make the following table fit in 80 columns */
#define IM INT_MAX
int32_t move_cost[num_character_types][num_terrain_types] = {
//  boulder,tree,path,mart,center,grass,clearing,mountain,forest,water,gate
  { IM, IM, 10, 10, 10, 20, 10, IM, IM, IM, 10 },
  { IM, IM, 10, 50, 50, 15, 10, 15, 15, IM, IM },
  { IM, IM, 10, 50, 50, 20, 10, IM, IM, IM, IM },
  { IM, IM, IM, IM, IM, IM, IM, IM, IM,  7, IM },
};
#undef IM

static int32_t path_cmp(const void *key, const void *with) {
  return ((path_t *) key)->cost - ((path_t *) with)->cost;
}

static int32_t edge_penalty(int8_t x, int8_t y)
{
  return (x == 1 || y == 1 || x == MAP_X - 2 || y == MAP_Y - 2) ? 2 : 1;
}

static void dijkstra_path(map_t *m, pair_t from, pair_t to)
{
  static path_t path[MAP_Y][MAP_X], *p;
  static uint32_t initialized = 0;
  heap_t h;
  uint32_t x, y;

  if (!initialized) {
    for (y = 0; y < MAP_Y; y++) {
      for (x = 0; x < MAP_X; x++) {
        path[y][x].pos[dim_y] = y;
        path[y][x].pos[dim_x] = x;
      }
    }
    initialized = 1;
  }
  
  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      path[y][x].cost = INT_MAX;
    }
  }

  path[from[dim_y]][from[dim_x]].cost = 0;

  heap_init(&h, path_cmp, NULL);

  for (y = 1; y < MAP_Y - 1; y++) {
    for (x = 1; x < MAP_X - 1; x++) {
      path[y][x].hn = heap_insert(&h, &path[y][x]);
    }
  }

  while ((p = heap_remove_min(&h))) {
    p->hn = NULL;

    if ((p->pos[dim_y] == to[dim_y]) && p->pos[dim_x] == to[dim_x]) {
      for (x = to[dim_x], y = to[dim_y];
           (x != from[dim_x]) || (y != from[dim_y]);
           p = &path[y][x], x = p->from[dim_x], y = p->from[dim_y]) {
        /* Don't overwrite the gate */
        if (x != to[dim_x] || y != to[dim_y]) {
          mapxy(x, y) = ter_path;
          heightxy(x, y) = 0;
        }
      }
      heap_delete(&h);
      return;
    }

    if ((path[p->pos[dim_y] - 1][p->pos[dim_x]    ].hn) &&
        (path[p->pos[dim_y] - 1][p->pos[dim_x]    ].cost >
         ((p->cost + heightpair(p->pos)) *
          edge_penalty(p->pos[dim_x], p->pos[dim_y] - 1)))) {
      path[p->pos[dim_y] - 1][p->pos[dim_x]    ].cost =
        ((p->cost + heightpair(p->pos)) *
         edge_penalty(p->pos[dim_x], p->pos[dim_y] - 1));
      path[p->pos[dim_y] - 1][p->pos[dim_x]    ].from[dim_y] = p->pos[dim_y];
      path[p->pos[dim_y] - 1][p->pos[dim_x]    ].from[dim_x] = p->pos[dim_x];
      heap_decrease_key_no_replace(&h, path[p->pos[dim_y] - 1]
                                           [p->pos[dim_x]    ].hn);
    }
    if ((path[p->pos[dim_y]    ][p->pos[dim_x] - 1].hn) &&
        (path[p->pos[dim_y]    ][p->pos[dim_x] - 1].cost >
         ((p->cost + heightpair(p->pos)) *
          edge_penalty(p->pos[dim_x] - 1, p->pos[dim_y])))) {
      path[p->pos[dim_y]][p->pos[dim_x] - 1].cost =
        ((p->cost + heightpair(p->pos)) *
         edge_penalty(p->pos[dim_x] - 1, p->pos[dim_y]));
      path[p->pos[dim_y]    ][p->pos[dim_x] - 1].from[dim_y] = p->pos[dim_y];
      path[p->pos[dim_y]    ][p->pos[dim_x] - 1].from[dim_x] = p->pos[dim_x];
      heap_decrease_key_no_replace(&h, path[p->pos[dim_y]    ]
                                           [p->pos[dim_x] - 1].hn);
    }
    if ((path[p->pos[dim_y]    ][p->pos[dim_x] + 1].hn) &&
        (path[p->pos[dim_y]    ][p->pos[dim_x] + 1].cost >
         ((p->cost + heightpair(p->pos)) *
          edge_penalty(p->pos[dim_x] + 1, p->pos[dim_y])))) {
      path[p->pos[dim_y]][p->pos[dim_x] + 1].cost =
        ((p->cost + heightpair(p->pos)) *
         edge_penalty(p->pos[dim_x] + 1, p->pos[dim_y]));
      path[p->pos[dim_y]    ][p->pos[dim_x] + 1].from[dim_y] = p->pos[dim_y];
      path[p->pos[dim_y]    ][p->pos[dim_x] + 1].from[dim_x] = p->pos[dim_x];
      heap_decrease_key_no_replace(&h, path[p->pos[dim_y]    ]
                                           [p->pos[dim_x] + 1].hn);
    }
    if ((path[p->pos[dim_y] + 1][p->pos[dim_x]    ].hn) &&
        (path[p->pos[dim_y] + 1][p->pos[dim_x]    ].cost >
         ((p->cost + heightpair(p->pos)) *
          edge_penalty(p->pos[dim_x], p->pos[dim_y] + 1)))) {
      path[p->pos[dim_y] + 1][p->pos[dim_x]    ].cost =
        ((p->cost + heightpair(p->pos)) *
         edge_penalty(p->pos[dim_x], p->pos[dim_y] + 1));
      path[p->pos[dim_y] + 1][p->pos[dim_x]    ].from[dim_y] = p->pos[dim_y];
      path[p->pos[dim_y] + 1][p->pos[dim_x]    ].from[dim_x] = p->pos[dim_x];
      heap_decrease_key_no_replace(&h, path[p->pos[dim_y] + 1]
                                           [p->pos[dim_x]    ].hn);
    }
  }
}

static int build_paths(map_t *m)
{
  pair_t from, to;

  /*  printf("%d %d %d %d\n", m->n, m->s, m->e, m->w);*/

  if (m->e != -1 && m->w != -1) {
    from[dim_x] = 1;
    to[dim_x] = MAP_X - 2;
    from[dim_y] = m->w;
    to[dim_y] = m->e;

    dijkstra_path(m, from, to);
  }

  if (m->n != -1 && m->s != -1) {
    from[dim_y] = 1;
    to[dim_y] = MAP_Y - 2;
    from[dim_x] = m->n;
    to[dim_x] = m->s;

    dijkstra_path(m, from, to);
  }

  if (m->e == -1) {
    if (m->s == -1) {
      from[dim_x] = 1;
      from[dim_y] = m->w;
      to[dim_x] = m->n;
      to[dim_y] = 1;
    } else {
      from[dim_x] = 1;
      from[dim_y] = m->w;
      to[dim_x] = m->s;
      to[dim_y] = MAP_Y - 2;
    }

    dijkstra_path(m, from, to);
  }

  if (m->w == -1) {
    if (m->s == -1) {
      from[dim_x] = MAP_X - 2;
      from[dim_y] = m->e;
      to[dim_x] = m->n;
      to[dim_y] = 1;
    } else {
      from[dim_x] = MAP_X - 2;
      from[dim_y] = m->e;
      to[dim_x] = m->s;
      to[dim_y] = MAP_Y - 2;
    }

    dijkstra_path(m, from, to);
  }

  if (m->n == -1) {
    if (m->e == -1) {
      from[dim_x] = 1;
      from[dim_y] = m->w;
      to[dim_x] = m->s;
      to[dim_y] = MAP_Y - 2;
    } else {
      from[dim_x] = MAP_X - 2;
      from[dim_y] = m->e;
      to[dim_x] = m->s;
      to[dim_y] = MAP_Y - 2;
    }

    dijkstra_path(m, from, to);
  }

  if (m->s == -1) {
    if (m->e == -1) {
      from[dim_x] = 1;
      from[dim_y] = m->w;
      to[dim_x] = m->n;
      to[dim_y] = 1;
    } else {
      from[dim_x] = MAP_X - 2;
      from[dim_y] = m->e;
      to[dim_x] = m->n;
      to[dim_y] = 1;
    }

    dijkstra_path(m, from, to);
  }

  return 0;
}

static int gaussian[5][5] = {
  {  1,  4,  7,  4,  1 },
  {  4, 16, 26, 16,  4 },
  {  7, 26, 41, 26,  7 },
  {  4, 16, 26, 16,  4 },
  {  1,  4,  7,  4,  1 }
};

static int smooth_height(map_t *m)
{
  int32_t i, x, y;
  int32_t s, t, p, q;
  queue_node_t *head, *tail, *tmp;
  /*  FILE *out;*/
  uint8_t height[MAP_Y][MAP_X];

  memset(&height, 0, sizeof (height));

  /* Seed with some values */
  for (i = 1; i < 255; i += 20) {
    do {
      x = rand() % MAP_X;
      y = rand() % MAP_Y;
    } while (height[y][x]);
    height[y][x] = i;
    if (i == 1) {
      head = tail = malloc(sizeof (*tail));
    } else {
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
    }
    tail->next = NULL;
    tail->x = x;
    tail->y = y;
  }

  /*
  out = fopen("seeded.pgm", "w");
  fprintf(out, "P5\n%u %u\n255\n", MAP_X, MAP_Y);
  fwrite(&height, sizeof (height), 1, out);
  fclose(out);
  */
  
  /* Diffuse the vaules to fill the space */
  while (head) {
    x = head->x;
    y = head->y;
    i = height[y][x];

    if (x - 1 >= 0 && y - 1 >= 0 && !height[y - 1][x - 1]) {
      height[y - 1][x - 1] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x - 1;
      tail->y = y - 1;
    }
    if (x - 1 >= 0 && !height[y][x - 1]) {
      height[y][x - 1] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x - 1;
      tail->y = y;
    }
    if (x - 1 >= 0 && y + 1 < MAP_Y && !height[y + 1][x - 1]) {
      height[y + 1][x - 1] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x - 1;
      tail->y = y + 1;
    }
    if (y - 1 >= 0 && !height[y - 1][x]) {
      height[y - 1][x] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x;
      tail->y = y - 1;
    }
    if (y + 1 < MAP_Y && !height[y + 1][x]) {
      height[y + 1][x] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x;
      tail->y = y + 1;
    }
    if (x + 1 < MAP_X && y - 1 >= 0 && !height[y - 1][x + 1]) {
      height[y - 1][x + 1] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x + 1;
      tail->y = y - 1;
    }
    if (x + 1 < MAP_X && !height[y][x + 1]) {
      height[y][x + 1] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x + 1;
      tail->y = y;
    }
    if (x + 1 < MAP_X && y + 1 < MAP_Y && !height[y + 1][x + 1]) {
      height[y + 1][x + 1] = i;
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
      tail->next = NULL;
      tail->x = x + 1;
      tail->y = y + 1;
    }

    tmp = head;
    head = head->next;
    free(tmp);
  }

  /* And smooth it a bit with a gaussian convolution */
  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      for (s = t = p = 0; p < 5; p++) {
        for (q = 0; q < 5; q++) {
          if (y + (p - 2) >= 0 && y + (p - 2) < MAP_Y &&
              x + (q - 2) >= 0 && x + (q - 2) < MAP_X) {
            s += gaussian[p][q];
            t += height[y + (p - 2)][x + (q - 2)] * gaussian[p][q];
          }
        }
      }
      m->height[y][x] = t / s;
    }
  }
  /* Let's do it again, until it's smooth like Kenny G. */
  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      for (s = t = p = 0; p < 5; p++) {
        for (q = 0; q < 5; q++) {
          if (y + (p - 2) >= 0 && y + (p - 2) < MAP_Y &&
              x + (q - 2) >= 0 && x + (q - 2) < MAP_X) {
            s += gaussian[p][q];
            t += height[y + (p - 2)][x + (q - 2)] * gaussian[p][q];
          }
        }
      }
      m->height[y][x] = t / s;
    }
  }

  /*
  out = fopen("diffused.pgm", "w");
  fprintf(out, "P5\n%u %u\n255\n", MAP_X, MAP_Y);
  fwrite(&height, sizeof (height), 1, out);
  fclose(out);

  out = fopen("smoothed.pgm", "w");
  fprintf(out, "P5\n%u %u\n255\n", MAP_X, MAP_Y);
  fwrite(&m->height, sizeof (m->height), 1, out);
  fclose(out);
  */

  return 0;
}

static void find_building_location(map_t *m, pair_t p)
{
  do {
    p[dim_x] = rand() % (MAP_X - 3) + 1;
    p[dim_y] = rand() % (MAP_Y - 3) + 1;

    if ((((mapxy(p[dim_x] - 1, p[dim_y]    ) == ter_path)     &&
          (mapxy(p[dim_x] - 1, p[dim_y] + 1) == ter_path))    ||
         ((mapxy(p[dim_x] + 2, p[dim_y]    ) == ter_path)     &&
          (mapxy(p[dim_x] + 2, p[dim_y] + 1) == ter_path))    ||
         ((mapxy(p[dim_x]    , p[dim_y] - 1) == ter_path)     &&
          (mapxy(p[dim_x] + 1, p[dim_y] - 1) == ter_path))    ||
         ((mapxy(p[dim_x]    , p[dim_y] + 2) == ter_path)     &&
          (mapxy(p[dim_x] + 1, p[dim_y] + 2) == ter_path)))   &&
        (((mapxy(p[dim_x]    , p[dim_y]    ) != ter_mart)     &&
          (mapxy(p[dim_x]    , p[dim_y]    ) != ter_center)   &&
          (mapxy(p[dim_x] + 1, p[dim_y]    ) != ter_mart)     &&
          (mapxy(p[dim_x] + 1, p[dim_y]    ) != ter_center)   &&
          (mapxy(p[dim_x]    , p[dim_y] + 1) != ter_mart)     &&
          (mapxy(p[dim_x]    , p[dim_y] + 1) != ter_center)   &&
          (mapxy(p[dim_x] + 1, p[dim_y] + 1) != ter_mart)     &&
          (mapxy(p[dim_x] + 1, p[dim_y] + 1) != ter_center))) &&
        (((mapxy(p[dim_x]    , p[dim_y]    ) != ter_path)     &&
          (mapxy(p[dim_x] + 1, p[dim_y]    ) != ter_path)     &&
          (mapxy(p[dim_x]    , p[dim_y] + 1) != ter_path)     &&
          (mapxy(p[dim_x] + 1, p[dim_y] + 1) != ter_path)))) {
          break;
    }
  } while (1);
}

static int place_pokemart(map_t *m)
{
  pair_t p;

  find_building_location(m, p);

  mapxy(p[dim_x]    , p[dim_y]    ) = ter_mart;
  mapxy(p[dim_x] + 1, p[dim_y]    ) = ter_mart;
  mapxy(p[dim_x]    , p[dim_y] + 1) = ter_mart;
  mapxy(p[dim_x] + 1, p[dim_y] + 1) = ter_mart;

  return 0;
}

static int place_center(map_t *m)
{  pair_t p;

  find_building_location(m, p);

  mapxy(p[dim_x]    , p[dim_y]    ) = ter_center;
  mapxy(p[dim_x] + 1, p[dim_y]    ) = ter_center;
  mapxy(p[dim_x]    , p[dim_y] + 1) = ter_center;
  mapxy(p[dim_x] + 1, p[dim_y] + 1) = ter_center;

  return 0;
}

/* Chooses tree or boulder for border cell.  Choice is biased by dominance *
 * of neighboring cells.                                                   */
static terrain_type_t border_type(map_t *m, int32_t x, int32_t y)
{
  int32_t p, q;
  int32_t r, t;
  int32_t miny, minx, maxy, maxx;
  
  r = t = 0;
  
  miny = y - 1 >= 0 ? y - 1 : 0;
  maxy = y + 1 <= MAP_Y ? y + 1: MAP_Y;
  minx = x - 1 >= 0 ? x - 1 : 0;
  maxx = x + 1 <= MAP_X ? x + 1: MAP_X;

  for (q = miny; q < maxy; q++) {
    for (p = minx; p < maxx; p++) {
      if (q != y || p != x) {
        if (m->map[q][p] == ter_mountain ||
            m->map[q][p] == ter_boulder) {
          r++;
        } else if (m->map[q][p] == ter_forest ||
                   m->map[q][p] == ter_tree) {
          t++;
        }
      }
    }
  }
  
  if (t == r) {
    return rand() & 1 ? ter_boulder : ter_tree;
  } else if (t > r) {
    if (rand() % 10) {
      return ter_tree;
    } else {
      return ter_boulder;
    }
  } else {
    if (rand() % 10) {
      return ter_boulder;
    } else {
      return ter_tree;
    }
  }
}

static int map_terrain(map_t *m, int8_t n, int8_t s, int8_t e, int8_t w)
{
  int32_t i, x, y;
  queue_node_t *head, *tail, *tmp;
  //  FILE *out;
  int num_grass, num_clearing, num_mountain, num_forest, num_water, num_total;
  terrain_type_t type;
  int added_current = 0;
  
  num_grass = rand() % 4 + 2;
  num_clearing = rand() % 4 + 2;
  num_mountain = rand() % 2 + 1;
  num_forest = rand() % 2 + 1;
  num_water = rand() % 2 + 1;
  num_total = num_grass + num_clearing + num_mountain + num_forest + num_water;

  memset(&m->map, 0, sizeof (m->map));

  /* Seed with some values */
  for (i = 0; i < num_total; i++) {
    do {
      x = rand() % MAP_X;
      y = rand() % MAP_Y;
    } while (m->map[y][x]);
    if (i == 0) {
      type = ter_grass;
    } else if (i == num_grass) {
      type = ter_clearing;
    } else if (i == num_grass + num_clearing) {
      type = ter_mountain;
    } else if (i == num_grass + num_clearing + num_mountain) {
      type = ter_forest;
    } else if (i == num_grass + num_clearing + num_mountain + num_forest) {
      type = ter_water;
    }
    m->map[y][x] = type;
    if (i == 0) {
      head = tail = malloc(sizeof (*tail));
    } else {
      tail->next = malloc(sizeof (*tail));
      tail = tail->next;
    }
    tail->next = NULL;
    tail->x = x;
    tail->y = y;
  }

  /*
  out = fopen("seeded.pgm", "w");
  fprintf(out, "P5\n%u %u\n255\n", MAP_X, MAP_Y);
  fwrite(&m->map, sizeof (m->map), 1, out);
  fclose(out);
  */

  /* Diffuse the vaules to fill the space */
  while (head) {
    x = head->x;
    y = head->y;
    i = m->map[y][x];
    
    if (x - 1 >= 0 && !m->map[y][x - 1]) {
      if ((rand() % 100) < 80) {
        m->map[y][x - 1] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x - 1;
        tail->y = y;
      } else if (!added_current) {
        added_current = 1;
        m->map[y][x] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x;
        tail->y = y;
      }
    }

    if (y - 1 >= 0 && !m->map[y - 1][x]) {
      if ((rand() % 100) < 20) {
        m->map[y - 1][x] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x;
        tail->y = y - 1;
      } else if (!added_current) {
        added_current = 1;
        m->map[y][x] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x;
        tail->y = y;
      }
    }

    if (y + 1 < MAP_Y && !m->map[y + 1][x]) {
      if ((rand() % 100) < 20) {
        m->map[y + 1][x] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x;
        tail->y = y + 1;
      } else if (!added_current) {
        added_current = 1;
        m->map[y][x] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x;
        tail->y = y;
      }
    }

    if (x + 1 < MAP_X && !m->map[y][x + 1]) {
      if ((rand() % 100) < 80) {
        m->map[y][x + 1] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x + 1;
        tail->y = y;
      } else if (!added_current) {
        added_current = 1;
        m->map[y][x] = i;
        tail->next = malloc(sizeof (*tail));
        tail = tail->next;
        tail->next = NULL;
        tail->x = x;
        tail->y = y;
      }
    }

    added_current = 0;
    tmp = head;
    head = head->next;
    free(tmp);
  }

  /*
  out = fopen("diffused.pgm", "w");
  fprintf(out, "P5\n%u %u\n255\n", MAP_X, MAP_Y);
  fwrite(&m->map, sizeof (m->map), 1, out);
  fclose(out);
  */
  
  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      if (y == 0 || y == MAP_Y - 1 ||
          x == 0 || x == MAP_X - 1) {
        mapxy(x, y) = border_type(m, x, y);
      }
    }
  }

  m->n = n;
  m->s = s;
  m->e = e;
  m->w = w;

  if (n != -1) {
    mapxy(n,         0        ) = ter_gate;
    mapxy(n,         1        ) = ter_gate;
  }
  if (s != -1) {
    mapxy(s,         MAP_Y - 1) = ter_gate;
    mapxy(s,         MAP_Y - 2) = ter_gate;
  }
  if (w != -1) {
    mapxy(0,         w        ) = ter_gate;
    mapxy(1,         w        ) = ter_gate;
  }
  if (e != -1) {
    mapxy(MAP_X - 1, e        ) = ter_gate;
    mapxy(MAP_X - 2, e        ) = ter_gate;
  }

  return 0;
}

static int place_boulders(map_t *m)
{
  int i;
  int x, y;

  for (i = 0; i < MIN_BOULDERS || rand() % 100 < BOULDER_PROB; i++) {
    y = rand() % (MAP_Y - 2) + 1;
    x = rand() % (MAP_X - 2) + 1;
    if (m->map[y][x] != ter_forest &&
        m->map[y][x] != ter_path   &&
        m->map[y][x] != ter_gate) {
      m->map[y][x] = ter_boulder;
    }
  }

  return 0;
}

static int place_trees(map_t *m)
{
  int i;
  int x, y;
  
  for (i = 0; i < MIN_TREES || rand() % 100 < TREE_PROB; i++) {
    y = rand() % (MAP_Y - 2) + 1;
    x = rand() % (MAP_X - 2) + 1;
    if (m->map[y][x] != ter_mountain &&
        m->map[y][x] != ter_path     &&
        m->map[y][x] != ter_water    &&
        m->map[y][x] != ter_gate) {
      m->map[y][x] = ter_tree;
    }
  }

  return 0;
}

// New map expects cur_idx to refer to the index to be generated.  If that
// map has already been generated then the only thing this does is set
// cur_map.
static int new_map()
{
  int d, p;
  int e, w, n, s;

  if (world.world[world.cur_idx[dim_y]][world.cur_idx[dim_x]]) {
    world.cur_map = world.world[world.cur_idx[dim_y]][world.cur_idx[dim_x]];
    return 0;
  }

  world.cur_map                                             =
    world.world[world.cur_idx[dim_y]][world.cur_idx[dim_x]] =
    malloc(sizeof (*world.cur_map));

  smooth_height(world.cur_map);
  
  if (!world.cur_idx[dim_y]) {
    n = -1;
  } else if (world.world[world.cur_idx[dim_y] - 1][world.cur_idx[dim_x]]) {
    n = world.world[world.cur_idx[dim_y] - 1][world.cur_idx[dim_x]]->s;
  } else {
    n = 1 + rand() % (MAP_X - 2);
  }
  if (world.cur_idx[dim_y] == WORLD_SIZE - 1) {
    s = -1;
  } else if (world.world[world.cur_idx[dim_y] + 1][world.cur_idx[dim_x]]) {
    s = world.world[world.cur_idx[dim_y] + 1][world.cur_idx[dim_x]]->n;
  } else  {
    s = 1 + rand() % (MAP_X - 2);
  }
  if (!world.cur_idx[dim_x]) {
    w = -1;
  } else if (world.world[world.cur_idx[dim_y]][world.cur_idx[dim_x] - 1]) {
    w = world.world[world.cur_idx[dim_y]][world.cur_idx[dim_x] - 1]->e;
  } else {
    w = 1 + rand() % (MAP_Y - 2);
  }
  if (world.cur_idx[dim_x] == WORLD_SIZE - 1) {
    e = -1;
  } else if (world.world[world.cur_idx[dim_y]][world.cur_idx[dim_x] + 1]) {
    e = world.world[world.cur_idx[dim_y]][world.cur_idx[dim_x] + 1]->w;
  } else {
    e = 1 + rand() % (MAP_Y - 2);
  }
  
  map_terrain(world.cur_map, n, s, e, w);
     
  place_boulders(world.cur_map);
  place_trees(world.cur_map);
  build_paths(world.cur_map);
  d = (abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)) +
       abs(world.cur_idx[dim_y] - (WORLD_SIZE / 2)));
  p = d > 200 ? 5 : (50 - ((45 * d) / 200));
  //  printf("d=%d, p=%d\n", d, p);
  if ((rand() % 100) < p || !d) {
    place_pokemart(world.cur_map);
  }
  if ((rand() % 100) < p || !d) {
    place_center(world.cur_map);
  }

  return 0;
}

static void print_map()
{
  int x, y;
  int default_reached = 0;

  printf("\n\n\n");

  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      if (world.pc.pos[dim_y] == y &&
          world.pc.pos[dim_x] == x) {
        putchar('@');
      } else if(world.trainers[y][x].display){  //FIXME: made change
          if(world.trainers[y][x].type == char_hiker){
            putchar('h');
          } else if(world.trainers[y][x].type == char_rival){
            putchar('r');
          } else if(world.trainers[y][x].type == char_pacer){
            putchar('p');
          } else if(world.trainers[y][x].type == char_wanderer){
            putchar('w');
          } else if(world.trainers[y][x].type == char_sentry){
            putchar('s');
          }else if(world.trainers[y][x].type == char_explorer){
            putchar('n');
          }
      }else {
        switch (world.cur_map->map[y][x]) {
        case ter_boulder:
          putchar(BOULDER_SYMBOL);
          break;
        case ter_mountain:
          putchar(MOUNTAIN_SYMBOL);
          break;
        case ter_tree:
          putchar(TREE_SYMBOL);
          break;
        case ter_forest:
          putchar(FOREST_SYMBOL);
          break;
        case ter_path:
          putchar(PATH_SYMBOL);
          break;
        case ter_gate:
          putchar(GATE_SYMBOL);
          break;
        case ter_mart:
          putchar(POKEMART_SYMBOL);
          break;
        case ter_center:
          putchar(POKEMON_CENTER_SYMBOL);
          break;
        case ter_grass:
          putchar(TALL_GRASS_SYMBOL);
          break;
        case ter_clearing:
          putchar(SHORT_GRASS_SYMBOL);
          break;
        case ter_water:
          putchar(WATER_SYMBOL);
          break;
        default:
          putchar(ERROR_SYMBOL);
          default_reached = 1;
          break;
        }
      }
    }
    putchar('\n');
  }

  if (default_reached) {
    fprintf(stderr, "Default reached in %s\n", __FUNCTION__);
  }
}

// The world is global because of its size, so init_world is parameterless
void init_world()
{
  world.cur_idx[dim_x] = world.cur_idx[dim_y] = WORLD_SIZE / 2;
  new_map();
}

void delete_world()
{
  int x, y;

  for (y = 0; y < WORLD_SIZE; y++) {
    for (x = 0; x < WORLD_SIZE; x++) {
      if (world.world[y][x]) {
        free(world.world[y][x]);
        world.world[y][x] = NULL;
      }
    }
  }
}

#define ter_cost(x, y, c) move_cost[c][m->map[y][x]]

static int32_t hiker_cmp(const void *key, const void *with) {
  return (world.hiker_dist[((path_t *) key)->pos[dim_y]]
                          [((path_t *) key)->pos[dim_x]] -
          world.hiker_dist[((path_t *) with)->pos[dim_y]]
                          [((path_t *) with)->pos[dim_x]]);
}

static int32_t rival_cmp(const void *key, const void *with) {
  return (world.rival_dist[((path_t *) key)->pos[dim_y]]
                          [((path_t *) key)->pos[dim_x]] -
          world.rival_dist[((path_t *) with)->pos[dim_y]]
                          [((path_t *) with)->pos[dim_x]]);
}

static int32_t trainer_cmp(const void *key, const void *with) { //FIXME: made change
  return (((((trainer_t *) key)->cost)-((trainer_t *) with)->cost));
}

void pathfind(map_t *m)
{
  heap_t h;
  uint32_t x, y;
  static path_t p[MAP_Y][MAP_X], *c;
  static uint32_t initialized = 0;

  if (!initialized) {
    initialized = 1;
    for (y = 0; y < MAP_Y; y++) {
      for (x = 0; x < MAP_X; x++) {
        p[y][x].pos[dim_y] = y;
        p[y][x].pos[dim_x] = x;
      }
    }
  }

  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      world.hiker_dist[y][x] = world.rival_dist[y][x] = INT_MAX;
    }
  }
  world.hiker_dist[world.pc.pos[dim_y]][world.pc.pos[dim_x]] = 
    world.rival_dist[world.pc.pos[dim_y]][world.pc.pos[dim_x]] = 0;

  heap_init(&h, hiker_cmp, NULL);

  for (y = 1; y < MAP_Y - 1; y++) {
    for (x = 1; x < MAP_X - 1; x++) {
      if (ter_cost(x, y, char_hiker) != INT_MAX) {
        p[y][x].hn = heap_insert(&h, &p[y][x]);
      } else {
        p[y][x].hn = NULL;
      }
    }
  }

  while ((c = heap_remove_min(&h))) {
    c->hn = NULL;
    if ((p[c->pos[dim_y] - 1][c->pos[dim_x] - 1].hn) &&
        (world.hiker_dist[c->pos[dim_y] - 1][c->pos[dim_x] - 1] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y] - 1][c->pos[dim_x] - 1] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] - 1][c->pos[dim_x] - 1].hn);
    }
    if ((p[c->pos[dim_y] - 1][c->pos[dim_x]    ].hn) &&
        (world.hiker_dist[c->pos[dim_y] - 1][c->pos[dim_x]    ] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y] - 1][c->pos[dim_x]    ] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] - 1][c->pos[dim_x]    ].hn);
    }
    if ((p[c->pos[dim_y] - 1][c->pos[dim_x] + 1].hn) &&
        (world.hiker_dist[c->pos[dim_y] - 1][c->pos[dim_x] + 1] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y] - 1][c->pos[dim_x] + 1] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] - 1][c->pos[dim_x] + 1].hn);
    }
    if ((p[c->pos[dim_y]    ][c->pos[dim_x] - 1].hn) &&
        (world.hiker_dist[c->pos[dim_y]    ][c->pos[dim_x] - 1] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y]    ][c->pos[dim_x] - 1] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y]    ][c->pos[dim_x] - 1].hn);
    }
    if ((p[c->pos[dim_y]    ][c->pos[dim_x] + 1].hn) &&
        (world.hiker_dist[c->pos[dim_y]    ][c->pos[dim_x] + 1] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y]    ][c->pos[dim_x] + 1] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y]    ][c->pos[dim_x] + 1].hn);
    }
    if ((p[c->pos[dim_y] + 1][c->pos[dim_x] - 1].hn) &&
        (world.hiker_dist[c->pos[dim_y] + 1][c->pos[dim_x] - 1] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y] + 1][c->pos[dim_x] - 1] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] + 1][c->pos[dim_x] - 1].hn);
    }
    if ((p[c->pos[dim_y] + 1][c->pos[dim_x]    ].hn) &&
        (world.hiker_dist[c->pos[dim_y] + 1][c->pos[dim_x]    ] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y] + 1][c->pos[dim_x]    ] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] + 1][c->pos[dim_x]    ].hn);
    }
    if ((p[c->pos[dim_y] + 1][c->pos[dim_x] + 1].hn) &&
        (world.hiker_dist[c->pos[dim_y] + 1][c->pos[dim_x] + 1] >
         world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker))) {
      world.hiker_dist[c->pos[dim_y] + 1][c->pos[dim_x] + 1] =
        world.hiker_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_hiker);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] + 1][c->pos[dim_x] + 1].hn);
    }
  }
  heap_delete(&h);

  heap_init(&h, rival_cmp, NULL);

  for (y = 1; y < MAP_Y - 1; y++) {
    for (x = 1; x < MAP_X - 1; x++) {
      if (ter_cost(x, y, char_rival) != INT_MAX) {
        p[y][x].hn = heap_insert(&h, &p[y][x]);
      } else {
        p[y][x].hn = NULL;
      }
    }
  }

  while ((c = heap_remove_min(&h))) {
    c->hn = NULL;
    if ((p[c->pos[dim_y] - 1][c->pos[dim_x] - 1].hn) &&
        (world.rival_dist[c->pos[dim_y] - 1][c->pos[dim_x] - 1] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y] - 1][c->pos[dim_x] - 1] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] - 1][c->pos[dim_x] - 1].hn);
    }
    if ((p[c->pos[dim_y] - 1][c->pos[dim_x]    ].hn) &&
        (world.rival_dist[c->pos[dim_y] - 1][c->pos[dim_x]    ] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y] - 1][c->pos[dim_x]    ] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] - 1][c->pos[dim_x]    ].hn);
    }
    if ((p[c->pos[dim_y] - 1][c->pos[dim_x] + 1].hn) &&
        (world.rival_dist[c->pos[dim_y] - 1][c->pos[dim_x] + 1] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y] - 1][c->pos[dim_x] + 1] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] - 1][c->pos[dim_x] + 1].hn);
    }
    if ((p[c->pos[dim_y]    ][c->pos[dim_x] - 1].hn) &&
        (world.rival_dist[c->pos[dim_y]    ][c->pos[dim_x] - 1] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y]    ][c->pos[dim_x] - 1] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y]    ][c->pos[dim_x] - 1].hn);
    }
    if ((p[c->pos[dim_y]    ][c->pos[dim_x] + 1].hn) &&
        (world.rival_dist[c->pos[dim_y]    ][c->pos[dim_x] + 1] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y]    ][c->pos[dim_x] + 1] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y]    ][c->pos[dim_x] + 1].hn);
    }
    if ((p[c->pos[dim_y] + 1][c->pos[dim_x] - 1].hn) &&
        (world.rival_dist[c->pos[dim_y] + 1][c->pos[dim_x] - 1] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y] + 1][c->pos[dim_x] - 1] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] + 1][c->pos[dim_x] - 1].hn);
    }
    if ((p[c->pos[dim_y] + 1][c->pos[dim_x]    ].hn) &&
        (world.rival_dist[c->pos[dim_y] + 1][c->pos[dim_x]    ] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y] + 1][c->pos[dim_x]    ] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] + 1][c->pos[dim_x]    ].hn);
    }
    if ((p[c->pos[dim_y] + 1][c->pos[dim_x] + 1].hn) &&
        (world.rival_dist[c->pos[dim_y] + 1][c->pos[dim_x] + 1] >
         world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
         ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival))) {
      world.rival_dist[c->pos[dim_y] + 1][c->pos[dim_x] + 1] =
        world.rival_dist[c->pos[dim_y]][c->pos[dim_x]] +
        ter_cost(c->pos[dim_x], c->pos[dim_y], char_rival);
      heap_decrease_key_no_replace(&h,
                                   p[c->pos[dim_y] + 1][c->pos[dim_x] + 1].hn);
    }
  }
  heap_delete(&h);
}

void init_pc()
{
  int x, y;

  do {
    x = rand() % (MAP_X - 2) + 1;
    y = rand() % (MAP_Y - 2) + 1;
  } while (world.cur_map->map[y][x] != ter_path);

  world.pc.pos[dim_x] = x;
  world.pc.pos[dim_y] = y;
}

void init_trainer(uint16_t num_trainer, map_t *m) { //FIXME: made change
  int x, y;
  for(y = 0; y < MAP_Y; y++) {
    for(x = 0; x < MAP_X; x++) {
      world.trainers[y][x].display = false;
    }
  }

  if(num_trainer == 1){
    do{
      x = rand() % (MAP_X - 2) + 1;
      y = rand() % (MAP_Y - 2) + 1;
    }while (world.hiker_dist[y][x] == INT_MAX);
    world.trainers[y][x].pos[0] = x;
    world.trainers[y][x].pos[1] = y;
    world.trainers[y][x].next_pos[0] = x;
    world.trainers[y][x].next_pos[1] = y;
    world.trainers[y][x].type = char_hiker;
    world.trainers[y][x].dir = 'z';
    world.trainers[y][x].ter = m->map[y][x];
    world.trainers[y][x].cost = 0;
    world.trainers[y][x].display = true;
  }else if(num_trainer == 2){
    do{
      x = rand() % (MAP_X - 2) + 1;
      y = rand() % (MAP_Y - 2) + 1;
    }while (world.hiker_dist[y][x] == INT_MAX);
    world.trainers[y][x].pos[0] = x;
    world.trainers[y][x].pos[1] = y;
    world.trainers[y][x].next_pos[0] = x;
    world.trainers[y][x].next_pos[1] = y;
    world.trainers[y][x].type = char_hiker;
    world.trainers[y][x].dir = 'z';
    world.trainers[y][x].ter = m->map[y][x];
    world.trainers[y][x].cost = 0;
    world.trainers[y][x].display = true;

    do{
      x = rand() % (MAP_X - 2) + 1;
      y = rand() % (MAP_Y - 2) + 1;
    }while ((world.rival_dist[y][x] == INT_MAX) && (!world.trainers[y][x].display));
    world.trainers[y][x].pos[0] = x;
    world.trainers[y][x].pos[1] = y;
    world.trainers[y][x].next_pos[0] = x;
    world.trainers[y][x].next_pos[1] = y;
    world.trainers[y][x].type = char_rival;
    world.trainers[y][x].dir = 'z';
    world.trainers[y][x].ter = m->map[y][x];
    world.trainers[y][x].cost = 0;
    world.trainers[y][x].display = true;
    }else if(num_trainer > 2){
      do{
      x = rand() % (MAP_X - 2) + 1;
      y = rand() % (MAP_Y - 2) + 1;
      }while (world.hiker_dist[y][x] == INT_MAX);
    world.trainers[y][x].pos[0] = x;
    world.trainers[y][x].pos[1] = y;
    world.trainers[y][x].next_pos[0] = x;
    world.trainers[y][x].next_pos[1] = y;
    world.trainers[y][x].type = char_hiker;
    world.trainers[y][x].dir = 'z';
    world.trainers[y][x].ter = m->map[y][x];
    world.trainers[y][x].cost = 0;
    world.trainers[y][x].display = true;

    do{
      x = rand() % (MAP_X - 2) + 1;
      y = rand() % (MAP_Y - 2) + 1;
    }while ((world.rival_dist[y][x] == INT_MAX) && (!world.trainers[y][x].display));
    world.trainers[y][x].pos[0] = x;
    world.trainers[y][x].pos[1] = y;
    world.trainers[y][x].next_pos[0] = x;
    world.trainers[y][x].next_pos[1] = y;
    world.trainers[y][x].type = char_rival;
    world.trainers[y][x].dir = 'z';
    world.trainers[y][x].ter = m->map[y][x];
    world.trainers[y][x].cost = 0;
    world.trainers[y][x].display = true;

    int i;
    for(i = 2; i < num_trainer; i++){
      int r = rand_range(1, num_character_types-1);
      do{
      x = rand() % (MAP_X - 2) + 1;
      y = rand() % (MAP_Y - 2) + 1;
      }while((world.rival_dist[y][x] == INT_MAX) && (!world.trainers[y][x].display));
    world.trainers[y][x].pos[0] = x;
    world.trainers[y][x].pos[1] = y;
    world.trainers[y][x].next_pos[0] = x;
    world.trainers[y][x].next_pos[1] = y;
    if(r == char_other){
      r = char_explorer;
    }
    world.trainers[y][x].type = r;
    world.trainers[y][x].dir = 'z';
    world.trainers[y][x].ter = m->map[y][x];
    world.trainers[y][x].cost = 0;
    world.trainers[y][x].display = true;
    }

    }else{
        //no trainers
    }
  }

/*int findLowestCost(trainer_t *t, map_t *m, int distMap[][MAX_SIZE]){ //FIXME: change made
  int min = INT_MAX;
  int newPosX = t->pos[0];
  int newPosY = t->pos[1];

  for(int dx = -1; dx <= 1; dx++){
    for(int dy = -1; dy <= 1; dy++){
      int posX = t->pos[0] + dx;
      int posY = t->pos[1] + dy;

      if(dx == 0 && dy == 0){
        continue;
      }

      if(posX >= 0 && posX < MAX_SIZE && posY >= 0 && posY < MAX_SIZE){
        int cost = distMap[posY][posX];
        if(cost < min){
          min = cost;
          newPosX = posX;
          newPosY = posY;
        }
      }
    }
  }
  t->next_pos[0] = newPosX;
  t->next_pos[1] = newPosY;

  return min;
}*/

void updateTrainerState(trainer_t* t, map_t* m){ //FIXME: change made
  if(t->type == char_rival){
    int min = INT_MAX;
    if((t->pos[0] == t->next_pos[0]) && (t->pos[1] == t->next_pos[1])){
      if(world.rival_dist[t->pos[1] - 1][t->pos[0] - 1] < min) { //up left

      }
      if(world.rival_dist[t->pos[1] - 1][t->pos[0]] < min) { //up
        
      }
      if(world.rival_dist[t->pos[1] - 1][t->pos[0] + 1] < min) { //up right
        
      }
      if(world.rival_dist[t->pos[1]][t->pos[0] - 1] < min) { //left
        
      }
      if(world.rival_dist[t->pos[1]][t->pos[0] + 1] < min) { //right
        
      }
      if(world.rival_dist[t->pos[1] + 1][t->pos[0] - 1] < min) { //down left
        
      }
      if(world.rival_dist[t->pos[1] + 1][t->pos[0]] < min) { //down
        
      }
      if(world.rival_dist[t->pos[1] + 1][t->pos[0] + 1] < min) { //down right
        
      }
    }
    //min = findLowestCost(t, m, world.rival_dist);
    t->cost = t->cost + ter_cost(t->next_pos[0], t->next_pos[1], char_rival); 
  }
  else if(t->type == char_hiker){
    int min = INT_MAX;
    if((t->pos[0] == t->next_pos[0]) && (t->pos[1] == t->next_pos[1])){
      if(world.hiker_dist[t->pos[1] - 1][t->pos[0] - 1] < min) { //up left

      }
      if(world.hiker_dist[t->pos[1] - 1][t->pos[0]] < min) { //up
        
      }
      if(world.hiker_dist[t->pos[1] - 1][t->pos[0] + 1] < min) { //up right
        
      }
      if(world.hiker_dist[t->pos[1]][t->pos[0] - 1] < min) { //left
        
      }
      if(world.hiker_dist[t->pos[1]][t->pos[0] + 1] < min) { //right
        
      }
      if(world.hiker_dist[t->pos[1] + 1][t->pos[0] - 1] < min) { //down left
        
      }
      if(world.hiker_dist[t->pos[1] + 1][t->pos[0]] < min) { //down
        
      }
      if(world.hiker_dist[t->pos[1] + 1][t->pos[0] + 1] < min) { //down right
        
      }      
    }
    //min = findLowestCost(t, m, world.hiker_dist);
    t->cost = t->cost + ter_cost(t->next_pos[0], t->next_pos[1], char_hiker); 
  }
  else if(t->type == char_pacer){
    if(t->dir == 'z'){
      int d = rand_range(0, 3);
      switch (d) { //FIXME: whatch carefully and if some error happen change to if statement
        case 0:
          t->dir = 'n';
          break;
        case 1:
          t->dir = 's';
          break;
        case 2:
          t->dir = 'e';
          break;
        case 3:
          t->dir = 'w';
          break;
        default:
          // Handle the default case if needed
          break;
      }
    }

    if(t->dir == 'n'){
      if(((ter_cost(t->pos[0], t->pos[1] - 1, char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1] - 1][t->pos[0]].display){

        }
      }
      else if(((ter_cost(t->pos[0], t->pos[1] + 1, char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1] + 1][t->pos[0]].display){

        }
      }
      else{

      }
    }
    else if(t->dir == 's'){
      if(((ter_cost(t->pos[0], t->pos[1] + 1, char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1] + 1][t->pos[0]].display){

        }
      }
      else if(((ter_cost(t->pos[0], t->pos[1] - 1, char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1] - 1][t->pos[0]].display){

        }
      }
      else{

      }      
    }
    else if(t->dir == 'e'){
      if(((ter_cost(t->pos[0] + 1, t->pos[1], char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1]][t->pos[0] + 1].display){

        }
      }
      else if(((ter_cost(t->pos[0] - 1, t->pos[1], char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1]][t->pos[0] - 1].display){

        }
      }
      else{

      }      
    }
    else if(t->dir == 'w'){
      if(((ter_cost(t->pos[0] - 1, t->pos[1], char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1]][t->pos[0] - 1].display){

        }
      }
      else if(((ter_cost(t->pos[0] + 1, t->pos[1], char_other)) != INT_MAX)){
        if(!world.trainers[t->pos[1]][t->pos[0] + 1].display){

        }
      }
      else{

      }       
    }
    if(t->cost != INT_MAX){
    t->cost = t->cost + ter_cost(t->next_pos[0], t->next_pos[1], char_other);
    }  
  }
  else if(t->type == char_wanderer){
    if(t->dir == 'z'){
       int d = rand_range(0, 3);
      switch (d) { //FIXME: whatch carefully and if some error happen change to if statement
        case 0:
          t->dir = 'n';
          break;
        case 1:
          t->dir = 's';
          break;
        case 2:
          t->dir = 'e';
          break;
        case 3:
          t->dir = 'w';
          break;
        default:
          // Handle the default case if needed
          break;
      }
    }
    else{
      if(t->dir == 'n'){
        if(((ter_cost(t->pos[0], t->pos[1] - 1, char_other)) != INT_MAX) && m->map[t->pos[1] - 1][t->pos[0]] == t->ter){
          if(!world.trainers[t->pos[1] - 1][t->pos[0]].display){

          }
        }
        else if(((ter_cost(t->pos[0], t->pos[1] + 1, char_other)) != INT_MAX) && m->map[t->pos[1] + 1][t->pos[0]] == t->ter){
          if(!world.trainers[t->pos[1] + 1][t->pos[0]].display){

          }
        }
        else{

        }
      }
      else if(t->dir == 's'){
        if((t->pos[0] == t->next_pos[0]) && (t->pos[1] == t->next_pos[1])){
          if(((ter_cost(t->pos[0], t->pos[1] + 1, char_other)) != INT_MAX) && m->map[t->pos[1] + 1][t->pos[0]] == t->ter){
            if(!world.trainers[t->pos[1] + 1][t->pos[0]].display){

            }
          }
          else if(((ter_cost(t->pos[0], t->pos[1] - 1, char_other)) != INT_MAX) && m->map[t->pos[1] - 1][t->pos[0]] == t->ter){
            if(!world.trainers[t->pos[1] - 1][t->pos[0]].display){

            }
          }
          else{

          }
        }
      }
      else if(t->dir == 'e'){
        if(((ter_cost(t->pos[0] + 1, t->pos[1], char_other)) != INT_MAX) && m->map[t->pos[1]][t->pos[0] + 1] == t->ter){
          if(!world.trainers[t->pos[1]][t->pos[0] + 1].display){

          }
        }
        else if(((ter_cost(t->pos[0] - 1, t->pos[1], char_other)) != INT_MAX) && m->map[t->pos[1]][t->pos[0] - 1] == t->ter){
          if(!world.trainers[t->pos[1]][t->pos[0] - 1].display){

          }
        }
        else{

        }        
      }
      else if(t->dir == 'w'){
        if(((ter_cost(t->pos[0] - 1, t->pos[1], char_other)) != INT_MAX) && m->map[t->pos[1]][t->pos[0] - 1] == t->ter){
          if(!world.trainers[t->pos[1]][t->pos[0] - 1].display){

          }
        }
        else if(((ter_cost(t->pos[0] - 1, t->pos[1], char_other)) != INT_MAX) && m->map[t->pos[1]][t->pos[0] - 1] == t->ter){
          if(!world.trainers[t->pos[1]][t->pos[0] + 1].display){ //FIXME: whatch out for [t->pos[1]][t->pos[0] + 1]

          }
        }
        else{

        }        
      }  
    }
    if(t->cost != INT_MAX){
      t->cost = t->cost + ter_cost(t->pos[0], t->pos[1], char_other);
    }
  }
  else if(t->type == char_sentry){
    //they don't move
  }
  else if(t->type == char_explorer){
    int new_dir = rand_range(0, 3);
    if(t->dir == 'z'){
      int d = rand_range(0, 3);
      switch (d) {
        case 0:
          t->dir = 'n';
          break;
        case 1:
          t->dir = 's';
          break;
        case 2:
          t->dir = 'e';
          break;
        case 3:
          t->dir = 'w';
          break;
        default:
          // Handle the default case if needed
          break;
      }
    }
    else{
      if(t->dir == 'n'){
        if((ter_cost(t->pos[0], t->pos[1] - 1, char_other)) != INT_MAX){
          if(!world.trainers[t->pos[1] - 1][t->pos[0]].display){
            t->next_pos[0] = t->pos[0];
            t->next_pos[1] = t->pos[1] - 1;
          }
          else if(new_dir == 0 || new_dir == 1){
            t->dir = 's';
          }
          else if(new_dir == 2){
            t->dir = 'e';
          }
          else{
            t->dir = 'w';
          }
        }
      }
      else if(t->dir == 's'){
        if((ter_cost(t->pos[0], t->pos[1] + 1, char_other)) != INT_MAX){
          if(!world.trainers[t->pos[1] + 1][t->pos[0]].display){
            t->next_pos[0] = t->pos[0];
            t->next_pos[1] = t->pos[1] + 1;
          }
          else if(new_dir == 0 || new_dir == 1){
            t->dir = 'n';
          }
          else if(new_dir == 2){
            t->dir = 'e';
          }
          else{
            t->dir = 'w';
          }
        }        
      }
      else if(t->dir == 'e'){
        if((ter_cost(t->pos[0] + 1, t->pos[1], char_other)) != INT_MAX){
          if(!world.trainers[t->pos[1]][t->pos[0] + 1].display){
            t->next_pos[0] = t->pos[0] + 1;
            t->next_pos[1] = t->pos[1];
          }
          else if(new_dir == 0 || new_dir == 1){
            t->dir = 'n';
          }
          else if(new_dir == 2){
            t->dir = 's';
          }
          else{
            t->dir = 'w';
          }
        }        
      }
      else if(t->dir == 'w'){
        if((ter_cost(t->pos[0] - 1, t->pos[1], char_other)) != INT_MAX){
          if(!world.trainers[t->pos[1]][t->pos[0] - 1].display){
            t->next_pos[0] = t->pos[0] - 1;
            t->next_pos[1] = t->pos[1];
          }
          else if(new_dir == 0 || new_dir == 1){
            t->dir = 'n';
          }
          else if(new_dir == 2){
            t->dir = 's';
          }
          else{
            t->dir = 'e';
          }
        }        
      }
      if(t->cost != INT_MAX){
        t->cost = t->cost + ter_cost(t->next_pos[0], t->next_pos[1], char_other);
      }      
    }
  }
}

void print_hiker_dist()
{
  int x, y;

  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      if (world.hiker_dist[y][x] == INT_MAX) {
        printf("   ");
      } else {
        printf(" %02d", world.hiker_dist[y][x] % 100);
      }
    }
    printf("\n");
  }
}

void print_rival_dist()
{
  int x, y;

  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      if (world.rival_dist[y][x] == INT_MAX || world.rival_dist[y][x] < 0) {
        printf("   ");
      } else {
        printf(" %02d", world.rival_dist[y][x] % 100);
      }
    }
    printf("\n");
  }
}

void print_trainers() { //FIXME: change made
  int x, y;

  for (y = 0; y < MAP_Y; y++) {
    for (x = 0; x < MAP_X; x++) {
      if (world.trainers[y][x].display == false) {
        printf(".");
      } else {
        switch (world.trainers[y][x].type) {
          case char_rival:
            printf("r");
            break;
          case char_hiker:
            printf("h");
            break;
          case char_other:
            printf("o");
            break;
          case char_pacer:
            printf("p");
            break;
          case char_wanderer:
            printf("w");
            break;
          case char_sentry:
            printf("s");
            break;
          case char_explorer:
            printf("n");
            break;
          default:
            // Handle any other cases if needed
            break;
        }
      }
    }
    printf("\n");
  }
}

int main(int argc, char *argv[])
{
  struct timeval tv;
  uint32_t seed;
  uint32_t num_trainers = 10; //FIXME:
  char c;
  int x, y;

  if (argc == 2) {
    seed = atoi(argv[1]);
  } else {
    gettimeofday(&tv, NULL);
    seed = (tv.tv_usec ^ (tv.tv_sec << 20)) & 0xffffffff;
  }

  if(argc > 2){ //FIXME:
    if(! strcmp(argv[1], "--numtrainers")){
      if(argc == 3){
        num_trainers = atoi(argv[2]);
      }
    }
    else if(! strcmp(argv[2], "--numtrainers")){
      if(argc == 4){
        num_trainers = atoi(argv[3]);
      }
    }
    else{
      printf("Invalid arg switch... using Defaults\n");
    }
  }

  printf("Using %d Trainers\n", num_trainers); //FIXME:

  printf("Using seed: %u\n", seed);
  srand(seed);

  init_world();
  init_pc();
  pathfind(world.cur_map);
  init_trainer(num_trainers, world.cur_map); //FIXME:

  heap_t h; //FIXME:
  heap_init(&h, trainer_cmp, NULL); //FIXME:

  for(y = 1; y > MAP_Y - 1; y++){ //FIXME:
    for(x = 1; x > MAP_X - 1; x++){
      if(world.trainers[y][x].display && world.trainers[y][x].type != char_sentry){
        heap_insert(&h, &world.trainers[y][x]);
      }
    }
  }



  while(1){ //FIXME:
    trainer_t* t;
    sleep(1);
    if(h.size > 0){
      t = heap_remove_min(&h);
    }
    else{
      break;
    }
  print_map();
  int temp_x = t->pos[0];
  int temp_y = t->pos[1];
  updateTrainerState(t, world.cur_map);
  
  world.trainers[t->next_pos[1]][t->next_pos[0]].cost = t->cost;
  world.trainers[t->next_pos[1]][t->next_pos[0]].ter =  t->ter;
  world.trainers[t->next_pos[1]][t->next_pos[0]].dir = t->dir;
  world.trainers[t->next_pos[1]][t->next_pos[0]].type = t->type;
  world.trainers[t->next_pos[1]][t->next_pos[0]].pos[0] = t->pos[0];
  world.trainers[t->next_pos[1]][t->next_pos[0]].pos[1] = t->pos[1];
  world.trainers[t->next_pos[1]][t->next_pos[0]].next_pos[0] = t->pos[0];
  world.trainers[t->next_pos[1]][t->next_pos[0]].next_pos[1] = t->pos[1];
  world.trainers[t->next_pos[1]][t->next_pos[0]].display = true;

  t->pos[0] = t->next_pos[0];
  t->pos[1] = t->next_pos[1];
  if(t->cost != 0 && !(temp_x == t->pos[0] && temp_y == t->pos[1])){
    world.trainers[temp_y][temp_x].display = false;
  }
  heap_insert(&h, t);
}


  return 0;
  
  do {
    print_map();  
    printf("Current position is %d%cx%d%c (%d,%d).  "
           "Enter command: ",
           abs(world.cur_idx[dim_x] - (WORLD_SIZE / 2)),
           world.cur_idx[dim_x] - (WORLD_SIZE / 2) >= 0 ? 'E' : 'W',
           abs(world.cur_idx[dim_y] - (WORLD_SIZE / 2)),
           world.cur_idx[dim_y] - (WORLD_SIZE / 2) <= 0 ? 'N' : 'S',
           world.cur_idx[dim_x] - (WORLD_SIZE / 2),
           world.cur_idx[dim_y] - (WORLD_SIZE / 2));
    if (scanf(" %c", &c) != 1) {
      /* To handle EOF */
      putchar('\n');
      break;
    }
    switch (c) {
    case 'n':
      if (world.cur_idx[dim_y]) {
        world.cur_idx[dim_y]--;
        new_map();
      }
      break;
    case 's':
      if (world.cur_idx[dim_y] < WORLD_SIZE - 1) {
        world.cur_idx[dim_y]++;
        new_map();
      }
      break;
    case 'e':
      if (world.cur_idx[dim_x] < WORLD_SIZE - 1) {
        world.cur_idx[dim_x]++;
        new_map();
      }
      break;
    case 'w':
      if (world.cur_idx[dim_x]) {
        world.cur_idx[dim_x]--;
        new_map();
      }
      break;
    case 'q':
      break;
    case 'f':
      scanf(" %d %d", &x, &y);
      if (x >= -(WORLD_SIZE / 2) && x <= WORLD_SIZE / 2 &&
          y >= -(WORLD_SIZE / 2) && y <= WORLD_SIZE / 2) {
        world.cur_idx[dim_x] = x + (WORLD_SIZE / 2);
        world.cur_idx[dim_y] = y + (WORLD_SIZE / 2);
        new_map();
      }
      break;
    case '?':
    case 'h':
      printf("Move with 'e'ast, 'w'est, 'n'orth, 's'outh or 'f'ly x y.\n"
             "Quit with 'q'.  '?' and 'h' print this help message.\n");
      break;
    default:
      fprintf(stderr, "%c: Invalid input.  Enter '?' for help.\n", c);
      break;
    }
  } while (c != 'q');

  delete_world();

  printf("But how are you going to be the very best if you quit?\n");
  
  return 0;
}
