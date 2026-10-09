// Game file access. The game opens DOS paths relative to its install folder
// ("Interface\\TitleBASE.pcx", "Saved Games\\x.sve"). The port mounts the
// user's copy of the game at /game in the Emscripten FS: files listed in the
// manifest are fetched from the server on first open, and paths are matched
// case-insensitively as on Windows.

#ifndef SIMGOLF_PORT_FS_H
#define SIMGOLF_PORT_FS_H

#ifdef __cplusplus
extern "C" {
#endif

#define FS_GAME_ROOT "/game"

void fs_init(void);
// Maps a game path to an absolute FS path in `out`. For reading, the file must
// exist (it's fetched if needed); for writing, missing directories are an
// error but a missing file is fine. Returns 0 on failure.
int fs_resolve(const char *dospath, char *out, int outlen, int for_write);

// Persistence (IndexedDB) for files the game writes: call after closing a
// file that was opened for writing, or after deleting one.
void fs_written(const char *realpath);
void fs_deleted(const char *realpath);

// Directory listing for FindFirstFile: calls fn for each entry of the
// resolved directory whose name matches the DOS wildcard pattern.
typedef void (*fs_list_fn)(void *ctx, const char *name, int is_dir, unsigned size);
int fs_list(const char *dospattern, fs_list_fn fn, void *ctx);

#ifdef __cplusplus
}
#endif
#endif  // SIMGOLF_PORT_FS_H
