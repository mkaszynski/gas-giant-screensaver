// Same-frame reuse is restricted to sky. Ground, mixed MSAA coverage and bright
// unresolved sources keep exact optical columns at the display pixel center.
uniform highp sampler2D uWeatherSkyCache,uWeatherScene;
bool cachedSkyWeather(out vec4 estimate){
 ivec2 size=textureSize(uWeatherSkyCache,0);
 vec2 q=uv*vec2(size)-.5,f=fract(q);
 ivec2 p=ivec2(floor(q));
 vec4 a=texelFetch(uWeatherSkyCache,clamp(p,ivec2(0),size-1),0);
 vec4 b=texelFetch(uWeatherSkyCache,clamp(p+ivec2(1,0),ivec2(0),size-1),0);
 vec4 c=texelFetch(uWeatherSkyCache,clamp(p+ivec2(0,1),ivec2(0),size-1),0);
 vec4 e=texelFetch(uWeatherSkyCache,clamp(p+ivec2(1,1),ivec2(0),size-1),0);
 vec4 low=min(min(a,b),min(c,e)),high=max(max(a,b),max(c,e));
 // A negative transmission marks any ground coverage in the coarse draw.
 if(low.a<0.||high.a-low.a>.008)return false;
 vec3 contrast=(high.rgb-low.rgb)*(uExposure/uSceneScale);
 if(max(contrast.x,max(contrast.y,contrast.z))>.010)return false;
 vec3 source=texelFetch(uWeatherScene,ivec2(gl_FragCoord.xy),0).rgb*(uExposure/uSceneScale);
 if(max(source.x,max(source.y,source.z))>2.)return false;
 estimate=mix(mix(a,b,f.x),mix(c,e,f.x),f.y);
 return true;
}
