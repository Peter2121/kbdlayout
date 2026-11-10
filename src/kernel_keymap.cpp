/*
 * kernel_keymap.cpp - read the keymap loaded in the FreeBSD kernel.
 */
#include "kernel_keymap.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#if defined(__FreeBSD__)
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/kbio.h>
#include <sys/sysctl.h>

#if NUM_KEYS != 256 || NUM_STATES != 8
#error "unexpected keymap_t layout in <sys/kbio.h>"
#endif
/* The parser uses the same special-key values as the kernel. */
static_assert(F_ACC == 0x89 && L_ACC == 0x97 && LSHA == 0x9b && RALTA == 0xa0 &&
    ALK == 0x82 && ASH == 0x83 && CLK == 0x04 && F_FN == 0x1b && L_FN == 0x7a,
    "kbio.h special key values changed");
#endif

int console_tty_fd()
{
	static int fd = -2;
	if (fd == -2) {
		if (isatty(STDIN_FILENO))
			fd = STDIN_FILENO;
		else
			fd = open("/dev/tty", O_RDONLY | O_CLOEXEC);
	}
	return fd;
}

bool kernel_console_is_vt()
{
#if defined(__FreeBSD__)
	char buf[16] = {0};
	size_t len = sizeof(buf) - 1;
	if (sysctlbyname("kern.vty", buf, &len, nullptr, 0) == 0)
		return strcmp(buf, "vt") == 0;
#endif
	return true;
}

bool kernel_keymap_read(unsigned *n_keys, uint32_t map[256][8], uint8_t spcl[256],
    uint8_t flgs[256], char *err, size_t errlen)
{
#if defined(__FreeBSD__)
	int fd = console_tty_fd();
	if (fd < 0) {
		snprintf(err, errlen, "no console tty: %s", strerror(errno));
		return false;
	}
	static keymap_t km;
	if (ioctl(fd, GIO_KEYMAP, &km) < 0) {
		snprintf(err, errlen, "GIO_KEYMAP: %s (not on a vt console?)", strerror(errno));
		return false;
	}
	*n_keys = km.n_keys;
	for (int i = 0; i < NUM_KEYS; i++) {
		for (int s = 0; s < NUM_STATES; s++)
			map[i][s] = km.key[i].map[s];
		spcl[i] = km.key[i].spcl;
		flgs[i] = km.key[i].flgs;
	}
	return true;
#else
	(void)n_keys; (void)map; (void)spcl; (void)flgs;
	snprintf(err, errlen, "reading the kernel keymap is only supported on FreeBSD");
	return false;
#endif
}
