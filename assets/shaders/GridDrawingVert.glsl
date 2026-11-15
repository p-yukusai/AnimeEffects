#version 330

in vec4  inPosition;

uniform mat4 uViewMatrix;

void main(void)
{
    gl_Position = uViewMatrix * inPosition;
}
