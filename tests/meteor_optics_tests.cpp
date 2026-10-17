#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
namespace {
constexpr int size = 64;
void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
GLuint compile(GLenum type, const std::string &source) {
  GLuint shader = glCreateShader(type);
  const char *text = source.c_str();
  glShaderSource(shader, 1, &text, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    throw std::runtime_error(log);
  }
  return shader;
}
std::vector<float> draw(GLuint program, double length, double angle,
                        double offset, double taper) {
  const double x = std::cos(angle) * length * .5,
               y = std::sin(angle) * length * .5;
  glUniform4f(glGetUniformLocation(program, "uTestLine"), 32 + offset - x,
              32.5 + offset * .63 - y, 32 + offset + x,
              32.5 + offset * .63 + y);
  glUniform1f(glGetUniformLocation(program, "uTestTaper"), taper);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  std::vector<float> pixels(size * size * 4);
  glReadPixels(0, 0, size, size, GL_RGBA, GL_FLOAT, pixels.data());
  require(glGetError() == GL_NO_ERROR, "meteor optical integration GL state");
  return pixels;
}
double energy(const std::vector<float> &p, int x0 = 0, int x1 = size,
              int y0 = 0, int y1 = size) {
  double total = 0;
  for (int y = y0; y < y1; ++y)
    for (int x = x0; x < x1; ++x) {
      const double value = p[(y * size + x) * 4];
      require(std::isfinite(value) && value >= 0,
              "optical energy stays finite and nonnegative");
      total += value;
    }
  return total;
}
} // namespace
int main() {
  GLFWwindow *window = nullptr;
  try {
    require(glfwInit(), "GLFW");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    window = glfwCreateWindow(size, size, "Meteor optics", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    std::ifstream input(Renderer::defaultDataDirectory() +
                        "/shaders/meteors.frag");
    std::ostringstream source;
    source << input.rdbuf();
    const auto text = source.str();
    const auto begin = text.find("float erfApprox"),
               end = text.find("void main");
    require(begin != std::string::npos && end > begin,
            "test executes the production meteor optical kernel");
    const auto vertex =
        compile(GL_VERTEX_SHADER,
                "#version 300 es\nvoid main(){vec2 p=vec2(float(gl_VertexID&1),"
                "float(gl_VertexID>>1));gl_Position=vec4(p*2.-1.,0.,1.);}");
    const auto fragment = compile(
        GL_FRAGMENT_SHADER,
        "#version 300 es\nprecision highp float;const float "
        "PI=3.141592653589793;\n" +
            text.substr(begin, end - begin) +
            "\nuniform vec4 uTestLine;uniform float uTestTaper;out vec4 "
            "color;\n"
            "void main(){float v=opticalLine(gl_FragCoord.xy,uTestLine.xy,"
            "uTestLine.zw,.16,uTestTaper);color=vec4(v,v,v,1.);}");
    const auto program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    require(linked, "optical test shader link");
    glUseProgram(program);
    GLuint framebuffer = 0, texture = 0, vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, size, size, 0, GL_RGBA, GL_FLOAT,
                 nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "floating point optical test target");
    glViewport(0, 0, size, size);
    double minimum = 1.e30, maximum = 0;
    for (double length : {0., .2, 1., 2., 3., 4., 5., 8., 12., 24.})
      for (double angle : {0., 15., 30., 45., 60., 75., 90.})
        for (double offset : {0., .25, .5, .75})
          for (double taper : {-.8, .8}) {
            const double flux =
                energy(draw(program, length, angle * pi / 180, offset, taper));
            minimum = std::min(minimum, flux);
            maximum = std::max(maximum, flux);
            if (std::abs(flux - 1) >= (length <= 1 ? .001 : .05))
              std::cerr << "Optical fixture length=" << length
                        << " angle=" << angle << " offset=" << offset
                        << " taper=" << taper << " energy=" << flux << '\n';
            require(std::abs(flux - 1) < (length <= 1 ? .001 : .05),
                    "pixel-integrated points and streaks retain unit energy "
                    "across orientations and subpixel positions");
          }
    const auto line = draw(program, 24, 0, 0, .88);
    const double total = energy(line);
    require(energy(line, 0, size, 32, 33) > total * .99,
            "an unresolved horizontal core fits into one centered pixel row");
    require(energy(line, 0, size, 0, 31) + energy(line, 0, size, 34, size) <
                .001,
            "ordinary meteors have no broad artificial glow");
    require(energy(line, 38, 44) > energy(line, 20, 26) * 3.5,
            "wake brightness tapers toward its older end");
    std::cout << "Meteor optical unit energy " << minimum << ".." << maximum
              << "; narrow core and tapered wake pass\n";
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &framebuffer);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    glfwDestroyWindow(window);
    glfwTerminate();
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    if (window)
      glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }
}
