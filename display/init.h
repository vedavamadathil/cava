// Standard headers
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

// GLAD
#include <glad/glad.h>

// GLFW headers
#include <GLFW/glfw3.h>

// Max number of bars
#define MAX_DISPLAY_BARS	1000

// Shaders and loading
struct Shader {
	GLuint		program;
	GLuint		vertex;
	GLuint		fragment;
};

struct Shader load_shader(const char *, const char *);

// Vertex array and buffer
struct VertexArray {
	GLuint	vao;
	GLuint	vbo;
};

struct VertexArray make_vertex_array();

// Uniform buffer
struct Buffer {
	GLuint	buffer;
	GLuint	binding;
};

struct Buffer make_buffer(GLuint, GLuint, size_t);

// Aligned value
struct afloat {
	float value;
	float x1;
	float x2;
	float x3;
} __attribute__((aligned(16)));

// Display context
struct Display {
	// Main window
	GLFWwindow *		window;

	// Dimensions
	int			width;
	int			height;

	// Shaders
	struct Shader		shader;

	// Vertex array
	struct VertexArray	vertex_array;

	// Uniform buffer
	struct Buffer		bars_ubo;
	struct Buffer		particles_ubo_f1;
	struct Buffer		particles_ubo_f2;

	// Bars
	int32_t			num_bars;
	struct afloat		bars[MAX_DISPLAY_BARS + 1];

	// Debug file
	FILE *			debug;
};

// Initialize display
struct Display *make_display(int, int, const char *);

// Render a frame
void render(struct Display *);
