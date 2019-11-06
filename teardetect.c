#include <glad/glad.h>
#if defined(WIN32)
#include <glad/glad_wgl.h>
#define GLFW_EXPOSE_NATIVE_WIN32
//#define GLFW_EXPOSE_NATIVE_WGL
#elif defined(LINUX)
#define GLFW_EXPOSE_NATIVE_X11
//#define GLFW_EXPOSE_NATIVE_GLX
#include <glad/glad_glx.h>
#endif
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <errno.h>

#define APPTITLE "GLTearDetect"

/****************************************************************************
 * DATA STRUCTURES                                                          *
 ****************************************************************************/
typedef struct {
	GLFWwindow *win;
	int pos[2];
	int size[2];
	int windowed_pos[2];
	int windowed_size[2];
	unsigned int flags;
} TDWindow;

/* window flags */
#define TDWIN_FULLSCREEN		0x1
#define TDWIN_FULLSCREEN_MODE_SWITCH	0x2
#define TDWIN_DECORATED			0x4
#define TDWIN_FLAGS_DEFAULT		TDWIN_DECORATED

typedef enum {
	TDDISP_NONE=0,
	TDDISP_COLORS,
	TDDISP_PULSE,
	TDDISP_BARS,
	TDDISP_MODE_COUNT
} TDDisplayMode;

typedef enum {
#if defined(WIN32)
	TDSWAP_CONTROL_EXT=0,
#elif defined(LINUX)
	TDSWAP_CONTROL_EXT=0,
	TDSWAP_CONTROL_SGI,
	TDSWAP_CONTROL_MESA,
#endif
	TDSWAP_CONTROL_COUNT
} TDSwapControlMode;

typedef struct {
	GLfloat speed;
} TDPulse;

typedef struct {
	GLuint program;
	GLuint vao;
	GLint loc_data;
	GLfloat data[3];
} TDBars;

#define TIMER_QUERY_COUNT 10

typedef struct {
	TDWindow win;
	TDDisplayMode mode;
	TDSwapControlMode swapControlMode;
	TDPulse pulse;
	TDBars bars;
	int swapInterval;
	unsigned int flags;
	unsigned int frame;
	unsigned int frame_int;
	GLfloat delta;
	GLfloat time;
	GLuint timer_query_obj[TIMER_QUERY_COUNT];
	GLuint64 timestamp[TIMER_QUERY_COUNT];
	double avg_lat;
	double avg_fps;
	double cur_lat;
	uint64_t busy_wait_ns;
	uint64_t sleep_ns;
} TDContext;

/* ctx flags */
#define TDCTX_RUN		0x1
#define TDCTX_DROP_WINDOW	0x2
#define TDCTX_SWAP_INTERVAL_SET	0x4
#define TDCTX_BINDING_EXTENSIONS_LOADED 0x8
#define TDCTX_GL_FLUSH		0x10
#define TDCTX_GL_FINISH		0x20
#define TDCTX_FLAGS_DEFAULT	TDCTX_RUN

/****************************************************************************
 * CONSOLE OUTPUT                                                           *
 ****************************************************************************/

static void
error(int exit_code, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n', stderr);
	fflush(stderr);
	exit(exit_code);
}

static void
info(int level, const char *fmt, ...)
{
	va_list args;

	(void)level;
	va_start(args, fmt);
	vfprintf(stdout, fmt, args);
	va_end(args);
	putchar('\n');
}

static void
warn(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	fputc('\n',stderr);
}
/****************************************************************************
 * TIMERS                                                                   *
 * nanoseconds since unspecified point in time                              *
 ****************************************************************************/

static uint64_t
get_current_time()
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)(ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
}

static void
sleep_nanoseconds(uint64_t amount)
{
	struct timespec ts;
	int repeat;

	ts.tv_sec=(time_t)(amount / 1000000000ULL);
	ts.tv_nsec = (long)(amount % 1000000000ULL);

	do {
		struct timespec rem;
		rem.tv_sec=0;
		rem.tv_nsec=0;
		repeat = 0;
		if (nanosleep(&ts, &rem)) {
			if (errno == EINTR) {
				ts = rem;
				repeat = 1;
			}
		}
	} while (repeat);
}

/****************************************************************************
 * GL WINDOW                                                                *
 ****************************************************************************/

static void
td_win_init(TDWindow *w)
{
	w->win=NULL;
	w->windowed_pos[0]=100;
	w->windowed_pos[1]=100;
	w->windowed_size[0]=800;
	w->windowed_size[1]=600;
	w->flags=TDWIN_FLAGS_DEFAULT;

	w->pos[0]=w->windowed_pos[0];
	w->pos[1]=w->windowed_pos[1];
	w->size[0]=w->windowed_size[0];
	w->size[1]=w->windowed_size[1];
}

static void
td_win_destroy(TDWindow *w)
{
	if (w) {
		if (w->win) {
			glfwDestroyWindow(w->win);
			info(1,"destroyed GL window");
			w->win=NULL;
		}
	}
}

static int
td_win_create(TDWindow *w)
{
	GLFWmonitor *monitor=NULL;

	td_win_destroy(w);

	glfwDefaultWindowHints();
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_DECORATED, (w->flags & TDWIN_DECORATED)?GL_TRUE:GL_FALSE);
	if (w->flags & TDWIN_FULLSCREEN) {
		monitor=glfwGetPrimaryMonitor();
		if (monitor) {
			const GLFWvidmode *videoMode=glfwGetVideoMode(monitor);
			if (videoMode) {
				// resolution
				w->size[0]=videoMode->width;
				w->size[1]=videoMode->height;
				w->pos[0]=0;
				w->pos[1]=0;
				if (w->flags & TDWIN_FULLSCREEN_MODE_SWITCH) {
					// color bit depths
					glfwWindowHint(GLFW_RED_BITS, videoMode->redBits);
					glfwWindowHint(GLFW_GREEN_BITS, videoMode->greenBits);
					glfwWindowHint(GLFW_BLUE_BITS, videoMode->blueBits);
					// refresh rate
					glfwWindowHint(GLFW_REFRESH_RATE, videoMode->refreshRate);
				} else {
					monitor=NULL;
				}
			} else {
				warn("failed to get video mode");
			}
		} else {
			warn("failed to get primary monitor");
		}
	} else {
		w->pos[0]=w->windowed_pos[0];
		w->pos[1]=w->windowed_pos[1];
		w->size[0]=w->windowed_size[0];
		w->size[1]=w->windowed_size[1];
	}

	if ( !(w->win=glfwCreateWindow( w->size[0], w->size[1], APPTITLE, monitor, NULL)) ) {
		return -1;
	}
	if (!monitor) {
		glfwSetWindowPos(w->win, w->pos[0], w->pos[1]);
	}

	info(1,"created new GL window (%dx%d)",w->size[0],w->size[1]);
	glfwMakeContextCurrent(w->win);
	if (!gladLoadGL()) {
		warn("failed to initialize GLAD");
		return -2;
	}
	return 0;
}

/****************************************************************************
 * GL HELPER                                                                *
 ****************************************************************************/

static GLuint make_shader(const GLchar *src, GLenum type)
{
	GLint status=GL_FALSE;
	GLuint sh=glCreateShader(type);
	if (sh) {
		glShaderSource(sh, 1, &src, NULL);
		glCompileShader(sh);
		glGetShaderiv(sh, GL_COMPILE_STATUS, &status);
		if (status != GL_TRUE) {
			GLchar buf[8192];
			glGetShaderInfoLog(sh, sizeof(buf), NULL, buf);
			warn("shader compilation failed: %s", buf);
		}
	}
	return sh;
}

static GLuint make_program(const GLchar *vs, const GLchar *fs)
{
	GLuint sh_vs, sh_fs, prog;
	GLint status=GL_FALSE;

	sh_vs=make_shader(vs, GL_VERTEX_SHADER);
	sh_fs=make_shader(fs, GL_FRAGMENT_SHADER);
	prog=glCreateProgram();
	glAttachShader(prog, sh_vs);
	glAttachShader(prog, sh_fs);
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &status);
	if (status != GL_TRUE) {
		GLchar buf[8192];
		glGetProgramInfoLog(prog, sizeof(buf), NULL, buf);
		warn("program link failed: %s",buf);

	} else {
		info(5,"program %u linked successfully", prog);
	}
	glDetachShader(prog, sh_fs);
	glDetachShader(prog, sh_vs);
	glDeleteShader(sh_vs);
	glDeleteShader(sh_fs);
	return prog;
}

/****************************************************************************
 * DIFFERENT DISPLAY MODES                                                  *
 ****************************************************************************/

/* ----------------------- TDDISP_NONE ------------------------------------*/

static void
td_disp_none(TDContext *ctx)
{
	(void)ctx;
}

/* ----------------------- TDDISP_COLORS ----------------------------------*/

static void
td_disp_colors(TDContext *ctx)
{
	static const GLfloat colors[][4]={
		{1.0f, 0.0f, 0.0f, 1.0f},
		{0.0f, 1.0f, 0.0f, 1.0f},
		{0.0f, 0.0f, 1.0f, 1.0f},
		{1.0f, 1.0f, 0.0f, 1.0f},
		{0.0f, 0.0f, 0.0f, 1.0f},
		{0.0f, 1.0f, 1.0f, 1.0f},
		{1.0f, 0.0f, 1.0f, 1.0f},
		{1.0f, 1.0f, 1.0f, 1.0f}
	};
	const GLfloat *c=&colors[ctx->frame % 8][0];
	glClearColor(c[0], c[1], c[2], c[3]);
	glClear(GL_COLOR_BUFFER_BIT);
}

/* ------------------------ TDDISP_PULSE ----------------------------------*/

static void
td_disp_pulse_init(TDPulse *pulse)
{
	pulse->speed=3.0f;
}

static void
td_disp_pulse(TDContext *ctx)
{
	GLfloat v=sinf(ctx->time * ctx->pulse.speed)*0.5f+0.5f;
	glClearColor(v,v,v,1.0f);
	glClear(GL_COLOR_BUFFER_BIT);
}

/* ----------------------- TDDISP_BARS -----------------------------------*/

static const GLchar *td_disp_bars_vs="#version 330 core\n"
	"void main() {\n"
	"	vec2 pos=vec2( (gl_VertexID & 2)>>1, 1 - (gl_VertexID & 1));\n"
	"	gl_Position=vec4(pos*2.0-1.0,0,1);\n"
	"}\n";

static const GLchar *td_disp_bars_fs="#version 330 core\n"
	"out vec4 color;\n"
	"uniform vec3 data;\n"
	"void main() {\n"
	"	vec3 c[2]=vec3[2](vec3(0.0f, 0.0f, 0.0f),vec3(1.0f,1.0f,1.0f));\n"
	"	color=vec4(c[(int(gl_FragCoord.x + data.z)/int(data.x))%2] ,1);\n"
	"}\n";

static void
td_disp_bars_init(TDBars *bars)
{
	bars->program=0;
	bars->vao=0;
	bars->loc_data=-1;
	bars->data[0]=32.0f;
	bars->data[1]=16.0f*bars->data[0];
	bars->data[2]=0.0f;
}

static void
td_disp_bars_gl_init(TDBars *bars)
{
	bars->program=make_program(td_disp_bars_vs, td_disp_bars_fs);
	bars->loc_data=glGetUniformLocation(bars->program, "data");
	glGenVertexArrays(1, &bars->vao);
}

static void
td_disp_bars_destroy(TDBars *bars)
{
	if (bars->program) {
		glDeleteProgram(bars->program);
		bars->program=0;
	}
	if (bars->vao) {
		glDeleteVertexArrays(1, &bars->vao);
		bars->vao=0;
	}
}

static void
td_disp_bars(TDContext *ctx)
{
	ctx->bars.data[2] = fmodf(ctx->bars.data[1] * ctx->time, ctx->bars.data[0]*2.0f);
	glClear(GL_COLOR_BUFFER_BIT);
	glUseProgram(ctx->bars.program);
	glUniform3fv(ctx->bars.loc_data, 1, ctx->bars.data);
	glBindVertexArray(ctx->bars.vao);
	glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
	glUseProgram(0);
	glBindVertexArray(0);
}

/* --------------------------- generic ------------------------------------*/
static void
td_disp(TDContext *ctx)
{
	switch (ctx->mode) {
		case TDDISP_NONE:
			td_disp_none(ctx);
			break;
		case TDDISP_COLORS:
			td_disp_colors(ctx);
			break;
		case TDDISP_PULSE:
			td_disp_pulse(ctx);
			break;
		case TDDISP_BARS:
			td_disp_bars(ctx);
			break;
		default:
			info(0,"invalid display mode 0x%x",(unsigned)ctx->mode);
	}

	if (ctx->flags & TDCTX_GL_FLUSH) {
		glFlush();
	}

	if (ctx->flags & TDCTX_GL_FINISH) {
		glFinish();
	}

	if (ctx->busy_wait_ns) {
		uint64_t now = get_current_time();
		volatile int i;
		while (get_current_time() - now < ctx->busy_wait_ns) {
			i++;
		}	
		(void)i;
	}
	if (ctx->sleep_ns) {
		sleep_nanoseconds(ctx->sleep_ns);
	}
}

/****************************************************************************
 * WINDOW TITLE                                                             *
 ****************************************************************************/

#if defined(WIN32)
#define my_snprintf sprintf_s
#else
#define my_snprintf snprintf
#endif

static void
td_ctx_set_title(TDContext *ctx)
{
	char title[2048],swapi[64];
	if (ctx->flags & TDCTX_SWAP_INTERVAL_SET) {
		my_snprintf(swapi,sizeof(swapi),"%d",ctx->swapInterval);
	} else {
		my_snprintf(swapi,sizeof(swapi),"unset");
	}
	my_snprintf(title, sizeof(title),
		APPTITLE": [%u:%s] %.2fFPS, lat: %.3fms, cur_lat: %.3fms%s%s, sleep: %.1fms, busywait: %.1fms", 
		(unsigned)ctx->swapControlMode, swapi, ctx->avg_fps, ctx->avg_lat, ctx->cur_lat, 
		((ctx->flags & TDCTX_GL_FLUSH)?", flush":""),
		((ctx->flags & TDCTX_GL_FINISH)?", finish":""),
		ctx->sleep_ns / 1000000.0, ctx->busy_wait_ns/1000000.0);
	if (ctx->win.flags & TDWIN_DECORATED) {
		glfwSetWindowTitle(ctx->win.win, title);
	}
	info(0,"%s",title);
}

/****************************************************************************
 * SWAP_CONTROL                                                             *
 ****************************************************************************/

static void
td_ctx_set_swap_interval(TDContext *ctx)
{
	const char *func="NONE";
#if defined(WIN32)
	if (!(ctx->flags & TDCTX_BINDING_EXTENSIONS_LOADED)) {
		HWND hwin=glfwGetWin32Window(ctx->win.win);
		HDC hdc=GetDC(hwin);
		if (!gladLoadWGL(hdc)) {
			warn("failed to load WGL extensions");
			ReleaseDC(hwin,hdc);
			return;
		}
		ReleaseDC(hwin,hdc);
		info(2,"loaded WGL extensions");
		ctx->flags |= TDCTX_BINDING_EXTENSIONS_LOADED;
	}
	switch(ctx->swapControlMode) {
		case TDSWAP_CONTROL_EXT:
			func="EXT";
			if (GLAD_WGL_EXT_swap_control) {
				wglSwapIntervalEXT(ctx->swapInterval);
			} else {
				warn("wglSwapIntervalEXT not available");
				return;
			}
			break;
		default:
			warn("swap control mode %u not available", (unsigned)ctx->swapControlMode);
			return;
	}
	info(0,"setting swap interval to %d [wglSwapInterval%s]", ctx->swapInterval, func);
#elif defined(LINUX)
	Display *dpy = glfwGetX11Display();

	if (!(ctx->flags & TDCTX_BINDING_EXTENSIONS_LOADED)) {
		Window win=glfwGetX11Window(ctx->win.win);
		int screen=0;
		XWindowAttributes attr;
		XGetWindowAttributes(dpy, win, &attr);
		screen=XScreenNumberOfScreen(attr.screen);
		if (!gladLoadGLX(dpy, screen)) {
			warn("failed to load GLX extensions");
			return;
		}
		info(2,"loaded GLX extensions");
		ctx->flags |= TDCTX_BINDING_EXTENSIONS_LOADED;
	}

	switch(ctx->swapControlMode) {
		case TDSWAP_CONTROL_EXT:
			func="EXT";
			if (GLAD_GLX_EXT_swap_control) {
				GLXDrawable drawable=glXGetCurrentDrawable(dpy);
				if (drawable == None) {
					warn("failed to get current GLX drawable!");
				}
				glXSwapIntervalEXT(dpy, drawable, ctx->swapInterval);
			} else {
				warn("glXSwapIntervalEXT not available");
				return;
			}
			break;
		case TDSWAP_CONTROL_SGI:
			func="SGI";
			if (GLAD_GLX_SGI_swap_control) {
				glXSwapIntervalSGI(ctx->swapInterval);
			} else {
				warn("glXSwapIntervalSGI not available");
				return;
			}
			break;
		/* TODO: glad currently does not have the MESA variant */
		default:
			warn("swap control mode %u not available", (unsigned)ctx->swapControlMode);
			return;
	}
	info(0,"setting swap interval to %d [glXSwapInterval%s]", ctx->swapInterval, func);
#else
	warn("swap control not implemented for this platform");
	return;
#endif
	ctx->flags |= TDCTX_SWAP_INTERVAL_SET;
	td_ctx_set_title(ctx);
}

/****************************************************************************
 * EVENT HANDLING                                                           *
 ****************************************************************************/

static void
td_ctx_switch_fullscreen(TDContext *ctx, unsigned int fs_flags)
{
	if (ctx->win.flags & TDWIN_FULLSCREEN) {
		info(1,"switch to windowed mode requested");
		ctx->win.flags &= ~(TDWIN_FULLSCREEN | TDWIN_FULLSCREEN_MODE_SWITCH);
		ctx->win.flags |= TDWIN_DECORATED;
	} else {
		info(1,"switch to fullscreen requested");
		ctx->win.flags &= ~(TDWIN_DECORATED | TDWIN_FULLSCREEN_MODE_SWITCH);
		ctx->win.flags |= TDWIN_FULLSCREEN | fs_flags;
	}
	ctx->flags |= TDCTX_DROP_WINDOW;
}

static void
td_ctx_keyhandler(GLFWwindow *win, int key, int scancode, int action, int mods)
{
	TDContext *ctx=glfwGetWindowUserPointer(win);

	(void)scancode;
	if (action == GLFW_PRESS) {
		switch(key) {
			case GLFW_KEY_ESCAPE:
				ctx->flags &= ~TDCTX_RUN;
				break;
			case GLFW_KEY_RIGHT:
			case GLFW_KEY_SPACE:
				if (++ctx->mode >= TDDISP_MODE_COUNT) {
					ctx->mode -= TDDISP_MODE_COUNT;
				}
				info(5,"switched to mode %u",(unsigned)ctx->mode);
				break;
			case GLFW_KEY_LEFT:
			case GLFW_KEY_BACKSPACE:
				if (ctx->mode > (TDDisplayMode)0) {
					ctx->mode--;
				} else {
					ctx->mode=TDDISP_MODE_COUNT-1;
				}
				info(5,"switched to mode %u",(unsigned)ctx->mode);
				break;
			case GLFW_KEY_ENTER:
			case 'W':
				ctx->flags |= TDCTX_DROP_WINDOW;
				break;
			case GLFW_KEY_UP:
				ctx->pulse.speed *= 2.0f;
				ctx->bars.data[1] *= 2.0f;
				break;
			case GLFW_KEY_DOWN:
				ctx->pulse.speed /= 2.0f;
				ctx->bars.data[1] /= 2.0f;
				break;
			case GLFW_KEY_HOME:
				ctx->pulse.speed = 3.0f;
				ctx->bars.data[0] = 32.0f;
				ctx->bars.data[1] = 16.0f*ctx->bars.data[0];
				break;
			case GLFW_KEY_KP_MULTIPLY:
				ctx->bars.data[0] *= 2.0f;
				break;
			case GLFW_KEY_KP_DIVIDE:
				ctx->bars.data[0] /= 2.0f;
				break;
			case 'F':
				td_ctx_switch_fullscreen(ctx, (mods & GLFW_MOD_SHIFT)?TDWIN_FULLSCREEN_MODE_SWITCH:0);
				break;
			case 'S':
				if (!(mods & GLFW_MOD_SHIFT)) {
					if (ctx->swapInterval) {
						ctx->swapInterval=0;
					} else {
						ctx->swapInterval=1;
					}
				}
				td_ctx_set_swap_interval(ctx);
				break;
			case '=':
			case GLFW_KEY_KP_ADD:
				ctx->swapInterval++;
				td_ctx_set_swap_interval(ctx);
				break;
			case '-':
			case GLFW_KEY_KP_SUBTRACT:
				ctx->swapInterval--;
				td_ctx_set_swap_interval(ctx);
				break;
			case GLFW_KEY_PAGE_UP:
				if (++ctx->swapControlMode >= TDSWAP_CONTROL_COUNT) {
					ctx->swapControlMode -= TDSWAP_CONTROL_COUNT;
				}
				td_ctx_set_title(ctx);
				break;
			case GLFW_KEY_PAGE_DOWN:
				if (ctx->swapControlMode > 0) {
					ctx->swapControlMode--;

				} else {
					ctx->swapControlMode=TDSWAP_CONTROL_COUNT-1;
				}
				td_ctx_set_title(ctx);
				break;
			case 'B':
				if ((mods & GLFW_MOD_SHIFT)) {
					if (ctx->busy_wait_ns > 1000000) {
						ctx->busy_wait_ns -= 1000000;
					} else {
						ctx->busy_wait_ns = 0;
					}
				} else {
					ctx->busy_wait_ns += 1000000;
				}
				td_ctx_set_title(ctx);
				break;
			case 'V':
				if ((mods & GLFW_MOD_SHIFT)) {
					if (ctx->sleep_ns > 1000000) {
						ctx->sleep_ns -= 1000000;
					} else {
						ctx->sleep_ns = 0;
					}
				} else {
					ctx->sleep_ns += 1000000;
				}
				td_ctx_set_title(ctx);
				break;
			case 'C':
				if ((mods & GLFW_MOD_SHIFT)) {
					ctx->flags ^= TDCTX_GL_FLUSH;
				} else {
					ctx->flags ^= TDCTX_GL_FINISH;
				}
				td_ctx_set_title(ctx);
				break;
		}

	}

}

static void
td_ctx_resize(GLFWwindow *win, int w, int h)
{
	TDContext *ctx=glfwGetWindowUserPointer(win);
	ctx->win.size[0]=w;
	ctx->win.size[1]=h;
	if (!(ctx->win.flags & TDWIN_FULLSCREEN)) {
		ctx->win.windowed_size[0]=w;
		ctx->win.windowed_size[1]=h;
	}
}

static void
td_ctx_reposition(GLFWwindow *win, int x, int y)
{
	TDContext *ctx=glfwGetWindowUserPointer(win);
	ctx->win.pos[0]=x;
	ctx->win.pos[1]=y;
	if (!(ctx->win.flags & TDWIN_FULLSCREEN)) {
		ctx->win.windowed_pos[0]=x;
		ctx->win.windowed_pos[1]=y;
	}
}

/****************************************************************************
 * BASIC FRAMEWORK                                                          *
 ****************************************************************************/

static void
td_ctx_reset(TDContext *ctx)
{
	ctx->avg_lat=-1.0;
	ctx->avg_fps=-1.0;
	ctx->cur_lat=-1.0;
	ctx->flags &= ~(TDCTX_DROP_WINDOW | TDCTX_BINDING_EXTENSIONS_LOADED | TDCTX_SWAP_INTERVAL_SET);
}

static void
td_ctx_init(TDContext *ctx)
{
	int i;

	td_win_init(&ctx->win);
	td_disp_pulse_init(&ctx->pulse);
	td_disp_bars_init(&ctx->bars);
	ctx->mode=TDDISP_BARS;
	ctx->swapControlMode=(TDSwapControlMode)0;
	ctx->swapInterval=1;
	ctx->flags=TDCTX_FLAGS_DEFAULT;
	td_ctx_reset(ctx);
	ctx->cur_lat=-1.0;
	for (i=0; i<TIMER_QUERY_COUNT; i++) {
		ctx->timer_query_obj[i]=0;
		ctx->timestamp[i]=0;
	}
	ctx->busy_wait_ns = 0;
	ctx->sleep_ns = 0;
}

static int
td_ctx_config(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	return 0;
}

static void
td_ctx_destroy(TDContext *ctx)
{
	td_disp_bars_destroy(&ctx->bars);
	td_win_destroy(&ctx->win);
}

static void
td_ctx_gl_init(TDContext *ctx)
{
	td_disp_bars_gl_init(&ctx->bars);
	glGenQueries(TIMER_QUERY_COUNT, ctx->timer_query_obj);
}

static void
td_ctx_gl_destroy(TDContext *ctx)
{
	td_disp_bars_destroy(&ctx->bars);
	glDeleteQueries(TIMER_QUERY_COUNT, ctx->timer_query_obj);
}

static void
td_ctx_main_loop(TDContext *ctx)
{
	double t_now,t_start=glfwGetTime(),t_last=t_start,t_prev=t_last;
	double lat_ms=0.0;
	ctx->frame=0;
	ctx->frame_int=0;

	while((ctx->flags & (TDCTX_RUN | TDCTX_DROP_WINDOW)) == TDCTX_RUN) {
		unsigned int cur_query = ctx->frame % TIMER_QUERY_COUNT;
		double elapsed;

		glfwPollEvents();
		if (glfwWindowShouldClose(ctx->win.win)) {
			ctx->flags &= ~ TDCTX_RUN;
		}

		glViewport(0,0,ctx->win.size[0],ctx->win.size[1]);
		td_disp(ctx);

		glfwSwapBuffers(ctx->win.win);
		t_now=glfwGetTime();
		if (ctx->frame >= TIMER_QUERY_COUNT) {
			GLuint64 result;
			glGetQueryObjectui64v(ctx->timer_query_obj[cur_query], GL_QUERY_RESULT, &result);
			ctx->cur_lat = ((double)(result - ctx->timestamp[cur_query]))/1000000.0;
			lat_ms += ctx->cur_lat;
		}
		glQueryCounter(ctx->timer_query_obj[cur_query], GL_TIMESTAMP);
		glGetInteger64v(GL_TIMESTAMP, (GLint64*)&ctx->timestamp[cur_query]);

		ctx->frame++;
		ctx->frame_int++;
	
		ctx->delta=(GLfloat)(t_now-t_prev);
		ctx->time=(GLfloat)(t_now-t_start);
		t_prev=t_now;
		elapsed=t_now-t_last;
		if (elapsed > 1.0) {
			ctx->avg_fps=(double)ctx->frame_int/elapsed;
			ctx->avg_lat=(double)lat_ms / (double)ctx->frame_int;
			td_ctx_set_title(ctx);
			ctx->frame_int=0;
			lat_ms=0.0f;
			t_last=t_now;
		}
	}
}

static void
td_ctx_run(TDContext *ctx)
{
	while(ctx->flags & TDCTX_RUN) {
		if (!ctx->win.win) {
			if (td_win_create(&ctx->win)) {
				error(3,"failed to create GL window");
				break;
			}
		}
		td_ctx_reset(ctx);
		td_ctx_set_title(ctx);
		glfwSetWindowUserPointer(ctx->win.win, ctx);
		glfwSetKeyCallback(ctx->win.win, td_ctx_keyhandler);
		glfwSetFramebufferSizeCallback(ctx->win.win, td_ctx_resize);
		glfwSetWindowPosCallback(ctx->win.win, td_ctx_reposition);
		td_ctx_gl_init(ctx);
		td_ctx_main_loop(ctx);
		td_ctx_gl_destroy(ctx);
		td_win_destroy(&ctx->win);
	}
}

/****************************************************************************
 * PROGRAM ENTRY POINT                                                      *
 ****************************************************************************/

int main(int argc, char **argv)
{
	TDContext ctx;

	if (!glfwInit()) {
		error(1,"GFLW initialization failed");
	}

	td_ctx_init(&ctx);
	if (td_ctx_config(argc, argv)) {
		error(2,"invalid parameters");
	}

	td_ctx_run(&ctx);

	td_ctx_destroy(&ctx);
	glfwTerminate();
	return 0;
}
