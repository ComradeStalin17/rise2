#version 450

layout (binding = 1) uniform sampler fontSampler;
layout (binding = 2) uniform texture2D fontTexture;

layout (location = 0) in vec2 inUV;
layout (location = 1) in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main()
{
    outColor = inColor * texture(sampler2D(fontTexture, fontSampler), inUV);
}