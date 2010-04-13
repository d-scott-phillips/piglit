/*
 * Copyright © 2010 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#define _GNU_SOURCE
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include "piglit-util.h"

int piglit_width = 250, piglit_height = 250;
int piglit_window_mode = GLUT_RGB | GLUT_DOUBLE;

static float gl_version = 0.0;
static float glsl_version = 0.0;

const char *test_start = NULL;

const char *shader_start;
GLuint vertex_shaders[256];
unsigned num_vertex_shaders = 0;
GLuint geometry_shaders[256];
unsigned num_geometry_shaders = 0;
GLuint fragment_shaders[256];
unsigned num_fragment_shaders = 0;

enum states {
	none = 0,
	requirements,
	vertex_shader,
	vertex_program,
	geometry_shader,
	geometry_program,
	fragment_shader,
	fragment_program,
	test,
};


enum comparison {
	equal = 0,
	not_equal,
	less,
	greater_equal,
	greater,
	less_equal
};


/**
 * Copy a string until either whitespace or the end of the string
 */
const char *
strcpy_to_space(char *dst, const char *src)
{
	while (!isspace(*src) && (*src != '\0'))
		*(dst++) = *(src++);

	*dst = '\0';
	return src;
}


/**
 * Skip over whitespace upto the end of line
 */
const char *
eat_whitespace(const char *src)
{
	while (isspace(*src) && (*src != '\n'))
		src++;

	return src;
}


/**
 * Compare two values given a specified comparison operator
 */
bool
compare(float ref, float value, enum comparison cmp)
{
	switch (cmp) {
	case equal:         return value == ref;
	case not_equal:     return value != ref;
	case less:          return value <  ref;
	case greater_equal: return value >= ref;
	case greater:       return value >  ref;
	case less_equal:    return value <= ref;
	}

	assert(!"Should not get here.");
}


/**
 * Get the string representation of a comparison operator
 */
const char *
comparison_string(enum comparison cmp)
{
	switch (cmp) {
	case equal:         return "==";
	case not_equal:     return "!=";
	case less:          return "<";
	case greater_equal: return ">=";
	case greater:       return ">";
	case less_equal:    return "<=";
	}

	assert(!"Should not get here.");
}


/**
 * Parse a binary comparison operator and return the matching token
 */
const char *
process_comparison(const char *src, enum comparison *cmp)
{
	char buf[32];

	switch (src[0]) {
	case '=':
		if (src[1] == '=') {
			*cmp = equal;
			return src + 2;
		}
		break;
	case '<':
		if (src[1] == '=') {
			*cmp = less_equal;
			return src + 2;
		} else {
			*cmp = less;
			return src + 1;
		}
	case '>':
		if (src[1] == '=') {
			*cmp = greater_equal;
			return src + 2;
		} else {
			*cmp = greater;
			return src + 1;
		}
	case '!':
		if (src[1] == '=') {
			*cmp = not_equal;
			return src + 2;
		}
		break;
	}

	strncpy(buf, src, sizeof(buf));
	buf[sizeof(buf) - 1] = '\0';
	printf("invalid comparison in test script:\n%s\n", buf);
	piglit_report_result(PIGLIT_FAILURE);

	/* Won't get here. */
	return NULL;
}


/**
 * Parse and check a line from the requirement section of the test
 */
void
process_requirement(const char *line)
{
	char buffer[4096];

	/* There are three types of requirements that a test can currently
	 * have:
	 *
	 *    * Require that some GL extension be supported
	 *    * Require some particular versions of GL
	 *    * Require some particular versions of GLSL
	 *
	 * The tests for GL and GLSL versions can be equal, not equal,
	 * less, less-or-equal, greater, or greater-or-equal.  Extension tests
	 * can also require that a particular extension not be supported by
	 * prepending ! to the extension name.
	 */
	if (strncmp("GL_", line, 3) == 0) {
		strcpy_to_space(buffer, line);
		piglit_require_extension(buffer);
	} else if (strncmp("!GL_", line, 4) == 0) {
		strcpy_to_space(buffer, line + 1);
		piglit_require_not_extension(buffer);
	} else if (strncmp("GLSL", line, 4) == 0) {
		enum comparison cmp;
		float version;

		line = eat_whitespace(line + 4);

		line = process_comparison(line, &cmp);

		version = strtof(line, NULL);
		if (!compare(version, glsl_version, cmp)) {
			printf("Test requires GLSL version %s %.1f.  "
			       "Actual version is %.1f.\n",
			       comparison_string(cmp),
			       version,
			       glsl_version);
			piglit_report_result(PIGLIT_SKIP);
		}
	} else if (strncmp("GL", line, 2) == 0) {
		enum comparison cmp;
		float version;

		line = eat_whitespace(line + 2);

		line = process_comparison(line, &cmp);

		version = strtof(line, NULL);
		if (!compare(version, gl_version, cmp)) {
			printf("Test requires GL version %s %.1f.  "
			       "Actual version is %.1f.\n",
			       comparison_string(cmp),
			       version,
			       gl_version);
			piglit_report_result(PIGLIT_SKIP);
		}
	}
}


void
leave_state(enum states state, const char *line)
{
	GLuint shader;

	switch (state) {
	case none:
		break;

	case requirements:
		break;

	case vertex_shader:
		shader = piglit_compile_shader_text_with_length(GL_VERTEX_SHADER,
								shader_start,
								line - shader_start);
		vertex_shaders[num_vertex_shaders] = shader;
		num_vertex_shaders++;
		break;

	case vertex_program:
		break;

	case geometry_shader:
		break;

	case geometry_program:
		break;

	case fragment_shader:
		shader = piglit_compile_shader_text_with_length(GL_FRAGMENT_SHADER,
								shader_start,
								line - shader_start);
		fragment_shaders[num_fragment_shaders] = shader;
		num_fragment_shaders++;
		break;

	case fragment_program:
		break;

	case test:
		break;
	}
}


void
link_and_use_shaders(void)
{
	GLuint prog;
	unsigned i;
	GLenum err;



	if ((num_vertex_shaders == 0)
	    && (num_fragment_shaders == 0)
	    && (num_geometry_shaders == 0))
		return;

	prog = glCreateProgram();

	for (i = 0; i < num_vertex_shaders; i++) {
		glAttachShader(prog, vertex_shaders[i]);
	}

	for (i = 0; i < num_geometry_shaders; i++) {
		glAttachShader(prog, geometry_shaders[i]);
	}

	for (i = 0; i < num_fragment_shaders; i++) {
		glAttachShader(prog, fragment_shaders[i]);
	}

	glLinkProgram(prog);
	glUseProgram(prog);

	err = glGetError();
	if (err) {
		printf("GL error after linking program: 0x%04x\n", err);
		piglit_report_result(PIGLIT_FAILURE);
	}
}


void
process_test_script(const char *script_name)
{
	unsigned text_size;
	char *text = piglit_load_text_file(script_name, &text_size);
	enum states state = none;
	const char *line = text;

	if (line == NULL) {
		printf("could not read file \"%s\"\n", script_name);
		piglit_report_result(PIGLIT_FAILURE);
	}

	while (line[0] != '\0') {
		if (line[0] == '[') {
			leave_state(state, line);

			if (strncmp(line, "[require]", 9) == 0) {
				state = requirements;
			} else if (strncmp(line, "[vertex shader]", 15) == 0) {
				state = vertex_shader;
				shader_start = NULL;
			} else if (strncmp(line, "[fragment shader]", 17) == 0) {
				state = fragment_shader;
				shader_start = NULL;
			} else if (strncmp(line, "[test]", 6) == 0) {
				test_start = strchrnul(line, '\n');
				if (test_start[0] != '\0')
					test_start++;
				return;
			}
		} else {
			switch (state) {
			case none:
				break;

			case requirements:
				process_requirement(line);
				break;

			case vertex_shader:
			case vertex_program:
			case geometry_shader:
			case geometry_program:
			case fragment_shader:
			case fragment_program:
				if (shader_start == NULL)
					shader_start = line;
				break;

			case test:
				break;
			}
		}

		line = strchrnul(line, '\n');
		if (line[0] != '\0')
			line++;
	}

	leave_state(state, line);
}


void
get_floats(const char *line, float *f, unsigned count)
{
	unsigned i;

	for (i = 0; i < count; i++)
		f[i] = strtof(line, (char **) &line);
}


void
set_uniform(const char *line)
{
	char name[512];
	float f[16];
	GLuint prog;
	GLint loc;

	glGetIntegerv(GL_CURRENT_PROGRAM, (GLint *) &prog);

	line = strcpy_to_space(name, eat_whitespace(line));
	loc = glGetUniformLocation(prog, name);
	if (loc < 0) {
		printf("cannot get location of uniform \"%s\"\n",
		       name);
		piglit_report_result(PIGLIT_FAILURE);
	}

	line = eat_whitespace(line);
	if (strncmp("vec4", line, 4) == 0) {
		get_floats(line + 4, f, 4);
		glUniform4fv(loc, 1, f);
	}
}


enum piglit_result
piglit_display(void)
{
	const char *line;
	bool pass = true;
	GLbitfield clear_bits = 0;

	if (test_start == NULL)
		return PIGLIT_SUCCESS;


	line = test_start;
	while (line[0] != '\0') {
		float c[32];

		line = eat_whitespace(line);

		if (strncmp("clear color", line, 11) == 0) {
			get_floats(line + 11, c, 4);
			glClearColor(c[0], c[1], c[2], c[3]);
			clear_bits |= GL_COLOR_BUFFER_BIT;
		} else if (strncmp("clear", line, 5) == 0) {
			glClear(clear_bits);
		} else if (strncmp("draw rect", line, 9) == 0) {
			get_floats(line + 9, c, 4);
			piglit_draw_rect(c[0], c[1], c[2], c[3]);
		} else if (strncmp("ortho", line, 5) == 0) {
			piglit_ortho_projection(piglit_width, piglit_height,
						GL_FALSE);
		} else if (strncmp("probe rgba", line, 10) == 0) {
			get_floats(line + 10, c, 6);
			if (!piglit_probe_pixel_rgb((int) c[0], (int) c[1],
						    & c[2])) {
				pass = false;
			}
		} else if (strncmp("probe rgb", line, 9) == 0) {
			get_floats(line + 9, c, 5);
			if (!piglit_probe_pixel_rgb((int) c[0], (int) c[1],
						    & c[2])) {
				pass = false;
			}
		} else if (strncmp("uniform", line, 7) == 0) {
			set_uniform(line + 7);
		} else if ((line[0] != '\n') && (line[0] != '\0')
			   && (line[0] != '#')) {
			printf("unknown command \"%s\"", line);
			piglit_report_result(PIGLIT_FAILURE);
		}

		line = strchrnul(line, '\n');
		if (line[0] != '\0')
			line++;
	}

	glutSwapBuffers();

	return pass ? PIGLIT_SUCCESS : PIGLIT_FAILURE;
}


void
piglit_init(int argc, char **argv)
{
	const char *glsl_version_string;

	gl_version = strtof((char *) glGetString(GL_VERSION), NULL);

	glsl_version_string = (char *)
		glGetString(GL_SHADING_LANGUAGE_VERSION);
	glsl_version = (glsl_version_string == NULL)
		? 0.0 : strtof(glsl_version_string, NULL);

	process_test_script(argv[1]);
	link_and_use_shaders();
}
