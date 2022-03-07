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

// Output is color
layout (location = 0) out vec4 fragment;

// PI constant
const float PI = 3.1415926535897932384626433832795;

// TODO: uniform values
int win_width = 800;
int win_height = 800;

// Get data at 2D index
vec4 get_data(int x, int y)
{
	// Check bounds
	if (x < 0 || x >= win_width || y < 0 || y >= win_height)
		return vec4(0.0, 0.0, 0.0, 0.0);

	// Get index
	int index = x + y * win_width;

	// Return data
	if (bars[0].y > 0.5)
		return particles_f2[index];
	return particles_f1[index];
}

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
	float value = bars[i + 1].x/2;
	float d = 0.3 + value;
	if (length(point) < d) {
		// Set the color
		fragment = vec4(0.6, 1.0, 0.6, radius);
	} else {
		// Get buf index
		float buf = bars[0].y;

		// Normalize coordinates
		float npx = (point.x + 1)/2;
		float npy = (point.y + 1)/2;

		// Get index of particle
		int x = int(npx * win_width);
		int y = int(npy * win_height);
		int pi = x + y * win_width;

		vec4 data = get_data(x, y);

		// Velocity
		vec2 v = data.xy;
		vec2 dir = normalize(point);

		// Calculate and apply bar force
		float f = (value - data.z)/radius;
		if (f > 0.0)
			v += f * dir;

		////////////////////////////////////////
		// Calculate and apply pressure force //
		////////////////////////////////////////

		// Get data from neighbor particles
		vec4 n1 = get_data(x - 1, y);
		vec4 n2 = get_data(x + 1, y);
		vec4 n3 = get_data(x, y - 1);
		vec4 n4 = get_data(x, y + 1);

		// Calculate pressure force
		vec2 p = vec2(0.0, 0.0);
		p += (n1.z - data.z) * normalize(vec2(n1.x, n1.y) - point);
		p += (n2.z - data.z) * normalize(vec2(n2.x, n2.y) - point);
		p += (n3.z - data.z) * normalize(vec2(n3.x, n3.y) - point);
		p += (n4.z - data.z) * normalize(vec2(n4.x, n4.y) - point);

		// Apply pressure force
		v += p;

		// Color by velocity
		float s = clamp(length(v), 0.0, 1.0);
		vec3 c1 = vec3(0.596, 0, 0.851);
		vec3 c2 = vec3(0.665, 0.66, 0.878);
		vec3 c = mix(c1, c2, s);
		fragment = vec4(c, 1.0);

		// Update particle
		data = vec4(v, value, 0);
		if (buf > 0.5)
			particles_f1[pi] = data;
		else
			particles_f2[pi] = data;
	}
}
