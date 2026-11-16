#version 330

uniform sampler2D uDestTexture;

in vec2 vDestCoord;

layout(location = 0, index = 0) out vec4 oFragColor;

void main(void)
{
    oFragColor = texelFetch(uDestTexture, ivec2(vDestCoord), 0);
}
