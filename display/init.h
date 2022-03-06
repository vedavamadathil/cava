// Standard headers
#include <stdio.h>
#include <stdlib.h>

// GLAD
#include <glad/glad.h>

// GLFW headers
#include <GLFW/glfw3.h>

// Display context
struct Display {
	GLFWwindow *	window;
	int		width;
	int		height;
};

// Initialize display
struct Display *make_display(int, int, const char *);
