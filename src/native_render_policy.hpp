#pragma once
#include <cstdlib>

namespace observatory {
inline void configureNativeRenderPolicy() {
#if defined(__linux__) && !defined(__EMSCRIPTEN__)
  // Animated uniforms cause RadeonSI to compile several literal-specialized
  // graphics variants while drawing. Its background compilers compete with
  // rendering on shared-power APUs. Keep normal shader optimization, but use
  // the general uniform path instead. This is a process-local Mesa option;
  // other drivers ignore it and an explicit operator override takes priority.
  ::setenv("radeonsi_inline_uniforms", "false", 0);
#endif
}
} // namespace observatory
