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
 *
 * Authors:
 *    Kristian Høgsberg <krh@bitplanet.net>
 */

#include "piglit-util.h"

int piglit_width = 100;
int piglit_height = 100;
int piglit_window_mode = GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH | GLUT_STENCIL;

enum piglit_result
piglit_display(void)
{
	GLint red_bits;

	glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER,
					      GL_BACK_LEFT,
					      GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE,
					      &red_bits);

	printf("Red bits: %d\n", red_bits);

	if (glGetError())
		return PIGLIT_FAILURE;

       	return PIGLIT_SUCCESS;
}

void piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_framebuffer_object");

	piglit_ortho_projection(1.0, 1.0, GL_FALSE);

	piglit_automatic = GL_TRUE;
}
