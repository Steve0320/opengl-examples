/* Copyright (c) 2014-2015 Scott Kuhl. All rights reserved.
 * License: This code is licensed under a 3-clause BSD license. See
 * the file named "LICENSE" for a full copy of the license.
 */

/** @file Tracks multiple positions and orientations using VRPN and
 * draws a small model at each of the locations.
 *
 * @author Scott Kuhl
 */

#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <GL/glew.h>
#ifdef FREEGLUT
#include <GL/freeglut.h>
#else
#include <GLUT/glut.h>
#endif

#include "kuhl-util.h"
#include "vecmat.h"
#include "dgr.h"
#include "projmat.h"
#include "viewmat.h"
#include "vrpn-help.h"

char *objectsToDraw[] = { "Tracker0", "Tracker1", "Wand", "HandL", "HandR", "SRVI_Mouse", "SRVI_Keyboard", NULL};



static kuhl_fps_state fps_state;

GLuint program = 0; // id value for the GLSL program
kuhl_geometry *modelgeom  = NULL;

/** Initial position of the camera. 1.55 is a good approximate
 * eyeheight in meters.*/
const float initCamPos[3]  = {0,1.55,5};

/** A point that the camera should initially be looking at. If
 * fitToView is set, this will also be the position that model will be
 * translated to. */
const float initCamLook[3] = {0,0,-5};

/** A vector indicating which direction is up. */
const float initCamUp[3]   = {0,1,0};


#define GLSL_VERT_FILE "assimp.vert"
#define GLSL_FRAG_FILE "assimp.frag"

/** Called by GLUT whenever a key is pressed. */
void keyboard(unsigned char key, int x, int y)
{
	switch(key)
	{
		case 'q':
		case 'Q':
		case 27: // ASCII code for Escape key
			dgr_exit();
			exit(EXIT_SUCCESS);
			break;
		case 'f': // full screen
			glutFullScreen();
			break;
		case 'F': // switch to window from full screen mode
			glutPositionWindow(0,0);
			break;
	}

	/* Whenever any key is pressed, request that display() get
	 * called. */ 
	glutPostRedisplay();
}

/** Draw a object at the location and orientation of the tracked vrpn object */
void drawCube(const char *vrpnObject, float viewMat[16])
{
	float scaleFactor = .5;
	
	float pos[4], orient[16];
	vrpn_get(vrpnObject, NULL, pos, orient);
	float modelMat[16],translate[16],scale[16];
	mat4f_scale_new(scale, scaleFactor, scaleFactor, scaleFactor);
	mat4f_translateVec_new(translate, pos);
	mat4f_mult_mat4f_many(modelMat, translate, orient, scale, NULL);

	float modelview[16];
	mat4f_mult_mat4f_new(modelview, viewMat, modelMat); // modelview = view * model

	/* Send the modelview matrix to the vertex program. */
	glUniformMatrix4fv(kuhl_get_uniform("ModelView"),
	                   1, // number of 4x4 float matrices
	                   0, // transpose
	                   modelview); // value

	kuhl_errorcheck();
	kuhl_geometry_draw(modelgeom); /* Draw the model */
	kuhl_errorcheck();

#if 0
	printf("%s is at\n", vrpnObject);
	vec3f_print(pos);
	mat4f_print(orient);
#endif
}


/** Called by GLUT whenever the window needs to be redrawn. This
 * function should not be called directly by the programmer. Instead,
 * we can call glutPostRedisplay() to request that GLUT call display()
 * at some point. */
void display()
{
	/* If we are using DGR, send or receive data to keep multiple
	 * processes/computers synchronized. */
	dgr_update();

	/* Get current frames per second calculations. */
	float fps = kuhl_getfps(&fps_state);

	if(dgr_is_enabled() == 0 || dgr_is_master())
	{
		// If DGR is being used, only display dgr counter if we are
		// the master process.

		// Check if FPS value was just updated by kuhl_getfps()
		if(fps_state.frame == 0)
			msg(INFO, "FPS: %0.1f", fps);
	}
	
	/* Render the scene once for each viewport. Frequently one
	 * viewport will fill the entire screen. However, this loop will
	 * run twice for HMDs (once for the left eye and once for the
	 * right. */
	viewmat_begin_frame();
	for(int viewportID=0; viewportID<viewmat_num_viewports(); viewportID++)
	{
		viewmat_begin_eye(viewportID);

		/* Where is the viewport that we are drawing onto and what is its size? */
		int viewport[4]; // x,y of lower left corner, width, height
		viewmat_get_viewport(viewport, viewportID);
		glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

		/* Clear the current viewport. Without glScissor(), glClear()
		 * clears the entire screen. We could call glClear() before
		 * this viewport loop---but on order for all variations of
		 * this code to work (Oculus support, etc), we can only draw
		 * after viewmat_begin_eye(). */
		glScissor(viewport[0], viewport[1], viewport[2], viewport[3]);
		glEnable(GL_SCISSOR_TEST);
		glClearColor(.2,.2,.2,0); // set clear color to grey
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		glDisable(GL_SCISSOR_TEST);
		glEnable(GL_DEPTH_TEST); // turn on depth testing
		kuhl_errorcheck();

		/* Turn on blending (note, if you are using transparent textures,
		   the transparency may not look correct unless you draw further
		   items before closer items.). */
		glEnable(GL_BLEND);
		glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
		glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);

		/* Get the view or camera matrix; update the frustum values if needed. */
		float viewMat[16], perspective[16];
		viewmat_get(viewMat, perspective, viewportID);

		glUseProgram(program);
		kuhl_errorcheck();
		/* Send the perspective projection matrix to the vertex program. */
		glUniformMatrix4fv(kuhl_get_uniform("Projection"),
		                   1, // number of 4x4 float matrices
		                   0, // transpose
		                   perspective); // value

		glUniform1i(kuhl_get_uniform("renderStyle"), 2);

		int i=0;
		while(objectsToDraw[i] != NULL)
		{
			drawCube(objectsToDraw[i], viewMat);
			i++;
		}

		glUseProgram(0); // stop using a GLSL program.

	} // finish viewport loop
	viewmat_end_frame();
	
	/* Update the model for the next frame based on the time. We
	 * convert the time to seconds and then use mod to cause the
	 * animation to repeat. */
	int time = glutGet(GLUT_ELAPSED_TIME);
	dgr_setget("time", &time, sizeof(int));
	kuhl_update_model(modelgeom, 0, ((time%10000)/1000.0));

	/* Check for errors. If there are errors, consider adding more
	 * calls to kuhl_errorcheck() in your code. */
	kuhl_errorcheck();

	//kuhl_video_record("videoout", 30);
	
	/* Ask GLUT to call display() again. We shouldn't call display()
	 * ourselves recursively because it will not leave time for GLUT
	 * to call other callback functions for when a key is pressed, the
	 * window is resized, etc. */
	glutPostRedisplay();
}

int main(int argc, char** argv)
{
	/* Initialize GLUT and GLEW */
	kuhl_ogl_init(&argc, argv, 512, 512, 32,
	              GLUT_DOUBLE | GLUT_RGB | GLUT_DEPTH | GLUT_MULTISAMPLE, 4);
	              
	// setup callbacks
	glutDisplayFunc(display);
	glutKeyboardFunc(keyboard);

	/* Compile and link a GLSL program composed of a vertex shader and
	 * a fragment shader. */
	program = kuhl_create_program(GLSL_VERT_FILE, GLSL_FRAG_FILE);

	dgr_init();     /* Initialize DGR based on environment variables. */
	projmat_init(); /* Figure out which projection matrix we should use based on environment variables */

	viewmat_init(initCamPos, initCamLook, initCamUp);

	// Clear the screen while things might be loading
	glClearColor(.2,.2,.2,1);
	glClear(GL_COLOR_BUFFER_BIT);

	// Load the model from the file
	//modelgeom = kuhl_load_model("../models/cube/cube.obj", NULL, program, NULL);
	modelgeom = kuhl_load_model("../models/origin/origin.obj", NULL, program, NULL);

	kuhl_getfps_init(&fps_state);
	
	/* Tell GLUT to start running the main loop and to call display(),
	 * keyboard(), etc callback methods as needed. */
	glutMainLoop();
	/* // An alternative approach:
	   while(1)
	   glutMainLoopEvent();
	*/

	exit(EXIT_SUCCESS);
}