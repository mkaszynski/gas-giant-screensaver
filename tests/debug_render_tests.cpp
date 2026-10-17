#define GLFW_INCLUDE_ES3
#include "debug_overlay.hpp"
#include <GLFW/glfw3.h>
#include <array>
#include <iostream>
using namespace observatory;
void require(bool b, const char *message) {
  if (!b)
    throw std::runtime_error(message);
}
std::array<unsigned char, 4> pixel(int x, int y) {
  std::array<unsigned char, 4> p;
  glReadPixels(x, y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, p.data());
  return p;
}
int main() {
  GLFWwindow *window = nullptr;
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window =
        glfwCreateWindow(1280, 720, "Debug overlay tests", nullptr, nullptr);
    require(window, "debug GLES context");
    glfwMakeContextCurrent(window);
    {
      DebugOverlay controls(900, 1800);
      DebugOverlay hover(900, 1800, false);
      glClearColor(.3, .2, .1, 1);
      glClear(GL_COLOR_BUFFER_BIT);
      const auto sky = pixel(640, 600);
      const auto terrain = pixel(320, 40);
      const auto corner = pixel(1260, 695);
      hover.draw(1280, 720, 1280, 720, 0);
      require(
          pixel(1260, 695) == corner && pixel(30, 690) == sky,
          "hidden menu draws nothing, including before lazy GL initialization");
      hover.menu.pointer(1280, 720, 1279, 1, false, 0);
      hover.draw(1280, 720, 1280, 720, 0);
      require(pixel(1260, 695) != corner && pixel(320, 40) == terrain,
              "corner invitation is visible without covering landscape");
      glClear(GL_COLOR_BUFFER_BIT);
      glViewport(4, 5, 600, 350);
      glEnable(GL_CULL_FACE);
      glEnable(GL_DEPTH_TEST);
      glEnable(GL_SCISSOR_TEST);
      glScissor(0, 0, 1, 1);
      glBlendFuncSeparate(GL_ONE, GL_ZERO, GL_ZERO, GL_ONE);
      glBlendEquationSeparate(GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT);
      controls.draw(1280, 720, 1280, 720, 129600);
      require(pixel(640, 600) == sky, "debug does not change upper scene");
      require(pixel(320, 40) == terrain, "debug leaves landscape unobscured");
      DebugLayout layout(1280, 720);
      int y = int(720 - layout.slider.y - layout.slider.height / 2);
      auto knob = pixel(int(layout.slider.x + layout.slider.width / 2), y),
           track = pixel(int(layout.slider.x + layout.slider.width * .9), y),
           fill = pixel(int(layout.slider.x + layout.slider.width * .25), y);
      require(knob[0] > 190 && knob[2] > 240,
              "time slider knob reaches midpoint");
      require(fill[2] > 200 && track[2] < 100,
              "filled and unfilled slider coverage");
      require(glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_CULL_FACE) &&
                  glIsEnabled(GL_SCISSOR_TEST) && !glIsEnabled(GL_BLEND),
              "overlay restores host enables");
      GLint viewport[4], equation, srcAlpha;
      glGetIntegerv(GL_VIEWPORT, viewport);
      glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &equation);
      glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcAlpha);
      require(viewport[0] == 4 && viewport[1] == 5 && viewport[2] == 600 &&
                  viewport[3] == 350 && equation == GL_FUNC_REVERSE_SUBTRACT &&
                  srcAlpha == GL_ZERO,
              "overlay restores viewport and separate blending");
      glDisable(GL_SCISSOR_TEST);
      glClear(GL_COLOR_BUFFER_BIT);
      // 2x framebuffer density: cursor/layout remain in logical coordinates.
      controls.draw(1280, 720, 640, 360, 129600);
      DebugLayout scaled(640, 360);
      auto scaledKnob =
          pixel(int((scaled.slider.x + scaled.slider.width / 2) * 2),
                int((360 - scaled.slider.y - scaled.slider.height / 2) * 2));
      require(scaledKnob[0] > 190 && scaledKnob[2] > 240,
              "slider renders correctly at 2x display density");
      // Weather selection has a visible selected state, and hiding is exact.
      DebugLayout logical(640, 360);
      auto storm = logical.weather(5);
      controls.menu.pointer(640, 360, storm.x + 2, storm.y + 2, true, 129600);
      controls.menu.pointer(640, 360, storm.x + 2, storm.y + 2, false, 129600);
      glClear(GL_COLOR_BUFFER_BIT);
      controls.draw(1280, 720, 640, 360, 129600);
      auto selected =
          pixel(int((storm.x + 2) * 2), int((360 - storm.y - 2) * 2));
      require(selected[1] > 55 && selected[2] > 75,
              "weather selection is visibly highlighted at HiDPI");
      auto close = logical.close();
      controls.menu.pointer(640, 360, close.x + 2, close.y + 2, true, 129601);
      controls.menu.pointer(640, 360, close.x + 2, close.y + 2, false, 129601);
      glClear(GL_COLOR_BUFFER_BIT);
      controls.draw(1280, 720, 640, 360, 129603);
      require(pixel(1260, 695) == corner,
              "closed menu leaves no pixels behind");
      require(glGetError() == GL_NO_ERROR, "debug GL errors");
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Debug overlay GPU: slider, unobscured sky, HiDPI and GL "
                 "state pass\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    if (window)
      glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
}
