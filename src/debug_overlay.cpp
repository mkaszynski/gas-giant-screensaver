#include "debug_overlay.hpp"
#include "surface.hpp"
#include <array>
#include <iomanip>
#include <sstream>

namespace observatory {
namespace {
GLuint compile(GLenum type, const char *source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[1024];
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    glDeleteShader(shader);
    throw std::runtime_error(log);
  }
  return shader;
}
// Original compact 5x7 UI glyphs, encoded as five vertical columns.
std::array<unsigned char, 5> glyph(char c) {
  constexpr std::array<std::array<unsigned char, 5>, 26> letters{
      {{126, 9, 9, 9, 126},   {127, 73, 73, 73, 54}, {62, 65, 65, 65, 34},
       {127, 65, 65, 65, 62}, {127, 73, 73, 73, 65}, {127, 9, 9, 9, 1},
       {62, 65, 73, 73, 58},  {127, 8, 8, 8, 127},   {65, 65, 127, 65, 65},
       {32, 64, 65, 63, 1},   {127, 8, 20, 34, 65},  {127, 64, 64, 64, 64},
       {127, 2, 12, 2, 127},  {127, 4, 8, 16, 127},  {62, 65, 65, 65, 62},
       {127, 9, 9, 9, 6},     {62, 65, 81, 33, 94},  {127, 9, 25, 41, 70},
       {38, 73, 73, 73, 50},  {1, 1, 127, 1, 1},     {63, 64, 64, 64, 63},
       {31, 32, 64, 32, 31},  {63, 64, 48, 64, 63},  {99, 20, 8, 20, 99},
       {7, 8, 112, 8, 7},     {97, 81, 73, 69, 67}}};
  constexpr std::array<std::array<unsigned char, 5>, 10> digits{
      {{62, 81, 73, 69, 62},
       {0, 66, 127, 64, 0},
       {98, 81, 73, 73, 70},
       {34, 65, 73, 73, 54},
       {24, 20, 18, 127, 16},
       {39, 69, 69, 69, 57},
       {62, 73, 73, 73, 50},
       {1, 113, 9, 5, 3},
       {54, 73, 73, 73, 54},
       {38, 73, 73, 73, 62}}};
  if (c >= 'A' && c <= 'Z')
    return letters[c - 'A'];
  if (c >= '0' && c <= '9')
    return digits[c - '0'];
  switch (c) {
  case ':':
    return {0, 0, 36, 0, 0};
  case '.':
    return {0, 64, 64, 0, 0};
  case '+':
    return {8, 8, 62, 8, 8};
  case '-':
    return {8, 8, 8, 8, 8};
  default:
    return {};
  }
}
} // namespace
DebugOverlay::DebugOverlay(double start, double daySeconds, bool open)
    : menu(start, daySeconds,
           stellarYearSeconds / simulationTimeScale(daySeconds), travelPeriod,
           open) {}
void DebugOverlay::initialize() {
  GLuint vertex =
      compile(GL_VERTEX_SHADER,
              "#version 300 es\nlayout(location=0) in vec2 "
              "p;layout(location=1) in vec4 c;"
              "uniform vec2 size;out vec4 tint;void main(){tint=c;"
              "gl_Position=vec4(p.x/size.x*2.-1.,1.-p.y/size.y*2.,0.,1.);}");
  GLuint fragment = compile(
      GL_FRAGMENT_SHADER,
      "#version 300 es\nprecision highp float;in vec4 tint;out vec4 color;"
      "void main(){color=tint;}");
  program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint ok;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok)
    throw std::runtime_error("Cannot link debug overlay");
  GLint oldVao, oldBuffer;
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &buffer);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), nullptr);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                        reinterpret_cast<void *>(2 * sizeof(float)));
  glBindVertexArray(oldVao);
  glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
  vertices.reserve(16000);
}
DebugOverlay::~DebugOverlay() {
  glDeleteBuffers(1, &buffer);
  glDeleteVertexArrays(1, &vao);
  glDeleteProgram(program);
}
void DebugOverlay::rectangle(DebugRect rect, float r, float g, float b,
                             float a) {
  Vertex tl{float(rect.x), float(rect.y), r, g, b, a};
  Vertex tr = tl, bl = tl, br = tl;
  tr.x = br.x = float(rect.x + rect.width);
  bl.y = br.y = float(rect.y + rect.height);
  vertices.insert(vertices.end(), {tl, bl, tr, tr, bl, br});
}
void DebugOverlay::text(double x, double y, double scale,
                        const std::string &s) {
  for (char c : s) {
    auto columns = glyph(c);
    for (int col = 0; col < 5; ++col)
      for (int row = 0; row < 7; ++row)
        if (columns[col] & (1 << row))
          rectangle({x + col * scale, y + row * scale, scale, scale}, .86, .94,
                    1.);
    x += 6 * scale;
  }
}
void DebugOverlay::draw(int w, int h, int ww, int wh, double real,
                        float opacity) {
  if (w <= 0 || h <= 0 || ww <= 0 || wh <= 0 || opacity <= 0 ||
      (!menu.open() && !menu.promptVisible(real)))
    return;
  if (!program)
    initialize();
  DebugLayout layout(ww, wh);
  auto &timeline = menu.timeline;
  const auto day = menu.day, year = menu.year;
  vertices.clear();
  if (menu.open()) {
    rectangle(layout.panel, .018, .028, .043, .90);
    std::array<std::string, 5> labels{timeline.playing() ? "PAUSE" : "PLAY",
                                      "DAY", "3 DAYS", "YEAR", "RESET"};
    for (int i = 0; i < 5; ++i) {
      auto button = layout.button(i);
      bool selected = (i == 1 && timeline.span() == day) ||
                      (i == 2 && timeline.span() == travelPeriod) ||
                      (i == 3 && timeline.span() == year);
      rectangle(button, selected ? .08 : .055, selected ? .28 : .09,
                selected ? .38 : .13);
      text(button.x +
               (button.width - labels[i].size() * 12 * layout.scale) / 2.,
           button.y + 5 * layout.scale, 2 * layout.scale, labels[i]);
    }
    const auto target = timeline.target(real);
    std::ostringstream label;
    label << (timeline.dragging() ? "SEEK " : "TIME ") << std::fixed
          << std::setprecision(1) << target << " S   RANGE "
          << timeline.span() / 3600. << " H";
    text(layout.panel.x + 8 * layout.scale, layout.panel.y + 46 * layout.scale,
         1.5 * layout.scale, label.str());
    auto bar = layout.slider;
    bar.y += 6 * layout.scale;
    bar.height = 6 * layout.scale;
    rectangle(bar, .12, .19, .24);
    const double knob = layout.slider.x + timeline.fraction(real) * bar.width;
    bar.width = knob - bar.x;
    rectangle(bar, .18, .72, .90);
    rectangle({knob - 4 * layout.scale, layout.slider.y, 8 * layout.scale,
               layout.slider.height},
              .8, .94, 1.);

    rectangle(layout.close(), .18, .09, .12);
    text(layout.close().x + 9 * layout.scale,
         layout.close().y + 5 * layout.scale, 2 * layout.scale, "X");
    text(layout.panel.x + 8 * layout.scale, layout.panel.y + 102 * layout.scale,
         1.5 * layout.scale, "WEATHER");
    for (int i = 0; i < int(weatherPresetNames.size()); ++i) {
      auto button = layout.weather(i);
      bool selected = static_cast<int>(menu.weather()) == i;
      rectangle(button, selected ? .08 : .055, selected ? .28 : .09,
                selected ? .38 : .13);
      text(button.x + 8 * layout.scale, button.y + 6 * layout.scale,
           2 * layout.scale, std::string(weatherPresetNames[i]));
    }
    text(layout.panel.x + 8 * layout.scale, layout.panel.y + 196 * layout.scale,
         1.5 * layout.scale, "AUTO RESTORES BIOME WEATHER");
  } else {
    auto button = layout.prompt(ww);
    rectangle(button, .018, .028, .043, .94);
    text(button.x + 10 * layout.scale, button.y + 9 * layout.scale,
         1.5 * layout.scale, "OPEN DEBUG MENU");
  }
  for (auto &v : vertices)
    v.a *= opacity;

  GLint oldProgram, oldVao, oldBuffer, viewport[4], srcRGB, dstRGB, srcA, dstA,
      eqRGB, eqA;
  glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
  glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
  glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetIntegerv(GL_BLEND_SRC_RGB, &srcRGB);
  glGetIntegerv(GL_BLEND_DST_RGB, &dstRGB);
  glGetIntegerv(GL_BLEND_SRC_ALPHA, &srcA);
  glGetIntegerv(GL_BLEND_DST_ALPHA, &dstA);
  glGetIntegerv(GL_BLEND_EQUATION_RGB, &eqRGB);
  glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &eqA);
  bool blend = glIsEnabled(GL_BLEND), depth = glIsEnabled(GL_DEPTH_TEST),
       scissor = glIsEnabled(GL_SCISSOR_TEST), cull = glIsEnabled(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_CULL_FACE);
  glEnable(GL_BLEND);
  glBlendEquation(GL_FUNC_ADD);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glViewport(0, 0, w, h);
  glUseProgram(program);
  glUniform2f(glGetUniformLocation(program, "size"), ww, wh);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, buffer);
  glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex),
               vertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, vertices.size());
  glUseProgram(oldProgram);
  glBindVertexArray(oldVao);
  glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glBlendFuncSeparate(srcRGB, dstRGB, srcA, dstA);
  glBlendEquationSeparate(eqRGB, eqA);
  for (auto [cap, enabled] :
       std::array<std::pair<GLenum, bool>, 4>{{{GL_BLEND, blend},
                                               {GL_DEPTH_TEST, depth},
                                               {GL_SCISSOR_TEST, scissor},
                                               {GL_CULL_FACE, cull}}}) {
    if (enabled)
      glEnable(cap);
    else
      glDisable(cap);
  }
}
} // namespace observatory
