#version 450 core

// Fragment input from the vertex shader
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 texCoord;

// Fragment output color
layout(location = 0) out vec4 fragColor;

// Fragment shader main function

struct PointLight {
    vec3 position;
    vec3 diffuse;
    float distance;
    float intensity;
};

const uint maxLightCount = 32;

layout(binding = 0) uniform Viewport {
	mat4 view;
	mat4 projection;
    PointLight pointLights[32];
} viewport;

layout(binding = 1) uniform Material {
	vec4 diffuseColor;
} material;

layout(binding = 2) uniform Model {
	mat4 transform;
} model;

layout(binding = 3) uniform sampler modelSampler;
layout(binding = 4) uniform texture2D modelTexture;

const float constantFactor = 1.0f;
const float linearFactor = 4.5;
const float quadraticFactor = 80.0;

void main()
{
    vec3 diffuseTex = texture(sampler2D(modelTexture, modelSampler), texCoord).xyz;
    vec3 norm = normalize(inNormal);
    vec3 resultColor = vec3(0.0, 0.0, 0.0);

    for (int i = 0; i != maxLightCount; ++i) {
        PointLight light = viewport.pointLights[i];
        vec3 lightDir = normalize(light.position - inPosition);

        float attenuation = 1.f;
        if (light.distance != 0) {
            float distance  = length(light.position - inPosition);
            float constant = constantFactor * light.intensity;
            float linear = linearFactor / light.distance;
            float quadratic = linearFactor / (light.distance * light.distance) * light.intensity;
            attenuation /= (constant + linear * distance + quadratic * (distance * distance));

            resultColor += max(dot(norm, lightDir), 0.0) * attenuation * light.diffuse
            * light.intensity;
        }
    }

    resultColor *= diffuseTex;
    resultColor *= material.diffuseColor.xyz;

    fragColor = vec4(resultColor, 1);
}