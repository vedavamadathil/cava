#version 450

// Inputs
layout (location = 0) in vec2 point;

// Uniform array of bars
#define MAX_DISPLAY_BARS 1024

layout (std140, binding = 0) uniform Bars
{
	vec4	bars[MAX_DISPLAY_BARS + 1];
};

// Uniform buffer of particles (double buffer)
#define MAX_PARTICLES 1024 * 1024

layout (std140, binding = 1) buffer Particles_F1
{
	vec4	particles_f1[MAX_PARTICLES];
};

layout (std140, binding = 2) buffer Particles_F2
{
	vec4	particles_f2[MAX_PARTICLES];
};

// TODO: later accept color gradients
// TODO: later pass screen size

layout (std140, binding = 3) buffer Self
{
	int buf;
};

// Output is color
layout (location = 0) out vec4 fragment;

// PI constant
const float PI = 3.1415926535897932384626433832795;

void main()
{
	// Radius
	float radius = length(point);

	// Get angle in full circle
	float theta = atan(point.y, point.x);

	// Rotate by 90 degrees
	theta += PI / 2.0;

	// Add PI to get angle in [0, 2PI] if necessary
	if (point.y < 0.0)
	 	theta += 2 * PI;

	// Mod by 2PI to get angle in [0, 2PI]
	theta = theta - floor(theta / (2 * PI)) * 2 * PI;

	// Get the index of the bar
	// in a circular array
	int num_bars = int(bars[0].x);
	int i = int(theta / (2 * PI) * num_bars);

	// Get the distance from the point to the bar
	float d = 0.3 + bars[i + 1].x;
	if (length(point) < d) {
		// Set the color
		fragment = vec4(1.0, 0.0, 0.0, radius);
	} else {
		int width = 800;
		int height = 800;

		// Switch to the next buffer
		if (buf % 2 == 0)
			buf = 1;
		else
			buf = 0;

		// Normalize coordinates
		int npx = int((point.x + 1)/2);
		int npy = int((point.y + 1)/2);

		// Get index of particle
		int pi = npy * width + npx;

		// Data
		vec4 data = particles_f1[pi];
		if (buf == 1)
			data = particles_f2[pi];
		
		// Velocity
		vec2 v = data.xy;

		// Apply force
		vec2 f = (bars[i + 1].x - data.z) * normalize(point);
		v += f * 0.001;

		// Color by velocity
		float s = length(v);

		fragment = vec4(s, s, s, radius);

		// Update particle
		data = vec4(v, bars[i + 1].x, 0);
		if (buf == 0)
			particles_f1[pi] = data;
		else
			particles_f2[pi] = data;
	}
}
