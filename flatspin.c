#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "bmflat.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define GLSL(__source) "#version 150 core\n" #__source

#define _MAX_VERTICES   4096
static float _vertices[_MAX_VERTICES][5];
static int _vertices_count;

static int flatspin_init();
static void flatspin_update(float dt);

static const char *flatspin_bmspath;
static const char *flatspin_basepath;

static inline void add_vertex(float x, float y, float r, float g, float b)
{
    if (_vertices_count >= _MAX_VERTICES) {
        fprintf(stderr, "> <  Too many vertices!");
        return;
    }
    _vertices[_vertices_count][0] = x;
    _vertices[_vertices_count][1] = y;
    _vertices[_vertices_count][2] = r;
    _vertices[_vertices_count][3] = g;
    _vertices[_vertices_count++][4] = b;
}

static inline void add_rect(
    float x, float y, float w, float h,
    float r, float g, float b, bool highlight)
{
    add_vertex(x, y + h, r, g, b);
    add_vertex(x, y, r, g, b);
    add_vertex(x + w, y, r, g, b);
    add_vertex(x + w, y, r, g, b);
    add_vertex(x + w, y + h,
        highlight ? (r * 0.7 + 0.3) : r,
        highlight ? (g * 0.7 + 0.3) : g,
        highlight ? (b * 0.7 + 0.3) : b);
    add_vertex(x, y + h, r, g, b);
}

static inline GLuint load_shader(GLenum type, const char *source)
{
    GLuint shader_id = glCreateShader(type);
    glShaderSource(shader_id, 1, &source, NULL);
    glCompileShader(shader_id);

    GLint status;
    glGetShaderiv(shader_id, GL_COMPILE_STATUS, &status);
    char msg_buf[1024];
    glGetShaderInfoLog(shader_id, sizeof(msg_buf) - 1, NULL, msg_buf);
    fprintf(stderr, "OvO  Compilation log for %s shader\n",
        (type == GL_VERTEX_SHADER ? "vertex" :
         type == GL_FRAGMENT_SHADER ? "fragment" : "unknown (!)"));
    fputs(msg_buf, stderr);
    fprintf(stderr, "=v=  End\n");
    if (status != GL_TRUE) {
        fprintf(stderr, "> <  Shader compilation failed\n");
        return 0;
    }

    return shader_id;
}

static GLFWwindow *window; 

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "=~=  Usage: %s <path to BMS>\n", argv[0]);
        return 0;
    }

    // Extract path and executable path
    flatspin_bmspath = argv[1];
    int p = -1;
    for (int i = 0; flatspin_bmspath[i] != '\0'; i++)
        if (flatspin_bmspath[i] == '/' || flatspin_bmspath[i] == '\\') p = i;
    if (p == -1) {
        flatspin_basepath = "./";
    } else {
        flatspin_basepath = (char *)malloc(p + 1);
        memcpy(flatspin_basepath, flatspin_bmspath, p + 1);
    }
    fprintf(stderr, "^ ^  Asset search path: %s\n", flatspin_basepath);

    int result = flatspin_init();
    if (result != 0) return result;

    // -- Initialization --

    if (!glfwInit()) {
        fprintf(stderr, "> <  Cannot initialize GLFW\n");
        return 2;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_RESIZABLE, GL_FALSE);

    window = glfwCreateWindow(960, 540, "bmflatspin", NULL, NULL);
    if (window == NULL) {
        fprintf(stderr, "> <  Cannot create GLFW window\n");
        return 2;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        fprintf(stderr, "> <  Cannot initialize GLEW\n");
        return 2;
    }

    // -- Resource allocation --
    GLuint vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);

    const char *vshader_source = GLSL(
        in vec2 ppp;
        in vec3 qwq;
        out vec3 qwq_frag;
        void main()
        {
            gl_Position = vec4(ppp, 0.0, 1.0);
            qwq_frag = qwq;
        }
    );
    GLuint vshader = load_shader(GL_VERTEX_SHADER, vshader_source);

    const char *fshader_source = GLSL(
        in vec3 qwq_frag;
        out vec4 ooo;
        void main()
        {
            ooo = vec4(qwq_frag, 1.0f);
        }
    );
    GLuint fshader = load_shader(GL_FRAGMENT_SHADER, fshader_source);

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vshader);
    glAttachShader(prog, fshader);
    glBindFragDataLocation(prog, 0, "ooo");
    glLinkProgram(prog);
    glUseProgram(prog);

    GLuint ppp_attrib_index = glGetAttribLocation(prog, "ppp");
    glEnableVertexAttribArray(ppp_attrib_index);
    glVertexAttribPointer(ppp_attrib_index, 2, GL_FLOAT, GL_FALSE,
        5 * sizeof(float), (void *)0);

    GLuint qwq_attrib_index = glGetAttribLocation(prog, "qwq");
    glEnableVertexAttribArray(qwq_attrib_index);
    glVertexAttribPointer(qwq_attrib_index, 3, GL_FLOAT, GL_FALSE,
        5 * sizeof(float), (void *)(2 * sizeof(float)));

    // -- Event/render loop --

    float last_time = 0, cur_time;

    while (!glfwWindowShouldClose(window)) {
        glClearColor(0.7f, 0.7f, 0.7f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        cur_time = glfwGetTime();
        flatspin_update(cur_time - last_time);
        last_time = cur_time;

        glBufferData(GL_ARRAY_BUFFER,
            _vertices_count * 5 * sizeof(float), _vertices, GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, _vertices_count);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}

// -- Application logic --

static char *read_file(const char *path)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) return NULL;

    char *buf = NULL;

    do {
        if (fseek(f, 0, SEEK_END) != 0) break;
        long len = ftell(f);
        if (fseek(f, 0, SEEK_SET) != 0) break;
        if ((buf = (char *)malloc(len)) == NULL) break;
        if (fread(buf, len, 1, f) != 1) { free(buf); buf = NULL; break; }
    } while (0);

    fclose(f);
    return buf;
}

static int msgs_count;
static struct bm_chart chart;
static struct bm_seq seq;

#define SCRATCH_WIDTH   4
#define KEY_WIDTH       3
#define BGTRACK_WIDTH   2

#define HITLINE_POS     -0.2f

static float unit;

static float play_pos;
static float scroll_speed;
static float fwd_range;
static float bwd_range;
#define Y_POS(__pos)    (((__pos) - play_pos) * scroll_speed + HITLINE_POS)

static float delta_ss_rate; // For easing of scroll speed changes
static float delta_ss_time;
#define SS_MIN  (0.1f / 48)
#define SS_MAX  (1.0f / 48)
#define SS_DELTA    (0.05f / 48)
#define SS_INITIAL  (0.2f / 48)

static inline void delta_ss_step(float dt)
{
    if (delta_ss_time <= 0) return;
    if (dt > delta_ss_time) dt = delta_ss_time;
    scroll_speed += delta_ss_rate * dt;
    delta_ss_time -= dt;
    if (scroll_speed < SS_MIN) scroll_speed = SS_MIN;
    if (scroll_speed > SS_MAX) scroll_speed = SS_MAX;
    fwd_range = (1 - HITLINE_POS) / scroll_speed;
    bwd_range = (HITLINE_POS + 1) / scroll_speed;
}

static inline void delta_ss_submit(float delta, float time)
{
    float total_delta = delta + delta_ss_rate * delta_ss_time;
    delta_ss_rate = total_delta / time;
    delta_ss_time = time;
}

static int flatspin_init()
{
    char *src = read_file(flatspin_bmspath);
    if (src == NULL) {
        fprintf(stderr, "> <  Cannot load BMS file %s\n", flatspin_bmspath);
        return 1;
    }

    msgs_count = bm_load(&chart, src);
    bm_to_seq(&chart, &seq);

    unit = 2.0f / (SCRATCH_WIDTH + KEY_WIDTH * 7 +
        BGTRACK_WIDTH * chart.tracks.background_count);

    play_pos = 142 * 48;
    scroll_speed = SS_INITIAL;  // Screen Y units per 1/48 beat
    fwd_range = (1 - HITLINE_POS) / scroll_speed;
    bwd_range = (HITLINE_POS + 1) / scroll_speed;

    delta_ss_rate = delta_ss_time = 0;
}

static inline void track_attr(
    int id, float *x, float *w, float *r, float *g, float *b)
{
    if (id == 16) {
        *x = -1.0f;
        *w = unit * SCRATCH_WIDTH;
        *r = 1.0f;
        *g = 0.4f;
        *b = 0.3f;
    } else if (id >= 11 && id <= 19 && id != 17) {
        int i = (id < 17 ? id - 11 : id - 13);
        *x = -1.0f + unit * (SCRATCH_WIDTH + KEY_WIDTH * i);
        *w = unit * KEY_WIDTH;
        *r = i % 2 == 0 ? 1.0f : 0.5f;
        *g = i % 2 == 0 ? 1.0f : 0.5f;
        *b = i % 2 == 0 ? 1.0f : 1.0f;
    } else if (id <= 0) {
        int i = -id;
        *x = -1.0f + unit * (SCRATCH_WIDTH + KEY_WIDTH * 7 + BGTRACK_WIDTH * i);
        *w = unit * BGTRACK_WIDTH;
        *r = i % 2 == 0 ? 1.0f : 0.6f;
        *g = i % 2 == 0 ? 0.9f : 0.8f;
        *b = i % 2 == 0 ? 0.6f : 0.5f;
    }
}

static inline void draw_track_background(int id)
{
    float x, w, r, g, b;
    track_attr(id, &x, &w, &r, &g, &b);
    add_rect(x, -1, w, 2, r * 0.3, g * 0.3, b * 0.3, false);
}

static void flatspin_update(float dt)
{
    // -- Events --

    static int keys_prev[4] = {
        GLFW_RELEASE, GLFW_RELEASE, GLFW_RELEASE, GLFW_RELEASE
    };
    int keys[4] = {
        glfwGetKey(window, GLFW_KEY_UP),
        glfwGetKey(window, GLFW_KEY_DOWN),
        glfwGetKey(window, GLFW_KEY_LEFT),
        glfwGetKey(window, GLFW_KEY_RIGHT)
    };

    if (keys[2] == GLFW_PRESS && keys_prev[2] == GLFW_RELEASE) {
        delta_ss_submit(-SS_DELTA, 0.1);
    } else if (keys[3] == GLFW_PRESS && keys_prev[3] == GLFW_RELEASE) {
        delta_ss_submit(+SS_DELTA, 0.1);
    }

    if (keys[0] == GLFW_PRESS && keys[1] == GLFW_RELEASE) {
        play_pos += dt * 384 / (scroll_speed / SS_INITIAL);
    } else if (keys[1] == GLFW_PRESS && keys[0] == GLFW_RELEASE) {
        play_pos -= dt * 384 / (scroll_speed / SS_INITIAL);
    }

    memcpy(keys_prev, keys, sizeof keys);

    // -- Drawing --

    delta_ss_step(dt);

    _vertices_count = 0;

    for (int i = 11; i <= 19; i++)
        if (i != 17) draw_track_background(i);
    for (int i = 0; i < chart.tracks.background_count; i++)
        draw_track_background(-i);

    int start = 0;
    // TODO: Use binary search
    while (start < seq.event_count && seq.events[start].pos <= play_pos - bwd_range) start++;

    for (int i = start; i < seq.event_count && seq.events[i].pos <= play_pos + fwd_range; i++) {
        if (seq.events[i].type == BM_BARLINE) {
            add_rect(-1, Y_POS(seq.events[i].pos), 2, 0.01, 0.3, 0.3, 0.3, false);
            // TODO: Add bar number
        }
    }

    for (int i = start; i < seq.event_count && seq.events[i].pos <= play_pos + fwd_range; i++) {
        struct bm_event ev = seq.events[i];
        float x, w, r, g, b;
        switch (ev.type) {
        case BM_NOTE:
        case BM_NOTE_LONG:
            track_attr(ev.track, &x, &w, &r, &g, &b);
            add_rect(x, Y_POS(ev.pos), w,
                ev.type == BM_NOTE ? 0.02f :
                    0.02f + ev.value_a * scroll_speed,
                r, g, b, true);
            break;
        default: break;
        }
    }

    add_rect(-1, HITLINE_POS, 2, 0.01, 1.0, 0.7, 0.4, false);
}
