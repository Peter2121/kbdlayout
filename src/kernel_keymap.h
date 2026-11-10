/*
 * kernel_keymap.h - the only FreeBSD-kernel-specific code (keyboard ioctls).
 * Kept apart from keymap.h because <sys/kbio.h> defines macros (NOP, LSH,
 * NUM_KEYS...) that would clash with the parser's own names.
 */
#pragma once

#include <cstddef>
#include <cstdint>

/* Fills the arrays from GIO_KEYMAP on the console tty. */
bool kernel_keymap_read(unsigned *n_keys, uint32_t map[256][8], uint8_t spcl[256],
    uint8_t flgs[256], char *err, size_t errlen);

/* true when the console driver is vt(4), false for syscons. */
bool kernel_console_is_vt();

/* File descriptor of the console tty (stdin if it is a tty, else /dev/tty). */
int console_tty_fd();
