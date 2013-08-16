/*
 * Copyright (c) 2012 Eric Radman <ericshane@eradman.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compat.h"

#ifndef fmemopen
struct fmem {
	char *string;
	size_t pos;
	size_t size;
	size_t len;
};

static int
stringread(void *v, char *b, int l) {
	struct fmem *mem = v;
	int i;

	for (i = 0; i < l && i + mem->pos < mem->len; i++)
		b[i] = mem->string[mem->pos + i];
	mem->pos += i;
	return i;
}

FILE *fmemopen(void *buf, size_t size, const char *mode) {
	struct fmem *mem;

	mem = malloc(sizeof(*mem));
	mem->pos = 0;
	mem->size = size;
	mem->len = strlen(buf);
	mem->string = buf;
	return funopen(mem, stringread, NULL, NULL, NULL);
}
#endif
