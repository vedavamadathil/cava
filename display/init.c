#include "init.h"

// Create a display context
struct Display *make_display(int width, int height, const char *title)
{
	// Context to return
	struct Display *display = (struct Display *) malloc(sizeof(struct Display));

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

	// Set display context members and return
	display->width = width;
	display->height = height;

	return display;
}

