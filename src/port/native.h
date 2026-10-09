// Native replacements for MSVC CRT functions that golf.exe calls. The
// translator maps each original CRT address to one of these names
// (tools/translate/golf.json) instead of translating the CRT, because the
// decompiled CRT loses implicit arguments (thiscall callbacks, x87 values).
//
// Prototypes use plain 32-bit types so they accept whatever Ghidra typed the
// call-site arguments as. Each one also has a W_ wrapper for icall.

#ifndef SIMGOLF_PORT_NATIVE_H
#define SIMGOLF_PORT_NATIVE_H

#include <stdint.h>

#ifndef ICALL_PARAMS
#include "port/runtime.h"
#endif

typedef uint32_t nu;   // a 32-bit argument of any type

void crt_free(nu p);
void crt_free_dbg(nu p, nu type);
nu crt_malloc(nu n);
nu crt_realloc(nu p, nu n);
nu crt_calloc(nu n, nu size);
void crt_exit(nu code);
void crt_vec_ctor(nu ptr, nu size, nu count, nu ctor);
void crt_vec_dtor(nu ptr, nu size, nu count, nu dtor);
int crt_atexit(nu fn);
nu crt_strpbrk(nu s, nu set);
int crt_read(nu fd, nu buf, nu n);
int crt_write(nu fd, nu buf, nu n);
int crt_close(nu fd);
int crt_open(nu path, nu flags, ...);
int crt_lseek(nu fd, nu off, nu whence);
int crt_tell(nu fd);
void crt_chkstk(void);
int crt_fclose(nu f);
nu crt_fopen(nu path, nu mode);
int crt_fprintf(nu f, nu fmt, ...);
nu crt_fwrite(nu buf, nu size, nu n, nu f);
nu crt_fread(nu buf, nu size, nu n, nu f);
int crt_remove(nu path);
int crt_isdigit(nu c);
int crt_isspace(nu c);
int crt_isprint(nu c);
nu crt_fgets(nu buf, nu n, nu f);
int crt_toupper(nu c);
int crt_tolower(nu c);
nu crt_strtok(nu s, nu delim);
int crt_atol(nu s);
int crt_atoi(nu s);
nu crt_memmove(nu dst, nu src, nu n);
void crt_rewind(nu f);
int crt_fseek(nu f, nu off, nu whence);
nu crt_itoa(nu value, nu buf, nu radix);
int crt_stricmp(nu a, nu b);
int crt_strnicmp(nu a, nu b, nu n);
int crt_filbuf(nu f);
int crt_flsbuf(nu c, nu f);
nu crt_strchr(nu s, nu c);
nu crt_strrchr(nu s, nu c);
nu crt_strstr(nu s, nu t);
int crt_strncmp(nu a, nu b, nu n);
nu crt_strncpy(nu d, nu s, nu n);
nu crt_memchr(nu s, nu c, nu n);
nu crt_memset(nu d, nu c, nu n);
nu crt_strlen(nu s);
int crt_strcmp(nu a, nu b);
int crt_ftol(double x);      // __ftol, given an ST0 parameter by ExportPort
nu crt_chkesp(void);
nu crt_strcpy(nu d, nu s);
nu crt_strcat(nu d, nu s);
int crt_setjmp(nu buf, ...);
void crt_longjmp(nu buf, nu v);
int crt_dbgreport(nu type, nu file, nu line, nu module, nu fmt, ...);
int crt_memcmp(nu a, nu b, nu n);
double crt_fabs_split(nu lo, nu hi);
double crt_fabs(double x);
double crt_sqrt(double x);
double crt_sin(double x);
double crt_cos(double x);
double crt_pow(double x, double y);
int crt_sprintf(nu buf, nu fmt, ...);
int crt_sscanf(nu str, nu fmt, ...);
nu crt_operator_new(nu n);
void crt_free_(nu p);
nu crt_free_r(nu p);
void crt_lock(nu n);
int crt_zero1(nu a);
int crt_one(void);
int crt_heapchk(void);
void crt_nop(void);

#endif  // SIMGOLF_PORT_NATIVE_H
