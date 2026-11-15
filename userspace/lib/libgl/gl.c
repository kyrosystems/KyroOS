#include "GL/gl.h"
#include <kyroolib.h>
#include <kyroos_gfx.h>
#include <math.h>
#include <string.h>

#define MAX_VERTICES 2048
#define STACK_DEPTH 16
#define MAX_TEXTURES 64

typedef struct {
    float x, y, z, w;
    float r, g, b, a;
    float u, v;
    float nx, ny, nz;
    float sx, sy, sz; // screen coords
} gl_vertex_t;

typedef struct {
    float m[16];
} gl_matrix_t;

typedef struct {
    GLuint id;
    int width, height;
    uint32_t *data;
} gl_texture_t;

typedef struct {
    // Framebuffer & Z-buffer
    uint32_t width, height;
    uint32_t *color_buffer;
    uint32_t *depth_buffer;

    // Viewport
    int vp_x, vp_y, vp_w, vp_h;

    // Matrix stacks
    GLenum matrix_mode;
    gl_matrix_t modelview_stack[STACK_DEPTH];
    int modelview_top;
    gl_matrix_t projection_stack[STACK_DEPTH];
    int projection_top;

    // Current state
    bool depth_test;
    bool lighting;
    bool texture_2d;
    bool cull_face;
    GLenum shade_model;

    // Clear values
    uint32_t clear_color;
    uint32_t clear_depth;

    // Current attributes
    float cur_r, cur_g, cur_b, cur_a;
    float cur_u, cur_v;
    float cur_nx, cur_ny, cur_nz;

    // Primitives
    GLenum begin_mode;
    bool in_begin;
    gl_vertex_t vertices[MAX_VERTICES];
    int vertex_count;

    // Textures
    gl_texture_t textures[MAX_TEXTURES];
    GLuint current_texture;

    // Light 0
    float light0_pos[4];
    float light0_diffuse[4];
} gl_context_t;

static gl_context_t gl_ctx;

// Matrix helpers
static void mat4_identity(float *m) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

static void mat4_mul(float *out, const float *a, const float *b) {
    float r[16];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            r[i*4 + j] = a[i*4 + 0] * b[0*4 + j] +
                        a[i*4 + 1] * b[1*4 + j] +
                        a[i*4 + 2] * b[2*4 + j] +
                        a[i*4 + 3] * b[3*4 + j];
        }
    }
    memcpy(out, r, 16 * sizeof(float));
}

int glInitKyroOS(void) {
    if (gfx_init() != 0) return -1;
    gl_ctx.width = gfx_get_width();
    gl_ctx.height = gfx_get_height();
    gl_ctx.color_buffer = gfx_get_buffer();

    size_t zbuf_size = gl_ctx.width * gl_ctx.height * sizeof(uint32_t);
    // Allocate dedicated depth buffer via sbrk syscall
    void *depth_mem = (void *)syscall(SYS_SBRK, (uint64_t)(zbuf_size + 4096), 0, 0);
    gl_ctx.depth_buffer = (uint32_t *)depth_mem;
    if (!gl_ctx.depth_buffer || (int64_t)gl_ctx.depth_buffer == -1) {
        gl_ctx.depth_buffer = gl_ctx.color_buffer;
    }

    gl_ctx.vp_x = 0; gl_ctx.vp_y = 0;
    gl_ctx.vp_w = gl_ctx.width; gl_ctx.vp_h = gl_ctx.height;

    gl_ctx.matrix_mode = GL_MODELVIEW;
    gl_ctx.modelview_top = 0;
    mat4_identity(gl_ctx.modelview_stack[0].m);

    gl_ctx.projection_top = 0;
    mat4_identity(gl_ctx.projection_stack[0].m);

    gl_ctx.depth_test = true;
    gl_ctx.lighting = false;
    gl_ctx.texture_2d = false;
    gl_ctx.cull_face = false;
    gl_ctx.shade_model = GL_SMOOTH;

    gl_ctx.clear_color = 0xFF000000; // Black
    gl_ctx.clear_depth = 0xFFFFFFFF; // Far

    gl_ctx.cur_r = 1.0f; gl_ctx.cur_g = 1.0f; gl_ctx.cur_b = 1.0f; gl_ctx.cur_a = 1.0f;
    gl_ctx.cur_u = 0.0f; gl_ctx.cur_v = 0.0f;
    gl_ctx.cur_nx = 0.0f; gl_ctx.cur_ny = 0.0f; gl_ctx.cur_nz = 1.0f;

    gl_ctx.in_begin = false;
    gl_ctx.vertex_count = 0;

    gl_ctx.light0_pos[0] = 0.0f; gl_ctx.light0_pos[1] = 0.0f;
    gl_ctx.light0_pos[2] = 1.0f; gl_ctx.light0_pos[3] = 0.0f;
    gl_ctx.light0_diffuse[0] = 1.0f; gl_ctx.light0_diffuse[1] = 1.0f;
    gl_ctx.light0_diffuse[2] = 1.0f; gl_ctx.light0_diffuse[3] = 1.0f;

    return 0;
}

void glClear(GLbitfield mask) {
    if (mask & GL_COLOR_BUFFER_BIT) {
        size_t count = gl_ctx.width * gl_ctx.height;
        for (size_t i = 0; i < count; i++) {
            gl_ctx.color_buffer[i] = gl_ctx.clear_color;
        }
    }
    if (mask & GL_DEPTH_BUFFER_BIT) {
        size_t count = gl_ctx.width * gl_ctx.height;
        for (size_t i = 0; i < count; i++) {
            gl_ctx.depth_buffer[i] = gl_ctx.clear_depth;
        }
    }
}

void glClearColor(GLclampf r, GLclampf g, GLclampf b, GLclampf a) {
    (void)a;
    uint32_t ir = (uint32_t)(r * 255.0f) & 0xFF;
    uint32_t ig = (uint32_t)(g * 255.0f) & 0xFF;
    uint32_t ib = (uint32_t)(b * 255.0f) & 0xFF;
    gl_ctx.clear_color = 0xFF000000 | (ir << 16) | (ig << 8) | ib;
}

void glClearDepth(GLclampd depth) {
    gl_ctx.clear_depth = (uint32_t)(depth * (double)0xFFFFFFFF);
}

void glEnable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) gl_ctx.depth_test = true;
    else if (cap == GL_LIGHTING) gl_ctx.lighting = true;
    else if (cap == GL_TEXTURE_2D) gl_ctx.texture_2d = true;
    else if (cap == GL_CULL_FACE) gl_ctx.cull_face = true;
}

void glDisable(GLenum cap) {
    if (cap == GL_DEPTH_TEST) gl_ctx.depth_test = false;
    else if (cap == GL_LIGHTING) gl_ctx.lighting = false;
    else if (cap == GL_TEXTURE_2D) gl_ctx.texture_2d = false;
    else if (cap == GL_CULL_FACE) gl_ctx.cull_face = false;
}

GLboolean glIsEnabled(GLenum cap) {
    if (cap == GL_DEPTH_TEST) return gl_ctx.depth_test;
    if (cap == GL_LIGHTING) return gl_ctx.lighting;
    if (cap == GL_TEXTURE_2D) return gl_ctx.texture_2d;
    if (cap == GL_CULL_FACE) return gl_ctx.cull_face;
    return GL_FALSE;
}

void glShadeModel(GLenum mode) { gl_ctx.shade_model = mode; }
void glCullFace(GLenum mode) { (void)mode; }
void glFrontFace(GLenum mode) { (void)mode; }
void glDepthFunc(GLenum func) { (void)func; }

void glViewport(GLint x, GLint y, GLsizei width, GLsizei height) {
    gl_ctx.vp_x = x; gl_ctx.vp_y = y;
    gl_ctx.vp_w = width; gl_ctx.vp_h = height;
}

static float *current_matrix(void) {
    if (gl_ctx.matrix_mode == GL_PROJECTION) {
        return gl_ctx.projection_stack[gl_ctx.projection_top].m;
    }
    return gl_ctx.modelview_stack[gl_ctx.modelview_top].m;
}

void glMatrixMode(GLenum mode) { gl_ctx.matrix_mode = mode; }

void glPushMatrix(void) {
    if (gl_ctx.matrix_mode == GL_PROJECTION) {
        if (gl_ctx.projection_top < STACK_DEPTH - 1) {
            gl_ctx.projection_top++;
            memcpy(gl_ctx.projection_stack[gl_ctx.projection_top].m,
                   gl_ctx.projection_stack[gl_ctx.projection_top - 1].m, 16 * sizeof(float));
        }
    } else {
        if (gl_ctx.modelview_top < STACK_DEPTH - 1) {
            gl_ctx.modelview_top++;
            memcpy(gl_ctx.modelview_stack[gl_ctx.modelview_top].m,
                   gl_ctx.modelview_stack[gl_ctx.modelview_top - 1].m, 16 * sizeof(float));
        }
    }
}

void glPopMatrix(void) {
    if (gl_ctx.matrix_mode == GL_PROJECTION) {
        if (gl_ctx.projection_top > 0) gl_ctx.projection_top--;
    } else {
        if (gl_ctx.modelview_top > 0) gl_ctx.modelview_top--;
    }
}

void glLoadIdentity(void) {
    mat4_identity(current_matrix());
}

void glLoadMatrixf(const GLfloat *m) {
    memcpy(current_matrix(), m, 16 * sizeof(float));
}

void glMultMatrixf(const GLfloat *m) {
    float *cur = current_matrix();
    mat4_mul(cur, cur, m);
}

void glTranslatef(GLfloat x, GLfloat y, GLfloat z) {
    float t[16];
    mat4_identity(t);
    t[12] = x; t[13] = y; t[14] = z;
    glMultMatrixf(t);
}

void glScalef(GLfloat x, GLfloat y, GLfloat z) {
    float s[16];
    mat4_identity(s);
    s[0] = x; s[5] = y; s[10] = z;
    glMultMatrixf(s);
}

void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z) {
    float rad = angle * 0.01745329251f;
    float c = cosf(rad);
    float s = sinf(rad);
    float len = sqrtf(x*x + y*y + z*z);
    if (len > 0.0f) { x /= len; y /= len; z /= len; }

    float r[16];
    r[0] = x*x*(1-c) + c;     r[4] = x*y*(1-c) - z*s;   r[8]  = x*z*(1-c) + y*s;   r[12] = 0.0f;
    r[1] = y*x*(1-c) + z*s;   r[5] = y*y*(1-c) + c;     r[9]  = y*z*(1-c) - x*s;   r[13] = 0.0f;
    r[2] = x*z*(1-c) - y*s;   r[6] = y*z*(1-c) + x*s;   r[10] = z*z*(1-c) + c;     r[14] = 0.0f;
    r[3] = 0.0f;              r[7] = 0.0f;              r[11] = 0.0f;              r[15] = 1.0f;

    glMultMatrixf(r);
}

void glOrtho(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val) {
    float m[16];
    mat4_identity(m);
    m[0]  = (float)(2.0 / (right - left));
    m[5]  = (float)(2.0 / (top - bottom));
    m[10] = (float)(-2.0 / (far_val - near_val));
    m[12] = (float)(-(right + left) / (right - left));
    m[13] = (float)(-(top + bottom) / (top - bottom));
    m[14] = (float)(-(far_val + near_val) / (far_val - near_val));
    glMultMatrixf(m);
}

void glFrustum(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble near_val, GLdouble far_val) {
    float m[16];
    memset(m, 0, 16 * sizeof(float));
    m[0]  = (float)((2.0 * near_val) / (right - left));
    m[5]  = (float)((2.0 * near_val) / (top - bottom));
    m[8]  = (float)((right + left) / (right - left));
    m[9]  = (float)((top + bottom) / (top - bottom));
    m[10] = (float)(-(far_val + near_val) / (far_val - near_val));
    m[11] = -1.0f;
    m[14] = (float)(-(2.0 * far_val * near_val) / (far_val - near_val));
    glMultMatrixf(m);
}

void glColor3f(GLfloat r, GLfloat g, GLfloat b) {
    gl_ctx.cur_r = r; gl_ctx.cur_g = g; gl_ctx.cur_b = b; gl_ctx.cur_a = 1.0f;
}
void glColor3ub(GLubyte r, GLubyte g, GLubyte b) {
    glColor3f(r / 255.0f, g / 255.0f, b / 255.0f);
}
void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) {
    gl_ctx.cur_r = r; gl_ctx.cur_g = g; gl_ctx.cur_b = b; gl_ctx.cur_a = a;
}
void glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) {
    glColor4f(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}
void glNormal3f(GLfloat nx, GLfloat ny, GLfloat nz) {
    gl_ctx.cur_nx = nx; gl_ctx.cur_ny = ny; gl_ctx.cur_nz = nz;
}
void glNormal3fv(const GLfloat *v) {
    glNormal3f(v[0], v[1], v[2]);
}
void glTexCoord2f(GLfloat u, GLfloat v) {
    gl_ctx.cur_u = u; gl_ctx.cur_v = v;
}

void glBegin(GLenum mode) {
    gl_ctx.begin_mode = mode;
    gl_ctx.in_begin = true;
    gl_ctx.vertex_count = 0;
}

// Transform vertex by ModelView and Projection
static void transform_vertex(gl_vertex_t *v) {
    const float *mv = gl_ctx.modelview_stack[gl_ctx.modelview_top].m;
    const float *pr = gl_ctx.projection_stack[gl_ctx.projection_top].m;

    // ModelView transform
    float ex = mv[0]*v->x + mv[4]*v->y + mv[8]*v->z + mv[12];
    float ey = mv[1]*v->x + mv[5]*v->y + mv[9]*v->z + mv[13];
    float ez = mv[2]*v->x + mv[6]*v->y + mv[10]*v->z + mv[14];
    float ew = mv[3]*v->x + mv[7]*v->y + mv[11]*v->z + mv[15];

    // Projection transform
    float cx = pr[0]*ex + pr[4]*ey + pr[8]*ez + pr[12]*ew;
    float cy = pr[1]*ex + pr[5]*ey + pr[9]*ez + pr[13]*ew;
    float cz = pr[2]*ex + pr[6]*ey + pr[10]*ez + pr[14]*ew;
    float cw = pr[3]*ex + pr[7]*ey + pr[11]*ez + pr[15]*ew;

    if (fabsf(cw) > 1e-6f) {
        float ndc_x = cx / cw;
        float ndc_y = cy / cw;
        float ndc_z = cz / cw;

        // Viewport mapping
        v->sx = (ndc_x + 1.0f) * 0.5f * gl_ctx.vp_w + gl_ctx.vp_x;
        v->sy = (1.0f - ndc_y) * 0.5f * gl_ctx.vp_h + gl_ctx.vp_y;
        v->sz = (ndc_z + 1.0f) * 0.5f;
    } else {
        v->sx = 0; v->sy = 0; v->sz = 1.0f;
    }
}

void glVertex3f(GLfloat x, GLfloat y, GLfloat z) {
    if (!gl_ctx.in_begin || gl_ctx.vertex_count >= MAX_VERTICES) return;
    gl_vertex_t *v = &gl_ctx.vertices[gl_ctx.vertex_count++];
    v->x = x; v->y = y; v->z = z; v->w = 1.0f;
    v->r = gl_ctx.cur_r; v->g = gl_ctx.cur_g; v->b = gl_ctx.cur_b; v->a = gl_ctx.cur_a;
    v->u = gl_ctx.cur_u; v->v = gl_ctx.cur_v;
    v->nx = gl_ctx.cur_nx; v->ny = gl_ctx.cur_ny; v->nz = gl_ctx.cur_nz;
    transform_vertex(v);
}
void glVertex2f(GLfloat x, GLfloat y) { glVertex3f(x, y, 0.0f); }
void glVertex3fv(const GLfloat *v) { glVertex3f(v[0], v[1], v[2]); }

// Forward declaration from rasterizer.c
void gl_rasterize_triangle(const gl_vertex_t *v0, const gl_vertex_t *v1, const gl_vertex_t *v2);
void gl_rasterize_line(const gl_vertex_t *v0, const gl_vertex_t *v1);

void glEnd(void) {
    if (!gl_ctx.in_begin) return;
    gl_ctx.in_begin = false;

    if (gl_ctx.begin_mode == GL_TRIANGLES) {
        for (int i = 0; i + 2 < gl_ctx.vertex_count; i += 3) {
            gl_rasterize_triangle(&gl_ctx.vertices[i], &gl_ctx.vertices[i+1], &gl_ctx.vertices[i+2]);
        }
    } else if (gl_ctx.begin_mode == GL_QUADS) {
        for (int i = 0; i + 3 < gl_ctx.vertex_count; i += 4) {
            gl_rasterize_triangle(&gl_ctx.vertices[i], &gl_ctx.vertices[i+1], &gl_ctx.vertices[i+2]);
            gl_rasterize_triangle(&gl_ctx.vertices[i], &gl_ctx.vertices[i+2], &gl_ctx.vertices[i+3]);
        }
    } else if (gl_ctx.begin_mode == GL_LINES) {
        for (int i = 0; i + 1 < gl_ctx.vertex_count; i += 2) {
            gl_rasterize_line(&gl_ctx.vertices[i], &gl_ctx.vertices[i+1]);
        }
    }
}

void glGenTextures(GLsizei n, GLuint *textures) {
    for (GLsizei i = 0; i < n; i++) {
        textures[i] = i + 1;
    }
}

void glBindTexture(GLenum target, GLuint texture) {
    (void)target;
    gl_ctx.current_texture = texture;
}

void glTexImage2D(GLenum target, GLint level, GLint internalFormat,
                  GLsizei width, GLsizei height, GLint border,
                  GLenum format, GLenum type, const GLvoid *pixels) {
    (void)target; (void)level; (void)internalFormat; (void)border; (void)format; (void)type;
    if (gl_ctx.current_texture > 0 && gl_ctx.current_texture < MAX_TEXTURES) {
        gl_texture_t *tex = &gl_ctx.textures[gl_ctx.current_texture];
        tex->width = width; tex->height = height;
        tex->data = (uint32_t *)pixels;
    }
}

void glLightfv(GLenum light, GLenum pname, const GLfloat *params) {
    (void)light;
    if (pname == GL_POSITION) {
        memcpy(gl_ctx.light0_pos, params, 4 * sizeof(float));
    } else if (pname == GL_DIFFUSE) {
        memcpy(gl_ctx.light0_diffuse, params, 4 * sizeof(float));
    }
}

void glFlush(void) {}
void glFinish(void) {}

void glSwapBuffers(void) {
    gfx_flip();
}
