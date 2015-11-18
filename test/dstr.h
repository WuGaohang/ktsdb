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

#ifndef DSTR_INCLUDED
#define DSTR_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stddef.h>

/* exported types */
typedef char *dstr;

/* exported functions */
extern dstr   dstr_new_len(const char *str, size_t length);
extern dstr   dstr_new(const char *str);
extern void   dstr_free(dstr ds);
extern size_t dstr_length(const dstr ds);
extern size_t dstr_avail(const dstr ds);
extern dstr   dstr_make_room(dstr ds, size_t length);
extern void   dstr_incr_len(dstr ds, int incr);
extern dstr   dstr_remove_avail(dstr ds);
extern size_t dstr_alloc_size(dstr ds);
extern dstr   dstr_cat_len(dstr ds, const char *str, size_t length);
extern dstr   dstr_cat(dstr ds, const char *str);
extern dstr   dstr_cat_vprintf(dstr ds, const char *fmt, va_list ap);
extern dstr   dstr_cat_printf(dstr ds, const char *fmt, ...);
extern dstr   dstr_trim(dstr ds, const char *cset);
extern dstr   dstr_range(dstr ds, int start, int end);
extern void   dstr_clear(dstr ds);
extern dstr  *dstr_split_len(const char *str, size_t length, const char *sep, size_t seplength, int *count);
extern void   dstr_free_tokens(dstr *tokens, int count);
extern dstr  *dstr_split_args(const char *line, int *argc);
extern void   dstr_free_args(dstr *argv, int argc);

#ifdef __cplusplus
}
#endif

#endif /* DSTR_INCLUDED */

