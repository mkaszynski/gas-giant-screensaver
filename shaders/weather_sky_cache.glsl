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
 vec4 low=min(min(a,b),min(c,e)),high=max(max(a,b),max(c,e));
 // A negative transmission marks any ground coverage in the coarse draw.
 if(low.a<0.)return false;
 vec3 background=texelFetch(uWeatherScene,ivec2(gl_FragCoord.xy),0).rgb*(uExposure/uSceneScale);
 // Tiny, intense sources need exact extinction even inside a smooth field.
 if(max(background.x,max(background.y,background.z))>16.)return false;
 // Bound the projected radiance range, including transmitted background.
 // Resolved bright bodies can reuse opaque/smooth clouds just like blue sky.
 vec3 minimum=low.rgb*(uExposure/uSceneScale)+background*low.a;
 vec3 spread=(high.rgb-low.rgb)*(uExposure/uSceneScale)+background*(high.a-low.a);
 vec3 relative=spread/(minimum+.02);
 if(max(relative.x,max(relative.y,relative.z))>.020)return false;
 estimate=mix(mix(a,b,f.x),mix(c,e,f.x),f.y);
 return true;
}
