#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "config.h"
#include "mtwist.h"
#include "cube.h"
#include "item.h"
#include "map.h"
#include "matrix.h"
#include "noise.h"
#include "lodepng.h"

typedef struct
{

    float x, y, z;
    float lx, ly, lz;
    float vx, vy, vz;

} Box;

typedef struct
{

    unsigned int fps;
    unsigned int frames;
    double since;

} FPS;

typedef struct
{

    Map map;
    Map lights;
    int p;
    int q;
    int faces;
    int dirty;
    int miny;
    int maxy;
    GLuint buffer;

} Chunk;

typedef struct
{

    Map *block_maps[3][3];
    Map *light_maps[3][3];
    GLfloat *data;

} WorkerItem;

typedef struct
{

    int x;
    int y;
    int z;
    int w;

} Block;

typedef struct
{

    float x;
    float y;
    float z;
    float vx;
    float vy;
    float vz;
    float rx;
    float ry;
    float dy;

} Player;

typedef struct
{

    GLuint program;
    GLuint position;
    GLuint normal;
    GLuint uv;
    GLuint matrix;
    GLuint sampler;
    GLuint camera;
    GLuint timer;
    GLuint extra1;
    GLuint extra2;
    GLuint extra3;
    GLuint extra4;

} Attrib;

typedef struct
{

    GLFWwindow *window;
    Chunk chunks[MAX_CHUNKS];
    int chunk_count;
    int render_radius;
    int delete_radius;
    Player player;
    int typing;
    char typing_buffer[MAX_TEXT_LENGTH];
    int message_index;
    char messages[MAX_MESSAGES][MAX_TEXT_LENGTH];
    int width;
    int height;
    int flying;
    int item_index;
    int scale;
    int ortho;
    float fov;
    int suppress_char;
    int mode_changed;
    int day_length;
    int time_changed;
    Block block0;
    Block block1;
    Block copy0;
    Block copy1;
    FPS fps;

} Model;

static Model model;
static Model *g = &model;

static int chunked(float x)
{

    return floorf(roundf(x) / CHUNK_SIZE);

}

static float time_of_day()
{

    float t;

    if (g->day_length <= 0)
        return 0.5;

    t = glfwGetTime();
    t = t / g->day_length;
    t = t - (int)t;

    return t;

}

static float get_daylight()
{

    float timer = time_of_day();

    if (timer < 0.5)
    {

        float t = (timer - 0.25) * 100;

        return 1 / (1 + powf(2, -t));

    }

    else
    {

        float t = (timer - 0.85) * 100;

        return 1 - 1 / (1 + powf(2, -t));

    }

}

static int get_scale_factor()
{

    int winw;
    int winh;
    int bufw;
    int bufh;
    int factor;

    glfwGetWindowSize(g->window, &winw, &winh);
    glfwGetFramebufferSize(g->window, &bufw, &bufh);

    factor = bufw / winw;
    factor = MAX(1, factor);
    factor = MIN(2, factor);

    return factor;

}

static void get_sight_vector(float rx, float ry, float *vx, float *vy, float *vz)
{

    float m = cosf(ry);

    *vx = cosf(rx - RADIANS(90)) * m;
    *vy = sinf(ry);
    *vz = sinf(rx - RADIANS(90)) * m;

}

static void get_motion_vector_flying(int sz, int sx, Player *player)
{

    player->vx = 0;
    player->vy = 0;
    player->vz = 0;

    if (!sz && !sx)
        return;

    float strafe = atan2f(sz, sx);
    float m = cosf(player->ry);
    float y = sinf(player->ry);

    if (sx)
    {

        if (!sz)
            y = 0;

        m = 1;

    }

    if (sz > 0)
        y = -y;

    player->vx = cosf(player->rx + strafe) * m;
    player->vy = y;
    player->vz = sinf(player->rx + strafe) * m;

}

static void get_motion_vector_normal(int sz, int sx, Player *player)
{

    player->vx = 0;
    player->vy = 0;
    player->vz = 0;

    if (!sz && !sx)
        return;

    float strafe = atan2f(sz, sx);

    player->vx = cosf(player->rx + strafe);
    player->vy = 0;
    player->vz = sinf(player->rx + strafe);

}

static GLuint gen_buffer(GLsizei size, GLfloat *data)
{

    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return buffer;

}

static void del_buffer(GLuint buffer)
{

    glDeleteBuffers(1, &buffer);

}

static GLfloat *malloc_faces(int components, int faces)
{

    return malloc(sizeof(GLfloat) * 6 * components * faces);

}

static GLuint gen_faces(int components, int faces, GLfloat *data)
{

    GLuint buffer = gen_buffer(sizeof(GLfloat) * 6 * components * faces, data);

    free(data);

    return buffer;

}

static GLuint gen_crosshair_buffer()
{

    int x = g->width / 2;
    int y = g->height / 2;
    int p = 10 * g->scale;

    float data[] = {
        x, y - p, x, y + p,
        x - p, y, x + p, y
    };

    return gen_buffer(sizeof(data), data);

}

static GLuint gen_sky_buffer()
{

    float data[12288];

    make_sphere(data, 1, 3);

    return gen_buffer(sizeof(data), data);

}

static GLuint gen_cube_buffer(float x, float y, float z, float n, int w)
{

    GLfloat *data = malloc_faces(10, 6);

    float ao[6][4] = {0};
    float light[6][4] = {
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5}
    };

    make_cube(data, ao, light, 1, 1, 1, 1, 1, 1, x, y, z, n, w);

    return gen_faces(10, 6, data);

}

static GLuint gen_plant_buffer(float x, float y, float z, float n, int w)
{

    GLfloat *data = malloc_faces(10, 4);
    float ao = 0;
    float light = 1;

    make_plant(data, ao, light, x, y, z, n, w, 45);

    return gen_faces(10, 4, data);

}

static GLuint gen_text_buffer(float x, float y, float n, char *text)
{

    int length = strlen(text);

    GLfloat *data = malloc_faces(4, length);

    for (int i = 0; i < length; i++)
    {

        make_character(data + i * 24, x, y, n / 2, n, text[i]);

        x += n;

    }

    return gen_faces(4, length, data);

}

static void draw_triangles_3d_ao(Attrib *attrib, GLuint buffer, int count)
{

    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(attrib->position);
    glEnableVertexAttribArray(attrib->normal);
    glEnableVertexAttribArray(attrib->uv);
    glVertexAttribPointer(attrib->position, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 10, 0);
    glVertexAttribPointer(attrib->normal, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 10, (GLvoid *)(sizeof(GLfloat) * 3));
    glVertexAttribPointer(attrib->uv, 4, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 10, (GLvoid *)(sizeof(GLfloat) * 6));
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisableVertexAttribArray(attrib->position);
    glDisableVertexAttribArray(attrib->normal);
    glDisableVertexAttribArray(attrib->uv);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

}

static void draw_triangles_3d(Attrib *attrib, GLuint buffer, int count)
{

    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(attrib->position);
    glEnableVertexAttribArray(attrib->normal);
    glEnableVertexAttribArray(attrib->uv);
    glVertexAttribPointer(attrib->position, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 8, 0);
    glVertexAttribPointer(attrib->normal, 3, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 8, (GLvoid *)(sizeof(GLfloat) * 3));
    glVertexAttribPointer(attrib->uv, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 8, (GLvoid *)(sizeof(GLfloat) * 6));
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisableVertexAttribArray(attrib->position);
    glDisableVertexAttribArray(attrib->normal);
    glDisableVertexAttribArray(attrib->uv);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

}

static void draw_triangles_2d(Attrib *attrib, GLuint buffer, int count)
{

    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(attrib->position);
    glEnableVertexAttribArray(attrib->uv);
    glVertexAttribPointer(attrib->position, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 4, 0);
    glVertexAttribPointer(attrib->uv, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 4, (GLvoid *)(sizeof(GLfloat) * 2));
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisableVertexAttribArray(attrib->position);
    glDisableVertexAttribArray(attrib->uv);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

}

static void draw_lines(Attrib *attrib, GLuint buffer, int components, int count)
{

    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glEnableVertexAttribArray(attrib->position);
    glVertexAttribPointer(attrib->position, components, GL_FLOAT, GL_FALSE, 0, 0);
    glDrawArrays(GL_LINES, 0, count);
    glDisableVertexAttribArray(attrib->position);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

}

static void draw_text(Attrib *attrib, GLuint buffer, int length)
{

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    draw_triangles_2d(attrib, buffer, length * 6);
    glDisable(GL_BLEND);

}

static Chunk *find_chunk(int p, int q)
{

    for (int i = 0; i < g->chunk_count; i++)
    {

        Chunk *chunk = g->chunks + i;

        if (chunk->p == p && chunk->q == q)
            return chunk;

    }

    return 0;

}

static int chunk_distance(Chunk *chunk, int p, int q)
{

    int dp = ABS(chunk->p - p);
    int dq = ABS(chunk->q - q);

    return MAX(dp, dq);

}

static int chunk_visible(float planes[6][4], int p, int q, int miny, int maxy)
{

    int x = p * CHUNK_SIZE - 1;
    int z = q * CHUNK_SIZE - 1;
    int d = CHUNK_SIZE + 1;
    int n = g->ortho ? 4 : 6;
    float points[8][3] = {
        {x + 0, miny, z + 0},
        {x + d, miny, z + 0},
        {x + 0, miny, z + d},
        {x + d, miny, z + d},
        {x + 0, maxy, z + 0},
        {x + d, maxy, z + 0},
        {x + 0, maxy, z + d},
        {x + d, maxy, z + d}
    };

    for (int i = 0; i < n; i++)
    {

        int in = 0;
        int out = 0;

        for (int j = 0; j < 8; j++)
        {

            float d = planes[i][0] * points[j][0] + planes[i][1] * points[j][1] + planes[i][2] * points[j][2] + planes[i][3];

            if (d < 0)
                out++;
            else
                in++;

            if (in && out)
                break;

        }

        if (in == 0)
            return 0;

    }

    return 1;

}

static int highest_block(float x, float z)
{

    int result = -1;
    int nx = roundf(x);
    int nz = roundf(z);
    int p = chunked(x);
    int q = chunked(z);
    Chunk *chunk = find_chunk(p, q);

    if (chunk)
    {

        Map *map = &chunk->map;

        MAP_FOR_EACH(map, ex, ey, ez, ew) {

            if (is_obstacle(ew) && ex == nx && ez == nz)
                result = MAX(result, ey);

        } END_MAP_FOR_EACH;

    }

    return result;

}

static int _hit_test(Map *map, float max_distance, int previous, float x, float y, float z, float vx, float vy, float vz, int *hx, int *hy, int *hz)
{

    int m = 32;
    int px = 0;
    int py = 0;
    int pz = 0;

    for (int i = 0; i < max_distance * m; i++)
    {

        int nx = roundf(x);
        int ny = roundf(y);
        int nz = roundf(z);

        if (nx != px || ny != py || nz != pz)
        {

            int hw = map_get(map, nx, ny, nz);

            if (hw > 0)
            {

                if (previous)
                {

                    *hx = px;
                    *hy = py;
                    *hz = pz;

                }

                else
                {

                    *hx = nx;
                    *hy = ny;
                    *hz = nz;

                }

                return hw;

            }

            px = nx;
            py = ny;
            pz = nz;

        }

        x += vx / m;
        y += vy / m;
        z += vz / m;

    }

    return 0;

}

static int hit_test(int previous, float x, float y, float z, float rx, float ry, int *bx, int *by, int *bz)
{

    int p = chunked(x);
    int q = chunked(z);
    int result = 0;
    float best = 0;
    float vx, vy, vz;

    get_sight_vector(rx, ry, &vx, &vy, &vz);

    for (int i = 0; i < g->chunk_count; i++)
    {

        int hx, hy, hz, hw;

        Chunk *chunk = g->chunks + i;

        if (chunk_distance(chunk, p, q) > 1)
            continue;

        hw = _hit_test(&chunk->map, 8, previous, x, y, z, vx, vy, vz, &hx, &hy, &hz);

        if (hw > 0)
        {

            float d = sqrtf(powf(hx - x, 2) + powf(hy - y, 2) + powf(hz - z, 2));

            if (best == 0 || d < best)
            {

                best = d;
                *bx = hx;
                *by = hy;
                *bz = hz;
                result = hw;

            }

        }

    }

    return result;

}

unsigned int aabbcheck(Box *b1, Box *b2)
{

    float aminx = b1->x;
    float amaxx = b1->x + b1->lx;
    float aminy = b1->y;
    float amaxy = b1->y + b1->ly;
    float aminz = b1->z;
    float amaxz = b1->z + b1->lz;
    float bminx = b2->x;
    float bmaxx = b2->x + b2->lx;
    float bminy = b2->y;
    float bmaxy = b2->y + b2->ly;
    float bminz = b2->z;
    float bmaxz = b2->z + b2->lz;

    if (aminx > bmaxx || amaxx < bminx)
        return 0;

    if (aminy > bmaxy || amaxy < bminy)
        return 0;

    if (aminz > bmaxz || amaxz < bminz)
        return 0;

    return 1;

}

static float aabbsweep(Box b1, Box b2, float *normalx, float *normaly, float *normalz)
{

    float xInvEntry, yInvEntry, zInvEntry;
    float xInvExit, yInvExit, zInvExit;

    if (b1.vx > 0.0f)
    {

        xInvEntry = b2.x - (b1.x + b1.lx);
        xInvExit = (b2.x + b2.lx) - b1.x;

    }

    else
    {

        xInvEntry = (b2.x + b2.lx) - b1.x;
        xInvExit = b2.x - (b1.x + b1.lx);

    }

    if (b1.vy > 0.0f)
    {

        yInvEntry = b2.y - (b1.y + b1.ly);
        yInvExit = (b2.y + b2.ly) - b1.y;

    }

    else
    {

        yInvEntry = (b2.y + b2.ly) - b1.y;
        yInvExit = b2.y - (b1.y + b1.ly);

    }

    if (b1.vz > 0.0f)
    {

        zInvEntry = b2.z - (b1.z + b1.lz);
        zInvExit = (b2.z + b2.lz) - b1.z;

    }

    else
    {

        zInvEntry = (b2.z + b2.lz) - b1.z;
        zInvExit = b2.z - (b1.z + b1.lz);

    }

    float xEntry, yEntry, zEntry;
    float xExit, yExit, zExit;

    if (b1.vx == 0.0f)
    {

        xEntry = -50000;
        xExit = 50000;

    }

    else
    {

        xEntry = xInvEntry / b1.vx;
        xExit = xInvExit / b1.vx;

    }

    if (b1.vy == 0.0f)
    {

        yEntry = -50000;
        yExit = 50000;

    }

    else
    {

        yEntry = yInvEntry / b1.vy;
        yExit = yInvExit / b1.vy;

    }

    if (b1.vz == 0.0f)
    {

        zEntry = -50000;
        zExit = 50000;

    }

    else
    {

        zEntry = zInvEntry / b1.vz;
        zExit = zInvExit / b1.vz;

    }

    float entryTime = MAX(xEntry, zEntry);
    float exitTime = MIN(xExit, zExit);

    if (xEntry > zEntry)
    {

        if (xInvEntry < 0.0f)
        {

            *normalx = 1.0f;
            *normalz = 0.0f;

        }

        else
        {

            *normalx = -1.0f;
            *normalz = 0.0f;

        }

    }

    else
    {

        if (zInvEntry < 0.0f)
        {

            *normalx = 0.0f;
            *normalz = 1.0f;

        }

        else
        {

            *normalx = 0.0f;
            *normalz = -1.0f;

        }

    }

    return entryTime;

}

static int collide(int height, Player *player)
{

    int bx = (int)(player->x);
    int by = (int)(player->y);
    int bz = (int)(player->z);

    Box box;
    box.x = player->x + 0.25;
    box.y = player->y;
    box.z = player->z + 0.25;
    box.lx = 0.5;
    box.ly = 1.0;
    box.lz = 0.5;
    box.vx = player->vx;
    box.vy = player->vy;
    box.vz = player->vz;

    for (int kx = -1; kx <= 1; kx++)
    {

        for (int ky = -1; ky <= 1; ky++)
        {

            for (int kz = -1; kz <= 1; kz++)
            {

                int cx = bx + kx;
                int cy = by + ky;
                int cz = bz + kz;

                Chunk *chunk = find_chunk(chunked(cx), chunked(cz));
                Map *map = &chunk->map;

                if (!is_obstacle(map_get(map, cx, cy, cz)))
                    continue;

                Box block;
                block.x = cx;
                block.y = cy;
                block.z = cz;
                block.lx = 1.0;
                block.ly = 1.0;
                block.lz = 1.0;
                block.vx = 0.0;
                block.vy = 0.0;
                block.vz = 0.0;

                if (!aabbcheck(&box, &block))
                    continue;

                float normalx;
                float normaly;
                float normalz;

                aabbsweep(box, block, &normalx, &normaly, &normalz);

                player->x -= (player->vx) * abs(normalx);
                player->z -= (player->vz) * abs(normalz);

            }

        }

    }



    /* HANDLE Y */

    int p = chunked(player->x);
    int q = chunked(player->z);
    Chunk *chunk = find_chunk(p, q);
    int result = 0;

    if (!chunk)
        return result;

    Map *map = &chunk->map;

    int nx = (int)(player->x);
    int ny = roundf(player->y);
    int nz = (int)(player->z);
    float py = player->y - ny;
    float pad = 0.25;



    if (py < -pad && is_obstacle(map_get(map, nx, ny - 2, nz)))
    {

        player->y = ny - pad;
        result = 1;

    }

    if (py > pad && is_obstacle(map_get(map, nx, ny, nz)))
    {

        player->y = ny + pad;
        result = 1;

    }

    return result;

}

static int player_intersects_block(int height, float x, float y, float z, int hx, int hy, int hz)
{

    int nx = roundf(x);
    int ny = roundf(y);
    int nz = roundf(z);

    for (int i = 0; i < height; i++)
    {

        if (nx == hx && ny - i == hy && nz == hz)
            return 1;

    }

    return 0;

}

static int has_lights(Chunk *chunk)
{

    for (int dp = -1; dp <= 1; dp++)
    {

        for (int dq = -1; dq <= 1; dq++)
        {

            Chunk *other = chunk;

            if (dp || dq)
                other = find_chunk(chunk->p + dp, chunk->q + dq);

            if (!other)
                continue;

            Map *map = &other->lights;

            if (map->size)
                return 1;

        }

    }

    return 0;

}

static void dirty_chunk(Chunk *chunk)
{

    chunk->dirty = 1;

    if (has_lights(chunk))
    {

        for (int dp = -1; dp <= 1; dp++)
        {

            for (int dq = -1; dq <= 1; dq++)
            {

                Chunk *other = find_chunk(chunk->p + dp, chunk->q + dq);

                if (other)
                    other->dirty = 1;

            }

        }

    }

}

static void occlusion(char neighbors[27], char lights[27], float shades[27], float ao[6][4], float light[6][4])
{

    static const int lookup3[6][4][3] = {
        {{0, 1, 3}, {2, 1, 5}, {6, 3, 7}, {8, 5, 7}},
        {{18, 19, 21}, {20, 19, 23}, {24, 21, 25}, {26, 23, 25}},
        {{6, 7, 15}, {8, 7, 17}, {24, 15, 25}, {26, 17, 25}},
        {{0, 1, 9}, {2, 1, 11}, {18, 9, 19}, {20, 11, 19}},
        {{0, 3, 9}, {6, 3, 15}, {18, 9, 21}, {24, 15, 21}},
        {{2, 5, 11}, {8, 5, 17}, {20, 11, 23}, {26, 17, 23}}
    };

    static const int lookup4[6][4][4] = {
        {{0, 1, 3, 4}, {1, 2, 4, 5}, {3, 4, 6, 7}, {4, 5, 7, 8}},
        {{18, 19, 21, 22}, {19, 20, 22, 23}, {21, 22, 24, 25}, {22, 23, 25, 26}},
        {{6, 7, 15, 16}, {7, 8, 16, 17}, {15, 16, 24, 25}, {16, 17, 25, 26}},
        {{0, 1, 9, 10}, {1, 2, 10, 11}, {9, 10, 18, 19}, {10, 11, 19, 20}},
        {{0, 3, 9, 12}, {3, 6, 12, 15}, {9, 12, 18, 21}, {12, 15, 21, 24}},
        {{2, 5, 11, 14}, {5, 8, 14, 17}, {11, 14, 20, 23}, {14, 17, 23, 26}}
    };

    static const float curve[4] = {0.0, 0.25, 0.5, 0.75};

    for (int i = 0; i < 6; i++)
    {

        for (int j = 0; j < 4; j++)
        {

            int corner = neighbors[lookup3[i][j][0]];
            int side1 = neighbors[lookup3[i][j][1]];
            int side2 = neighbors[lookup3[i][j][2]];
            int value = side1 && side2 ? 3 : corner + side1 + side2;
            float shade_sum = 0;
            float light_sum = 0;
            int is_light = lights[13] == 15;

            for (int k = 0; k < 4; k++)
            {

                shade_sum += shades[lookup4[i][j][k]];
                light_sum += lights[lookup4[i][j][k]];

            }

            if (is_light)
                light_sum = 15 * 4 * 10;

            float total = curve[value] + shade_sum / 4.0;

            ao[i][j] = MIN(total, 1.0);
            light[i][j] = light_sum / 15.0 / 4.0;

        }

    }

}

static void light_fill(char *opaque, char *light, int x, int y, int z, int w, int force)
{

    if (x + w < XZ_LO || z + w < XZ_LO)
        return;

    if (x - w > XZ_HI || z - w > XZ_HI)
        return;

    if (y < 0 || y >= Y_SIZE)
        return;

    if (light[XYZ(x, y, z)] >= w)
        return;

    if (!force && opaque[XYZ(x, y, z)])
        return;

    light[XYZ(x, y, z)] = w--;
    light_fill(opaque, light, x - 1, y, z, w, 0);
    light_fill(opaque, light, x + 1, y, z, w, 0);
    light_fill(opaque, light, x, y - 1, z, w, 0);
    light_fill(opaque, light, x, y + 1, z, w, 0);
    light_fill(opaque, light, x, y, z - 1, w, 0);
    light_fill(opaque, light, x, y, z + 1, w, 0);

}

static void compute_chunk(Chunk *chunk, WorkerItem *item)
{

    char *opaque = (char *)calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char));
    char *light = (char *)calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char));
    char *highest = (char *)calloc(XZ_SIZE * XZ_SIZE, sizeof(char));
    int ox = chunk->p * CHUNK_SIZE - CHUNK_SIZE - 1;
    int oy = -1;
    int oz = chunk->q * CHUNK_SIZE - CHUNK_SIZE - 1;
    int has_light = 0;

    for (int a = 0; a < 3; a++)
    {

        for (int b = 0; b < 3; b++)
        {

            Map *map = item->light_maps[a][b];

            if (map && map->size)
                has_light = 1;

        }

    }

    for (int a = 0; a < 3; a++)
    {

        for (int b = 0; b < 3; b++)
        {

            Map *map = item->block_maps[a][b];

            if (!map)
                continue;

            MAP_FOR_EACH(map, ex, ey, ez, ew) {

                int x = ex - ox;
                int y = ey - oy;
                int z = ez - oz;
                int w = ew;

                // TODO: this should be unnecessary

                if (x < 0 || y < 0 || z < 0)
                    continue;

                if (x >= XZ_SIZE || y >= Y_SIZE || z >= XZ_SIZE)
                    continue;

                // END TODO

                opaque[XYZ(x, y, z)] = !is_transparent(w);

                if (opaque[XYZ(x, y, z)])
                    highest[XZ(x, z)] = MAX(highest[XZ(x, z)], y);

            } END_MAP_FOR_EACH;

        }

    }

    if (has_light)
    {

        for (int a = 0; a < 3; a++)
        {

            for (int b = 0; b < 3; b++)
            {

                Map *map = item->light_maps[a][b];

                if (!map)
                    continue;

                MAP_FOR_EACH(map, ex, ey, ez, ew) {

                    int x = ex - ox;
                    int y = ey - oy;
                    int z = ez - oz;

                    light_fill(opaque, light, x, y, z, ew, 1);

                } END_MAP_FOR_EACH;

            }

        }

    }

    Map *map = item->block_maps[1][1];

    chunk->miny = 256;
    chunk->maxy = 0;
    chunk->faces = 0;

    MAP_FOR_EACH(map, ex, ey, ez, ew) {

        if (ew <= 0)
            continue;

        int x = ex - ox;
        int y = ey - oy;
        int z = ez - oz;
        int f1 = !opaque[XYZ(x - 1, y, z)];
        int f2 = !opaque[XYZ(x + 1, y, z)];
        int f3 = !opaque[XYZ(x, y + 1, z)];
        int f4 = !opaque[XYZ(x, y - 1, z)] && (ey > 0);
        int f5 = !opaque[XYZ(x, y, z - 1)];
        int f6 = !opaque[XYZ(x, y, z + 1)];
        int total = f1 + f2 + f3 + f4 + f5 + f6;

        if (total == 0)
            continue;

        if (is_plant(ew))
            total = 4;

        chunk->miny = MIN(chunk->miny, ey);
        chunk->maxy = MAX(chunk->maxy, ey);
        chunk->faces += total;

    } END_MAP_FOR_EACH;

    item->data = malloc_faces(10, chunk->faces);
    int offset = 0;

    MAP_FOR_EACH(map, ex, ey, ez, ew) {

        if (ew <= 0)
            continue;

        int x = ex - ox;
        int y = ey - oy;
        int z = ez - oz;
        int f1 = !opaque[XYZ(x - 1, y, z)];
        int f2 = !opaque[XYZ(x + 1, y, z)];
        int f3 = !opaque[XYZ(x, y + 1, z)];
        int f4 = !opaque[XYZ(x, y - 1, z)] && (ey > 0);
        int f5 = !opaque[XYZ(x, y, z - 1)];
        int f6 = !opaque[XYZ(x, y, z + 1)];
        int total = f1 + f2 + f3 + f4 + f5 + f6;

        if (total == 0)
            continue;

        char neighbors[27] = {0};
        char lights[27] = {0};
        float shades[27] = {0};
        int index = 0;

        for (int dx = -1; dx <= 1; dx++)
        {

            for (int dy = -1; dy <= 1; dy++)
            {

                for (int dz = -1; dz <= 1; dz++)
                {

                    neighbors[index] = opaque[XYZ(x + dx, y + dy, z + dz)];
                    lights[index] = light[XYZ(x + dx, y + dy, z + dz)];
                    shades[index] = 0;

                    if (y + dy <= highest[XZ(x + dx, z + dz)])
                    {

                        for (int oy = 0; oy < 8; oy++)
                        {

                            if (opaque[XYZ(x + dx, y + dy + oy, z + dz)])
                            {

                                shades[index] = 1.0 - oy * 0.125;

                                break;

                            }

                        }

                    }

                    index++;

                }

            }

        }

        float ao[6][4];
        float light[6][4];

        occlusion(neighbors, lights, shades, ao, light);

        if (is_plant(ew))
        {

            total = 4;

            float min_ao = 1;
            float max_light = 0;

            for (int a = 0; a < 6; a++)
            {

                for (int b = 0; b < 4; b++)
                {

                    min_ao = MIN(min_ao, ao[a][b]);
                    max_light = MAX(max_light, light[a][b]);

                }

            }

            float rotation = noise_simplex2(ex, ez, 4, 0.5, 2) * 360;

            make_plant(item->data + offset, min_ao, max_light, ex, ey, ez, 0.5, ew, rotation);

        }

        else
        {

            make_cube(item->data + offset, ao, light, f1, f2, f3, f4, f5, f6, ex, ey, ez, 0.5, ew);

        }

        offset += total * 60;

    } END_MAP_FOR_EACH;

    free(opaque);
    free(light);
    free(highest);

}

static void gen_chunk_buffer(Chunk *chunk)
{

    WorkerItem item;

    for (int dp = -1; dp <= 1; dp++)
    {

        for (int dq = -1; dq <= 1; dq++)
        {

            Chunk *other = chunk;

            if (dp || dq)
                other = find_chunk(chunk->p + dp, chunk->q + dq);

            if (other)
            {

                item.block_maps[dp + 1][dq + 1] = &other->map;
                item.light_maps[dp + 1][dq + 1] = &other->lights;

            }

            else
            {

                item.block_maps[dp + 1][dq + 1] = 0;
                item.light_maps[dp + 1][dq + 1] = 0;

            }

        }

    }

    compute_chunk(chunk, &item);
    del_buffer(chunk->buffer);

    chunk->buffer = gen_faces(10, chunk->faces, item.data);
    chunk->dirty = 0;

}

static void createworld(Map *map, int p, int q)
{

    int pad = 1;

    for (int dx = -pad; dx < CHUNK_SIZE + pad; dx++)
    {

        for (int dz = -pad; dz < CHUNK_SIZE + pad; dz++)
        {

            int flag = 1;

            if (dx < 0 || dz < 0 || dx >= CHUNK_SIZE || dz >= CHUNK_SIZE)
                flag = -1;

            int x = p * CHUNK_SIZE + dx;
            int z = q * CHUNK_SIZE + dz;
            float f = noise_simplex2(x * 0.01, z * 0.01, 4, 0.5, 2);
            float g = noise_simplex2(-x * 0.01, -z * 0.01, 2, 0.9, 2);
            int mh = g * 32 + 16;
            int h = f * mh;

            if (h <= 12)
                h = 12;

            for (int y = 0; y < 10; y++)
                map_set(map, x, y, z, CEMENT * flag);

            for (int y = 10; y < 12; y++)
                map_set(map, x, y, z, SAND * flag);

            for (int y = 12; y < h - 1; y++)
                map_set(map, x, y, z, DIRT * flag);

            if (h > 12)
                map_set(map, x, h - 1, z, GRASS * flag);

            if (h > 12)
            {

                if (noise_simplex2(-x * 0.1, z * 0.1, 4, 0.8, 2) > 0.6)
                {

                    map_set(map, x, h, z, TALL_GRASS * flag);

                }

                if (noise_simplex2(x * 0.05, -z * 0.05, 4, 0.8, 2) > 0.7)
                {

                    int w = YELLOW_FLOWER + noise_simplex2(x * 0.1, z * 0.1, 4, 0.8, 2) * 7;

                    map_set(map, x, h, z, w * flag);

                }

                int ok = 1;

                if (dx - 4 < 0 || dz - 4 < 0 || dx + 4 >= CHUNK_SIZE || dz + 4 >= CHUNK_SIZE)
                    ok = 0;

                if (ok && noise_simplex2(x, z, 6, 0.5, 2) > 0.84)
                {

                    for (int y = h + 3; y < h + 8; y++)
                    {

                        for (int ox = -3; ox <= 3; ox++)
                        {

                            for (int oz = -3; oz <= 3; oz++)
                            {

                                int d = (ox * ox) + (oz * oz) + (y - (h + 4)) * (y - (h + 4));

                                if (d < 11)
                                    map_set(map, x + ox, y, z + oz, LEAVES);

                            }

                        }

                    }

                    for (int y = h; y < h + 7; y++)
                        map_set(map, x, y, z, WOOD);

                }

            }

            for (int y = 64; y < 72; y++)
            {

                if (noise_simplex3(x * 0.01, y * 0.1, z * 0.01, 8, 0.5, 2) > 0.75)
                    map_set(map, x, y, z, CLOUD * flag);

            }

        }

    }

}

static void create_chunk(Chunk *chunk, int p, int q)
{

    int dx = p * CHUNK_SIZE - 1;
    int dy = 0;
    int dz = q * CHUNK_SIZE - 1;

    chunk->p = p;
    chunk->q = q;
    chunk->faces = 0;
    chunk->buffer = 0;

    dirty_chunk(chunk);
    map_alloc(&chunk->map, dx, dy, dz, 0x7fff);
    map_alloc(&chunk->lights, dx, dy, dz, 0xf);

    createworld(&chunk->map, chunk->p, chunk->q);

}

static void delete_chunks()
{

    int count = g->chunk_count;

    for (int i = 0; i < count; i++)
    {

        Chunk *chunk = g->chunks + i;

        int p = chunked(g->player.x);
        int q = chunked(g->player.z);
        int delete = 1;

        if (chunk_distance(chunk, p, q) < g->delete_radius)
        {

            delete = 0;

            break;

        }

        if (delete)
        {

            map_free(&chunk->map);
            map_free(&chunk->lights);
            del_buffer(chunk->buffer);

            Chunk *other = g->chunks + (--count);

            memcpy(chunk, other, sizeof(Chunk));

        }

    }

    g->chunk_count = count;

}

static void delete_all_chunks()
{

    for (int i = 0; i < g->chunk_count; i++)
    {

        Chunk *chunk = g->chunks + i;

        map_free(&chunk->map);
        map_free(&chunk->lights);
        del_buffer(chunk->buffer);

    }

    g->chunk_count = 0;

}

static void load_chunks(Player *player, int radius, int max)
{

    int p = chunked(player->x);
    int q = chunked(player->z);

    for (int dp = -radius; dp <= radius; dp++)
    {

        for (int dq = -radius; dq <= radius; dq++)
        {

            int a = p + dp;
            int b = q + dq;
            Chunk *chunk = find_chunk(a, b);

            if (!chunk)
            {

                if (g->chunk_count < MAX_CHUNKS)
                {

                    chunk = g->chunks + g->chunk_count++;

                    create_chunk(chunk, a, b);

                }

            }

            if (chunk && chunk->dirty)
            {

                gen_chunk_buffer(chunk);

                if (--max <= 0)
                    return;

            }

        }

    }

}

static void toggle_light(int x, int y, int z)
{

    Chunk *chunk = find_chunk(chunked(x), chunked(z));

    if (chunk)
    {

        int w = map_get(&chunk->lights, x, y, z);

        map_set(&chunk->lights, x, y, z, w ? 0 : 15);

        dirty_chunk(chunk);

    }

}

static void _set_block(int p, int q, int x, int y, int z, int w)
{

    Chunk *chunk = find_chunk(p, q);

    if (chunk)
    {

        if (map_set(&chunk->map, x, y, z, w))
            dirty_chunk(chunk);

    }

    if (w == 0 && chunked(x) == p && chunked(z) == q)
    {

        if (map_set(&chunk->lights, x, y, z, w))
            dirty_chunk(chunk);

    }

}

static void set_block(int x, int y, int z, int w)
{

    int p = chunked(x);
    int q = chunked(z);

    _set_block(p, q, x, y, z, w);

    for (int dx = -1; dx <= 1; dx++)
    {

        for (int dz = -1; dz <= 1; dz++)
        {

            if (dx == 0 && dz == 0)
                continue;

            if (dx && chunked(x + dx) == p)
                continue;

            if (dz && chunked(z + dz) == q)
                continue;

            _set_block(p + dx, q + dz, x, y, z, -w);

        }

    }

}

static void record_block(int x, int y, int z, int w)
{

    memcpy(&g->block1, &g->block0, sizeof(Block));

    g->block0.x = x;
    g->block0.y = y;
    g->block0.z = z;
    g->block0.w = w;

}

static int get_block(int x, int y, int z)
{

    Chunk *chunk = find_chunk(chunked(x), chunked(z));

    if (chunk)
        return map_get(&chunk->map, x, y, z);

    return 0;

}

static int render_chunks(Attrib *attrib, Player *player)
{

    int p = chunked(player->x);
    int q = chunked(player->z);
    float matrix[16];
    float planes[6][4];
    int result = 0;

    set_matrix_3d(matrix, g->width, g->height, player->x, player->y, player->z, player->rx, player->ry, g->fov, g->ortho, g->render_radius);
    frustum_planes(planes, g->render_radius, matrix);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform3f(attrib->camera, player->x, player->y, player->z);
    glUniform1i(attrib->sampler, 0);
    glUniform1i(attrib->extra1, 2);
    glUniform1f(attrib->extra2, get_daylight());
    glUniform1f(attrib->extra3, g->render_radius * CHUNK_SIZE);
    glUniform1i(attrib->extra4, g->ortho);
    glUniform1f(attrib->timer, time_of_day());

    for (int i = 0; i < g->chunk_count; i++)
    {

        Chunk *chunk = g->chunks + i;

        if (chunk_distance(chunk, p, q) > g->render_radius)
            continue;

        if (!chunk_visible(planes, chunk->p, chunk->q, chunk->miny, chunk->maxy))
            continue;

        draw_triangles_3d_ao(attrib, chunk->buffer, chunk->faces * 6);

        result += chunk->faces;

    }

    return result;

}

static void render_sky(Attrib *attrib, Player *player, GLuint buffer)
{

    float matrix[16];

    set_matrix_3d(matrix, g->width, g->height, 0, 0, 0, player->rx, player->ry, g->fov, 0, g->render_radius);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform1i(attrib->sampler, 2);
    glUniform1f(attrib->timer, time_of_day());
    draw_triangles_3d(attrib, buffer, 512 * 3);

}

static void render_crosshairs(Attrib *attrib)
{

    float matrix[16];

    set_matrix_2d(matrix, g->width, g->height);
    glUseProgram(attrib->program);
    glLineWidth(4 * g->scale);
    glEnable(GL_COLOR_LOGIC_OP);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);

    GLuint crosshair_buffer = gen_crosshair_buffer();

    draw_lines(attrib, crosshair_buffer, 2, 4);
    del_buffer(crosshair_buffer);
    glDisable(GL_COLOR_LOGIC_OP);

}

static void render_item(Attrib *attrib)
{

    float matrix[16];

    set_matrix_item(matrix, g->width, g->height, g->scale);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform3f(attrib->camera, 0, 0, 5);
    glUniform1i(attrib->sampler, 0);
    glUniform1f(attrib->timer, time_of_day());

    int w = items[g->item_index];

    if (is_plant(w))
    {

        GLuint buffer = gen_plant_buffer(0, 0, 0, 0.5, w);

        draw_triangles_3d_ao(attrib, buffer, 24);
        del_buffer(buffer);

    }

    else
    {

        GLuint buffer = gen_cube_buffer(0, 0, 0, 0.5, w);

        draw_triangles_3d_ao(attrib, buffer, 36);
        del_buffer(buffer);

    }

}

static void render_text(Attrib *attrib, int justify, float x, float y, float n, char *text)
{

    float matrix[16];

    set_matrix_2d(matrix, g->width, g->height);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform1i(attrib->sampler, 1);
    glUniform1i(attrib->extra1, 0);

    int length = strlen(text);

    x -= n * justify * (length - 1) / 2;

    GLuint buffer = gen_text_buffer(x, y, n, text);

    draw_text(attrib, buffer, length);
    del_buffer(buffer);

}

static void add_message(const char *text)
{

    printf("%s\n", text);
    snprintf(g->messages[g->message_index], MAX_TEXT_LENGTH, "%s", text);

    g->message_index = (g->message_index + 1) % MAX_MESSAGES;

}

static void parse_command(const char *buffer, int forward)
{

    int radius;

    if (sscanf(buffer, "/view %d", &radius) == 1)
    {

        if (radius >= 1 && radius <= 24)
        {

            g->render_radius = radius;
            g->delete_radius = radius + 4;

        }

        else
        {

            add_message("Viewing distance must be between 1 and 24.");

        }

    }

}

static void addlight()
{

    int hx, hy, hz, hw;

    hw = hit_test(0, g->player.x, g->player.y, g->player.z, g->player.rx, g->player.ry, &hx, &hy, &hz);

    if (hy > 0 && hy < 256 && is_destructable(hw))
        toggle_light(hx, hy, hz);

}

static void addblock()
{

    int hx, hy, hz, hw;

    hw = hit_test(1, g->player.x, g->player.y, g->player.z, g->player.rx, g->player.ry, &hx, &hy, &hz);

    if (hy > 0 && hy < 256 && is_obstacle(hw))
    {

        if (!player_intersects_block(2, g->player.x, g->player.y, g->player.z, hx, hy, hz))
        {

            set_block(hx, hy, hz, items[g->item_index]);
            record_block(hx, hy, hz, items[g->item_index]);

        }

    }

}

static void removeblock()
{

    int hx, hy, hz, hw;

    hw = hit_test(0, g->player.x, g->player.y, g->player.z, g->player.rx, g->player.ry, &hx, &hy, &hz);

    if (hy > 0 && hy < 256 && is_destructable(hw))
    {

        set_block(hx, hy, hz, 0);
        record_block(hx, hy, hz, 0);

        if (is_plant(get_block(hx, hy + 1, hz)))
            set_block(hx, hy + 1, hz, 0);

    }

}

static void selectblock()
{

    int hx, hy, hz, hw;
    unsigned int i;

    hw = hit_test(0, g->player.x, g->player.y, g->player.z, g->player.rx, g->player.ry, &hx, &hy, &hz);

    for (i = 0; i < item_count; i++)
    {

        if (items[i] == hw)
        {

            g->item_index = i;

            break;

        }

    }

}

static void onkey(GLFWwindow *window, int key, int scancode, int action, int mods)
{

    int control = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER);
    int exclusive = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

    if (action == GLFW_RELEASE)
        return;

    if (key == GLFW_KEY_BACKSPACE)
    {

        if (g->typing)
        {

            int n = strlen(g->typing_buffer);

            if (n > 0)
                g->typing_buffer[n - 1] = '\0';

        }

    }

    if (action != GLFW_PRESS)
        return;

    if (key == GLFW_KEY_ESCAPE)
    {

        if (g->typing)
            g->typing = 0;
        else if (exclusive)
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    }

    if (key == GLFW_KEY_ENTER)
    {

        if (g->typing)
        {

            if (mods & GLFW_MOD_SHIFT)
            {

                int n = strlen(g->typing_buffer);

                if (n < MAX_TEXT_LENGTH - 1)
                {

                    g->typing_buffer[n] = '\r';
                    g->typing_buffer[n + 1] = '\0';

                }

            }

            else
            {

                g->typing = 0;

                if (g->typing_buffer[0] == '/')
                {

                    parse_command(g->typing_buffer, 1);

                }

            }

        }

    }

    if (control && key == 'V')
    {

        const char *buffer = glfwGetClipboardString(window);

        if (g->typing)
        {

            g->suppress_char = 1;

            strncat(g->typing_buffer, buffer, MAX_TEXT_LENGTH - strlen(g->typing_buffer) - 1);

        }

        else
        {

            parse_command(buffer, 0);

        }

    }

    if (!g->typing)
    {

        if (key == CRAFT_KEY_FLY)
            g->flying = !g->flying;

        if (key >= '1' && key <= '9')
            g->item_index = key - '1';

        if (key == '0')
            g->item_index = 9;

        if (key == CRAFT_KEY_ITEM_NEXT)
            g->item_index = (g->item_index + 1) % item_count;

        if (key == CRAFT_KEY_ITEM_PREV)
        {

            g->item_index--;

            if (g->item_index < 0)
                g->item_index = item_count - 1;

        }

    }

}

static void onchar(GLFWwindow *window, unsigned int u)
{

    if (g->suppress_char)
    {

        g->suppress_char = 0;

        return;

    }

    if (g->typing)
    {

        if (u >= 32 && u < 128)
        {

            char c = (char)u;
            int n = strlen(g->typing_buffer);

            if (n < MAX_TEXT_LENGTH - 1)
            {

                g->typing_buffer[n] = c;
                g->typing_buffer[n + 1] = '\0';

            }

        }

    }

    else
    {

        if (u == CRAFT_KEY_COMMAND)
        {

            g->typing = 1;
            g->typing_buffer[0] = '/';
            g->typing_buffer[1] = '\0';

        }

    }

}

static void onscroll(GLFWwindow *window, double xdelta, double ydelta)
{

    static double ypos = 0;

    ypos += ydelta;

    if (ypos < -SCROLL_THRESHOLD)
    {

        g->item_index = (g->item_index + 1) % item_count;

        ypos = 0;

    }

    if (ypos > SCROLL_THRESHOLD)
    {

        g->item_index--;

        if (g->item_index < 0)
            g->item_index = item_count - 1;

        ypos = 0;

    }

}

static void onmousebutton(GLFWwindow *window, int button, int action, int mods)
{

    int control = mods & (GLFW_MOD_CONTROL | GLFW_MOD_SUPER);
    int exclusive = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

    if (action != GLFW_PRESS)
        return;

    if (button == GLFW_MOUSE_BUTTON_LEFT)
    {

        if (exclusive)
        {

            removeblock();

        }

        else
        {

            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

        }

    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {

        if (exclusive)
        {

            if (control)
                addlight();
            else
                addblock();

        }

    }

    if (button == GLFW_MOUSE_BUTTON_MIDDLE)
    {

        if (exclusive)
            selectblock();

    }

}

static void handle_mouse_input()
{

    int exclusive = glfwGetInputMode(g->window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    static double px = 0;
    static double py = 0;

    if (exclusive && (px || py))
    {

        double mx, my;
        float m = 0.0025;

        glfwGetCursorPos(g->window, &mx, &my);

        g->player.rx += (mx - px) * m;

        if (INVERT_MOUSE)
            g->player.ry += (my - py) * m;
        else
            g->player.ry -= (my - py) * m;

        if (g->player.rx < 0)
            g->player.rx += RADIANS(360);

        if (g->player.rx >= RADIANS(360))
            g->player.rx -= RADIANS(360);

        g->player.ry = MAX(g->player.ry, -RADIANS(90));
        g->player.ry = MIN(g->player.ry, RADIANS(90));
        px = mx;
        py = my;

    }

    else
    {

        glfwGetCursorPos(g->window, &px, &py);

    }

}

static void handle_movement(double dt)
{

    float speed = g->flying ? 32 : 8;
    int step = 8;
    float ut = dt / step;
    int sz = 0;
    int sx = 0;

    if (!g->typing)
    {

        g->ortho = glfwGetKey(g->window, CRAFT_KEY_ORTHO) ? 64 : 0;
        g->fov = glfwGetKey(g->window, CRAFT_KEY_ZOOM) ? 15 : 65;

        if (glfwGetKey(g->window, CRAFT_KEY_FORWARD))
            sz--;

        if (glfwGetKey(g->window, CRAFT_KEY_BACKWARD))
            sz++;

        if (glfwGetKey(g->window, CRAFT_KEY_LEFT))
            sx--;

        if (glfwGetKey(g->window, CRAFT_KEY_RIGHT))
            sx++;

        if (glfwGetKey(g->window, GLFW_KEY_LEFT))
            g->player.rx -= 1.0;

        if (glfwGetKey(g->window, GLFW_KEY_RIGHT))
            g->player.rx += 1.0;

        if (glfwGetKey(g->window, GLFW_KEY_UP))
            g->player.ry += 1.0;

        if (glfwGetKey(g->window, GLFW_KEY_DOWN))
            g->player.ry -= 1.0;

    }

    if (g->flying)
        get_motion_vector_flying(sz, sx, &g->player);
    else
        get_motion_vector_normal(sz, sx, &g->player);

    if (!g->typing)
    {

        if (glfwGetKey(g->window, CRAFT_KEY_JUMP))
        {

            if (g->flying)
                g->player.vy = 1;
            else if (g->player.dy == 0)
                g->player.dy = 2.0;

        }

        if (glfwGetKey(g->window, CRAFT_KEY_CROUCH))
        {

            if (g->flying)
                g->player.vy = -1;

        }

    }

    g->player.vx = g->player.vx * ut * speed;
    g->player.vy = g->player.vy * ut * speed;
    g->player.vz = g->player.vz * ut * speed;

    for (int i = 0; i < step; i++)
    {

        if (g->flying)
        {

            g->player.dy = 0;

        }

        else
        {

            g->player.dy -= ut * 8.0;
            g->player.dy = MAX(g->player.dy, -250);

        }

        g->player.vy += g->player.dy * ut;
        g->player.x += g->player.vx;
        g->player.y += g->player.vy;
        g->player.z += g->player.vz;

        if (collide(2, &g->player))
            g->player.dy = 0;

    }

    if (g->player.y < 0)
        g->player.y = highest_block(g->player.x, g->player.z) + 2;

}

static GLuint load_shader(GLenum type, const char *path)
{

    FILE *file;
    char data[4096];
    const GLchar *pdata = data;
    GLuint shader;
    GLint status;
    int count;

    file = fopen(path, "rb");

    count = fread(data, 1, 4096, file);

    data[count] = '\0';

    fclose(file);

    shader = glCreateShader(type);

    glShaderSource(shader, 1, &pdata, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);

    if (status == GL_FALSE)
    {

        GLint length;

        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        GLchar *info = calloc(length, sizeof(GLchar));

        glGetShaderInfoLog(shader, length, NULL, info);
        fprintf(stderr, "glCompileShader failed:\n%s\n", info);
        free(info);

    }

    return shader;

}

static GLuint load_program(const char *path1, const char *path2)
{

    GLuint shader1 = load_shader(GL_VERTEX_SHADER, path1);
    GLuint shader2 = load_shader(GL_FRAGMENT_SHADER, path2);
    GLuint program = glCreateProgram();
    GLint status;

    glAttachShader(program, shader1);
    glAttachShader(program, shader2);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &status);

    if (status == GL_FALSE)
    {

        GLint length;

        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);

        GLchar *info = calloc(length, sizeof(GLchar));

        glGetProgramInfoLog(program, length, NULL, info);

        fprintf(stderr, "glLinkProgram failed: %s\n", info);
        free(info);

    }

    glDetachShader(program, shader1);
    glDetachShader(program, shader2);
    glDeleteShader(shader1);
    glDeleteShader(shader2);

    return program;

}

static void flip_image_vertical(unsigned char *data, unsigned int width, unsigned int height)
{

    unsigned int size = width * height * 4;
    unsigned int stride = sizeof(char) * width * 4;
    unsigned char *new_data = malloc(sizeof(unsigned char) * size);

    for (unsigned int i = 0; i < height; i++)
    {

        unsigned int j = height - i - 1;

        memcpy(new_data + j * stride, data + i * stride, stride);

    }

    memcpy(data, new_data, size);

    free(new_data);

}

static void load_png_texture(const char *file_name)
{

    unsigned int error;
    unsigned char *data;
    unsigned int width, height;

    error = lodepng_decode32_file(&data, &width, &height, file_name);

    if (error)
    {

        fprintf(stderr, "load_png_texture %s failed, error %u: %s\n", file_name, error, lodepng_error_text(error));
        exit(1);

    }

    flip_image_vertical(data, width, height);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    free(data);

}

int main(int argc, char **argv)
{

    struct mtwist_state state;
    int window_width = WINDOW_WIDTH;
    int window_height = WINDOW_HEIGHT;

/*
    mtwist_seed1(&state, time(NULL));
*/

    mtwist_seed1(&state, 1234);
    noise_seed(&state);

    if (!glfwInit())
        return -1;

    GLFWmonitor *monitor = NULL;

    if (FULLSCREEN)
    {

        int mode_count;

        monitor = glfwGetPrimaryMonitor();

        const GLFWvidmode *modes = glfwGetVideoModes(monitor, &mode_count);

        window_width = modes[mode_count - 1].width;
        window_height = modes[mode_count - 1].height;

    }

    g->window = glfwCreateWindow(window_width, window_height, "Craft", monitor, NULL);

    if (!g->window)
    {

        glfwTerminate();

        return -1;

    }

    glfwMakeContextCurrent(g->window);
    glfwSwapInterval(VSYNC);
    glfwSetInputMode(g->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(g->window, onkey);
    glfwSetCharCallback(g->window, onchar);
    glfwSetMouseButtonCallback(g->window, onmousebutton);
    glfwSetScrollCallback(g->window, onscroll);

    if (glewInit() != GLEW_OK)
        return -1;

    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glLogicOp(GL_INVERT);
    glClearColor(0, 0, 0, 1);

    GLuint texture;
    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    load_png_texture("textures/texture.png");

    GLuint font;
    glGenTextures(1, &font);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, font);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    load_png_texture("textures/font.png");

    GLuint sky;
    glGenTextures(1, &sky);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, sky);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    load_png_texture("textures/sky.png");

    Attrib block_attrib = {0};
    Attrib line_attrib = {0};
    Attrib text_attrib = {0};
    Attrib sky_attrib = {0};
    GLuint program;

    program = load_program("shaders/block_vertex.glsl", "shaders/block_fragment.glsl");
    block_attrib.program = program;
    block_attrib.position = glGetAttribLocation(program, "position");
    block_attrib.normal = glGetAttribLocation(program, "normal");
    block_attrib.uv = glGetAttribLocation(program, "uv");
    block_attrib.matrix = glGetUniformLocation(program, "matrix");
    block_attrib.sampler = glGetUniformLocation(program, "sampler");
    block_attrib.extra1 = glGetUniformLocation(program, "sky_sampler");
    block_attrib.extra2 = glGetUniformLocation(program, "daylight");
    block_attrib.extra3 = glGetUniformLocation(program, "fog_distance");
    block_attrib.extra4 = glGetUniformLocation(program, "ortho");
    block_attrib.camera = glGetUniformLocation(program, "camera");
    block_attrib.timer = glGetUniformLocation(program, "timer");

    program = load_program("shaders/line_vertex.glsl", "shaders/line_fragment.glsl");
    line_attrib.program = program;
    line_attrib.position = glGetAttribLocation(program, "position");
    line_attrib.matrix = glGetUniformLocation(program, "matrix");

    program = load_program("shaders/text_vertex.glsl", "shaders/text_fragment.glsl");
    text_attrib.program = program;
    text_attrib.position = glGetAttribLocation(program, "position");
    text_attrib.uv = glGetAttribLocation(program, "uv");
    text_attrib.matrix = glGetUniformLocation(program, "matrix");
    text_attrib.sampler = glGetUniformLocation(program, "sampler");

    program = load_program("shaders/sky_vertex.glsl", "shaders/sky_fragment.glsl");
    sky_attrib.program = program;
    sky_attrib.position = glGetAttribLocation(program, "position");
    sky_attrib.normal = glGetAttribLocation(program, "normal");
    sky_attrib.uv = glGetAttribLocation(program, "uv");
    sky_attrib.matrix = glGetUniformLocation(program, "matrix");
    sky_attrib.sampler = glGetUniformLocation(program, "sampler");
    sky_attrib.timer = glGetUniformLocation(program, "timer");

    g->render_radius = RENDER_CHUNK_RADIUS;
    g->delete_radius = RENDER_CHUNK_RADIUS + 4;

    int running = 1;

    while (running)
    {

        double last_update = glfwGetTime();
        double previous = last_update;

        g->day_length = DAY_LENGTH;
        g->time_changed = 1;

        glfwSetTime(g->day_length / 3.0);

        GLuint sky_buffer = gen_sky_buffer();

        load_chunks(&g->player, g->render_radius, ((g->render_radius * 2) + 1) * ((g->render_radius * 2) + 1));

        g->player.y = highest_block(g->player.x, g->player.z) + 2;

        while (1)
        {

            g->scale = get_scale_factor();

            glfwGetFramebufferSize(g->window, &g->width, &g->height);
            glViewport(0, 0, g->width, g->height);

            if (g->time_changed)
            {

                g->time_changed = 0;
                last_update = glfwGetTime();

                memset(&g->fps, 0, sizeof(FPS));

            }

            g->fps.frames++;

            double now = glfwGetTime();
            double elapsed = now - g->fps.since;
            double dt = now - previous;

            previous = now;

            if (now - last_update > 0.1)
                last_update = now;

            if (elapsed >= 1)
            {

                g->fps.fps = round(g->fps.frames / elapsed);
                g->fps.frames = 0;
                g->fps.since = now;

            }

            dt = MIN(dt, 0.2);
            dt = MAX(dt, 0.0);

            /* Input */
            handle_mouse_input();
            handle_movement(dt);

            /* Logic */
            delete_chunks();
            load_chunks(&g->player, 1, 9);
            load_chunks(&g->player, g->render_radius, 1);

            /* Rendering */
            glClear(GL_COLOR_BUFFER_BIT);
            glClear(GL_DEPTH_BUFFER_BIT);
            render_sky(&sky_attrib, &g->player, sky_buffer);
            glClear(GL_DEPTH_BUFFER_BIT);

            int face_count = render_chunks(&block_attrib, &g->player);

            glClear(GL_DEPTH_BUFFER_BIT);
            render_crosshairs(&line_attrib);
            render_item(&block_attrib);

            char text_buffer[1024];
            float ts = 12 * g->scale;
            float tx = ts / 2;
            float ty = g->height - ts;
            int hour = time_of_day() * 24;
            char am_pm = hour < 12 ? 'a' : 'p';

            hour = hour % 12;
            hour = hour ? hour : 12;

            snprintf(text_buffer, 1024, "(%d, %d) (%.2f, %.2f, %.2f) [%d, %d] %d%cm %dfps", chunked(g->player.x), chunked(g->player.z), g->player.x, g->player.y, g->player.z, g->chunk_count, face_count * 2, hour, am_pm, g->fps.fps);
            render_text(&text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);

            ty -= ts * 2;

            for (int i = 0; i < MAX_MESSAGES; i++)
            {

                int index = (g->message_index + i) % MAX_MESSAGES;

                if (strlen(g->messages[index]))
                {

                    render_text(&text_attrib, ALIGN_LEFT, tx, ty, ts, g->messages[index]);

                    ty -= ts * 2;

                }

            }

            if (g->typing)
            {

                snprintf(text_buffer, 1024, "> %s", g->typing_buffer);
                render_text(&text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);

                ty -= ts * 2;

            }

            glfwSwapBuffers(g->window);
            glfwPollEvents();

            if (glfwWindowShouldClose(g->window))
            {

                running = 0;

                break;

            }

            if (g->mode_changed)
            {

                g->mode_changed = 0;

                break;

            }

        }

        del_buffer(sky_buffer);
        delete_all_chunks();

    }

    glfwTerminate();

    return 0;

}

