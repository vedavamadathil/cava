#include "init.h"
#include <GL/glext.h>

// Read contents of file in string
char *read_file(const char *filename)
{
	FILE *fp = fopen(filename, "r");
	if (fp == NULL) {
		printf("Error opening file %s\n", filename);
		exit(1);
	}

	fseek(fp, 0, SEEK_END);
	long fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	char *string = malloc(fsize + 1);
	fread(string, fsize, 1, fp);
	fclose(fp);

	string[fsize] = 0;

	return string;
}

// Load a shader
struct Shader load_shader(const char *vertex, const char *fragment)
{
	struct Shader shader;
	
	printf("Loading shader %s and %s\n", vertex, fragment);

	char *vertex_source = read_file(vertex);
	char *fragment_source = read_file(fragment);
	
	// Error handling
	int success;
	char info_log[512];

	// Compile vertex shader
	shader.vertex = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(shader.vertex, 1, &vertex_source, NULL);
	glCompileShader(shader.vertex);

	// Check for errors
	glGetShaderiv(shader.vertex, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader.vertex, 512, NULL, info_log);
		printf("Error compiling vertex shader: %s\n", info_log);
		exit(1);
	}

	// Compile fragment shader
	shader.fragment = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(shader.fragment, 1, &fragment_source, NULL);
	glCompileShader(shader.fragment);

	glGetShaderiv(shader.fragment, GL_COMPILE_STATUS, &success);
	if (!success) {
		glGetShaderInfoLog(shader.fragment, 512, NULL, info_log);
		printf("Error compiling fragment shader: %s\n", info_log);
		exit(1);
	}

	// Create shader program
	shader.program = glCreateProgram();
	glAttachShader(shader.program, shader.vertex);
	glAttachShader(shader.program, shader.fragment);
	glLinkProgram(shader.program);

	// Check for errors
	glGetProgramiv(shader.program, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(shader.program, 512, NULL, info_log);
		printf("Error linking shader program: %s\n", info_log);
		exit(1);
	}

	// Delete shaders
	glDeleteShader(shader.vertex);
	glDeleteShader(shader.fragment);

	// Free source
	free(vertex_source);
	free(fragment_source);

	return shader;
}

// Create vertex array
struct VertexArray make_vertex_array()
{
	struct VertexArray array;

	// Create vertex array
	glGenVertexArrays(1, &array.vao);
	glBindVertexArray(array.vao);

	// Create vertex buffer
	glGenBuffers(1, &array.vbo);
	glBindBuffer(GL_ARRAY_BUFFER, array.vbo);

	// Fill vertex buffer (vec2 for each)
	float vertices[] = {
		// Triangle 1
		-1.0, -1.0,
		1.0, -1.0,
		-1.0, 1.0,

		// Triangle 2
		1.0, 1.0,
		-1.0, 1.0,
		1.0, -1.0
	};

	glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void *) 0);
	glEnableVertexAttribArray(0);

	// Unbind buffers
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glBindVertexArray(0);

	return array;
}

// Create a uniform buffer
struct Buffer make_buffer(GLuint type, GLuint binding, size_t size)
{
	struct Buffer buffer;

	// Create buffer
	glGenBuffers(1, &buffer.buffer);
	glBindBuffer(type, buffer.buffer);

	// Allocate space
	glBufferData(type, size, NULL, GL_DYNAMIC_DRAW);

	// Bind to binding point
	glBindBufferBase(type, binding, buffer.buffer);

	// Unbind buffer
	glBindBuffer(type, 0);

	return buffer;
}

// Initialize GLFW and display structure
struct Display *init_display(int width, int height, const char *title)
{
	// Context to return
	struct Display *display = (struct Display *) malloc(sizeof(struct Display));

	// Create debug file
	static const char *debug_file = "turbavis.log";

	display->debug = fopen(debug_file, "w");

	/* Redirect stderr and stdout to debug file
	if (display->debug != NULL) {
		dup2(fileno(display->debug), STDERR_FILENO);
		dup2(fileno(display->debug), STDOUT_FILENO);
	} */

	printf("Initializing display\n");

	// Initialize GLFW
	if (!glfwInit()) {
		fprintf(stderr, "Failed to initialize GLFW\n");
		return NULL;
	}

	// Set the required OpenGL version
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);

	// Create a windowed mode window and its OpenGL context
	display->window = glfwCreateWindow(
		width, height, title,
		NULL, NULL
	);

	// Check if window creation failed
	if (!display->window) {
		fprintf(stderr, "Failed to open GLFW window.\n");
		glfwTerminate();
		return NULL;
	}

	// Make the window's context current
	glfwMakeContextCurrent(display->window);

	// Initialize GLAD
	if (!gladLoadGLLoader((GLADloadproc) glfwGetProcAddress)) {
		fprintf(stderr, "Failed to initialize GLAD\n");
		return NULL;
	}

	// Set the viewport size
	glViewport(0, 0, width, height);

	// Set the clear color
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	// Multi-sampling
	// glEnable(GL_MULTISAMPLE);

	// Set display context members
	display->width = width;
	display->height = height;

	display->num_bars = 0;

	return display;
}

// Create and fully initialize a display context
struct Display *make_display(int width, int height, const char *title)
{
	struct Display *display = init_display(width, height, title);

	// Load shaders
	static const char *vert_file = "display/shaders/visualizer.vert";
	static const char *frag_file = "display/shaders/default.frag";

	struct Shader shader = load_shader(vert_file, frag_file);

	display->shader = shader;

	// Create vertex array
	struct VertexArray array = make_vertex_array();

	display->vertex_array = array;

	// Create uniform buffers
	size_t bars_size = sizeof(struct afloat) * (MAX_DISPLAY_BARS + 1);
	struct Buffer bars_buffer = make_buffer(GL_UNIFORM_BUFFER, 0, bars_size);
	display->bars_ubo = bars_buffer;

	size_t particles = width * height * sizeof(struct afloat);

	// Create temporary buffer to initialize particles
	struct afloat *particles_buffer = (struct afloat *) malloc(particles);
	for (int i = 0; i < width * height; i++) {
		particles_buffer[i].value = 0.0f;
		particles_buffer[i].x1 = 0.0f;
		particles_buffer[i].x2 = 0.0f;
		particles_buffer[i].x3 = 0.0f;
	}

	// Create uniform buffer for particles
	display->particles_ubo_f1 = make_buffer(GL_SHADER_STORAGE_BUFFER, 1, particles);
	display->particles_ubo_f2 = make_buffer(GL_SHADER_STORAGE_BUFFER, 2, particles);

	// Copy particles to buffer
	glBindBuffer(GL_UNIFORM_BUFFER, display->particles_ubo_f1.buffer);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, particles, particles_buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
	
	// Copy particles to buffer
	glBindBuffer(GL_UNIFORM_BUFFER, display->particles_ubo_f2.buffer);
	glBufferSubData(GL_UNIFORM_BUFFER, 0, particles, particles_buffer);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	// Free temporary buffer
	free(particles_buffer);

	// Return the display context
	return display;
}

// Render a frame
void render(struct Display *display)
{
	static int buf = 0;

	// Clear the color buffer
	glClear(GL_COLOR_BUFFER_BIT);

	// Use the shader program
	glUseProgram(display->shader.program);

	/* printf("Bars: %d\n", display->num_bars);
	for (int i = 1; i <= display->num_bars; i++) {
		printf("%f ", display->bars[i].value);
	}
	printf("\n"); */

	// Copy to uniform buffer
	// TODO: another function
	glBindBuffer(GL_UNIFORM_BUFFER, display->bars_ubo.buffer);

	buf = (buf + 1) % 2;
	display->bars[0].value = display->num_bars;
	display->bars[0].x1 = buf;

	glBufferSubData(GL_UNIFORM_BUFFER, 0,
		sizeof(struct afloat) * (display->num_bars + 1),
		display->bars
	);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);

	// Bind vertex array
	glBindVertexArray(display->vertex_array.vao);

	// Draw 6 vertices
	glDrawArrays(GL_TRIANGLES, 0, 6);

	// Swap front and back buffers
	glfwSwapBuffers(display->window);

	// Poll for and process events
	glfwPollEvents();
}
