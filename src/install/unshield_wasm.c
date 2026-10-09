// Thin wrapper over libunshield for the in-browser installer (web/pages/install-worker.js).
// The worker mounts the disc's data1.hdr / data*.cab as Blobs under /cd and
// calls these through ccall.
#include <stdio.h>
#include <string.h>
#include <emscripten.h>
#include "libunshield.h"

static Unshield *us;
static char path_buf[1024];

EMSCRIPTEN_KEEPALIVE int us_open(const char *cab) {
  us = unshield_open(cab);
  return us != NULL;
}

// Index range [first, last] of a file group, or -1 if there is none.
EMSCRIPTEN_KEEPALIVE int us_group_first(const char *name) {
  UnshieldFileGroup *g = us ? unshield_file_group_find(us, name) : NULL;
  return g ? (int)g->first_file : -1;
}

EMSCRIPTEN_KEEPALIVE int us_group_last(const char *name) {
  UnshieldFileGroup *g = us ? unshield_file_group_find(us, name) : NULL;
  return g ? (int)g->last_file : -1;
}

EMSCRIPTEN_KEEPALIVE int us_valid(int i) { return unshield_file_is_valid(us, i); }

EMSCRIPTEN_KEEPALIVE int us_size(int i) { return (int)unshield_file_size(us, i); }

// "Directory\\Name" as stored in the cabinet (backslash separated).
EMSCRIPTEN_KEEPALIVE const char *us_path(int i) {
  const char *dir = unshield_directory_name(us, unshield_file_directory(us, i));
  const char *name = unshield_file_name(us, i);
  snprintf(path_buf, sizeof path_buf, "%s%s%s", dir ? dir : "", dir && *dir ? "\\" : "", name ? name : "");
  return path_buf;
}

EMSCRIPTEN_KEEPALIVE int us_save(int i, const char *out) { return unshield_file_save(us, i, out); }
