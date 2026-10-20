// Headless GLES 3.1 compute micro-benchmarks for the Adreno 506 (Mesa freedreno).
// Usage: gpubench [test ...]   tests: info bw alu gemm conv (default: all)
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl32.h>
#include <gbm.h>
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static double now_ms(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

static void die(const char *m)
{
	fprintf(stderr, "fatal: %s (gl 0x%x egl 0x%x)\n", m, glGetError(), eglGetError());
	exit(1);
}

static void GL_APIENTRY dbg(GLenum src, GLenum type, GLuint id, GLenum sev, GLsizei len, const GLchar *msg,
			    const void *user)
{
	fprintf(stderr, "  [gl] %s\n", msg);
}

static void init_egl(void)
{
	int fd = open("/dev/dri/renderD128", O_RDWR);
	if (fd < 0)
		die("open renderD128");
	struct gbm_device *gbm = gbm_create_device(fd);
	if (!gbm)
		die("gbm_create_device");
	EGLDisplay dpy = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, NULL);
	if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL))
		die("eglInitialize");
	if (!eglBindAPI(EGL_OPENGL_ES_API))
		die("eglBindAPI");
	const EGLint attr[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
				EGL_NONE };
	EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attr);
	if (ctx == EGL_NO_CONTEXT)
		die("eglCreateContext");
	if (!eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
		die("eglMakeCurrent");
	if (getenv("GPUBENCH_DEBUG")) {
		glEnable(GL_DEBUG_OUTPUT);
		glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
		glDebugMessageCallback(dbg, NULL);
	}
}

static GLuint program(const char *name, const char *defs, const char *body)
{
	static char src[65536];
	snprintf(src, sizeof(src), "#version 310 es\n%s\n%s", defs, body);
	const char *p = src;
	GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
	glShaderSource(sh, 1, &p, NULL);
	glCompileShader(sh);
	GLint ok;
	char log[8192];
	glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		fprintf(stderr, "%s: compile failed:\n%s\n", name, log);
		return 0;
	}
	GLuint pr = glCreateProgram();
	glAttachShader(pr, sh);
	glLinkProgram(pr);
	glGetProgramiv(pr, GL_LINK_STATUS, &ok);
	if (!ok) {
		glGetProgramInfoLog(pr, sizeof(log), NULL, log);
		fprintf(stderr, "%s: link failed:\n%s\n", name, log);
		return 0;
	}
	return pr;
}

static GLuint buffer(GLsizeiptr size, const void *data)
{
	GLuint b;
	glGenBuffers(1, &b);
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
	glBufferData(GL_SHADER_STORAGE_BUFFER, size, data, GL_STATIC_DRAW);
	return b;
}

static void *readback(GLuint b, GLsizeiptr size)
{
	void *out = malloc(size);
	glMemoryBarrier(GL_ALL_BARRIER_BITS);
	glFinish();
	glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
	void *m = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, size, GL_MAP_READ_BIT);
	if (!m)
		die("map");
	memcpy(out, m, size);
	glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
	return out;
}

// Mean milliseconds per dispatch, best of 3 batches; batches grow until >= 150 ms.
static double time_dispatch(GLuint prog, GLuint gx, GLuint gy, GLuint gz)
{
	glUseProgram(prog);
	glDispatchCompute(gx, gy, gz);
	glFinish();
	double t0 = now_ms();
	glDispatchCompute(gx, gy, gz);
	glFinish();
	double one = now_ms() - t0;
	int iters = one > 0 ? (int)(150.0 / one) + 1 : 50;
	if (iters > 200)
		iters = 200;
	double best = 1e30;
	for (int rep = 0; rep < 3; rep++) {
		t0 = now_ms();
		for (int i = 0; i < iters; i++) {
			glDispatchCompute(gx, gy, gz);
			glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
		}
		glFinish();
		double t = (now_ms() - t0) / iters;
		if (t < best)
			best = t;
	}
	GLenum e = glGetError();
	if (e)
		fprintf(stderr, "  gl error 0x%x\n", e);
	return best;
}

static float frand(void)
{
	return (float)rand() / RAND_MAX - 0.5f;
}

// IEEE half conversion (round to nearest, no denormal handling needed for our data).
static uint16_t f2h(float f)
{
	uint32_t x;
	memcpy(&x, &f, 4);
	uint32_t s = (x >> 16) & 0x8000;
	int e = ((x >> 23) & 0xff) - 127 + 15;
	uint32_t m = x & 0x7fffff;
	if (e <= 0)
		return s;
	if (e >= 31)
		return s | 0x7c00;
	uint32_t h = s | (e << 10) | (m >> 13);
	if (m & 0x1000)
		h++;
	return h;
}

static void *to_half(const float *f, size_t n)
{
	uint16_t *h = malloc(n * 2);
	for (size_t i = 0; i < n; i++)
		h[i] = f2h(f[i]);
	return h;
}

static const char *PREC32 = "precision highp float;\n"
			    "#define VEC vec4\n#define ST vec4\n#define LD(v_) (v_)\n";
static const char *PREC16 = "precision mediump float;\n"
			    "#define VEC vec4\n#define ST uvec2\n"
			    "#define LD(v_) vec4(unpackHalf2x16((v_).x), unpackHalf2x16((v_).y))\n";

/* ---------------------------------------------------------------- info */

static void test_info(void)
{
	GLint v[3];
	printf("GL_RENDERER: %s\nGL_VERSION: %s\nGLSL: %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION),
	       glGetString(GL_SHADING_LANGUAGE_VERSION));
	glGetIntegerv(GL_MAX_COMPUTE_WORK_GROUP_INVOCATIONS, v);
	printf("max_invocations: %d\n", v[0]);
	glGetIntegerv(GL_MAX_COMPUTE_SHARED_MEMORY_SIZE, v);
	printf("max_shared_mem: %d\n", v[0]);
	glGetIntegerv(GL_MAX_SHADER_STORAGE_BLOCK_SIZE, v);
	printf("max_ssbo_size: %d\n", v[0]);
	glGetIntegerv(GL_MAX_COMPUTE_SHADER_STORAGE_BLOCKS, v);
	printf("max_compute_ssbos: %d\n", v[0]);
	for (int i = 0; i < 3; i++)
		glGetIntegeri_v(GL_MAX_COMPUTE_WORK_GROUP_COUNT, i, &v[i]);
	printf("max_groups: %d %d %d\n", v[0], v[1], v[2]);
	GLint hi[2], prec;
	glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_MEDIUM_FLOAT, hi, &prec);
	printf("mediump float (fs): range %d,%d precision %d\n", hi[0], hi[1], prec);
}

/* ---------------------------------------------------------------- bw */

static void test_bw(void)
{
	const size_t n = 4 << 20; // vec4 count: 64 MiB per buffer
	GLuint in = buffer(n * 16, NULL), out = buffer(n * 16, NULL);
	GLuint p = program("copy", "",
			   "layout(local_size_x=256) in;\n"
			   "layout(std430,binding=0) readonly buffer I{vec4 i[];};\n"
			   "layout(std430,binding=1) writeonly buffer O{vec4 o[];};\n"
			   "void main(){uint x=gl_GlobalInvocationID.x; o[x]=i[x];}\n");
	GLuint w = program("write", "",
			   "layout(local_size_x=256) in;\n"
			   "layout(std430,binding=1) writeonly buffer O{vec4 o[];};\n"
			   "void main(){uint x=gl_GlobalInvocationID.x; o[x]=vec4(float(x));}\n");
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, in);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out);
	if (p) {
		double t = time_dispatch(p, n / 256, 1, 1);
		printf("bw copy 64MiB->64MiB: %.2f ms  %.2f GB/s (read+write)\n", t, 2.0 * n * 16 / t / 1e6);
	}
	if (w) {
		double t = time_dispatch(w, n / 256, 1, 1);
		printf("bw write 64MiB:       %.2f ms  %.2f GB/s\n", t, 1.0 * n * 16 / t / 1e6);
	}
}

/* ---------------------------------------------------------------- alu */

static void test_alu(void)
{
	const char *body = "layout(local_size_x=128) in;\n"
			   "layout(std430,binding=1) writeonly buffer O{vec4 o[];};\n"
			   "uniform float k, m;\n"
			   "void main(){uint x=gl_GlobalInvocationID.x; VEC s=VEC(float(x)*1e-6);\n"
			   " VEC a=s, b=s+0.1, c=s+0.2, d=s+0.3, e=s+0.4, f=s+0.5, g=s+0.6, h=s+0.7;\n"
			   " for(int i=0;i<128;i++){\n"
			   "  a=a*k+m; b=b*k+m; c=c*k+m; d=d*k+m; e=e*k+m; f=f*k+m; g=g*k+m; h=h*k+m;\n"
			   "  a=a*k+m; b=b*k+m; c=c*k+m; d=d*k+m; e=e*k+m; f=f*k+m; g=g*k+m; h=h*k+m;}\n"
			   " o[x]=vec4(a+b+c+d+e+f+g+h);}\n";
	const GLuint threads = 128 * 1024;
	GLuint out = buffer(threads * 16, NULL);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out);
	const char *names[2] = { "fp32", "fp16" };
	const char *defs[2] = { PREC32, PREC16 };
	for (int v = 0; v < 2; v++) {
		GLuint p = program(names[v], defs[v], body);
		if (!p)
			continue;
		glUseProgram(p);
		glUniform1f(glGetUniformLocation(p, "k"), 0.999f);
		glUniform1f(glGetUniformLocation(p, "m"), 0.001f);
		double t = time_dispatch(p, threads / 128, 1, 1);
		double flops = (double)threads * 128 * 16 * 4 * 2;
		printf("alu %s: %.2f ms  %.1f GFLOPS\n", names[v], t, flops / t / 1e6);
	}
}


/* ---------------------------------------------------------------- alu2 */

// FMA-only loops with N independent vec4 chains, to find the issue-rate ceiling.
static void test_alu2(void)
{
	const GLuint threads = 256 * 1024;
	GLuint out = buffer(threads * 16, NULL);
	glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, out);
	const int chains[] = { 1, 2, 4 };
	for (int prec = 0; prec < 2; prec++)
		for (int ci = 0; ci < 3; ci++) {
			int n = chains[ci];
			char body[4096], *b = body;
			b += sprintf(b, "layout(local_size_x=128) in;\n"
					"layout(std430,binding=1) writeonly buffer O{vec4 o[];};\n"
					"uniform float k, m;\n"
					"void main(){ VEC s=VEC(float(gl_GlobalInvocationID.x)*1e-6);\n");
			for (int c = 0; c < n; c++)
				b += sprintf(b, " VEC a%d=s+%d.0;\n", c, c);
			b += sprintf(b, " for(int i=0;i<64;i++){\n");
			for (int u = 0; u < 32 / n; u++)
				for (int c = 0; c < n; c++)
					b += sprintf(b, "  a%d=a%d*k+m;\n", c, c);
			b += sprintf(b, " }\n VEC r=s;");
			for (int c = 0; c < n; c++)
				b += sprintf(b, " r+=a%d;", c);
			b += sprintf(b, " o[gl_GlobalInvocationID.x]=vec4(r);}\n");
			char name[32];
			snprintf(name, sizeof(name), "alu2_%s_x%d", prec ? "fp16" : "fp32", n);
			GLuint p = program(name, prec ? PREC16 : PREC32, body);
			if (!p)
				continue;
			glUseProgram(p);
			glUniform1f(glGetUniformLocation(p, "k"), 0.999f);
			glUniform1f(glGetUniformLocation(p, "m"), 0.001f);
			double t = time_dispatch(p, threads / 128, 1, 1);
			double flops = (double)threads * 64 * 32 * 4 * 2;
			printf("%s: %.2f ms  %.1f GFLOPS\n", name, t, flops / t / 1e6);
			fflush(stdout);
		}
}


/* ---------------------------------------------------------------- ssbo */

// Writes o[i] = i over buffers of growing size and reports the first index
// whose value did not land, then reads them back through a second shader.
static void test_ssbo(void)
{
	GLuint w = program("ssbo_write", "",
			   "layout(local_size_x=64) in;\n"
			   "layout(std430,binding=0) writeonly buffer O{uint o[];};\n"
			   "uniform uint n;\n"
			   "void main(){uint x=gl_GlobalInvocationID.y*65536u+gl_GlobalInvocationID.x; if(x<n) o[x]=x+1u;}\n");
	GLuint r = program("ssbo_read", "",
			   "layout(local_size_x=64) in;\n"
			   "layout(std430,binding=0) readonly buffer I{uint i[];};\n"
			   "layout(std430,binding=1) writeonly buffer O{uint o[];};\n"
			   "uniform uint n;\n"
			   "void main(){uint x=gl_GlobalInvocationID.y*65536u+gl_GlobalInvocationID.x; if(x<n) o[x]=i[x];}\n");
	const size_t sizes[] = { 16 << 10, 64 << 10, (64 << 10) + 4096, 128 << 10, 256 << 10, (256 << 10) + 4096,
				 1 << 20, 3 << 20, 16 << 20, 64 << 20 };
	for (unsigned s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
		size_t bytes = sizes[s], n = bytes / 4;
		GLuint b = buffer(bytes, NULL), b2 = buffer(bytes, NULL);
		GLuint gy = (n + 65535) / 65536, gx = (n < 65536 ? (n + 63) / 64 : 1024);
		glUseProgram(w);
		glUniform1ui(glGetUniformLocation(w, "n"), n);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, b);
		glDispatchCompute(gx, gy, 1);
		uint32_t *o = readback(b, bytes);
		size_t bad = n, nbad = 0;
		for (size_t i = 0; i < n; i++)
			if (o[i] != i + 1) {
				if (bad == n)
					bad = i;
				nbad++;
			}
		if (getenv("GPUBENCH_VERBOSE"))
			fprintf(stderr, "  o[0..3]=%u %u %u %u  o[n-1]=%u\n", o[0], o[1], o[2], o[3], o[n - 1]);
		free(o);
		glUseProgram(r);
		glUniform1ui(glGetUniformLocation(r, "n"), n);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, b);
		glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, b2);
		glDispatchCompute(gx, gy, 1);
		o = readback(b2, bytes);
		size_t rbad = n, nrbad = 0;
		for (size_t i = 0; i < n; i++)
			if (o[i] != i + 1) {
				if (rbad == n)
					rbad = i;
				nrbad++;
			}
		free(o);
		printf("ssbo %8zu B: write first-bad %zu (%zu bad)  copy first-bad %zu (%zu bad)  [n=%zu]\n", bytes,
		       bad == n ? (size_t)-1 : bad * 4, nbad, rbad == n ? (size_t)-1 : rbad * 4, nrbad, n);
		fflush(stdout);
		glDeleteBuffers(1, &b), glDeleteBuffers(1, &b2);
	}
}


/* ---------------------------------------------------------------- fsalu */

// The alu2 FMA loop as a fragment shader over a 1024x1024 RGBA8 target, to
// compare the fragment and compute paths' ALU throughput.
static void test_fsalu(void)
{
	const char *vs = "#version 310 es\n"
			 "void main(){ vec2 p=vec2(float((gl_VertexID<<1)&2), float(gl_VertexID&2));\n"
			 " gl_Position=vec4(p*2.0-1.0,0.0,1.0);}\n";
	const int D = 1024;
	GLuint tex, fbo;
	glGenTextures(1, &tex);
	glBindTexture(GL_TEXTURE_2D, tex);
	glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, D, D);
	glGenFramebuffers(1, &fbo);
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
	glViewport(0, 0, D, D);
	GLuint vao;
	glGenVertexArrays(1, &vao);
	glBindVertexArray(vao);
	for (int prec = 0; prec < 2; prec++)
		for (int n = 1; n <= 4; n *= 2) {
			char fs[4096], *b = fs;
			b += sprintf(b, "#version 310 es\nprecision %s float;\nuniform float k, m;\nout vec4 o;\n"
					"void main(){ vec4 s=vec4(gl_FragCoord.x*1e-6);\n",
				     prec ? "mediump" : "highp");
			for (int c = 0; c < n; c++)
				b += sprintf(b, " vec4 a%d=s+%d.0;\n", c, c);
			b += sprintf(b, " for(int i=0;i<64;i++){\n");
			for (int u = 0; u < 32 / n; u++)
				for (int c = 0; c < n; c++)
					b += sprintf(b, "  a%d=a%d*k+m;\n", c, c);
			b += sprintf(b, " }\n vec4 r=s;");
			for (int c = 0; c < n; c++)
				b += sprintf(b, " r+=a%d;", c);
			b += sprintf(b, " o=r*1e-3;}\n");
			GLuint shv = glCreateShader(GL_VERTEX_SHADER), shf = glCreateShader(GL_FRAGMENT_SHADER);
			const char *pv = vs, *pf = fs;
			glShaderSource(shv, 1, &pv, NULL);
			glShaderSource(shf, 1, &pf, NULL);
			glCompileShader(shv);
			glCompileShader(shf);
			GLuint p = glCreateProgram();
			glAttachShader(p, shv);
			glAttachShader(p, shf);
			glLinkProgram(p);
			GLint ok;
			glGetProgramiv(p, GL_LINK_STATUS, &ok);
			if (!ok) {
				char log[4096];
				glGetProgramInfoLog(p, sizeof(log), NULL, log);
				fprintf(stderr, "fsalu link failed: %s\n", log);
				continue;
			}
			glUseProgram(p);
			glUniform1f(glGetUniformLocation(p, "k"), 0.999f);
			glUniform1f(glGetUniformLocation(p, "m"), 0.001f);
			glDrawArrays(GL_TRIANGLES, 0, 3);
			glFinish();
			double best = 1e30;
			for (int rep = 0; rep < 3; rep++) {
				double t0 = now_ms();
				for (int i = 0; i < 4; i++)
					glDrawArrays(GL_TRIANGLES, 0, 3);
				glFinish();
				double t = (now_ms() - t0) / 4;
				if (t < best)
					best = t;
			}
			double flops = (double)D * D * 64 * 32 * 4 * 2;
			printf("fsalu_%s_x%d: %.2f ms  %.1f GFLOPS\n", prec ? "fp16" : "fp32", n, best, flops / best / 1e6);
			fflush(stdout);
		}
}

/* ---------------------------------------------------------------- gemm */

// C[M][N] = A[M][K] * B[K][N].  A is passed transposed (AT[K][M]) so that a
// thread's four rows are one vec4.  Matrices are stored as vec4 (fp32) or
// uvec2 of 4 halves (fp16).
static const char *GEMM_NAIVE =
	"layout(local_size_x=8, local_size_y=8) in;\n"
	"layout(std430,binding=0) readonly buffer BA{ST at[];};\n"
	"layout(std430,binding=1) readonly buffer BB{ST b[];};\n"
	"layout(std430,binding=2) writeonly buffer BC{vec4 c[];};\n"
	"uniform int M4, N4, K;\n"
	"void main(){ int col4=int(gl_GlobalInvocationID.x), row=int(gl_GlobalInvocationID.y);\n"
	" int r4=row>>2, ri=row&3; VEC acc=VEC(0.0);\n"
	" for(int k=0;k<K;k++){ VEC a=LD(at[k*M4+r4]); acc+=a[ri]*LD(b[k*N4+col4]); }\n"
	" c[row*N4+col4]=vec4(acc);}\n";

static const char *GEMM_RB =
	"layout(local_size_x=8, local_size_y=8) in;\n"
	"layout(std430,binding=0) readonly buffer BA{ST at[];};\n"
	"layout(std430,binding=1) readonly buffer BB{ST b[];};\n"
	"layout(std430,binding=2) writeonly buffer BC{vec4 c[];};\n"
	"uniform int M4, N4, K;\n"
	"void main(){ int col4=int(gl_GlobalInvocationID.x), r4=int(gl_GlobalInvocationID.y);\n"
	" VEC c0=VEC(0.0), c1=VEC(0.0), c2=VEC(0.0), c3=VEC(0.0);\n"
	" for(int k=0;k<K;k++){ VEC a=LD(at[k*M4+r4]); VEC bb=LD(b[k*N4+col4]);\n"
	"  c0+=a.x*bb; c1+=a.y*bb; c2+=a.z*bb; c3+=a.w*bb; }\n"
	" int o=(r4*4)*N4+col4; c[o]=vec4(c0); c[o+N4]=vec4(c1); c[o+2*N4]=vec4(c2); c[o+3*N4]=vec4(c3);}\n";

// 8x8 threads, each 4x4 outputs, 32x32 tile; K stepped by 16 through shared memory.
static const char *GEMM_TILED =
	"layout(local_size_x=8, local_size_y=8) in;\n"
	"layout(std430,binding=0) readonly buffer BA{ST at[];};\n"
	"layout(std430,binding=1) readonly buffer BB{ST b[];};\n"
	"layout(std430,binding=2) writeonly buffer BC{vec4 c[];};\n"
	"uniform int M4, N4, K;\n"
	"shared VEC sa[16][8]; shared VEC sb[16][8];\n"
	"void main(){ int tx=int(gl_LocalInvocationID.x), ty=int(gl_LocalInvocationID.y);\n"
	" int col4=int(gl_WorkGroupID.x)*8+tx, r4=int(gl_WorkGroupID.y)*8+ty;\n"
	" int lid=ty*8+tx;\n"
	" VEC c0=VEC(0.0), c1=VEC(0.0), c2=VEC(0.0), c3=VEC(0.0);\n"
	" for(int k0=0;k0<K;k0+=16){\n"
	"  for(int j=lid;j<128;j+=64){ int kk=j>>3, q=j&7;\n"
	"   sa[kk][q]=LD(at[(k0+kk)*M4+int(gl_WorkGroupID.y)*8+q]);\n"
	"   sb[kk][q]=LD(b[(k0+kk)*N4+int(gl_WorkGroupID.x)*8+q]); }\n"
	"  barrier();\n"
	"  for(int kk=0;kk<16;kk++){ VEC a=sa[kk][ty]; VEC bb=sb[kk][tx];\n"
	"   c0+=a.x*bb; c1+=a.y*bb; c2+=a.z*bb; c3+=a.w*bb; }\n"
	"  barrier(); }\n"
	" int o=(r4*4)*N4+col4; c[o]=vec4(c0); c[o+N4]=vec4(c1); c[o+2*N4]=vec4(c2); c[o+3*N4]=vec4(c3);}\n";

static void test_gemm(int sz)
{
	const int M = sz, N = sz, K = sz;
	float *A = malloc(sizeof(float) * M * K), *AT = malloc(sizeof(float) * M * K), *B = malloc(sizeof(float) * K * N);
	for (int i = 0; i < M * K; i++)
		A[i] = frand();
	for (int i = 0; i < K * N; i++)
		B[i] = frand();
	for (int m = 0; m < M; m++)
		for (int k = 0; k < K; k++)
			AT[k * M + m] = A[m * K + k];
	void *ATh = to_half(AT, M * K), *Bh = to_half(B, K * N);
	GLuint bat32 = buffer(4 * M * K, AT), bb32 = buffer(4 * K * N, B);
	GLuint bat16 = buffer(2 * M * K, ATh), bb16 = buffer(2 * K * N, Bh);
	GLuint bc = buffer(4 * M * N, NULL);

	struct {
		const char *name, *body;
		int rows_per_thread;
	} kern[] = { { "naive", GEMM_NAIVE, 1 }, { "regblock", GEMM_RB, 4 }, { "tiled", GEMM_TILED, 4 } };
	for (int prec = 0; prec < 2; prec++) {
		for (unsigned v = 0; v < sizeof(kern) / sizeof(kern[0]); v++) {
			char name[64];
			snprintf(name, sizeof(name), "gemm_%s_%s", kern[v].name, prec ? "fp16" : "fp32");
			GLuint p = program(name, prec ? PREC16 : PREC32, kern[v].body);
			if (!p)
				continue;
			glUseProgram(p);
			glUniform1i(glGetUniformLocation(p, "M4"), M / 4);
			glUniform1i(glGetUniformLocation(p, "N4"), N / 4);
			glUniform1i(glGetUniformLocation(p, "K"), K);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, prec ? bat16 : bat32);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, prec ? bb16 : bb32);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bc);
			double t = time_dispatch(p, N / 4 / 8, M / kern[v].rows_per_thread / 8, 1);
			float *C = readback(bc, 4 * M * N);
			double maxerr = 0;
			for (int s = 0; s < 64; s++) {
				int m = rand() % M, n = rand() % N;
				double ref = 0;
				for (int k = 0; k < K; k++)
					ref += (double)A[m * K + k] * B[k * N + n];
				double e = fabs(C[m * N + n] - ref);
				if (e > maxerr)
					maxerr = e;
				if (e > 0.05 && getenv("GPUBENCH_VERBOSE"))
					fprintf(stderr, "  C[%d][%d]=%f ref=%f\n", m, n, C[m * N + n], ref);
			}
			free(C);
			printf("%s %dx%dx%d: %.2f ms  %.2f GFLOPS  maxabserr %.4f\n", name, M, N, K, t,
			       2.0 * M * N * K / t / 1e6, maxerr);
			fflush(stdout);
		}
	}
	free(A), free(AT), free(B), free(ATh), free(Bh);
}

/* ---------------------------------------------------------------- conv */

// Direct 3x3 stride-1 convolution, PHWC4 layout: activations are
// [y][x][c/4] of 4-channel texels, input padded by one pixel on each side.
// Weights: for each (oc4, tap, ic4) four texels, texel j holding the weights
// of input channel 4*ic4+j for the four output channels.
// A thread computes 2 horizontally adjacent pixels x 2 output-channel blocks.
static const char *CONV =
	"layout(local_size_x=8, local_size_y=8) in;\n"
	"layout(std430,binding=0) readonly buffer BI{ST src[];};\n"
	"layout(std430,binding=1) readonly buffer BW{ST w[];};\n"
	"layout(std430,binding=2) writeonly buffer BO{vec4 dst[];};\n"
	"uniform int W, H, IC4, OC4;\n"
	"void main(){ int x=int(gl_GlobalInvocationID.x)*2, y=int(gl_GlobalInvocationID.y);\n"
	" int oc=int(gl_GlobalInvocationID.z)*2; int PW=W+2; if(x>=W||y>=H) return;\n"
	" VEC a0=VEC(0.0), a1=VEC(0.0), b0=VEC(0.0), b1=VEC(0.0);\n"
	" for(int ky=0;ky<3;ky++) for(int kx=0;kx<3;kx++){\n"
	"  int ib=((y+ky)*PW+x+kx)*IC4; int tap=ky*3+kx;\n"
	"  int wa=((oc*9+tap)*IC4)*4, wb=(((oc+1)*9+tap)*IC4)*4;\n"
	"  for(int c=0;c<IC4;c++){\n"
	"   VEC i0=LD(src[ib+c]), i1=LD(src[ib+IC4+c]);\n"
	"   VEC w0=LD(w[wa]),w1=LD(w[wa+1]),w2=LD(w[wa+2]),w3=LD(w[wa+3]); wa+=4;\n"
	"   a0+=i0.x*w0+i0.y*w1+i0.z*w2+i0.w*w3; a1+=i1.x*w0+i1.y*w1+i1.z*w2+i1.w*w3;\n"
	"   w0=LD(w[wb]);w1=LD(w[wb+1]);w2=LD(w[wb+2]);w3=LD(w[wb+3]); wb+=4;\n"
	"   b0+=i0.x*w0+i0.y*w1+i0.z*w2+i0.w*w3; b1+=i1.x*w0+i1.y*w1+i1.z*w2+i1.w*w3; } }\n"
	" int o=(y*W+x)*OC4+oc; dst[o]=vec4(a0); dst[o+1]=vec4(b0); dst[o+OC4]=vec4(a1); dst[o+OC4+1]=vec4(b1);}\n";

static void test_conv(void)
{
	const int shapes[][3] = { { 160, 32, 32 }, { 80, 64, 64 }, { 40, 128, 128 }, { 20, 256, 256 } };
	for (int prec = 0; prec < 2; prec++) {
		GLuint p = program(prec ? "conv_fp16" : "conv_fp32", prec ? PREC16 : PREC32, CONV);
		if (!p)
			continue;
		double tot_flop = 0, tot_ms = 0;
		for (unsigned s = 0; s < sizeof(shapes) / sizeof(shapes[0]); s++) {
			int HW = shapes[s][0], IC = shapes[s][1], OC = shapes[s][2];
			int IC4 = IC / 4, OC4 = OC / 4, PW = HW + 2;
			size_t nin = (size_t)PW * PW * IC, nw = (size_t)OC * 9 * IC, nout = (size_t)HW * HW * OC;
			float *in = calloc(nin, 4), *wt = malloc(nw * 4);
			for (int y = 1; y <= HW; y++)
				for (int x = 1; x <= HW; x++)
					for (int c = 0; c < IC; c++)
						in[((size_t)y * PW + x) * IC + c] = frand();
			for (size_t i = 0; i < nw; i++)
				wt[i] = frand() * 0.1f;
			// wt layout = [oc4][tap][ic4][j][4]: element (oc4,tap,ic4,j,l) = weight(oc=4*oc4+l, ic=4*ic4+j)
			void *inh = prec ? to_half(in, nin) : NULL, *wh = prec ? to_half(wt, nw) : NULL;
			GLuint bi = buffer(nin * (prec ? 2 : 4), prec ? inh : (void *)in);
			GLuint bw = buffer(nw * (prec ? 2 : 4), prec ? wh : (void *)wt);
			GLuint bo = buffer(nout * 4, NULL);
			glUseProgram(p);
			glUniform1i(glGetUniformLocation(p, "W"), HW);
			glUniform1i(glGetUniformLocation(p, "H"), HW);
			glUniform1i(glGetUniformLocation(p, "IC4"), IC4);
			glUniform1i(glGetUniformLocation(p, "OC4"), OC4);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, bi);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bw);
			glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, bo);
			double t = time_dispatch(p, (HW / 2 + 7) / 8, (HW + 7) / 8, OC4 / 2);
			float *out = readback(bo, nout * 4);
			double maxerr = 0;
			for (int k = 0; k < 32; k++) {
				int y = rand() % HW, x = rand() % HW, oc = rand() % OC;
				double ref = 0;
				for (int ky = 0; ky < 3; ky++)
					for (int kx = 0; kx < 3; kx++)
						for (int ic = 0; ic < IC; ic++) {
							size_t wi = ((((size_t)(oc / 4) * 9 + ky * 3 + kx) * IC4 + ic / 4) * 4 +
								     ic % 4) * 4 + oc % 4;
							ref += (double)in[((size_t)(y + ky) * PW + x + kx) * IC + ic] * wt[wi];
						}
				double e = fabs(out[((size_t)y * HW + x) * OC + oc] - ref);
				if (e > maxerr)
					maxerr = e;
			}
			double flop = 2.0 * HW * HW * OC * IC * 9;
			tot_flop += flop, tot_ms += t;
			printf("conv3x3_%s %dx%d %d->%d: %.2f ms  %.2f GFLOPS  maxabserr %.4f\n", prec ? "fp16" : "fp32",
			       HW, HW, IC, OC, t, flop / t / 1e6, maxerr);
			fflush(stdout);
			free(out), free(in), free(wt), free(inh), free(wh);
			glDeleteBuffers(1, &bi), glDeleteBuffers(1, &bw), glDeleteBuffers(1, &bo);
		}
		printf("conv3x3_%s aggregate: %.2f GFLOPS\n", prec ? "fp16" : "fp32", tot_flop / tot_ms / 1e6);
	}
}

int main(int argc, char **argv)
{
	init_egl();
	int all = argc < 2;
	for (int i = all ? 0 : 1; all ? i < 1 : i < argc; i++) {
		const char *t = all ? "" : argv[i];
		if (all || !strcmp(t, "info"))
			test_info();
		if (all || !strcmp(t, "bw"))
			test_bw();
		if (all || !strcmp(t, "alu"))
			test_alu();
		if (all || !strcmp(t, "ssbo"))
			test_ssbo();
		if (all || !strcmp(t, "fsalu"))
			test_fsalu();
		if (all || !strcmp(t, "alu2"))
			test_alu2();
		if (all || !strcmp(t, "gemm")) {
			test_gemm(512);
			test_gemm(1024);
		}
		if (all || !strcmp(t, "conv"))
			test_conv();
	}
	return 0;
}
