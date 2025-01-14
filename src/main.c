// Shaders can be found at: http://glslsandbox.com

#include <GL/glew.h>
#include <GL/glx.h>
#include <GL/gl.h>

#include <Imlib2.h>

#include <X11/X.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>

#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <stdio.h>
#include <time.h>
#include <math.h>

#include "shader.h"
#include "arghandler.h"

static volatile sig_atomic_t keep_running = 1;

typedef struct {
	float quality;  // shader quality
	float speed;    // shader animation speed
	float opacity;  // background transparency

	enum Mode {
		BACKGROUND,
		WINDOW,
		ROOT,
	} mode;

} Option;

int mode_conversion_amount = 3;
EnumConvertInfo mode_conversion_table[] = {
	{ .name = "background", .enum_val = BACKGROUND },
	{ .name = "window", .enum_val = WINDOW },
	{ .name = "root", .enum_val = ROOT },
};

static Option options = {
	.quality = 1,
	.speed = 1,
	.opacity = 1,
	.mode = BACKGROUND,
};

static Shader shader;


Display *dpy;
XVisualInfo *vi;

Window root;

/* Window used in background & window mode */
Window win;
XWindowAttributes gwa;


void init(char *filepath) {
	GLXContext glc;
	int screen;

	Colormap cmap;
	XSetWindowAttributes swa;

	/* open display, screen & root */
	if (!(dpy = XOpenDisplay(NULL))) {
		fprintf(stderr, "Error while opening display.\n");
		exit(EXIT_FAILURE);
	}

	screen = DefaultScreen(dpy);
	root = RootWindow(dpy, screen);

	/* setup imlib */
	imlib_context_set_display(dpy);
	imlib_context_set_visual(DefaultVisual(dpy, screen));
	imlib_context_set_colormap(DefaultColormap(dpy, screen));

	/* get visual matching attr */
	GLint attr[] = { GLX_RGBA, GLX_DEPTH_SIZE, 24, GLX_DOUBLEBUFFER, None };

	if (!(vi = glXChooseVisual(dpy, 0, attr))) {
		fprintf(stderr, "No appropriate visual found\n");
		exit(EXIT_FAILURE);
	}

	/* screen resolution */
	Screen *s = ScreenOfDisplay(dpy, 0);
	int width = s->width;
	int height = s->height;

	/* create a new window if mode: window, background */
	if (options.mode == WINDOW || options.mode == BACKGROUND) {
		cmap = XCreateColormap(dpy, root, vi->visual, AllocNone);
		swa.colormap = cmap;
		swa.event_mask = ExposureMask;

		if (options.mode == BACKGROUND) {
			win = XCreateWindow(dpy, root, 0, 0, width, height, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
			Atom window_type = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
			long value = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DESKTOP", False);
			XChangeProperty(dpy, win, window_type, XA_ATOM, 32, PropModeReplace, (unsigned char *) &value, 1);
		} else {
			win = XCreateWindow(dpy, root, 0, 0, 600, 600, 0, vi->depth, InputOutput, vi->visual, CWColormap | CWEventMask, &swa);
		}

		// make window transparent
		if (options.opacity < 1) {
			uint32_t cardinal_alpha = (uint32_t) (options.opacity * (uint32_t)-1);
			XChangeProperty(dpy, win, XInternAtom(dpy, "_NET_WM_WINDOW_OPACITY", 0), XA_CARDINAL, 32, PropModeReplace, (uint8_t*) &cardinal_alpha,1) ;
		}

		XMapWindow(dpy, win);
		XStoreName(dpy, win, "Show");
	}

	/* create new context for offscreen rendering */
	if (!(glc = glXCreateContext(dpy, vi, NULL, GL_TRUE))) {
		fprintf(stderr, "Failed to create context\n");
		exit(EXIT_FAILURE);
	}

	if (options.mode == ROOT) {
		glXMakeCurrent(dpy, root, glc);
	} else {
		glXMakeCurrent(dpy, win, glc);
	}

	/* setup opengl */
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 1, 1, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();


	glEnable(GL_PROGRAM_POINT_SIZE);
	glEnable(GL_VERTEX_PROGRAM_POINT_SIZE);

	/* init Glew */
	GLenum err = glewInit();
	if (err != GLEW_OK || !GLEW_VERSION_2_1) {
		fprintf(stderr, "Failed to init GLEW\n");
		exit(EXIT_FAILURE);
	}

	/* initialize shader program from user path */
	if (!(shader = shader_compile(filepath))) {
		fprintf(stderr, "Failed to compile Shader\n");
		exit(EXIT_FAILURE);
	}
}



/*
 *  Draws pixmap on the root window
 *  original: https://github.com/derf/feh/blob/master/src/wallpaper.c
 */
void set_pixmap_to_root(Pixmap pmap_d1, int width, int height) {

	/* Used for setting pixmap to root window */
	XGCValues gcvalues;
	GC gc;
	Atom prop_root, prop_esetroot, type;
	int format;
	unsigned long length, after;
	unsigned char *data_root = NULL, *data_esetroot = NULL;

	/* local display to set closedownmode on */
	Display *dpy2;
	Window root2;

	int depth2;

	Pixmap pmap_d2;

	if (!(dpy2 = XOpenDisplay(NULL))) {
		fprintf(stderr, "Can't reopen X display.");
		exit(EXIT_FAILURE);
	}

	root2 = RootWindow(dpy2, DefaultScreen(dpy2));
	depth2 = DefaultDepth(dpy2, DefaultScreen(dpy2));
	XSync(dpy, False);
	pmap_d2 = XCreatePixmap(dpy2, root2, width, height, depth2);
	gcvalues.fill_style = FillTiled;
	gcvalues.tile = pmap_d1;
	gc = XCreateGC(dpy2, pmap_d2, GCFillStyle | GCTile, &gcvalues);
	XFillRectangle(dpy2, pmap_d2, gc, 0, 0, width, height);
	XFreeGC(dpy2, gc);
	XSync(dpy2, False);
	XSync(dpy, False);

	prop_root = XInternAtom(dpy2, "_XROOTPMAP_ID", True);
	prop_esetroot = XInternAtom(dpy2, "ESETROOT_PMAP_ID", True);

	if (prop_root != None && prop_esetroot != None) {
		XGetWindowProperty(dpy2, root2, prop_root, 0L, 1L,
				False, AnyPropertyType, &type, &format, &length, &after, &data_root);
		if (type == XA_PIXMAP) {
			XGetWindowProperty(dpy2, root2,
					prop_esetroot, 0L, 1L,
					False, AnyPropertyType,
					&type, &format, &length, &after, &data_esetroot);
			if (data_root && data_esetroot) {
				if (type == XA_PIXMAP && *((Pixmap *) data_root) == *((Pixmap *) data_esetroot)) {
					XKillClient(dpy2, *((Pixmap *)
								data_root));
				}
			}
		}
	}

	if (data_root) {
		XFree(data_root);
	}

	if (data_esetroot) {
		XFree(data_esetroot);
	}

	/* This will locate the property, creating it if it doesn't exist */
	prop_root = XInternAtom(dpy2, "_XROOTPMAP_ID", False);
	prop_esetroot = XInternAtom(dpy2, "ESETROOT_PMAP_ID", False);

	if (prop_root == None || prop_esetroot == None) {
		fprintf(stderr, "creation of pixmap property failed.");
	}

	XChangeProperty(dpy2, root2, prop_root, XA_PIXMAP, 32, PropModeReplace, (unsigned char *) &pmap_d2, 1);
	XChangeProperty(dpy2, root2, prop_esetroot, XA_PIXMAP, 32, PropModeReplace, (unsigned char *) &pmap_d2, 1);

	XSetWindowBackgroundPixmap(dpy2, root2, pmap_d2);
	XClearWindow(dpy2, root2);
	XFlush(dpy2);
	XSetCloseDownMode(dpy2, RetainPermanent);
	XCloseDisplay(dpy2);
}

void draw() {
	/* screen resolution */
	Screen *screen = ScreenOfDisplay(dpy, 0);
	int width = screen->width;
	int height = screen->height;

	int depth = DefaultDepth(dpy, DefaultScreen(dpy));
	Pixmap pmap = XCreatePixmap(dpy, root, width, height, depth);

	/* locate uniforms */
	shader_bind(shader);
	int locResolution = shader_get_location(shader, "resolution");
	int locMouse = shader_get_location(shader, "mouse");
	int locTime = shader_get_location(shader, "time");
	shader_unbind();

	/* create a new framebuffer */
	unsigned int fbo;
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);

	/* create a new texture */
	unsigned int texture;
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);


	/* apply texture to framebuffer */
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	/* setup timer */
	struct timespec start, end;
	clock_gettime(CLOCK_MONOTONIC_RAW, &start);

	/* used for converting framebuffer to Imlib_Image */
	unsigned int* buffer = (unsigned int*) malloc(width * height * 4);

	Window window_returned;
	int root_x, root_y;
	int win_x, win_y;
	unsigned int mask_return;

	while (keep_running) {
		// TODO: add framerate limiter here

		if (options.mode == WINDOW) {
			XGetWindowAttributes(dpy, win, &gwa);
			width = gwa.width;
			height = gwa.height;
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		}

		clock_gettime(CLOCK_MONOTONIC_RAW, &end);
		uint64_t delta_us = (end.tv_sec - start.tv_sec) * 1000000 + (end.tv_nsec - start.tv_nsec) / 1000;

		/* change viewport, and scale it down depending on quality level */
		glViewport(0, 0, width * options.quality, height * options.quality);

		/* clear Framebuffer */
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		/* capture mouse position */
		XQueryPointer(dpy, root, &window_returned,
				&window_returned, &root_x, &root_y, &win_x, &win_y,
				&mask_return);

		/* bind shader background */
		shader_bind(shader);
		shader_set_float(locTime, (float)delta_us * 0.000001f * options.speed);
		shader_set_vec2(locResolution, width * options.quality, height * options.quality);
		shader_set_vec2(locMouse, (float)(root_x) / width, 1.0 - (float)(root_y) / height);

		/* render shader on framebuffer */
		glPushMatrix();
		glColor3f(1.0, 1.0, 1.0);
		glBegin(GL_QUADS);
		glVertex2f(0.0, 0.0);
		glVertex2f(1.0, 0.0);
		glVertex2f(1.0, 1.0);
		glVertex2f(0.0, 1.0);
		glEnd();
		glPopMatrix();
		shader_unbind();

		/* change viewport to default */
		glViewport(0, 0, width, height);

		/* bind texture to render it on screen */
		glEnable(GL_TEXTURE_2D);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);  // unbind FBO to set the default framebuffer
		glBindTexture(GL_TEXTURE_2D, texture); // color attachment texture

		/* clear screen */
		glClearColor(0.0, 0.0, 0.0, 1.0);
		glClear(GL_COLOR_BUFFER_BIT);

		/* render texture on screen */
		glPushMatrix();
		glScalef(1.0 / options.quality, 1.0 / options.quality, 1.0);
		glTranslatef(0.0, options.quality - 1.0, 0.0);
		glColor3f(1.0, 1.0, 1.0);
		glBegin(GL_QUADS);
		glTexCoord2f(0, 1);
		glVertex2f(0, 0);
		glTexCoord2f(1, 1);
		glVertex2f(1, 0);
		glTexCoord2f(1, 0);
		glVertex2f(1, 1);
		glTexCoord2f(0, 0);
		glVertex2f(0, 1);
		glEnd();
		glPopMatrix();

		// in root mode, get pixels from gl context and convert it to an Pixbuf and draw it on root window
		if (options.mode == ROOT) { 

			/* create Imlib_Image from current Frame */
			glReadPixels(0, 0, width, height, GL_BGRA, GL_UNSIGNED_BYTE, buffer); // a lot of cpu usage here :/

			Imlib_Image img = imlib_create_image_using_data(width, height, buffer);
			imlib_context_set_image(img);
			imlib_context_set_drawable(pmap);
			imlib_image_flip_vertical();
			imlib_render_image_on_drawable_at_size(0, 0, width, height);
			imlib_free_image_and_decache();

			set_pixmap_to_root(pmap, width, height);
		} else { 
			glXSwapBuffers(dpy, win); // on mode window, swap buffer to x11 window
		}
	}

	free(buffer);
}

static void sig_handler(int sig) {
    keep_running = 0;
}

int main(int argc, char **argv) {
	signal(SIGINT, sig_handler);

	int argument_count = 4;
	ArgOption arguments[] = {
		(ArgOption){
			.abbreviation = "-q", .value = "1", .name = "--quality",
			.description = "Changes quality level of the shader, default: 1."
		}, (ArgOption){
			.abbreviation = "-s", .value = "1", .name = "--speed",
			.description = "Changes animation speed, default 1."
		}, (ArgOption){
			.abbreviation = "-o", .value = "1", .name = "--opacity",
			.description = "Sets background window transparency if in window/background mode"
		}, (ArgOption){
			.abbreviation = "-m", .value = "background", .name = "--mode",
			.description = "Changes rendering mode. Modes: root, window, background"
	}};

	// Check for arguments
	if (argc <= 1) {
		print_help(arguments, argument_count);
		return 0;
	}

	char *file_path = get_argument_values(argc, argv, arguments, argument_count);
	if (*file_path == '\0') {
		fprintf(stderr, "Error: File not specified!\n");
		print_help(arguments, argument_count);
		return EXIT_FAILURE;
	}

	// Check if file exists
	if (access(file_path, F_OK) == -1) {
		fprintf(stderr, "ERROR: File at '%s' does not exist\n", file_path);
		return EXIT_FAILURE;
	}
	
	options.quality = fmin(fmax(atof(arguments[0].value), 0.01), 1);
	options.speed = atof(arguments[1].value);
	options.opacity = atof(arguments[2].value);
	options.mode = in_to_enum(arguments[3].value, mode_conversion_table, 3);
	
	if (options.mode == -1) {
		fprintf(stderr, "ERROR: Mode \"%s\" does not exist\n", arguments[3].value);
		print_help(arguments, argument_count);
		return EXIT_FAILURE;
	}

	init(file_path);
	draw();

	return EXIT_SUCCESS;
}
