// Skinning (M6): up to 4 bones per vertex, matrices = model-space bone transform * inverse bind matrix.
#ifdef SKINNED
layout(location = 5) in uvec4 aJoints;
layout(location = 6) in vec4 aWeights;

layout(std140, binding = 2) uniform Bones
{
    mat4 uBones[128]; // asset::kMaxBones
};

mat4 skinMatrix()
{
    return uBones[aJoints.x] * aWeights.x + uBones[aJoints.y] * aWeights.y + uBones[aJoints.z] * aWeights.z +
           uBones[aJoints.w] * aWeights.w;
}
#endif
