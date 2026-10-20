#pragma once
#include "emissions.hpp"
#include "graphics_quality.hpp"
#include "meteors.hpp"
#include "surface_renderer.hpp"
#include "system.hpp"
#include "weather.hpp"
#include <GLES3/gl3.h>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
namespace observatory {
struct StarfieldOptions {
  bool galaxy = true, stars = true;
  bool compressed = true;
};
class Renderer {
public:
  explicit Renderer(std::string dataDirectory, StarfieldOptions = {},
                    SurfaceOptions = {}, std::optional<GraphicsSettings> = {});
  ~Renderer();
  Renderer(const Renderer &) = delete;
  Renderer &operator=(const Renderer &) = delete;
  // A negative weatherSeconds follows the emission clock (when enabled),
  // otherwise scene time. Explicit weather time remains independent of the sky.
  void render(int width, int height, double seconds, double daySeconds = 1800,
              float opacity = 1, bool drawBodies = true, bool drawRings = true,
              EmissionFrame emission = {}, double weatherSeconds = -1);
  void renderScene(int width, int height, const Scene &, double seconds,
                   double daySeconds = 1800, float opacity = 1,
                   bool drawBodies = true, bool drawRings = true,
                   EmissionFrame emission = {}, double weatherSeconds = -1);
  // A host can retire viewport-dependent storage before allocating a new size.
  // Wait for previous GPU work before calling, then retire deletions before draw.
  bool releaseSizeTargetsFor(int width, int height);
  std::array<int, 2> renderResolution() const {
    return {scene.width, scene.height};
  }
  static std::string defaultDataDirectory();
  std::array<int, 2> weatherResolution() const {
    return {weather.width, weather.height};
  }
  std::array<int, 2> weatherSkyCacheResolution() const {
    return {skyWeatherCache.width, skyWeatherCache.height};
  }
  void setWeatherPreset(WeatherPreset preset) { weatherPreset = preset; }
  void setAtmosphericOptics(bool enabled) {
    surfaceOptions.atmosphericOptics = enabled;
  }
  void setWhitecaps(bool enabled) { surfaceOptions.whitecaps = enabled; }
  // Diagnostic reference for detecting clipped filtering or atmospheric glow.
  void setCelestialScissor(bool enabled) { celestialScissor = enabled; }
  void setMeteorFrame(MeteorFrame frame) { meteorFrame = frame; }
  bool meteorDarkEnough() const { return meteorDark; }
  bool floatingWeatherNoise() const { return floatingNoise; }
  bool computingWeather() const { return weatherComputeProgram != 0; }
  bool computingWeatherCache() const { return weatherSkyComputeProgram != 0; }
  bool specializingFragmentSky() const { return weatherSkyFragmentProgram != 0; }
  int clearWeatherRows() const { return weatherClearRows; }
  bool computingCalmWeather() const {
    return weatherCalmComputeProgram && weatherState.storm == 0. &&
           weatherState.flashRadiance == 0.;
  }
  size_t surfaceMeshRevision() const {
    return surface ? surface->meshRevision() : 0;
  }

  size_t surfaceShadowRevision() const {
    return surface ? surface->shadowRevision() : 0;
  }

  size_t surfaceUploadBytes() const {
    return surface ? surface->uploadBytes() : 0;
  }

  size_t surfaceReflectionRevision() const {
    return surface ? surface->reflectionRevision() : 0;
  }

  size_t surfaceBuildsStarted() const {
    return surface ? surface->buildsStarted() : 0;
  }

  bool surfaceBuildPending() const {
    return surface && surface->buildPending();
  }

private:
  struct Target {
    GLuint texture = 0, framebuffer = 0, depth = 0, skyTexture = 0;
    int width = 0, height = 0;
    GLenum format = 0;
    bool immutable = false;
  };
  std::optional<GraphicsSettings> graphics;
  std::string root;
  StarfieldOptions starfield;
  SurfaceOptions surfaceOptions;
  std::unique_ptr<SurfaceRenderer> surface;
  TravelLocation surfaceLocation{};
  double lastAltitude = -1e30;
  WeatherState weatherState;
  std::array<float, 2> whitecapState{0.f, 1.f};
  WeatherPreset weatherPreset = WeatherPreset::Auto;
  MeteorFrame meteorFrame;
  Vec3 previewRadiant{};
  Scene meteorField;
  double meteorFieldAspect = 16. / 9.;
  uint64_t meteorWindowId = uint64_t(-1);
  MeteorMode lastMeteorMode = MeteorMode::Off;
  double lastMeteorPreview = -1, lastMeteorInterval = -1;
  uint64_t lastMeteorRestart = 0;
  bool meteorDark = false;
  GLuint meteorProgram = 0, meteorVao = 0, meteorBuffer = 0;
  std::vector<float> meteorVertices;
  bool floatingNoise = false;
  std::vector<GLuint> programs, textures;
  GLuint transProgram = 0, multiProgram = 0, skyProgram = 0,
         backgroundProgram = 0, bodyProgram = 0, starProgram = 0,
         postProgram = 0, presentationProgram = 0, ringProgram = 0,
         giantProgram = 0, giantLimbProgram = 0, emissionProgram = 0,
         weatherProgram = 0, weatherLightProgram = 0, weatherRainProgram = 0,
         weatherSkyFragmentProgram = 0;
  GLuint weatherComputeProgram = 0, weatherCalmComputeProgram = 0,
         weatherSkyComputeProgram = 0, weatherSkyCalmComputeProgram = 0,
         weatherPixelProgram = 0, weatherClearComputeProgram = 0;
  int weatherClearRows = 0;
  GLuint giant = 0, mountains = 0, noise = 0, ringProfile = 0, giantSlab = 0,
         terrainNormal = 0, terrainGeometry = 0, horizonA = 0, horizonB = 0,
         galaxy = 0, vao = 0, starBuffer = 0, starVao = 0, emissionBuffer = 0,
         emissionVao = 0, weatherNoiseTexture = 0, weatherLightTexture = 0,
         weatherLightFbo = 0, opticsPhaseTexture = 0;
  Target trans, multi, sky, scene, weather, skyWeatherCache, rainDensityCache,
      composite;
  Target weatherPixels, weatherSkyPixels;
  double lastSky = -1e30, lastDay = 0;
  double lastVisibility = -1;
  Vec3 lastSun;
  bool lastRings = false;
  int stars = 0;
  std::string shader(const std::string &);
  GLuint program(const std::string &, const std::string & = "fullscreen.vert",
                 int giantAtmosphere = 0, std::string_view defines = {});
  GLuint imageTexture(const std::string &);
  GLuint galaxyTexture();
  void target(Target &, int, int, bool depth = false, bool skyLayer = false,
              GLenum format = GL_RGBA16F, bool immutable = false);
  GLuint createWeatherComputeProgram(bool calm = false, bool skyPass = false,
                                     bool noVolume = false);
  void weatherPixelCoordinates(Target &, int, int, int, int);
  void dispatchWeather(GLuint, const Target &, const Target &, int firstRow = 0,
                       int rows = -1);
  void release(Target &);
  void bind(GLuint, int, const char *, GLuint);
  void common(GLuint, const Scene &, int, int);
  void cloudUniforms(GLuint, const Scene &, double);
  void weatherUniforms(GLuint, const Scene &);
  bool needsFloatingWeatherNoise();
  void rings(GLuint, const Scene &, bool enabled);
  void quad(GLuint);
  void drawMeteors(const Scene &, int, int, double daySeconds);
  void revealMeteors(const Scene &, int, int, GLuint surfaceTexture);
  void emissions(const Scene &, int, int, EmissionFrame, bool ringsEnabled,
                 double daySeconds);
  bool celestialScissor = true;
};
void savePng(const std::string &path, int w, int h);
} // namespace observatory
