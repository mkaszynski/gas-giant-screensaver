#define GLFW_INCLUDE_ES3
#include "renderer.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace observatory;
namespace {
void require(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
std::string read(const std::string &file) {
  std::ifstream input(std::filesystem::path(Renderer::defaultDataDirectory()) /
                      "shaders" / file);
  require(bool(input), "shader source exists");
  std::ostringstream s;
  s << input.rdbuf();
  return s.str();
}
GLuint compile(GLenum type, const std::string &source) {
  auto shader = glCreateShader(type);
  auto p = source.c_str();
  glShaderSource(shader, 1, &p, nullptr);
  glCompileShader(shader);
  GLint ok;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[8192];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    throw std::runtime_error(log);
  }
  return shader;
}
GLuint program(bool reference) {
  auto atmosphere = read("giant_atmosphere.glsl");
  if (reference) {
    auto at = atmosphere.find("const int steps=6;");
    require(at != std::string::npos, "reference sample count marker");
    atmosphere.replace(at, 18, "const int steps=192;");
  }
  std::string source = R"(#version 300 es
precision highp float;
out vec4 color;
const float PI=3.141592653589793;
uniform vec3 uSun;
uniform vec4 uBody;
uniform int uOccluderCount;
uniform vec4 uOccluders[21];
uniform vec4 uProbe;
uniform int uMode;
)" + read("sphere_shadow.glsl") +
                       read("ring_profile.glsl") + atmosphere + R"(
void main(){
 if(uMode==1){
   vec3 p=vec3(0.,0.,1.+uProbe.x);
   vec3 direction=vec3(sqrt(max(0.,1.-uProbe.y*uProbe.y)),0.,uProbe.y);
   color=vec4(giantColumn(p,direction),0.,0.,1.);return;
 }
 float b=uProbe.x;
 vec3 d=vec3(b/3.,0.,sqrt(1.-b*b/9.));
 vec3 L;float a;giantAtmosphere(d,b,vec3(uProbe.y),1.,L,a);
 color=vec4(L,a);
})";
  auto vs = compile(GL_VERTEX_SHADER, R"(#version 300 es
void main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.-1.,0.,1.);})");
  auto fs = compile(GL_FRAGMENT_SHADER, source);
  auto p = glCreateProgram();
  glAttachShader(p, vs);
  glAttachShader(p, fs);
  glLinkProgram(p);
  GLint ok;
  glGetProgramiv(p, GL_LINK_STATUS, &ok);
  require(ok, "probe program link");
  glDeleteShader(vs);
  glDeleteShader(fs);
  return p;
}
std::array<float, 4> probe(GLuint p, float impact, Vec3 sun,
                           bool blocker = false, float cloud = 0, int mode = 0,
                           float mu = 0) {
  glUseProgram(p);
  glUniform4f(glGetUniformLocation(p, "uBody"), 0, 0, 3, 1);
  glUniform3f(glGetUniformLocation(p, "uSun"), sun.x, sun.y, sun.z);
  glUniform4f(glGetUniformLocation(p, "uProbe"), impact, mode ? mu : cloud, 0,
              0);
  glUniform1i(glGetUniformLocation(p, "uMode"), mode);
  glUniform1i(glGetUniformLocation(p, "uOccluderCount"), blocker ? 1 : 0);
  glUniform4f(glGetUniformLocation(p, "uOccluders[0]"), sun.x * 10, sun.y * 10,
              3 + sun.z * 10, 2);
  glDrawArrays(GL_TRIANGLES, 0, 3);
  std::array<float, 4> value{};
  glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, value.data());
  for (auto x : value)
    require(std::isfinite(x) && x >= 0,
            "finite nonnegative physical radiance/opacity");
  return value;
}
double numericColumn(double height, double mu) {
  // Independent high-resolution integration of the actual spherical density.
  constexpr double H = .0001905;
  double r = 1 + height,
         distance = -r * mu + std::sqrt(r * r * mu * mu +
                                        (1 + 20 * H) * (1 + 20 * H) - r * r);
  constexpr int n = 20000;
  double sum = 0;
  for (int i = 0; i < n; ++i) {
    double t = distance * (i + .5) / n;
    sum += std::exp(-(std::sqrt(r * r + t * t + 2 * r * mu * t) - 1) / H);
  }
  return sum * distance / n / H;
}
} // namespace
int main() {
  try {
    require(glfwInit(), "GLFW init");
    glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_ES_API);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    auto window =
        glfwCreateWindow(16, 16, "Atmosphere physics", nullptr, nullptr);
    require(window, "GLES context");
    glfwMakeContextCurrent(window);
    GLuint target, texture, vao;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 1, 1, 0, GL_RGBA, GL_FLOAT,
                 nullptr);
    glGenFramebuffers(1, &target);
    glBindFramebuffer(GL_FRAMEBUFFER, target);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           texture, 0);
    require(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE,
            "float probe framebuffer");
    glViewport(0, 0, 1, 1);
    auto p = program(false), reference = program(true);
    double maxColumnError = 0;
    for (double h : {0., .0001905, .000762})
      for (double mu : {0., .01, .05, .3, 1.}) {
        auto value = probe(p, h, {}, false, 0, 1, mu)[0];
        double expected = numericColumn(h, mu),
               relative = std::abs(value / expected - 1);
        maxColumnError = std::max(maxColumnError, relative);
      }
    std::cout << "Curved column max relative error: " << maxColumnError << '\n';
    require(maxColumnError < .012,
            "Chapman column agrees with spherical numerical reference");
    const Vec3 contact = normalized(Vec3{1. / 3., 0, std::sqrt(8. / 9.)});
    auto rim = probe(p, 1.00015, contact);
    auto dark = probe(p, 1.00015, {0, 0, 1});
    auto shadow = probe(p, 1.00015, contact, true);
    auto outside = probe(p, 1.003, contact);
    auto day = probe(p, 0, {0, 0, -1}, false, .1);
    std::cout << "Contact rim RGB: " << rim[0] << ", " << rim[1] << ", "
              << rim[2] << "; opacity " << rim[3] << '\n';
    require(rim[2] > rim[0] && rim[2] > .0001 && rim[2] < .12,
            "restrained molecular blue scattering at contact");
    require(dark[0] + dark[1] + dark[2] < 1.e-7,
            "no luminous halo during a central eclipse");
    require(shadow[0] + shadow[1] + shadow[2] < 1.e-7,
            "moon shadow extinguishes atmospheric direct scattering");
    require(outside[0] + outside[1] + outside[2] + outside[3] == 0,
            "no artificially enlarged atmosphere");
    require(day[0] > .095 && day[2] < .105,
            "daytime cloud color is only subtly affected");
    GLuint ringProfile;
    glGenTextures(1, &ringProfile);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, ringProfile);
    const float ringData[2] = {1., 1.};
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, 1, 1, 0, GL_RG, GL_FLOAT,
                 ringData);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glUseProgram(p);
    glUniform1i(glGetUniformLocation(p, "uRingProfile"), 1);
    glUniform1i(glGetUniformLocation(p, "uRingsEnabled"), 1);
    glUniform3f(glGetUniformLocation(p, "uRingCenter"), 0, 0, 3);
    glUniform3f(glGetUniformLocation(p, "uRingNormal"), -.5, 0, std::sqrt(.75));
    glUniform2f(glGetUniformLocation(p, "uRingBounds"), ringInner, ringOuter);
    auto ringShadow = probe(p, 1.00015, contact);
    std::cout << "Ring-shadow atmospheric transmission: "
              << ringShadow[2] / rim[2] << '\n';
    require(
        ringShadow[2] > .01 * rim[2] && ringShadow[2] < .5 * rim[2],
        "translucent rings attenuate atmosphere without making shadows opaque");
    glUniform1i(glGetUniformLocation(p, "uRingsEnabled"), 0);
    glDeleteTextures(1, &ringProfile);
    double maxError = 0;
    for (double b : {.99, .995, .999, 1., 1.00015, 1.0006, 1.0015})
      for (double angle : {-.03, 0., .03, .3, 2.8}) {
        Vec3 sun = normalized(Vec3{std::sin(std::asin(1. / 3.) + angle), 0,
                                   std::cos(std::asin(1. / 3.) + angle)});
        auto a = probe(p, b, sun), ref = probe(reference, b, sun);
        for (int c = 0; c < 3; ++c)
          maxError = std::max(maxError, double(std::abs(a[c] - ref[c])));
        require(a[3] <= 1., "transmission remains energy bounded");
      }
    std::cout << "6 vs 192 steps maximum radiance error: " << maxError << '\n';
    require(maxError < .006,
            "limb integration converges to high-sample reference");
    require(glGetError() == GL_NO_ERROR, "atmosphere GL errors");
    glDeleteProgram(p);
    glDeleteProgram(reference);
    glDeleteFramebuffers(1, &target);
    glDeleteTextures(1, &texture);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(window);
    glfwTerminate();
    std::cout << "Atmospheric column, phase, scale, extinction and eclipse "
                 "tests passed\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    glfwTerminate();
    return 1;
  }
}
