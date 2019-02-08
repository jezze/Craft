#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "lodepng.h"
#include "matrix.h"
#include "util.h"

GLuint gen_buffer(GLsizei size, GLfloat *data)
{

    GLuint buffer;
    glGenBuffers(1, &buffer);
    glBindBuffer(GL_ARRAY_BUFFER, buffer);
    glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    return buffer;

}

void del_buffer(GLuint buffer)
{

    glDeleteBuffers(1, &buffer);

}

GLfloat *malloc_faces(int components, int faces)
{

    return malloc(sizeof(GLfloat) * 6 * components * faces);

}

GLuint gen_faces(int components, int faces, GLfloat *data)
{

    GLuint buffer = gen_buffer(sizeof(GLfloat) * 6 * components * faces, data);

    free(data);

    return buffer;

}

GLuint load_shader(GLenum type, const char *path)
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

GLuint load_program(const char *path1, const char *path2)
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

void flip_image_vertical(unsigned char *data, unsigned int width, unsigned int height)
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

void load_png_texture(const char *file_name)
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

