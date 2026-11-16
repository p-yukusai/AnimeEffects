#version 330

in vec4  inPosition;

uniform mat4 uViewMatrix;
uniform vec2 uScreenSize;

out vec2 vDestCoord;

void main(void)
{
    vec4 pos = uViewMatrix * inPosition;
    vec3 screenPos = pos.xyz / pos.w;

    gl_Position = pos;
    vDestCoord = vec2(uScreenSize.x * (screenPos.x + 1.0) * 0.5, uScreenSize.y * (screenPos.y + 1.0) * 0.5);
}
