#version 450

// Position input
layout (location = 0) in vec2 position;

// Outputs
layout (location = 0) out vec2 coord;

// TODO: imgui color picker as uniforms to this

// Main returns corresponding position
void main()
{
	coord = position;
	gl_Position = vec4(position, 0.0, 1.0);
}
