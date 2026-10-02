// Depth test of debug drawing against the scene (render::DebugDrawRenderer): debug geometry is
// drawn after the post pass into the window and compares itself with the HDR target's depth.

layout(binding = 0) uniform sampler2D uSceneDepth; // Depth32F, reverse-Z
uniform int uHasSceneDepth;

// True if scene geometry lies in front of `depth` (window depth, reverse-Z: larger = nearer).
bool debugHidden(float depth)
{
    if (uHasSceneDepth == 0 || depth < 0.0)
    {
        return false;
    }
    const float scene = texelFetch(uSceneDepth, ivec2(gl_FragCoord.xy), 0).r;
    // Reverse-Z depth is ~ near / distance, so a relative tolerance is a relative distance: lines
    // lying on a surface stay visible.
    return scene > depth * 1.002;
}
