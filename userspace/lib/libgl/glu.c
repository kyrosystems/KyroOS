#include "GL/glu.h"
#include <math.h>

#define PI 3.14159265358979323846

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar) {
    GLdouble ymax = zNear * tan(fovy * PI / 360.0);
    GLdouble ymin = -ymax;
    GLdouble xmin = ymin * aspect;
    GLdouble xmax = ymax * aspect;
    glFrustum(xmin, xmax, ymin, ymax, zNear, zFar);
}

void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
               GLdouble centerX, GLdouble centerY, GLdouble centerZ,
               GLdouble upX, GLdouble upY, GLdouble upZ) {
    GLfloat forward[3], side[3], up[3];
    GLfloat m[16];

    forward[0] = (GLfloat)(centerX - eyeX);
    forward[1] = (GLfloat)(centerY - eyeY);
    forward[2] = (GLfloat)(centerZ - eyeZ);

    GLfloat len = sqrtf(forward[0]*forward[0] + forward[1]*forward[1] + forward[2]*forward[2]);
    if (len > 0.0f) {
        forward[0] /= len; forward[1] /= len; forward[2] /= len;
    }

    up[0] = (GLfloat)upX; up[1] = (GLfloat)upY; up[2] = (GLfloat)upZ;

    // Side = forward x up
    side[0] = forward[1] * up[2] - forward[2] * up[1];
    side[1] = forward[2] * up[0] - forward[0] * up[2];
    side[2] = forward[0] * up[1] - forward[1] * up[0];

    len = sqrtf(side[0]*side[0] + side[1]*side[1] + side[2]*side[2]);
    if (len > 0.0f) {
        side[0] /= len; side[1] /= len; side[2] /= len;
    }

    // Up = side x forward
    up[0] = side[1] * forward[2] - side[2] * forward[1];
    up[1] = side[2] * forward[0] - side[0] * forward[2];
    up[2] = side[0] * forward[1] - side[1] * forward[0];

    m[0] = side[0];    m[4] = side[1];    m[8]  = side[2];    m[12] = 0.0f;
    m[1] = up[0];      m[5] = up[1];      m[9]  = up[2];      m[13] = 0.0f;
    m[2] = -forward[0];m[6] = -forward[1];m[10] = -forward[2];m[14] = 0.0f;
    m[3] = 0.0f;       m[7] = 0.0f;       m[11] = 0.0f;       m[15] = 1.0f;

    glMultMatrixf(m);
    glTranslatef((GLfloat)-eyeX, (GLfloat)-eyeY, (GLfloat)-eyeZ);
}

void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top) {
    glOrtho(left, right, bottom, top, -1.0, 1.0);
}
