#version 330

uniform vec4 uColor;
layout(location = 0, index = 0) out vec4 oFragColor;

void main(void)
{
    oFragColor = uColor;
}
