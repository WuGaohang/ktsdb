/*
 * Copyright (c) 2006-2012, Salvatore Sanfilippo <antirez at gmail dot com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * tailored by xiaoyem
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "mem.h"
#include "dstr.h"

struct dshdr {
	size_t	len, avail;
	char	buf[0];
};

/* FIXME */
#define DSTR_MAX_PREALLOC (1024 * 1024)

static inline int is_hex_digit(char c) {
	return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static inline int hex_digit_to_int(char c) {
	switch (c) {
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'a':
	case 'A': return 10;
	case 'b':
	case 'B': return 11;
	case 'c':
	case 'C': return 12;
	case 'd':
	case 'D': return 13;
	case 'e':
	case 'E': return 14;
	case 'f':
	case 'F': return 15;
	default:  return 0;
	}
}

dstr dstr_new_len(const char *str, size_t length) {
	struct dshdr *dh;

	dh = str ? ALLOC(sizeof *dh + length + 1) : CALLOC(1, sizeof *dh + length + 1);
	if (dh == NULL)
		return NULL;
	dh->len   = length;
	dh->avail = 0;
	if (str && length)
		memcpy(dh->buf, str, length);
	dh->buf[length] = '\0';
	return dh->buf;
}

dstr dstr_new(const char *str) {
	size_t length = str ? strlen(str) : 0;

	return dstr_new_len(str, length);
}

/* FIXME */
void dstr_free(dstr ds) {
	struct dshdr *dh;

	if (ds == NULL)
		return;
	dh = (struct dshdr *)(ds - sizeof *dh);
	FREE(dh);
}

size_t dstr_length(const dstr ds) {
	struct dshdr *dh;

	if (ds == NULL)
		return 0;
	dh = (struct dshdr *)(ds - sizeof *dh);
	return dh->len;
}

size_t dstr_avail(const dstr ds) {
	struct dshdr *dh;

	if (ds == NULL)
		return 0;
	dh = (struct dshdr *)(ds - sizeof *dh);
	return dh->avail;
}

dstr dstr_make_room(dstr ds, size_t length) {
	struct dshdr *dh;
	size_t newlen;

	if (ds == NULL)
		return NULL;
	dh = (struct dshdr *)(ds - sizeof *dh);
	if (dh->avail >= length)
		return ds;
	newlen = dh->len + length;
	if (newlen < DSTR_MAX_PREALLOC)
		newlen *= 2;
	else
		newlen += DSTR_MAX_PREALLOC;
	if (RESIZE(dh, sizeof *dh + newlen + 1) == NULL)
		return NULL;
	dh->avail = newlen - dh->len;
	return dh->buf;
}

void dstr_incr_len(dstr ds, int incr) {
	struct dshdr *dh;

	if (ds == NULL)
		return;
	dh = (struct dshdr *)(ds - sizeof *dh);
	if (dh->avail >= incr) {
		dh->len   += incr;
		dh->avail -= incr;
		ds[dh->len] = '\0';
	}
}

dstr dstr_remove_avail(dstr ds) {
	struct dshdr *dh;

	if (ds == NULL)
		return NULL;
	dh = (struct dshdr *)(ds - sizeof *dh);
	if (RESIZE(dh, sizeof *dh + dh->len + 1) == NULL)
		return NULL;
	dh->avail = 0;
	return dh->buf;
}

size_t dstr_alloc_size(dstr ds) {
	struct dshdr *dh;

	if (ds == NULL)
		return 0;
	dh = (struct dshdr *)(ds - sizeof *dh);
	return sizeof *dh + dh->len + dh->avail + 1;
}

dstr dstr_cat_len(dstr ds, const char *str, size_t length) {
	struct dshdr *dh;
	size_t len = dstr_length(ds);

	if ((ds = dstr_make_room(ds, length)) == NULL)
		return NULL;
	memcpy(ds + len, str, length);
	ds[len + length] = '\0';
	dh = (struct dshdr *)(ds - sizeof *dh);
	dh->len = len + length;
	dh->avail -= length;
	return ds;
}

dstr dstr_cat(dstr ds, const char *str) {
	size_t length = str ? strlen(str) : 0;

	return dstr_cat_len(ds, str, length);
}

dstr dstr_cat_vprintf(dstr ds, const char *fmt, va_list ap) {
	char *buf, *tmp;
	size_t len = 16;
	va_list cpy;

	for (;;) {
		if ((buf = ALLOC(len)) == NULL)
			return NULL;
		buf[len - 2] = '\0';
		va_copy(cpy, ap);
		vsnprintf(buf, len, fmt, cpy);
		if (buf[len - 2] != '\0') {
			FREE(buf);
			len *= 2;
			continue;
		}
		break;
	}
	tmp = dstr_cat(ds, buf);
	FREE(buf);
	return tmp;
}

dstr dstr_cat_printf(dstr ds, const char *fmt, ...) {
	va_list ap;
	char *tmp;

	va_start(ap, fmt);
	tmp = dstr_cat_vprintf(ds, fmt, ap);
	va_end(ap);
	return tmp;
}

dstr dstr_trim(dstr ds, const char *cset) {
	struct dshdr *dh;
	char *start, *sp, *end, *ep;
	size_t len;

	if (ds == NULL)
		return NULL;
	dh = (struct dshdr *)(ds - sizeof *dh);
	sp = start = ds;
	ep = end   = ds + dstr_length(ds) - 1;
	while (sp <= end && strchr(cset, *sp))
		++sp;
	while (ep > start && strchr(cset, *ep))
		--ep;
	len = sp > ep ? 0 : ep - sp + 1;
	if (dh->buf != sp)
		memmove(dh->buf, sp, len);
	dh->buf[len] = '\0';
	dh->avail += dh->len - len;
	dh->len = len;
	return ds;
}

dstr dstr_range(dstr ds, int start, int end) {
	struct dshdr *dh;
	size_t newlen;

	if (ds == NULL)
		return NULL;
	dh = (struct dshdr *)(ds - sizeof *dh);
	if (dh->len == 0)
		return NULL;
	if (start < 0) {
		start += dh->len;
		if (start < 0)
			start = 0;
	}
	if (end < 0) {
		end += dh->len;
		if (end < 0)
			end = 0;
	}
	newlen = start > end ? 0 : end - start + 1;
	if (newlen) {
		if (start >= (signed)dh->len)
			newlen = 0;
		else if (end >= (signed)dh->len) {
			end = dh->len - 1;
			newlen = start > end ? 0 : end - start + 1;
		}
	} else
		start = 0;
	/* FIXME */
	if (start && newlen)
		memmove(ds, ds + start, newlen);
	ds[newlen] = '\0';
	dh->avail += dh->len - newlen;
	dh->len = newlen;
	return ds;
}

void dstr_clear(dstr ds) {
	struct dshdr *dh;

	if (ds == NULL)
		return;
	dh = (struct dshdr *)(ds - sizeof *dh);
	dh->avail += dh->len;
	dh->len = 0;
	dh->buf[0] = '\0';
}

dstr *dstr_split_len(const char *str, size_t length, const char *sep, size_t seplength, int *count) {
	int slots = 5, i, nelems = 0, start = 0;
	dstr *tokens;

	/* FIXME */
	if (str == NULL || sep == NULL)
		return NULL;
	if (length < 0 || seplength < 1)
		return NULL;
	if ((tokens = ALLOC(slots * sizeof *tokens)) == NULL)
		return NULL;
	if (length == 0) {
		*count = 0;
		return tokens;
	}

	for (i = 0; i < length - seplength + 1; ++i) {
		/* make sure there is room for the next and final ones */
		if (slots < nelems + 2) {
			slots *= 2;
			if (RESIZE(tokens, slots * sizeof *tokens) == NULL)
				goto err;
		}
		if ((seplength == 1 && str[i] == sep[0]) || !memcmp(str + i, sep, seplength)) {
			if ((tokens[nelems] = dstr_new_len(str + start, i - start)) == NULL)
				goto err;
			++nelems;
			start = i + seplength;
			i += seplength - 1;
		}
	}
	/* add the final one */
	if ((tokens[nelems] = dstr_new_len(str + start, length - start)) == NULL)
		goto err;
	++nelems;
	*count = nelems;
	return tokens;

err:
	for (i = 0; i < nelems; ++i)
		dstr_free(tokens[i]);
	FREE(tokens);
	*count = 0;
	return NULL;
}

void dstr_free_tokens(dstr *tokens, int count) {
	int i;

	if (tokens == NULL)
		return;
	for (i = 0; i < count; ++i)
		dstr_free(tokens[i]);
	FREE(tokens);
}

dstr *dstr_split_args(const char *line, int *argc) {
	const char *p = line;
	dstr current = NULL;
	dstr *argv = NULL;

	*argc = 0;
	for (;;) {
		while (*p && isspace(*p))
			++p;
		if (*p) {
			int inq  = 0; /* 1 if in quotes */
			int insq = 0; /* 1 if in single quotes */
			int done = 0;

			if (current == NULL)
				current = dstr_new_len("", 0);
			while (!done) {
				/* FIXME */
				if (inq) {
					if (*p == '\\' && *(p + 1) == 'x' &&
						is_hex_digit(*(p + 2)) && is_hex_digit(*(p + 3))) {
						unsigned char byte = 16 * hex_digit_to_int(*(p + 2)) +
							hex_digit_to_int(*(p + 3));

						p += 3;
						current = dstr_cat_len(current, (char *)&byte, 1);
					} else if (*p == '\\' && *(p + 1)) {
						char c;

						++p;
						switch (*p) {
						case 'a':
							c = '\a';
							break;
						case 'b':
							c = '\b';
							break;
						case 'n':
							c = '\n';
							break;
						case 'r':
							c = '\r';
							break;
						case 't':
							c = '\t';
							break;
						default:
							c = *p;
							break;
						}
						current = dstr_cat_len(current, &c, 1);
					} else if (*p == '"') {
						/* closing quote must be followed by a space or not at all */
						if (*(p + 1) && !isspace(*(p + 1)))
							goto err;
						done = 1;
					/* unterminated quotes */
					} else if (*p == '\0')
						goto err;
					else
						current = dstr_cat_len(current, p, 1);
				} else if (insq) {
					if (*p == '\\' && *(p + 1) == '\'') {
						++p;
						current = dstr_cat_len(current, "'", 1);
					} else if (*p == '\'') {
						/* closing quote must be followed by a space or not at all */
						if (*(p + 1) && !isspace(*(p + 1)))
							goto err;
						done = 1;
					/* unterminated quotes */
					} else if (*p == '\0')
						goto err;
					else
						current = dstr_cat_len(current, p, 1);
				} else
					switch (*p) {
					case ' ':
					case '\0':
					case '\n':
					case '\r':
					case '\t':
						done = 1;
						break;
					case '"':
						inq = 1;
						break;
					case '\'':
						insq = 1;
						break;
					default:
						current = dstr_cat_len(current, p, 1);
						break;
					}
				if (*p)
					++p;
			}
			if (RESIZE(argv, (*argc + 1) * sizeof (char *)) == NULL)
				goto err;
			argv[*argc] = current;
			++*argc;
			current = NULL;
		} else
			return argv;
	}

err:
	{
		int i;

		for (i = 0; i < *argc; ++i)
			dstr_free(argv[i]);
		FREE(argv);
		if (current)
			dstr_free(current);
		return NULL;
	}
}

inline void dstr_free_args(dstr *argv, int argc) {
	dstr_free_tokens(argv, argc);
}

