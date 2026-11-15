#ifndef __OSMESA_H__
#define __OSMESA_H__

#include "gl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OSMESA_COLOR_INDEX      GL_COLOR_INDEX
#define OSMESA_RGBA             GL_RGBA
#define OSMESA_BGRA             0x1
#define OSMESA_ARGB             0x2
#define OSMESA_RGB              GL_RGB
#define OSMESA_BGR              0x4
#define OSMESA_ROW_LENGTH       0x10
#define OSMESA_Y_UP             0x11

typedef struct osmesa_context *OSMesaContext;

OSMesaContext OSMesaCreateContext(GLenum format, OSMesaContext sharelist);
GLboolean OSMesaMakeCurrent(OSMesaContext ctx, void *buffer, GLenum type,
                            GLsizei width, GLsizei height);
void OSMesaDestroyContext(OSMesaContext ctx);

#ifdef __cplusplus
}
#endif

#endif // __OSMESA_H__
