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
    int p;
    int q;
    int faces;
    int dirty;
    GLuint buffer;

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
    int faces[6] = {1, 1, 1, 1, 1, 1};
    float ao[6][4] = {
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0},
        {0.0, 0.0, 0.0, 0.0}
    };
    float light[6][4] = {
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5},
        {0.5, 0.5, 0.5, 0.5}
    };

    make_cube(data, ao, light, faces, blocks[w], x, y, z, n);

    return gen_buffer(sizeof(data), data);

}

static GLuint gen_plant_buffer(float x, float y, float z, float n, int w)
{

    GLfloat data[240];

    make_plant(data, 0.0, 1.0, x, y, z, n, w, 45);

    return gen_buffer(sizeof(data), data);

}

static GLuint gen_text_buffer(float x, float y, float n, char *text)
{

    int length = strlen(text);

    GLfloat *data = malloc(sizeof(GLfloat) * 24 * length);
    GLuint buffer;

    for (int i = 0; i < length; i++)
    {

        make_character(data + i * 24, x, y, n / 2, n, text[i]);

        x += n;

    }

    buffer = gen_buffer(sizeof(GLfloat) * 24 * length, data);

    free(data);

    return buffer;

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

static void aabbsweep(Box *b1, Box *b2, float *newx, float *newy, float *newz)
{

    float xInvEntry, yInvEntry, zInvEntry;
    float xInvExit, yInvExit, zInvExit;
    float xEntry, yEntry, zEntry;
    float xExit, yExit, zExit;
    float normalx, normaly, normalz;

    if (b1->vx > 0.0f)
    {

        xInvEntry = b2->x - (b1->x + b1->lx);
        xInvExit = (b2->x + b2->lx) - b1->x;

    }

    else
    {

        xInvEntry = (b2->x + b2->lx) - b1->x;
        xInvExit = b2->x - (b1->x + b1->lx);

    }

    if (b1->vy > 0.0f)
    {

        yInvEntry = b2->y - (b1->y + b1->ly);
        yInvExit = (b2->y + b2->ly) - b1->y;

    }

    else
    {

        yInvEntry = (b2->y + b2->ly) - b1->y;
        yInvExit = b2->y - (b1->y + b1->ly);

    }

    if (b1->vz > 0.0f)
    {

        zInvEntry = b2->z - (b1->z + b1->lz);
        zInvExit = (b2->z + b2->lz) - b1->z;

    }

    else
    {

        zInvEntry = (b2->z + b2->lz) - b1->z;
        zInvExit = b2->z - (b1->z + b1->lz);

    }

    if (b1->vx == 0.0f)
    {

        xEntry = 0;
        xExit = 0;

    }

    else
    {

        xEntry = xInvEntry / b1->vx;
        xExit = xInvExit / b1->vx;

    }

    if (b1->vy == 0.0f)
    {

        yEntry = 0;
        yExit = 0;

    }

    else
    {

        yEntry = yInvEntry / b1->vy;
        yExit = yInvExit / b1->vy;

    }

    if (b1->vz == 0.0f)
    {

        zEntry = 0;
        zExit = 0;

    }

    else
    {

        zEntry = zInvEntry / b1->vz;
        zExit = zInvExit / b1->vz;

    }

    float entryTime = MAX(MAX(xEntry, yEntry), zEntry);
    float exitTime = MIN(MIN(xExit, yExit), zExit);

    normalx = 0.0f;
    normaly = 0.0f;
    normalz = 0.0f;

    if (xEntry > yEntry && xEntry > zEntry)
    {

        if (xInvEntry < 0.0f)
            normalx = 1.0f;
        else
            normalx = -1.0f;

    }

    else if (yEntry > xEntry && yEntry > zEntry)
    {

        if (yInvEntry < 0.0f)
            normaly = 1.0f;
        else
            normaly = -1.0f;

    }

    else if (zEntry > xEntry && zEntry > yEntry)
    {

        if (zInvEntry < 0.0f)
            normalz = 1.0f;
        else
            normalz = -1.0f;

    }

    if (normalx)
        *newx = 0;

    if (normaly)
        *newy = 0;

    if (normalz)
        *newz = 0;

}

static void player_collide(Player *player, int x, int y, int z)
{

    Box box;
    Box block;

    box.x = player->box.x + player->box.vx + 0.2;
    box.y = player->box.y + player->box.vy;
    box.z = player->box.z + player->box.vz + 0.2;
    box.lx = 0.6;
    box.ly = 1.0;
    box.lz = 0.6;
    box.vx = player->box.vx;
    box.vy = player->box.vy;
    box.vz = player->box.vz;
    block.x = x;
    block.y = y;
    block.z = z;
    block.lx = 1.0;
    block.ly = 1.0;
    block.lz = 1.0;
    block.vx = 0.0;
    block.vy = 0.0;
    block.vz = 0.0;

    float newx = player->box.vx;
    float newy = player->box.vy;
    float newz = player->box.vz;

    for (int kx = -1; kx <= 1; kx++)
    {

        for (int ky = -1; ky <= 1; ky++)
        {

            for (int kz = -1; kz <= 1; kz++)
            {

                Chunk *chunk;

                block.x = x + kx;
                block.y = y + ky;
                block.z = z + kz;
                chunk = find_chunk(chunked(block.x), chunked(block.z));

                if (!chunk)
                    continue;

                if (!is_obstacle(map_get(&chunk->map, block.x, block.y, block.z)))
                    continue;

                if (!aabbcheck(&box, &block))
                    continue;

                aabbsweep(&box, &block, &newx, &newy, &newz);

            }

        }

    }

    player->box.x += newx;
    player->box.y += newy;
    player->box.z += newz;

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

static void compute_chunk(Chunk *chunk)
{

    Map *map = &chunk->map;
    char *opaque = (char *)calloc(XZ_SIZE * XZ_SIZE * Y_SIZE, sizeof(char));
    int ox = chunk->p * CHUNK_SIZE - 1;
    int oy = -1;
    int oz = chunk->q * CHUNK_SIZE - 1;
    int offset = 0;
    unsigned int i;
    GLfloat *data;

    for (i = 0; i <= map->mask; i++)
    {

        MapEntry *entry = map->data + i;
        int x, y, z;

        if (entry->value == 0)
            continue;

        if (entry->e.w <= 0)
            continue;

        x = entry->e.x + map->dx - ox;
        y = entry->e.y + map->dy - oy;
        z = entry->e.z + map->dz - oz;

        opaque[XYZ(x, y, z)] = !is_transparent(entry->e.w);

    }

    chunk->faces = 0;

    for (i = 0; i <= map->mask; i++)
    {

        MapEntry *entry = map->data + i;
        int total;

        if (entry->value == 0)
            continue;

        if (entry->e.w <= 0)
            continue;

        if (is_plant(entry->e.w))
        {

            total = 4;

        }

        else
        {

            int x = entry->e.x + map->dx - ox;
            int y = entry->e.y + map->dy - oy;
            int z = entry->e.z + map->dz - oz;
            int faces[6];

            faces[0] = !opaque[XYZ(x - 1, y, z)];
            faces[1] = !opaque[XYZ(x + 1, y, z)];
            faces[2] = !opaque[XYZ(x, y + 1, z)];
            faces[3] = !opaque[XYZ(x, y - 1, z)];
            faces[4] = !opaque[XYZ(x, y, z - 1)];
            faces[5] = !opaque[XYZ(x, y, z + 1)];
            total = faces[0] + faces[1] + faces[2] + faces[3] + faces[4] + faces[5];

        }

        chunk->faces += total;

    }

    data = malloc(sizeof(GLfloat) * 60 * chunk->faces);

    for (i = 0; i <= map->mask; i++)
    {

        MapEntry *entry = map->data + i;
        int ex, ey, ez;
        int total;

        if (entry->value == 0)
            continue;

        if (entry->e.w <= 0)
            continue;

        ex = entry->e.x + map->dx;
        ey = entry->e.y + map->dy;
        ez = entry->e.z + map->dz;

        if (is_plant(entry->e.w))
        {

            float rotation = noise_simplex2(ex, ez, 4, 0.5, 2) * 360;

            total = 4;

            make_plant(data + offset, 0.0, 1.0, ex, ey, ez, 0.5, entry->e.w, rotation);

        }

        else
        {

            int x = entry->e.x + map->dx - ox;
            int y = entry->e.y + map->dy - oy;
            int z = entry->e.z + map->dz - oz;
            int faces[6];

            float ao[6][4] = {
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0}
            };

            float light[6][4] = {
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0},
                {0.0, 0.0, 0.0, 0.0}
            };

            faces[0] = !opaque[XYZ(x - 1, y, z)];
            faces[1] = !opaque[XYZ(x + 1, y, z)];
            faces[2] = !opaque[XYZ(x, y + 1, z)];
            faces[3] = !opaque[XYZ(x, y - 1, z)];
            faces[4] = !opaque[XYZ(x, y, z - 1)];
            faces[5] = !opaque[XYZ(x, y, z + 1)];
            total = faces[0] + faces[1] + faces[2] + faces[3] + faces[4] + faces[5];

            if (total == 0)
                continue;

            make_cube(data + offset, ao, light, faces, blocks[entry->e.w], ex, ey, ez, 0.5);

        }

        offset += total * 60;

    }

    del_buffer(chunk->buffer);

    chunk->buffer = gen_buffer(sizeof(GLfloat) * 60 * chunk->faces, data);

    free(data);
    free(opaque);

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

    chunk->p = p;
    chunk->q = q;
    chunk->faces = 0;
    chunk->buffer = 0;
    chunk->dirty = 1;

    map_alloc(&chunk->map, chunk->p * CHUNK_SIZE, 0, chunk->q * CHUNK_SIZE, 0x7fff);
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

                chunk->dirty = 0;

                if (--max <= 0)
                    return;

            }

        }

    }

}

static int getblock(int x, int y, int z)
{

    Chunk *chunk = find_chunk(chunked(x), chunked(z));

    return (chunk) ? map_get(&chunk->map, x, y, z) : 0;

}

static void setblock(int x, int y, int z, int w)
{

    Chunk *chunk = find_chunk(chunked(x), chunked(z));

    if (chunk && map_set(&chunk->map, x, y, z, w))
        chunk->dirty = 1;

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

        if (is_plant(getblock(hx, hy + 1, hz)))
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
            addblock();

    }

    if (button == GLFW_MOUSE_BUTTON_MIDDLE)
    {

        if (exclusive)
            selectblock();

    }

}

static void handle_movement(void)
{

    int exclusive = glfwGetInputMode(g->window, GLFW_CURSOR) == GLFW_CURSOR_DISABLED;
    float speed = g->flying ? 0.18 : 0.1;
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

    g->player.box.vx *= speed;
    g->player.box.vy *= speed;
    g->player.box.vz *= speed;

    player_collide(&g->player, g->player.box.x, g->player.box.y, g->player.box.z);

    g->player.box.vx = 0;
    g->player.box.vy = 0;
    g->player.box.vz = 0;


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

        if (now - last_update > 0.1)
            last_update = now;

        if (elapsed >= 1)
        {

            g->fps = round(g->frames / elapsed);
            g->frames = 0;
            g->since = now;

        }

        handle_movement();
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

    }

    del_buffer(sky_buffer);
    delete_all_chunks();
    glfwTerminate();

    return 0;

}

