// Screen-space reflection (render.md "Reflexion"; water M17 W2, window glass M17 windows F3): steps growing along
// the ray, each projected onto an image of the scene; a hit where the ray passes behind the scene by less than a
// thickness, refined by a binary search. `depth` holds the image's reverse-Z depth in .r (an R32F copy), `view`
// the camera it was drawn with. Returns x, y: uv in that image, z: weight (0 = no hit; fades towards the image's
// edge and the far end of the ray).

struct SsrView
{
    mat4 viewProjection;
    mat4 inverseViewProjection;
    vec3 cameraPosition;
    vec3 cameraForward;
};

vec3 ssrWorldAt(SsrView view, vec2 uv, float depth)
{
    const vec4 p = view.inverseViewProjection * vec4(uv * 2.0 - 1.0, depth, 1.0);
    return p.xyz / p.w;
}

float ssrViewDepth(SsrView view, vec3 p)
{
    return dot(p - view.cameraPosition, view.cameraForward);
}

// `jitter` 0..1 per pixel (spreads the steps), `steps` and `maxDistance` how far and how finely the ray goes.
vec3 ssrTrace(sampler2D depth, SsrView view, vec3 origin, vec3 direction, int steps, float maxDistance, float jitter)
{
    float previous = 0.0;
    for (int i = 1; i <= steps; ++i)
    {
        const float s = (float(i) - jitter * 0.5) / float(steps);
        const float t = maxDistance * s * s; // short steps near the surface, longer far away
        const vec3 point = origin + direction * t;
        const vec4 clip = view.viewProjection * vec4(point, 1.0);
        if (clip.w <= 0.0)
        {
            return vec3(0.0); // behind the camera
        }
        const vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
        if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
        {
            return vec3(0.0); // left the image: nothing known there
        }
        const float behind =
            ssrViewDepth(view, point) - ssrViewDepth(view, ssrWorldAt(view, uv, texture(depth, uv).r));
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
                const vec4 c = view.viewProjection * vec4(p, 1.0);
                const vec2 m = c.xy / c.w * 0.5 + 0.5;
                if (ssrViewDepth(view, p) > ssrViewDepth(view, ssrWorldAt(view, m, texture(depth, m).r)))
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
