// Per-draw data of multi-draw batches (render::MeshRenderer::drawBatched). With MULTI_DRAW every draw
// of a batch carries its index as baseInstance; the instance attribute aDrawIndex (divisor 1, values
// 0, 1, 2 ...) turns that into an index here - no gl_DrawID or ARB_shader_draw_parameters needed.
#ifdef MULTI_DRAW
struct DrawData
{
    mat4 model;
    mat4 normalMatrix; // transpose(inverse(model)) as 3 columns in a mat4: computed once per draw on the CPU
    ivec4 lights0; // point light indices 0..3
    ivec4 lights1; // 4..7
    ivec4 counts;  // x = number of point lights
};
layout(std430, binding = 0) readonly buffer Draws
{
    DrawData uDraws[];
};
#endif
