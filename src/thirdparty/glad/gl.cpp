/**
 * glad.c - Minimal GLAD source.
 *
 * With system OpenGL (libGL) providing all entry points directly, no
 * runtime function-pointer loading is required. This translation unit is
 * compiled into the `glad` static library target so CMake targets that
 * link against `glad` continue to work.
 */

#include "glad.h"

/* Stub GLAD loader: system OpenGL provides all symbols directly at link
 * time, so gladLoadGLLoader is a no-op that always succeeds. */
bool gladLoadGLLoader(GLADloadproc load) {
    (void)load;
    return true;
}
