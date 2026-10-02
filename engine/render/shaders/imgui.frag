#version 450 core
// Dear ImGui: vertex colour times texture (font atlas or user image), written as display values.

layout(binding = 0) uniform sampler2D uTexture;

in vec2 vUv;
in vec4 vColor;
out vec4 fragColor;

void main()
{
    fragColor = vColor * texture(uTexture, vUv);
}
