#pragma once
#include <filesystem>
#include <fstream>

// Freeze the pre-optimization, separately expanded lookups. Image comparisons
// must not silently use the candidate's lookup loop on both sides.
inline void freezeWeatherQuadrature(const std::filesystem::path &root) {
  std::ofstream(root / "shaders/weather_quadrature.glsl") << R"GLSL(
float weatherStepDensity(vec3 d,float position,float stepLength,float lod,
                         inout float distance){
 if(stepLength<=.06)return marchDensity(d,distance,lod);
 float a=position+stepLength*.2113248654,b=position+stepLength*.7886751346;
 float da=marchDensity(d,a,lod),db=marchDensity(d,b,lod);
 float density=.5*(da+db);
 float moment=.5*(a*da+b*db),weight=smoothstep(.06,.12,stepLength);
 if(weight<1.){
  float middle=marchDensity(d,distance,lod);
  density=mix(middle,density,weight);
  moment=mix(distance*middle,moment,weight);
 }
 if(density>.00001)distance=moment/density;
 return density;
}
)GLSL";
}
