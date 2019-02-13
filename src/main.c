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

    float x;
    float y;
    float z;
    float lx;
    float ly;
    float lz;
    float vx;
    float vy;
    float vz;

} Box;

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
    GLfloat *data;

} Chunk;

typedef struct
{

    int x;
    int y;
    int z;
    int w;

} Block;

typedef struct
{

    Box box;
    float rx;
    float ry;

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
    int mode_changed;
    int day_length;
    unsigned int fps;
    unsigned int frames;
    double since;
    Attrib block_attrib;
    Attrib line_attrib;
    Attrib text_attrib;
    Attrib sky_attrib;

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

static GLuint gen_crosshair_buffer(void)
{

    int x = g->width / 2;
    int y = g->height / 2;
    int p = 10 * g->scale;
    GLfloat data[] = {x, y - p, x, y + p, x - p, y, x + p, y};

    return gen_buffer(sizeof(data), data);

}

static GLuint gen_sky_buffer(void)
{

    GLfloat data[12288];

    make_sphere(data, 1, 3);

    return gen_buffer(sizeof(data), data);

}

static GLuint gen_cube_buffer(float x, float y, float z, float n, int w)
{

    GLfloat data[360];
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

    return gen_buffer(sizeof(data), data);

}

static GLuint gen_plant_buffer(float x, float y, float z, float n, int w)
{

    GLfloat data[240];
    float ao = 0;
    float light = 1;

    make_plant(data, ao, light, x, y, z, n, w, 45);

    return gen_buffer(sizeof(data), data);

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

static int hit_test(int previous, Player *player, int *bx, int *by, int *bz)
{

    int p = chunked(player->box.x);
    int q = chunked(player->box.z);
    int result = 0;
    float best = 0;
    float vx;
    float vy;
    float vz;
    float m;

    m = cosf(player->ry);
    vx = cosf(player->rx - RADIANS(90)) * m;
    vy = sinf(player->ry);
    vz = sinf(player->rx - RADIANS(90)) * m;

    for (int i = 0; i < g->chunk_count; i++)
    {

        int hx, hy, hz, hw;

        Chunk *chunk = g->chunks + i;

        if (chunk_distance(chunk, p, q) > 1)
            continue;

        hw = _hit_test(&chunk->map, 16, previous, player->box.x, player->box.y, player->box.z, vx, vy, vz, &hx, &hy, &hz);

        if (hw > 0)
        {

            float d = sqrtf(powf(hx - player->box.x, 2) + powf(hy - player->box.y, 2) + powf(hz - player->box.z, 2));

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

static unsigned int aabbcheck(Box *b1, Box *b2)
{

    if (b1->x > b2->x + b2->lx || b1->x + b1->lx < b2->x)
        return 0;

    if (b1->y > b2->y + b2->ly || b1->y + b1->ly < b2->y)
        return 0;

    if (b1->z > b2->z + b2->lz || b1->z + b1->lz < b2->z)
        return 0;

    return 1;

}

static float aabbsweep(Box b1, Box b2, float *normalx, float *normaly, float *normalz)
{

    float xInvEntry, yInvEntry, zInvEntry;
    float xInvExit, yInvExit, zInvExit;
    float xEntry, yEntry, zEntry;
    float xExit, yExit, zExit;

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

    if (b1.vx == 0.0f)
    {

        xEntry = 0;
        xExit = 0;

    }

    else
    {

        xEntry = xInvEntry / b1.vx;
        xExit = xInvExit / b1.vx;

    }

    if (b1.vy == 0.0f)
    {

        yEntry = 0;
        yExit = 0;

    }

    else
    {

        yEntry = yInvEntry / b1.vy;
        yExit = yInvExit / b1.vy;

    }

    if (b1.vz == 0.0f)
    {

        zEntry = 0;
        zExit = 0;

    }

    else
    {

        zEntry = zInvEntry / b1.vz;
        zExit = zInvExit / b1.vz;

    }

    float entryTime = MAX(MAX(xEntry, yEntry), zEntry);
    float exitTime = MIN(MIN(xExit, yExit), zExit);

    *normalx = 0.0f;
    *normaly = 0.0f;
    *normalz = 0.0f;

/*
    if (entryTime > exitTime || (xEntry < 0.0f && yEntry < 0.0f && zEntry < 0.0f) || xEntry > 1.0f || yEntry > 1.0f || zEntry > 1.0f)
        return 1.0f;
*/

    if (xEntry > yEntry && xEntry > zEntry)
    {

        if (xInvEntry < 0.0f)
        {

            *normalx = 1.0f;
            *normaly = 0.0f;
            *normalz = 0.0f;

        }

        else
        {

            *normalx = -1.0f;
            *normaly = 0.0f;
            *normalz = 0.0f;

        }

    }

    else if (yEntry > xEntry && yEntry > zEntry)
    {

        if (yInvEntry < 0.0f)
        {

            *normalx = 0.0f;
            *normaly = 1.0f;
            *normalz = 0.0f;

        }

        else
        {

            *normalx = 0.0f;
            *normaly = -1.0f;
            *normalz = 0.0f;

        }

    }

    else if (zEntry > xEntry && zEntry > yEntry)
    {

        if (zInvEntry < 0.0f)
        {

            *normalx = 0.0f;
            *normaly = 0.0f;
            *normalz = 1.0f;

        }

        else
        {

            *normalx = 0.0f;
            *normaly = 0.0f;
            *normalz = -1.0f;

        }

    }

    return entryTime;

}

static void player_collide(Player *player)
{

    Box box;
    Box block;

    box.x = player->box.x;
    box.y = player->box.y;
    box.z = player->box.z;
    box.lx = 1.0;
    box.ly = 1.0;
    box.lz = 1.0;
    box.vx = player->box.vx;
    box.vy = player->box.vy;
    box.vz = player->box.vz;
    block.x = (int)player->box.x;
    block.y = (int)player->box.y;
    block.z = (int)player->box.z;
    block.lx = 1.0;
    block.ly = 1.0;
    block.lz = 1.0;
    block.vx = 0.0;
    block.vy = 0.0;
    block.vz = 0.0;

    for (int kx = -1; kx <= 1; kx++)
    {

        for (int ky = -1; ky <= 1; ky++)
        {

            for (int kz = -1; kz <= 1; kz++)
            {

                Chunk *chunk;
                float normalx;
                float normaly;
                float normalz;

                block.x = (int)player->box.x + kx;
                block.y = (int)player->box.y + ky;
                block.z = (int)player->box.z + kz;
                chunk = find_chunk(chunked(block.x), chunked(block.z));

                if (!chunk)
                    continue;

                if (!is_obstacle(map_get(&chunk->map, block.x, block.y, block.z)))
                    continue;

                if (!aabbcheck(&box, &block))
                    continue;

                float collisiontime = aabbsweep(box, block, &normalx, &normaly, &normalz);

                if (collisiontime < 1.0f)
                {

                    box.vx = box.vx - box.vx * abs(normalx);
                    box.vy = box.vy - box.vy * abs(normaly);
                    box.vz = box.vz - box.vz * abs(normalz);

                }

            }

        }

    }

    g->player.box.x += box.vx;
    g->player.box.y += box.vy;
    g->player.box.z += box.vz;

}

static int player_intersects_block(int height, Player *player, int hx, int hy, int hz)
{

    int nx = roundf(player->box.x);
    int ny = roundf(player->box.y);
    int nz = roundf(player->box.z);

    for (int i = 0; i < height; i++)
    {

        if (nx == hx && ny - i == hy && nz == hz)
            return 1;

    }

    return 0;

}

static void dirty_chunk(Chunk *chunk, int radius)
{

    for (int dp = -radius; dp <= radius; dp++)
    {

        for (int dq = -radius; dq <= radius; dq++)
        {

            Chunk *neighbour = find_chunk(chunk->p + dp, chunk->q + dq);

            if (neighbour)
                neighbour->dirty = 1;

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

static void compute_chunk(Chunk *chunk)
{

    char *opaque = (char *)calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char));
    char *light = (char *)calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char));
    char *highest = (char *)calloc(XZ_SIZE * XZ_SIZE, sizeof(char));
    int ox = chunk->p * CHUNK_SIZE - CHUNK_SIZE - 1;
    int oy = -1;
    int oz = chunk->q * CHUNK_SIZE - CHUNK_SIZE - 1;
    int has_light = 0;
    Map *block_maps[3][3];
    Map *light_maps[3][3];

    for (int dp = -1; dp <= 1; dp++)
    {

        for (int dq = -1; dq <= 1; dq++)
        {

            Chunk *other = chunk;

            if (dp || dq)
                other = find_chunk(chunk->p + dp, chunk->q + dq);

            if (other)
            {

                block_maps[dp + 1][dq + 1] = &other->map;
                light_maps[dp + 1][dq + 1] = &other->lights;

            }

            else
            {

                block_maps[dp + 1][dq + 1] = 0;
                light_maps[dp + 1][dq + 1] = 0;

            }

        }

    }

    for (int a = 0; a < 3; a++)
    {

        for (int b = 0; b < 3; b++)
        {

            Map *map = light_maps[a][b];

            if (map && map->size)
                has_light = 1;

        }

    }

    for (int a = 0; a < 3; a++)
    {

        for (int b = 0; b < 3; b++)
        {

            Map *map = block_maps[a][b];

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

                Map *map = light_maps[a][b];

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

    Map *map = block_maps[1][1];

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

    chunk->data = malloc_faces(10, chunk->faces);
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

            make_plant(chunk->data + offset, min_ao, max_light, ex, ey, ez, 0.5, ew, rotation);

        }

        else
        {

            make_cube(chunk->data + offset, ao, light, f1, f2, f3, f4, f5, f6, ex, ey, ez, 0.5, ew);

        }

        offset += total * 60;

    } END_MAP_FOR_EACH;

    free(opaque);
    free(light);
    free(highest);

}

static void createworld(Map *map, int p, int q)
{

    unsigned int dx;
    unsigned int dz;

    for (dx = 0; dx < CHUNK_SIZE; dx++)
    {

        for (dz = 0; dz < CHUNK_SIZE; dz++)
        {

            int x = p * CHUNK_SIZE + dx;
            int z = q * CHUNK_SIZE + dz;
            float f = noise_simplex2(x * 0.01, z * 0.01, 4, 0.5, 2);
            float g = noise_simplex2(-x * 0.01, -z * 0.01, 2, 0.9, 2);
            int mh = g * 32 + 16;
            int h = f * mh;
            int y;

            if (h <= 12)
                h = 12;

            for (y = 0; y < 10; y++)
                map_set(map, x, y, z, CEMENT);

            for (y = 10; y < 12; y++)
                map_set(map, x, y, z, SAND);

            for (y = 12; y < h - 1; y++)
                map_set(map, x, y, z, DIRT);

            if (h > 12)
                map_set(map, x, h - 1, z, GRASS);

            if (h > 12)
            {

                if (noise_simplex2(-x * 0.1, z * 0.1, 4, 0.8, 2) > 0.6)
                {

                    map_set(map, x, h, z, TALL_GRASS);

                }

                if (noise_simplex2(x * 0.05, -z * 0.05, 4, 0.8, 2) > 0.7)
                {

                    int w = YELLOW_FLOWER + noise_simplex2(x * 0.1, z * 0.1, 4, 0.8, 2) * 7;

                    map_set(map, x, h, z, w);

                }

                int ok = 1;

                if (dx - 4 < 0 || dz - 4 < 0 || dx + 4 >= CHUNK_SIZE || dz + 4 >= CHUNK_SIZE)
                    ok = 0;

                if (ok && noise_simplex2(x, z, 6, 0.5, 2) > 0.84)
                {

                    for (y = h + 3; y < h + 8; y++)
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

                    for (y = h; y < h + 7; y++)
                        map_set(map, x, y, z, WOOD);

                }

            }

            for (y = 64; y < 72; y++)
            {

                if (noise_simplex3(x * 0.01, y * 0.1, z * 0.01, 8, 0.5, 2) > 0.75)
                    map_set(map, x, y, z, CLOUD);

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
    chunk->dirty = 1;

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

        int p = chunked(g->player.box.x);
        int q = chunked(g->player.box.z);
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

    int p = chunked(player->box.x);
    int q = chunked(player->box.z);

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

                compute_chunk(chunk);
                del_buffer(chunk->buffer);

                chunk->buffer = gen_faces(10, chunk->faces, chunk->data);
                chunk->dirty = 0;

                if (--max <= 0)
                    return;

            }

        }

    }

}

static void setblock(int x, int y, int z, int w)
{

    int p = chunked(x);
    int q = chunked(z);
    Chunk *chunk = find_chunk(p, q);

    if (chunk)
    {

        if (map_set(&chunk->map, x, y, z, w))
            dirty_chunk(chunk, 1);

    }

    if (w == 0 && chunked(x) == p && chunked(z) == q)
    {

        if (map_set(&chunk->lights, x, y, z, w))
            dirty_chunk(chunk, 1);

    }

}

static int get_block(int x, int y, int z)
{

    Chunk *chunk = find_chunk(chunked(x), chunked(z));

    if (chunk)
        return map_get(&chunk->map, x, y, z);

    return 0;

}

static void render_chunks(Attrib *attrib, Player *player)
{

    int p = chunked(player->box.x);
    int q = chunked(player->box.z);
    float matrix[16];
    float planes[6][4];

    set_matrix_3d(matrix, g->width, g->height, player->box.x, player->box.y, player->box.z, player->rx, player->ry, g->fov, g->ortho, g->render_radius);
    frustum_planes(planes, g->render_radius, matrix);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform3f(attrib->camera, player->box.x, player->box.y, player->box.z);
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

    }

}

static void render_sky(Attrib *attrib, Player *player, GLuint buffer)
{

    float matrix[16];

    set_matrix_3d(matrix, g->width, g->height, 0, 0, 0, player->rx, player->ry, g->fov, 0, g->render_radius);
    glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_DEPTH_BUFFER_BIT);
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
    glClear(GL_DEPTH_BUFFER_BIT);
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
    int w = items[g->item_index];

    set_matrix_item(matrix, g->width, g->height, g->scale);
    glClear(GL_DEPTH_BUFFER_BIT);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform3f(attrib->camera, 0, 0, 5);
    glUniform1i(attrib->sampler, 0);
    glUniform1f(attrib->timer, time_of_day());

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
    int length;

    set_matrix_2d(matrix, g->width, g->height);
    glUseProgram(attrib->program);
    glUniformMatrix4fv(attrib->matrix, 1, GL_FALSE, matrix);
    glUniform1i(attrib->sampler, 1);
    glUniform1i(attrib->extra1, 0);

    length = strlen(text);

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

static void parse_command(const char *buffer)
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

static void addlight(void)
{

    int hx, hy, hz, hw;

    hw = hit_test(0, &g->player, &hx, &hy, &hz);

    if (hy > 0 && hy < 256 && is_destructable(hw))
    {

        Chunk *chunk = find_chunk(chunked(hx), chunked(hz));

        if (chunk)
        {

            int w = map_get(&chunk->lights, hx, hy, hz);

            map_set(&chunk->lights, hx, hy, hz, w ? 0 : 15);

            dirty_chunk(chunk, 1);

        }

    }

}

static void addblock(void)
{

    int hx, hy, hz, hw;

    hw = hit_test(1, &g->player, &hx, &hy, &hz);

    if (hy > 0 && hy < 256 && is_obstacle(hw))
    {

        if (!player_intersects_block(2, &g->player, hx, hy, hz))
            setblock(hx, hy, hz, items[g->item_index]);

    }

}

static void removeblock(void)
{

    int hx, hy, hz, hw;

    hw = hit_test(0, &g->player, &hx, &hy, &hz);

    if (hy > 0 && hy < 256 && is_destructable(hw))
    {

        setblock(hx, hy, hz, 0);

        if (is_plant(get_block(hx, hy + 1, hz)))
            setblock(hx, hy + 1, hz, 0);

    }

}

static void selectblock(void)
{

    int hx, hy, hz, hw;
    unsigned int i;

    hw = hit_test(0, &g->player, &hx, &hy, &hz);

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

    int exclusive = glfwGetInputMode(window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;

    if (action != GLFW_PRESS)
        return;

    if (g->typing)
    {

        if (key == GLFW_KEY_BACKSPACE)
        {

            int n = strlen(g->typing_buffer);

            if (n > 0)
                g->typing_buffer[n - 1] = '\0';

        }

        if (key == GLFW_KEY_ESCAPE)
        {

            g->typing = 0;

        }

        if (key == GLFW_KEY_ENTER)
        {

            if (g->typing_buffer[0] == '/')
                parse_command(g->typing_buffer);

            g->typing = 0;

        }

    }

    else
    {

        if (key == GLFW_KEY_ESCAPE)
        {

            if (exclusive)
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

        }

        if (key == CRAFT_KEY_FLY)
            g->flying = !g->flying;

        if (key >= '1' && key <= '9')
            g->item_index = key - '1';

        if (key == '0')
            g->item_index = 9;

        if (key == CRAFT_KEY_ITEM_NEXT)
        {

            g->item_index = (g->item_index + 1) % item_count;

        }

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
            removeblock();
        else
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

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

static void handle_movement(double dt)
{

    int exclusive = glfwGetInputMode(g->window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    float speed = g->flying ? 8 : 8;
    int step = 8;
    float ut = dt / step;
    int sz = 0;
    int sx = 0;
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

        if (sx || sz)
        {

            float strafe = atan2f(sz, sx);

            g->player.box.vx = cosf(g->player.rx + strafe);
            g->player.box.vy = 0;
            g->player.box.vz = sinf(g->player.rx + strafe);

            if (g->flying)
            {

                float m = cosf(g->player.ry);
                float y = sinf(g->player.ry);

                if (sx)
                {

                    if (!sz)
                        y = 0;

                    m = 1;

                }

                if (sz > 0)
                    y = -y;

                g->player.box.vx *= m;
                g->player.box.vy = y;
                g->player.box.vz *= m;

            }

        }

        if (glfwGetKey(g->window, CRAFT_KEY_JUMP))
        {

            if (g->flying)
                g->player.box.vy = 1;

        }

        if (glfwGetKey(g->window, CRAFT_KEY_CROUCH))
        {

            if (g->flying)
                g->player.box.vy = -1;

        }

    }

    g->player.box.vx *= ut * speed;
    g->player.box.vy *= ut * speed;
    g->player.box.vz *= ut * speed;

    for (int i = 0; i < step; i++)
        player_collide(&g->player);

}

static GLuint loadshader(GLenum type, const char *path)
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

static void loadshaders(void)
{

    GLuint program;
    GLuint shader1;
    GLuint shader2;

    program = glCreateProgram();
    shader1 = loadshader(GL_VERTEX_SHADER, "shaders/block_vertex.glsl");
    shader2 = loadshader(GL_FRAGMENT_SHADER, "shaders/block_fragment.glsl");

    glAttachShader(program, shader1);
    glAttachShader(program, shader2);
    glLinkProgram(program);
    glDetachShader(program, shader1);
    glDetachShader(program, shader2);
    glDeleteShader(shader1);
    glDeleteShader(shader2);

    g->block_attrib.program = program;
    g->block_attrib.position = glGetAttribLocation(program, "position");
    g->block_attrib.normal = glGetAttribLocation(program, "normal");
    g->block_attrib.uv = glGetAttribLocation(program, "uv");
    g->block_attrib.matrix = glGetUniformLocation(program, "matrix");
    g->block_attrib.sampler = glGetUniformLocation(program, "sampler");
    g->block_attrib.extra1 = glGetUniformLocation(program, "sky_sampler");
    g->block_attrib.extra2 = glGetUniformLocation(program, "daylight");
    g->block_attrib.extra3 = glGetUniformLocation(program, "fog_distance");
    g->block_attrib.extra4 = glGetUniformLocation(program, "ortho");
    g->block_attrib.camera = glGetUniformLocation(program, "camera");
    g->block_attrib.timer = glGetUniformLocation(program, "timer");

    program = glCreateProgram();
    shader1 = loadshader(GL_VERTEX_SHADER, "shaders/line_vertex.glsl");
    shader2 = loadshader(GL_FRAGMENT_SHADER, "shaders/line_fragment.glsl");

    glAttachShader(program, shader1);
    glAttachShader(program, shader2);
    glLinkProgram(program);
    glDetachShader(program, shader1);
    glDetachShader(program, shader2);
    glDeleteShader(shader1);
    glDeleteShader(shader2);

    g->line_attrib.program = program;
    g->line_attrib.position = glGetAttribLocation(program, "position");
    g->line_attrib.matrix = glGetUniformLocation(program, "matrix");

    program = glCreateProgram();
    shader1 = loadshader(GL_VERTEX_SHADER, "shaders/text_vertex.glsl");
    shader2 = loadshader(GL_FRAGMENT_SHADER, "shaders/text_fragment.glsl");

    glAttachShader(program, shader1);
    glAttachShader(program, shader2);
    glLinkProgram(program);
    glDetachShader(program, shader1);
    glDetachShader(program, shader2);
    glDeleteShader(shader1);
    glDeleteShader(shader2);

    g->text_attrib.program = program;
    g->text_attrib.position = glGetAttribLocation(program, "position");
    g->text_attrib.uv = glGetAttribLocation(program, "uv");
    g->text_attrib.matrix = glGetUniformLocation(program, "matrix");
    g->text_attrib.sampler = glGetUniformLocation(program, "sampler");

    program = glCreateProgram();
    shader1 = loadshader(GL_VERTEX_SHADER, "shaders/sky_vertex.glsl");
    shader2 = loadshader(GL_FRAGMENT_SHADER, "shaders/sky_fragment.glsl");

    glAttachShader(program, shader1);
    glAttachShader(program, shader2);
    glLinkProgram(program);
    glDetachShader(program, shader1);
    glDetachShader(program, shader2);
    glDeleteShader(shader1);
    glDeleteShader(shader2);

    g->sky_attrib.program = program;
    g->sky_attrib.position = glGetAttribLocation(program, "position");
    g->sky_attrib.normal = glGetAttribLocation(program, "normal");
    g->sky_attrib.uv = glGetAttribLocation(program, "uv");
    g->sky_attrib.matrix = glGetUniformLocation(program, "matrix");
    g->sky_attrib.sampler = glGetUniformLocation(program, "sampler");
    g->sky_attrib.timer = glGetUniformLocation(program, "timer");

}

static void loadtexture(const char *filename)
{

    unsigned char *data;
    unsigned char *new_data;
    unsigned int width;
    unsigned int height;
    unsigned int size;
    unsigned int stride;
    unsigned int i;

    lodepng_decode32_file(&data, &width, &height, filename);

    size = width * height * 4;
    stride = sizeof(char) * width * 4;
    new_data = malloc(sizeof(unsigned char) * size);

    for (i = 0; i < height; i++)
    {

        unsigned int j = height - i - 1;

        memcpy(new_data + j * stride, data + i * stride, stride);

    }

    memcpy(data, new_data, size);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
    free(new_data);
    free(data);

}

static void loadtextures(void)
{

    GLuint texture;

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    loadtexture("textures/texture.png");

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    loadtexture("textures/font.png");

    glGenTextures(1, &texture);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    loadtexture("textures/sky.png");

}

static void initrng(void)
{

    struct mtwist_state state;

    mtwist_seed1(&state, 1234);
    noise_seed(&state);

}

int main(int argc, char **argv)
{

    const GLFWvidmode *modes;
    GLFWmonitor *monitor;
    int mode_count;
    int winw;
    int winh;

    if (!glfwInit())
        return -1;

    monitor = glfwGetPrimaryMonitor();
    modes = glfwGetVideoModes(monitor, &mode_count);
    winw = modes[mode_count - 1].width;
    winh = modes[mode_count - 1].height;
    g->window = glfwCreateWindow(winw, winh, "Craft", monitor, NULL);

    glfwGetFramebufferSize(g->window, &g->width, &g->height);
    glfwMakeContextCurrent(g->window);
    glfwSwapInterval(VSYNC);
    glfwSetInputMode(g->window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwSetKeyCallback(g->window, onkey);
    glfwSetCharCallback(g->window, onchar);
    glfwSetMouseButtonCallback(g->window, onmousebutton);
    glfwSetScrollCallback(g->window, onscroll);

    if (glewInit() != GLEW_OK)
        return -1;

    glViewport(0, 0, g->width, g->height);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    glLogicOp(GL_INVERT);
    glClearColor(0, 0, 0, 1);
    loadtextures();
    loadshaders();
    initrng();

    double last_update = glfwGetTime();
    double previous = last_update;
    int running = 1;

    g->player.box.x = 0;
    g->player.box.y = 24;
    g->player.box.z = 0;
    g->day_length = DAY_LENGTH;
    g->fps = 0;
    g->frames = 0;
    g->since = 0;
    g->render_radius = RENDER_CHUNK_RADIUS;
    g->delete_radius = RENDER_CHUNK_RADIUS + 4;
    g->scale = g->width / winw;
    g->scale = MAX(1, g->scale);
    g->scale = MIN(2, g->scale);
    g->flying = 1;

    glfwSetTime(g->day_length / 3.0);

    GLuint sky_buffer = gen_sky_buffer();

    load_chunks(&g->player, g->render_radius, ((g->render_radius * 2) + 1) * ((g->render_radius * 2) + 1));

    while (running)
    {

        g->frames++;

        double now = glfwGetTime();
        double elapsed = now - g->since;
        double dt = now - previous;

        previous = now;

        if (now - last_update > 0.1)
            last_update = now;

        if (elapsed >= 1)
        {

            g->fps = round(g->frames / elapsed);
            g->frames = 0;
            g->since = now;

        }

        dt = MIN(dt, 0.2);
        dt = MAX(dt, 0.0);

        handle_movement(dt);
        delete_chunks();
        load_chunks(&g->player, 1, 9);
        load_chunks(&g->player, g->render_radius, 1);
        render_sky(&g->sky_attrib, &g->player, sky_buffer);
        render_chunks(&g->block_attrib, &g->player);
        render_crosshairs(&g->line_attrib);
        render_item(&g->block_attrib);

        char text_buffer[1024];
        float ts = 12 * g->scale;
        float tx = ts / 2;
        float ty = g->height - ts;
        int hour = time_of_day() * 24;
        char am_pm = hour < 12 ? 'a' : 'p';

        hour = hour % 12;
        hour = hour ? hour : 12;

        snprintf(text_buffer, 1024, "(%d, %d) (%.2f, %.2f, %.2f) %d%cm %dfps", chunked(g->player.box.x), chunked(g->player.box.z), g->player.box.x, g->player.box.y, g->player.box.z, hour, am_pm, g->fps);
        render_text(&g->text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);

        ty -= ts * 2;

        for (int i = 0; i < MAX_MESSAGES; i++)
        {

            int index = (g->message_index + i) % MAX_MESSAGES;

            if (strlen(g->messages[index]))
            {

                render_text(&g->text_attrib, ALIGN_LEFT, tx, ty, ts, g->messages[index]);

                ty -= ts * 2;

            }

        }

        if (g->typing)
        {

            snprintf(text_buffer, 1024, "> %s", g->typing_buffer);
            render_text(&g->text_attrib, ALIGN_LEFT, tx, ty, ts, text_buffer);

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
    glfwTerminate();

    return 0;

}

