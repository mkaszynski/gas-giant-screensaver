// Same-frame reuse is restricted to sky. Ground, mixed MSAA coverage and bright
// unresolved sources keep exact optical columns at the display pixel center.
uniform highp sampler2D uWeatherSkyCache,uWeatherScene;
bool cachedSkyWeather(vec3 direction,out vec4 estimate){
 if(dot(direction,uSun)>.995)return false;
 ivec2 size=textureSize(uWeatherSkyCache,0);
 vec2 q=uv*vec2(size)-.5,f=fract(q);
 ivec2 p=ivec2(floor(q));
 vec4 a=texelFetch(uWeatherSkyCache,clamp(p,ivec2(0),size-1),0);
 vec4 b=texelFetch(uWeatherSkyCache,clamp(p+ivec2(1,0),ivec2(0),size-1),0);
 vec4 c=texelFetch(uWeatherSkyCache,clamp(p+ivec2(0,1),ivec2(0),size-1),0);
 vec4 e=texelFetch(uWeatherSkyCache,clamp(p+ivec2(1,1),ivec2(0),size-1),0);
 vec4 low=min(min(a,b),min(c,e));
 // A negative transmission marks any ground coverage in the coarse draw.
 if(low.a<0.)return false;
 vec3 background=texelFetch(uWeatherScene,ivec2(weatherPixelPosition()),0).rgb*(uExposure/uSceneScale);
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
 estimate=mix(mix(a,b,f.x),mix(c,e,f.x),f.y);
 return true;
}
