#version 330

in vec4  inPosition;
in vec2  inTexCoord;

uniform mat4 uViewMatrix;
uniform vec2 uScreenSize;
uniform vec2 uImageSize;
uniform vec2 uTexCoordOffset;

out vec2 vTexCoord;
out vec2 vDestCoord;

void main(void)
{
    vec4 pos = uViewMatrix * inPosition;
    vec3 screenPos = pos.xyz / pos.w;

    gl_Position = pos;
    vDestCoord = vec2(uScreenSize.x * (screenPos.x + 1.0) * 0.5, uScreenSize.y * (screenPos.y + 1.0) * 0.5);

    vec2 texCoord = inTexCoord + uTexCoordOffset;
    vTexCoord = vec2(texCoord.x / uImageSize.x, texCoord.y / uImageSize.y);
}
