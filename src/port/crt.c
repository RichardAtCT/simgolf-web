// Native replacements for the MSVC 6 CRT functions golf.exe calls (see
// native.h). stdio keeps MSVC's FILE layout because the game's inlined getc,
// putc and feof macros read the struct directly: _cnt stays 0 so every inline
// getc/putc falls through to _filbuf/_flsbuf, which land here.

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "port/fs.h"
#include "port/native.h"
#include "port/runtime.h"

#define P(x) ((void *)(uintptr_t)(x))
#define S(x) ((char *)(uintptr_t)(x))
#define U(x) ((nu)(uintptr_t)(x))

static double U2D_(uint32_t lo, uint32_t hi) {
  union { uint64_t u; double d; } v = {((uint64_t)hi << 32) | lo};
  return v.d;
}

// ------------------------------------------------------------------ memory

void crt_free(nu p) { free(P(p)); }
void crt_free_dbg(nu p, nu type) { (void)type; free(P(p)); }
nu crt_malloc(nu n) {
  if (n > 0x4000000) {
    fprintf(stderr, "crt_malloc(%u): implausible size\n", n);
    port_trace_dump(48);
    port_backtrace();
  }
  return U(malloc(n ? n : 1));
}
nu crt_realloc(nu p, nu n) {
  if (!p) return crt_malloc(n);
  if (!n) { free(P(p)); return 0; }
  return U(realloc(P(p), n));
}
nu crt_calloc(nu n, nu size) { return U(calloc(n ? n : 1, size ? size : 1)); }

void crt_exit(nu code) {
  fprintf(stderr, "game called exit(%u)\n", code);
  port_abort("exit");
}

// `eh vector constructor iterator': ctor is a thiscall taking `this` only.
void crt_vec_ctor(nu ptr, nu size, nu count, nu ctor) {
  IcallFn f = icall_lookup(ctor);
  for (nu i = 0; i < count; i++) f(ptr + i * size, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

void crt_vec_dtor(nu ptr, nu size, nu count, nu dtor) {
  IcallFn f = icall_lookup(dtor);
  ptr += size * count;
  while (count-- > 0) {
    ptr -= size;
    f(ptr, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
  }
}

// Static destructors registered at startup; the browser never exits cleanly,
// so they're recorded and ignored.
int crt_atexit(nu fn) { (void)fn; return 0; }

void crt_chkstk(void) {}

// ------------------------------------------------------------------ strings

nu crt_strpbrk(nu s, nu set) { return U(strpbrk(S(s), S(set))); }
nu crt_strtok(nu s, nu delim) { return U(strtok(S(s), S(delim))); }
int crt_isdigit(nu c) { return (c & 0xff) >= '0' && (c & 0xff) <= '9' ? 4 : 0; }
int crt_isspace(nu c) {
  c &= 0xff;
  return (c == ' ' || (c >= 9 && c <= 13)) ? 8 : 0;
}
int crt_isprint(nu c) { c &= 0xff; return (c >= 0x20 && c < 0x7f) ? 0x157 : 0; }
int crt_toupper(nu c) { return (c >= 'a' && c <= 'z') ? c - 0x20 : (int)c; }
int crt_tolower(nu c) { return (c >= 'A' && c <= 'Z') ? c + 0x20 : (int)c; }
int crt_atol(nu s) { return (int)atol(S(s)); }
int crt_atoi(nu s) { return atoi(S(s)); }
nu crt_memmove(nu d, nu s, nu n) { return U(memmove(P(d), P(s), n)); }
int crt_stricmp(nu a, nu b) { return strcasecmp(S(a), S(b)); }
int crt_strnicmp(nu a, nu b, nu n) { return strncasecmp(S(a), S(b), n); }
nu crt_strchr(nu s, nu c) { return U(strchr(S(s), (char)c)); }
nu crt_strrchr(nu s, nu c) { return U(strrchr(S(s), (char)c)); }
nu crt_strstr(nu s, nu t) { return U(strstr(S(s), S(t))); }
int crt_strncmp(nu a, nu b, nu n) { return strncmp(S(a), S(b), n); }
nu crt_strncpy(nu d, nu s, nu n) { return U(strncpy(S(d), S(s), n)); }
nu crt_memchr(nu s, nu c, nu n) { return U(memchr(P(s), (int)(c & 0xff), n)); }
nu crt_memset(nu d, nu c, nu n) { return U(memset(P(d), (int)c, n)); }
nu crt_strlen(nu s) { return (nu)strlen(S(s)); }
int crt_strcmp(nu a, nu b) { return strcmp(S(a), S(b)); }

// MSVC _itoa: negative numbers only get a sign in radix 10.
nu crt_itoa(nu value, nu buf, nu radix) {
  char *out = S(buf);
  char tmp[40];
  int n = 0;
  uint32_t v = value;
  int neg = 0;
  if (radix == 10 && (int32_t)value < 0) {
    neg = 1;
    v = (uint32_t)(-(int32_t)value);
  }
  if (radix < 2 || radix > 36) radix = 10;
  do {
    uint32_t d = v % radix;
    tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
    v /= radix;
  } while (v);
  int i = 0;
  if (neg) out[i++] = '-';
  while (n) out[i++] = tmp[--n];
  out[i] = 0;
  return buf;
}

int crt_ftol(double x) { return (int)(long long)x; }
nu crt_chkesp(void) { return 0; }
nu crt_strcpy(nu d, nu s) { return U(strcpy(S(d), S(s))); }
nu crt_strcat(nu d, nu s) { return U(strcat(S(d), S(s))); }
int crt_setjmp(nu buf, ...) { (void)buf; return 0; }
void crt_longjmp(nu buf, nu v) {
  (void)buf; (void)v;
  port_abort("longjmp (libpng error path) is not supported");
}
int crt_dbgreport(nu type, nu file, nu line, nu module, nu fmt, ...) {
  (void)module;
  fprintf(stderr, "CrtDbgReport type %u %s:%u: %s\n", type, file ? S(file) : "?", line,
          fmt ? S(fmt) : "");
  return 0;
}
int crt_memcmp(nu a, nu b, nu n) { return memcmp(P(a), P(b), n); }
double crt_fabs(double x) { return __builtin_fabs(x); }
double crt_sqrt(double x) { return __builtin_sqrt(x); }
double crt_sin(double x) { return __builtin_sin(x); }
double crt_cos(double x) { return __builtin_cos(x); }
double crt_pow(double x, double y) { return __builtin_pow(x, y); }
double crt_fabs_split(nu lo, nu hi) { return __builtin_fabs(U2D_(lo, hi)); }
int crt_sprintf(nu buf, nu fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsprintf(S(buf), S(fmt), ap);
  va_end(ap);
  return r;
}
void crt_lock(nu n) { (void)n; }
int crt_sscanf(nu str, nu fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsscanf(S(str), S(fmt), ap);
  va_end(ap);
  return r;
}
nu crt_operator_new(nu n) { return crt_malloc(n); }
nu crt_free_r(nu p) { crt_free(p); return 0; }
void crt_free_(nu p) { crt_free(p); }
int crt_zero1(nu a) { (void)a; return 0; }
int crt_one(void) { return 1; }
int crt_heapchk(void) { return -2; }  // _HEAPOK
void crt_nop(void) {}

// ------------------------------------------------------------------ low-level io

static char *g_fd_written[256];   // FS path of fds opened for writing

int crt_open(nu path, nu flags, ...) {
  char real[512];
  int write = (flags & 3) != 0 || (flags & 0x100);
  if (!fs_resolve(S(path), real, sizeof real, write)) {
    errno = ENOENT;
    return -1;
  }
  int f = 0;
  switch (flags & 3) {
    case 0: f = O_RDONLY; break;
    case 1: f = O_WRONLY; break;
    default: f = O_RDWR; break;
  }
  if (flags & 0x8) f |= O_APPEND;
  if (flags & 0x100) f |= O_CREAT;
  if (flags & 0x200) f |= O_TRUNC;
  if (flags & 0x400) f |= O_EXCL;
  int fd = open(real, f, 0666);
  if (fd >= 0 && fd < 256 && write) g_fd_written[fd] = strdup(real);
  return fd;
}
int crt_read(nu fd, nu buf, nu n) { return (int)read((int)fd, P(buf), n); }
int crt_write(nu fd, nu buf, nu n) { return (int)write((int)fd, P(buf), n); }
int crt_close(nu fd) {
  int r = close((int)fd);
  if (fd < 256 && g_fd_written[fd]) {
    fs_written(g_fd_written[fd]);
    free(g_fd_written[fd]);
    g_fd_written[fd] = NULL;
  }
  return r;
}
int crt_lseek(nu fd, nu off, nu whence) { return (int)lseek((int)fd, (int32_t)off, (int)whence); }
int crt_tell(nu fd) { return crt_lseek(fd, 0, SEEK_CUR); }

int crt_remove(nu path) {
  char real[512];
  if (!fs_resolve(S(path), real, sizeof real, 0)) return -1;
  if (unlink(real) != 0) return -1;
  fs_deleted(real);
  return 0;
}

// ------------------------------------------------------------------ stdio

typedef struct {
  uint32_t ptr;
  int32_t cnt;
  uint32_t base;
  int32_t flag;
  int32_t file;
  int32_t charbuf;
  int32_t bufsiz;
  uint32_t tmpfname;
} MsFile;

enum { IOREAD = 1, IOWRT = 2, IOEOF = 0x10, IOERR = 0x20, IORW = 0x80 };

#define MAX_FILES 64
static FILE *g_files[MAX_FILES];
static int g_text[MAX_FILES];
static char *g_written[MAX_FILES];   // FS path of files opened for writing

static FILE *host(nu f, int *idx) {
  MsFile *m = P(f);
  if (!m || m->file < 0 || m->file >= MAX_FILES || !g_files[m->file]) return NULL;
  if (idx) *idx = m->file;
  return g_files[m->file];
}

nu crt_fopen(nu path, nu mode) {
  const char *md = S(mode);
  int write = strchr(md, 'w') || strchr(md, 'a') || strchr(md, '+');
  char real[512];
  if (!fs_resolve(S(path), real, sizeof real, write)) {
    fprintf(stderr, "fopen(%s, %s): not found\n", S(path), md);
    return 0;
  }
  char hm[8];
  int j = 0;
  for (const char *p = md; *p && j < 6; p++)
    if (*p == 'r' || *p == 'w' || *p == 'a' || *p == '+') hm[j++] = *p;
  hm[j] = 0;
  FILE *h = fopen(real, hm);
  if (!h) return 0;
  int i;
  for (i = 0; i < MAX_FILES && g_files[i]; i++) {}
  if (i == MAX_FILES) { fclose(h); return 0; }
  g_files[i] = h;
  g_text[i] = strchr(md, 'b') == NULL;
  g_written[i] = write ? strdup(real) : NULL;
  MsFile *m = calloc(1, sizeof *m);
  m->file = i;
  m->flag = write ? (strchr(md, '+') ? IORW : IOWRT) : IOREAD;
  return U(m);
}

int crt_fclose(nu f) {
  int i;
  FILE *h = host(f, &i);
  if (!h) return -1;
  fclose(h);
  g_files[i] = NULL;
  if (g_written[i]) {
    fs_written(g_written[i]);
    free(g_written[i]);
    g_written[i] = NULL;
  }
  free(P(f));
  return 0;
}

static void set_eof(nu f, FILE *h) {
  MsFile *m = P(f);
  if (feof(h)) m->flag |= IOEOF;
  if (ferror(h)) m->flag |= IOERR;
}

// One character, with text-mode CR dropping.
static int getc_text(FILE *h, int text) {
  int c = fgetc(h);
  while (text && c == '\r') c = fgetc(h);
  return c;
}

int crt_filbuf(nu f) {
  int i;
  FILE *h = host(f, &i);
  if (!h) return -1;
  MsFile *m = P(f);
  m->cnt = 0;
  int c = getc_text(h, g_text[i]);
  set_eof(f, h);
  return c == EOF ? -1 : (c & 0xff);
}

int crt_flsbuf(nu c, nu f) {
  FILE *h = host(f, NULL);
  if (!h) return -1;
  ((MsFile *)P(f))->cnt = 0;
  return fputc((int)(c & 0xff), h) == EOF ? -1 : (int)(c & 0xff);
}

nu crt_fgets(nu buf, nu n, nu f) {
  int i;
  FILE *h = host(f, &i);
  if (!h || (int)n <= 0) return 0;
  char *out = S(buf);
  int k = 0;
  while (k < (int)n - 1) {
    int c = getc_text(h, g_text[i]);
    if (c == EOF) break;
    out[k++] = (char)c;
    if (c == '\n') break;
  }
  set_eof(f, h);
  if (k == 0) return 0;
  out[k] = 0;
  return buf;
}

nu crt_fread(nu buf, nu size, nu n, nu f) {
  int i;
  FILE *h = host(f, &i);
  if (!h || !size) return 0;
  size_t got;
  if (!g_text[i]) {
    got = fread(P(buf), size, n, h);
  } else {
    char *out = S(buf);
    size_t total = (size_t)size * n, k = 0;
    while (k < total) {
      int c = getc_text(h, 1);
      if (c == EOF) break;
      out[k++] = (char)c;
    }
    got = k / size;
  }
  set_eof(f, h);
  return (nu)got;
}

nu crt_fwrite(nu buf, nu size, nu n, nu f) {
  FILE *h = host(f, NULL);
  if (!h) return 0;
  return (nu)fwrite(P(buf), size, n, h);
}

int crt_fprintf(nu f, nu fmt, ...) {
  FILE *h = host(f, NULL);
  if (!h) return -1;
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(h, S(fmt), ap);
  va_end(ap);
  return r;
}

void crt_rewind(nu f) {
  FILE *h = host(f, NULL);
  if (!h) return;
  rewind(h);
  ((MsFile *)P(f))->flag &= ~(IOEOF | IOERR);
}

int crt_fseek(nu f, nu off, nu whence) {
  FILE *h = host(f, NULL);
  if (!h) return -1;
  ((MsFile *)P(f))->flag &= ~IOEOF;
  return fseek(h, (int32_t)off, (int)whence) == 0 ? 0 : -1;
}

// ------------------------------------------------------------------ icall wrappers

#define W(name, call) uint32_t W_##name(ICALL_PARAMS) { \
  (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6; (void)a7; \
  (void)a8; (void)a9; (void)a10; (void)a11; (void)a12; (void)a13; (void)a14; (void)a15; call; }
W(crt_free, crt_free(a0); return 0)
W(crt_free_dbg, crt_free(a0); return 0)
W(crt_malloc, return crt_malloc(a0))
W(crt_realloc, return crt_realloc(a0, a1))
W(crt_exit, crt_exit(a0); return 0)
W(crt_vec_ctor, crt_vec_ctor(a0, a1, a2, a3); return 0)
W(crt_vec_dtor, crt_vec_dtor(a0, a1, a2, a3); return 0)
W(crt_atexit, return crt_atexit(a0))
W(crt_strpbrk, return crt_strpbrk(a0, a1))
W(crt_read, return crt_read(a0, a1, a2))
W(crt_write, return crt_write(a0, a1, a2))
W(crt_close, return crt_close(a0))
W(crt_open, return crt_open(a0, a1, a2))
W(crt_lseek, return crt_lseek(a0, a1, a2))
W(crt_tell, return crt_tell(a0))
W(crt_chkstk, return 0)
W(crt_fclose, return crt_fclose(a0))
W(crt_fopen, return crt_fopen(a0, a1))
W(crt_fprintf, return crt_fprintf(a0, a1, a2, a3, a4, a5, a6, a7))
W(crt_fwrite, return crt_fwrite(a0, a1, a2, a3))
W(crt_fread, return crt_fread(a0, a1, a2, a3))
W(crt_remove, return crt_remove(a0))
W(crt_isdigit, return crt_isdigit(a0))
W(crt_isspace, return crt_isspace(a0))
W(crt_isprint, return crt_isprint(a0))
W(crt_fgets, return crt_fgets(a0, a1, a2))
W(crt_toupper, return crt_toupper(a0))
W(crt_tolower, return crt_tolower(a0))
W(crt_strtok, return crt_strtok(a0, a1))
W(crt_atol, return crt_atol(a0))
W(crt_atoi, return crt_atoi(a0))
W(crt_memmove, return crt_memmove(a0, a1, a2))
W(crt_rewind, crt_rewind(a0); return 0)
W(crt_fseek, return crt_fseek(a0, a1, a2))
W(crt_itoa, return crt_itoa(a0, a1, a2))
W(crt_stricmp, return crt_stricmp(a0, a1))
W(crt_strnicmp, return crt_strnicmp(a0, a1, a2))
W(crt_filbuf, return crt_filbuf(a0))
W(crt_flsbuf, return crt_flsbuf(a0, a1))
W(crt_strchr, return crt_strchr(a0, a1))
W(crt_strrchr, return crt_strrchr(a0, a1))
W(crt_strstr, return crt_strstr(a0, a1))
W(crt_strncmp, return crt_strncmp(a0, a1, a2))
W(crt_strncpy, return crt_strncpy(a0, a1, a2))
W(crt_memchr, return crt_memchr(a0, a1, a2))
W(crt_memset, return crt_memset(a0, a1, a2))
W(crt_strlen, return crt_strlen(a0))
W(crt_strcmp, return crt_strcmp(a0, a1))
W(crt_ftol, return crt_ftol(U2D_(a0, a1)))
W(crt_chkesp, return 0)
W(crt_strcpy, return crt_strcpy(a0, a1))
W(crt_strcat, return crt_strcat(a0, a1))
W(crt_setjmp, return 0)
W(crt_longjmp, crt_longjmp(a0, a1); return 0)
W(crt_dbgreport, return crt_dbgreport(a0, a1, a2, a3, a4))
W(crt_memcmp, return crt_memcmp(a0, a1, a2))
W(crt_fabs_split, g_fret = crt_fabs_split(a0, a1); return 0)
W(crt_fabs, g_fret = crt_fabs(U2D_(a0, a1)); return 0)
W(crt_sqrt, g_fret = crt_sqrt(U2D_(a0, a1)); return 0)
W(crt_sin, g_fret = crt_sin(U2D_(a0, a1)); return 0)
W(crt_cos, g_fret = crt_cos(U2D_(a0, a1)); return 0)
W(crt_pow, g_fret = crt_pow(U2D_(a0, a1), U2D_(a2, a3)); return 0)
W(crt_sprintf, return crt_sprintf(a0, a1, a2, a3, a4, a5, a6, a7))
W(crt_lock, return 0)
W(crt_sscanf, return crt_sscanf(a0, a1, a2, a3, a4, a5, a6, a7))
W(crt_operator_new, return crt_malloc(a0))
W(crt_free_r, crt_free(a0); return 0)
W(crt_free_, crt_free(a0); return 0)
W(crt_zero1, return 0)
W(crt_one, return 1)
W(crt_heapchk, return (uint32_t)-2)
W(crt_nop, return 0)
W(crt_calloc, return crt_calloc(a0, a1))
