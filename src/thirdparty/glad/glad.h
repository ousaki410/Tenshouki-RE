/**
 * glad.h - Local minimal GLAD wrapper for OpenGL function loading.
 *
 * The project uses FetchContent to download GLAD from GitHub, which fails
 * without network access. This local wrapper exposes the system OpenGL
 * headers (gl.h + glext.h) under the glad include path so the rest of the
 * codebase can keep including <glad/glad.h> unchanged.
 *
 * No GLAD_* feature macros are used anywhere in the codebase, so a plain
 * pass-through to the system OpenGL headers is sufficient.
 */

#pragma once

#ifndef GLAD_GL_H_
#define GLAD_GL_H_

/* Declare OpenGL extension function prototypes (glGenVertexArrays, etc.)
 * directly from the system glext.h so we don't need runtime function
 * pointer loading. System libGL provides the symbols at link time. */
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES 1
#endif

#include <GL/gl.h>
#include <GL/glext.h>

#endif /* GLAD_GL_H_ */

/* Stub GLAD loader API: system OpenGL provides all symbols directly,
 * so gladLoadGLLoader is a no-op that always succeeds. */
#ifndef GLAD_GL_API_H_
#define GLAD_GL_API_H_

#include <cstddef>

typedef void (*GLADloadproc)(void);

bool gladLoadGLLoader(GLADloadproc load);

#endif /* GLAD_GL_API_H_ */
