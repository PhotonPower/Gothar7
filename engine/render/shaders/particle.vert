#version 450 core
// Particles (render::ParticleRenderer): one camera-facing quad per instance, sparks stretched along their motion.

layout(location = 0) in vec4 aPositionSize; // xyz world, w size (m)
layout(location = 1) in vec4 aColor;        // linear rgb, alpha
layout(location = 2) in vec4 aVelocitySprite; // xyz velocity, w sprite layer (0 soft, 1 smoke, 2 spark)

uniform mat4 uViewProjection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
uniform vec3 uCameraPosition;

out vec2 vUv;
out vec4 vColor;
flat out float vLayer;

void main()
{
    const vec2 corners[6] = vec2[6](vec2(-1, -1), vec2(1, -1), vec2(1, 1), vec2(-1, -1), vec2(1, 1), vec2(-1, 1));
    const vec2 c = corners[gl_VertexID];
    vec3 right = uCameraRight;
    vec3 up = uCameraUp;
    float radius = aPositionSize.w * 0.5;
    if (aVelocitySprite.w > 1.5)
    {
        // A spark: its long side along the motion as seen from the camera.
        const vec3 toCamera = normalize(uCameraPosition - aPositionSize.xyz);
        vec3 along = aVelocitySprite.xyz - toCamera * dot(aVelocitySprite.xyz, toCamera);
        if (length(along) > 1e-4)
        {
            up = normalize(along);
            right = normalize(cross(up, toCamera));
        }
        const vec3 world = aPositionSize.xyz + right * c.x * radius * 0.25 + up * c.y * radius * 2.0;
        gl_Position = uViewProjection * vec4(world, 1.0);
    }
    else
    {
        const vec3 world = aPositionSize.xyz + right * c.x * radius + up * c.y * radius;
        gl_Position = uViewProjection * vec4(world, 1.0);
    }
    vUv = c * 0.5 + 0.5;
    vColor = aColor;
    vLayer = aVelocitySprite.w;
}
