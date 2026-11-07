#version 450 core
// Water surface (M17 W1, render::WaterRenderer): waves from a tiling normal map (two drifting layers, flowing
// along uFlow for rivers), light refraction from the copy of the scene, colour by the water depth (Beer-Lambert),
// soft shore edge, Fresnel (Schlick) between that and the reflection, sun glint, fog. Output linear HDR.
// The reflection (M17 W2, quality high): a ray marched through the copy of the depth (screen-space reflection),
// fading out towards the screen edge, the sky where it finds nothing.

layout(binding = 0) uniform sampler2D uSceneColor; // copy of the scene, linear (medium/high)
layout(binding = 1) uniform sampler2D uSceneDepth; // copy of the depth (R32F, reverse-Z)
layout(binding = 2) uniform sampler2D uWaves;      // xz tilt in rg (render::makeWaterNormals)

uniform int uRefract; // 0: low quality (no copies)
uniform int uReflect; // 1: screen-space reflection (high quality, needs the copies)
uniform mat4 uViewProjection;
uniform vec3 uCameraForward;
uniform int uSteps;         // ray steps (W2: 32)
uniform float uMaxDistance; // metres the ray goes at most
uniform mat4 uInverseViewProjection;
uniform vec3 uCameraPosition;
uniform vec2 uViewportSize;
uniform float uTime;
uniform vec2 uFlow; // m/s in world xz
uniform vec3 uAbsorption;
uniform vec3 uScatter;
uniform float uWaveScale;
uniform float uWaveSpeed;
uniform float uWaveStrength;
uniform float uRefraction;
uniform float uGlint;
uniform float uShininess;
uniform float uShoreFade;
uniform vec3 uSunDirection;
uniform vec3 uSunColor;
uniform vec3 uAmbient;
uniform vec3 uSkyHorizon;
uniform vec3 uSkyZenith;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogDensity;

in vec3 vWorld;
out vec4 fragColor;

vec3 worldAt(vec2 uv, float depth)
{
    const vec4 p = uInverseViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return p.xyz / p.w;
}

// Same as render::waterFresnel.
float fresnel(float cosTheta)
{
    const float m = 1.0 - clamp(cosTheta, 0.0, 1.0);
    return 0.02 + 0.98 * m * m * m * m * m;
}

float viewDepth(vec3 p)
{
    return dot(p - uCameraPosition, uCameraForward);
}

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

// Screen-space reflection (render.md "Reflexion"): steps growing along the ray, each projected onto the screen;
// a hit where the ray passes behind the scene by less than a thickness, refined by a binary search. x, y: uv,
// z: weight (0 = no hit; fades towards the screen edge and the far end of the ray).
vec3 traceReflection(vec3 origin, vec3 direction)
{
    const float jitter = hash(gl_FragCoord.xy);
    float previous = 0.0;
    for (int i = 1; i <= uSteps; ++i)
    {
        const float s = (float(i) - jitter * 0.5) / float(uSteps);
        const float t = uMaxDistance * s * s; // short steps near the water, longer far away
        const vec3 point = origin + direction * t;
        const vec4 clip = uViewProjection * vec4(point, 1.0);
        if (clip.w <= 0.0)
        {
            return vec3(0.0); // behind the camera
        }
        const vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        {
            return vec3(0.0); // left the screen: nothing known there
        }
        const float behind = viewDepth(point) - viewDepth(worldAt(uv, texture(uSceneDepth, uv).r));
        const float thickness = max(0.3, 0.1 * t);
        if (behind > 0.0)
        {
            if (behind > thickness)
            {
                return vec3(0.0); // passed behind an object: hidden, not a hit
            }
            // Binary search between the last point in front and this one.
            float lo = previous;
            float hi = t;
            vec2 hit = uv;
            for (int k = 0; k < 5; ++k)
            {
                const float mid = 0.5 * (lo + hi);
                const vec3 p = origin + direction * mid;
                const vec4 c = uViewProjection * vec4(p, 1.0);
                const vec2 m = c.xy / c.w * 0.5 + 0.5;
                if (viewDepth(p) > viewDepth(worldAt(m, texture(uSceneDepth, m).r)))
                {
                    hi = mid;
                    hit = m;
                }
                else
                {
                    lo = mid;
                }
            }
            const vec2 edge = min(hit, 1.0 - hit);
            const float fade = smoothstep(0.0, 0.08, min(edge.x, edge.y)) * (1.0 - smoothstep(0.7, 1.0, s));
            return vec3(hit, fade);
        }
        previous = t;
    }
    return vec3(0.0);
}

void main()
{
    // Waves: two layers drifting apart (fract keeps the offsets small over hours), carried by the flow.
    const vec2 uv = vWorld.xz / uWaveScale;
    const vec2 flow = fract(uFlow * uTime / uWaveScale);
    const float t = uTime * uWaveSpeed;
    const vec2 a = texture(uWaves, uv + flow + fract(vec2(t, 0.6 * t))).rg * 2.0 - 1.0;
    const vec2 b = texture(uWaves, uv * 1.7 + flow * 1.7 + fract(vec2(-0.8 * t, 0.5 * t)) + 0.37).rg * 2.0 - 1.0;
    const vec2 tilt = (a + 0.6 * b) * uWaveStrength;
    vec3 normal = normalize(vec3(tilt.x, 1.0, tilt.y));

    const vec3 toCamera = uCameraPosition - vWorld;
    const float surfaceDistance = length(toCamera);
    const vec3 view = toCamera / max(surfaceDistance, 1e-4);
    const vec3 lit = uAmbient + uSunColor * max(uSunDirection.y, 0.0);
    const vec3 deep = uScatter * lit;

    if (view.y < 0.0)
    {
        // Seen from below (diving): murky until W3 brings the underwater view.
        fragColor = vec4(deep + 0.3 * uSkyHorizon, 0.75);
        return;
    }

    const float f = fresnel(dot(normal, view));
    const vec3 r = reflect(-view, normal);
    vec3 sky = mix(uSkyHorizon, uSkyZenith, smoothstep(0.0, 0.6, max(r.y, 0.0)));
    if (uReflect == 1)
    {
        const vec3 hit = traceReflection(vWorld, r);
        sky = mix(sky, texture(uSceneColor, hit.xy).rgb, hit.z);
    }
    const vec3 halfway = normalize(view + uSunDirection);
    const float sunUp = smoothstep(-0.02, 0.05, uSunDirection.y);
    const vec3 glint = uSunColor * uGlint * sunUp * pow(max(dot(normal, halfway), 0.0), uShininess);

    vec3 color;
    float alpha;
    if (uRefract == 1)
    {
        const vec2 screen = gl_FragCoord.xy / uViewportSize;
        const vec3 behind = worldAt(screen, texture(uSceneDepth, screen).r);
        const float depthHere = max(distance(uCameraPosition, behind) - surfaceDistance, 0.0);
        // Light refraction: offset by the tilt, growing with the water depth over the first metre, the same number
        // of pixels in x and y; only where the offset point lies under the water too.
        vec2 refracted = screen + tilt * uRefraction * vec2(1.0, uViewportSize.x / uViewportSize.y) *
                                      clamp(depthHere, 0.0, 1.0);
        float path = distance(uCameraPosition, worldAt(refracted, texture(uSceneDepth, refracted).r)) -
                     surfaceDistance;
        if (path < 0.0)
        {
            refracted = screen;
            path = depthHere;
        }
        const vec3 transmittance = exp(-uAbsorption * path);
        const vec3 under = texture(uSceneColor, refracted).rgb * transmittance + deep * (1.0 - transmittance);
        color = mix(under, sky, f) + glint;
        alpha = clamp(depthHere / uShoreFade, 0.0, 1.0); // the shore edge fades in, no hard cut
    }
    else
    {
        color = mix(deep, sky, f) + glint;
        alpha = mix(0.7, 1.0, f);
    }
    const float fog = max(surfaceDistance - uFogStart, 0.0) * uFogDensity;
    color = mix(color, uFogColor, 1.0 - exp(-fog * fog));
    fragColor = vec4(color, alpha);
}
