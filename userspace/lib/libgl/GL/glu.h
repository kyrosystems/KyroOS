#ifndef __GLU_H__
#define __GLU_H__

#include "gl.h"

#ifdef __cplusplus
extern "C" {
#endif

void gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar);
void gluLookAt(GLdouble eyeX, GLdouble eyeY, GLdouble eyeZ,
               GLdouble centerX, GLdouble centerY, GLdouble centerZ,
               GLdouble upX, GLdouble upY, GLdouble upZ);
void gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top);

#ifdef __cplusplus
}
#endif

#endif // __GLU_H__
