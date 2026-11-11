#ifndef _UNISTD_H
#define _UNISTD_H
#include <stddef.h>
#include <sys/types.h>
int close(int fd);
ssize_t read(int fd, void *buf, size_t n);
ssize_t write(int fd, const void *buf, size_t n);
int access(const char *path, int mode);
int unlink(const char *path);
int isatty(int fd);
#define F_OK 0
#define R_OK 4
#define W_OK 2
#endif
