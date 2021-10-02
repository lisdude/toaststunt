/*
 * fileio.h
 *
 */

#ifndef EXT_FILE_IO_H

#define EXT_FILE_IO_H 1

static char *line_read;
size_t line_size = 0;

extern const char *file_resolve_path(const char *pathname);

#endif
