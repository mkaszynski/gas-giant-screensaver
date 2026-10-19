// Same-frame column reuse. Partial silhouettes and unresolved bright sources
// retain exact columns; complete opaque-ground columns use a stricter bound.
uniform highp sampler2D uWeatherSkyCache,uWeatherScene;
bool cachedSkyWeather(vec3 direction,out vec4 estimate){
 if(dot(direction,uSun)>.995)return false;
 ivec2 size=textureSize(uWeatherSkyCache,0);
#ifdef OBSERVATORY_WEATHER_COMPUTE
 vec2 q=weatherCachePixel;
#else
 vec2 q=uv*vec2(size)-.5;
#endif
 vec2 f=fract(q);
 ivec2 p=ivec2(floor(q));
 vec4 a=texelFetch(uWeatherSkyCache,clamp(p,ivec2(0),size-1),0);
 vec4 b=texelFetch(uWeatherSkyCache,clamp(p+ivec2(1,0),ivec2(0),size-1),0);
 vec4 c=texelFetch(uWeatherSkyCache,clamp(p+ivec2(0,1),ivec2(0),size-1),0);
 vec4 e=texelFetch(uWeatherSkyCache,clamp(p+ivec2(1,1),ivec2(0),size-1),0);
 vec4 low=min(min(a,b),min(c,e));
 // A negative transmission marks any ground coverage in the coarse draw.
#ifdef OBSERVATORY_WEATHER_COMPLETE_COLUMNS
 bool ground=uWeatherCompleteColumns&&weatherGroundColumn;
 highp uint signs=floatBitsToUint(a.a)|floatBitsToUint(b.a)|floatBitsToUint(c.a)|floatBitsToUint(e.a);
 if(!ground&&(signs&0x80000000u)!=0u)return false;
 if(ground){
  a.a=abs(a.a);b.a=abs(b.a);c.a=abs(c.a);e.a=abs(e.a);
  // Transparent corners cannot rule out a narrow wisp between cache rays.
  // Reuse only optically thick interiors; skins and clear columns stay exact.
  if(max(max(a.a,b.a),max(c.a,e.a))>.2)return false;
 }
#else
 if(low.a<0.)return false;
#endif
 vec3 background=texelFetch(uWeatherScene,ivec2(weatherPixelPosition()),0).rgb*(uExposure/uSceneScale);
#ifdef OBSERVATORY_WEATHER_COMPLETE_COLUMNS
 if(ground)background=texelFetch(uSurface,ivec2(weatherPixelPosition()),0).rgb*(uExposure/uSceneScale);
#endif
 // Tiny, intense sources need exact extinction even inside a smooth field.
 if(max(background.x,max(background.y,background.z))>16.)return false;
 // Project each emission/transmission pair together: their changes often
 // cancel over the actual background. Independent extremes invent contrast.
 vec3 pa=a.rgb*(uExposure/uSceneScale)+background*a.a;
 vec3 pb=b.rgb*(uExposure/uSceneScale)+background*b.a;
 vec3 pc=c.rgb*(uExposure/uSceneScale)+background*c.a;
 vec3 pe=e.rgb*(uExposure/uSceneScale)+background*e.a;
 vec3 minimum=min(min(pa,pb),min(pc,pe));
 vec3 maximum=max(max(pa,pb),max(pc,pe));
 vec3 relative=(maximum-minimum)/(minimum+.02);
 // Grazing rays retain the strict test: their longer optical paths contain
 // finer structure than the shorter columns higher in the sky.
 float tolerance=mix(.020,.240,smoothstep(.02,.06,direction.y));
 if(max(relative.x,max(relative.y,relative.z))>tolerance)return false;
#ifdef OBSERVATORY_WEATHER_COMPLETE_COLUMNS
 // For ACES followed by gamma, x*f'(x)<=2/2.2<1. Positive foreground fog
 // cannot increase relative contrast. Limit the projected neighbor range to
 // under half a display code even across an unknown fog attenuation/addition.
 if(ground&&any(greaterThan(maximum-minimum,max(minimum,vec3(1.e-6))*(.5/255.))))return false;
#endif
 estimate=mix(mix(a,b,f.x),mix(c,e,f.x),f.y);
 return true;
}
