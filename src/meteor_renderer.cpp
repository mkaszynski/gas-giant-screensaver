#include "renderer.hpp"
#include <algorithm>
namespace observatory {
void Renderer::drawMeteors(const Scene &s, int width, int height, double day) {
  meteorVertices.clear();
  meteorDark = meteorNight(s) > .01;
  const bool preview = meteorFrame.options.mode == MeteorMode::Preview;
  if ((!preview && !meteorDark) || meteorFrame.seconds < 0 ||
      meteorFrame.options.mode == MeteorMode::Off)
    return;
  const auto window =
      meteorWindowAt(meteorFrame.seconds, meteorFrame.options.intervalHours);
  if (!preview && (meteorFrame.seconds < window.start ||
                   meteorFrame.seconds > window.start + window.duration + 6))
    return;
  if (lastMeteorMode != meteorFrame.options.mode ||
      (!preview && lastMeteorInterval != meteorFrame.options.intervalHours) ||
      (preview ? meteorFrame.options.previewStart != lastMeteorPreview ||
                     meteorFrame.options.restart != lastMeteorRestart
               : window.id != meteorWindowId)) {
    meteorField = s;
    meteorFieldAspect = double(width) / height;
    if (preview)
      previewRadiant =
          meteor_detail::world(s, normalized(s.forward * .65 + Vec3{0, .8, 0}));
    lastMeteorPreview = meteorFrame.options.previewStart;
    lastMeteorRestart = meteorFrame.options.restart;
    lastMeteorInterval = meteorFrame.options.intervalHours;
    meteorWindowId = window.id;
    lastMeteorMode = meteorFrame.options.mode;
  }
  const auto batch = meteorsAt(meteorFrame, s, meteorFieldAspect, day,
                               previewRadiant, &meteorField);
  if (!batch.count)
    return;
  if (!meteorProgram) {
    meteorProgram = program("meteors.frag", "meteors.vert");
    glGenVertexArrays(1, &meteorVao);
    glGenBuffers(1, &meteorBuffer);
    glBindVertexArray(meteorVao);
    glBindBuffer(GL_ARRAY_BUFFER, meteorBuffer);
    for (int i = 0; i < 3; ++i) {
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i, i == 0 ? 2 : 4, GL_FLOAT, GL_FALSE,
                            10 * sizeof(float),
                            reinterpret_cast<void *>(size_t(i == 0   ? 0
                                                            : i == 1 ? 2
                                                                     : 6) *
                                                     sizeof(float)));
    }
    meteorVertices.reserve(batch.segments.size() * 6 * 10);
  }
  struct Pixel {
    double x, y;
  };
  const double focal = height / (2 * std::tan(29 * pi / 180));
  auto project = [&](Vec3 p) {
    const double z = dot(p, s.forward);
    return Pixel{width * .5 + dot(p, s.right) / z * focal,
                 height * .5 + dot(p, s.cameraUp) / z * focal};
  };
  for (size_t i = 0; i < batch.count; ++i) {
    const auto &segment = batch.segments[i];
    if (dot(segment.tail, s.forward) < 1 || dot(segment.head, s.forward) < 1)
      continue;
    const auto a = project(segment.tail), b = project(segment.head);
    if (std::max(a.x, b.x) < -10 || std::min(a.x, b.x) > width + 10 ||
        std::max(a.y, b.y) < -10 || std::min(a.y, b.y) > height + 10)
      continue;
    const double length = std::hypot(b.x - a.x, b.y - a.y);
    const Pixel along = length > .001
                            ? Pixel{(b.x - a.x) / length, (b.y - a.y) / length}
                            : Pixel{1, 0};
    const Pixel across{-along.y, along.x};
    auto corner = [&](Pixel p, double end, double side) {
      return Pixel{p.x + 8 * (end * along.x + side * across.x),
                   p.y + 8 * (end * along.y + side * across.y)};
    };
    const auto tl = corner(a, -1, 1), bl = corner(a, -1, -1),
               tr = corner(b, 1, 1), br = corner(b, 1, -1);
    const Vec3 flux =
        segment.color * (segment.flux * .8 * std::pow(height / 1080., 2));
    for (auto p : {tl, bl, tr, tr, bl, br})
      meteorVertices.insert(
          meteorVertices.end(),
          {float(p.x), float(p.y), float(a.x), float(a.y), float(b.x),
           float(b.y), float(flux.x), float(flux.y), float(flux.z),
           float(std::max(.4, segment.sigma * height / 1080.))});
  }
  if (meteorVertices.empty())
    return;
  common(meteorProgram, s, width, height);
  glUniform1i(glGetUniformLocation(meteorProgram, "uMeteorManual"), preview);
  glUniform1i(glGetUniformLocation(meteorProgram, "uMeteorReveal"), false);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
  glBindVertexArray(meteorVao);
  glBindBuffer(GL_ARRAY_BUFFER, meteorBuffer);
  glBufferData(GL_ARRAY_BUFFER, meteorVertices.size() * sizeof(float),
               meteorVertices.data(), GL_STREAM_DRAW);
  glDrawArrays(GL_TRIANGLES, 0, meteorVertices.size() / 10);
  glDisable(GL_BLEND);
}
void Renderer::revealMeteors(const Scene &s, int width, int height,
                             GLuint surfaceTexture) {
  if (meteorFrame.options.mode != MeteorMode::Preview || meteorVertices.empty())
    return;
  // The manual trigger is a visibility override. The scene already contains
  // the meteor: add only its cloud-blocked fraction to the weather radiance.
  // Destination alpha is transmission, so blending provides (1-T) without
  // sampling a framebuffer attachment or allocating another render target.
  common(meteorProgram, s, width, height);
  glUniform1i(glGetUniformLocation(meteorProgram, "uMeteorManual"), true);
  glUniform1i(glGetUniformLocation(meteorProgram, "uMeteorReveal"), true);
  bind(meteorProgram, 15, "uMeteorSurface", surfaceTexture);
  glUniform1f(glGetUniformLocation(meteorProgram, "uMeteorSurfaceFraction"),
              surface->coverageFraction());
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFuncSeparate(GL_ONE_MINUS_DST_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO,
                      GL_ONE);
  glBindVertexArray(meteorVao);
  glBindBuffer(GL_ARRAY_BUFFER, meteorBuffer);
  glDrawArrays(GL_TRIANGLES, 0, meteorVertices.size() / 10);
  glDisable(GL_BLEND);
}
} // namespace observatory
