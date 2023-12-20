/*
 * keyd - A key remapping daemon.
 *
 * © 2019 Raheman Vaiya (see also: LICENSE).
 */
#ifndef KEYD_H_
#define KEYD_H_

#include <assert.h>
#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <grp.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <termios.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

#ifdef __FreeBSD__
	#include <dev/evdev/input.h>
#else
	#include <linux/input.h>
#endif

#include "config.h"
#include "string.h"

#define MAX_IPC_MESSAGE_SIZE 4096

#define ARRAY_SIZE(x) (int)(sizeof(x)/sizeof(x[0]))

struct ipc_message {
	enum {
		IPC_SUCCESS,
		IPC_FAIL,

		IPC_BIND,
		IPC_INPUT,
		IPC_MACRO,
		IPC_RELOAD,
		IPC_LAYER_LISTEN,
	} type;
	
	uint32_t timeout;
	char data[MAX_IPC_MESSAGE_SIZE];
	size_t sz;
};

void xwrite(int fd, const void *buf, size_t sz);
void xread(int fd, void *buf, size_t sz);

int ipc_create_server();
int ipc_connect();

#endif
