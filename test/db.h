/*
 * Copyright (c) 2013-2015, Dalian Futures Information Technology Co., Ltd.
 *
 * Bo Wang
 * Xiaoye Meng <mengxiaoye at dce dot com dot cn>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef DB_INCLUDED
#define DB_INCLUDED

#include <leveldb/c.h>

/* FIXME */
typedef leveldb_t              db_t;
typedef leveldb_options_t      db_options_t;
typedef leveldb_readoptions_t  db_readoptions_t;
typedef leveldb_writeoptions_t db_writeoptions_t;
typedef leveldb_iterator_t     db_iterator_t;
typedef leveldb_writebatch_t   db_writebatch_t;

/* FIXME: exported functions */
extern db_t              *db_open(db_options_t *o, const char *name, char **errptr);
extern void               db_close(db_t **dbptr);
extern void               db_put(db_t *db, const db_writeoptions_t *wo,
				const char *key, size_t klen, const char *value, size_t vlen, char **errptr);
extern void               db_delete(db_t *db, const db_writeoptions_t *wo,
				const char *key, size_t klen, char **errptr);
/* "leveldb.num-files-at-level<N>", "leveldb.stats", and "leveldb.sstables" */
extern char              *db_property_value(db_t *db, const char *propname);
extern void               db_compact_range(db_t *db, const char *skey, size_t sklen,
				const char *lkey, size_t lklen);
extern db_options_t      *db_options_create(void);
extern void               db_options_destroy(db_options_t **optr);
/* default: 4MB (MemTable) */
extern void               db_options_set_write_buffer_size(db_options_t *o, size_t s);
/* default: 1000 (TableCache) */
extern void               db_options_set_max_open_files(db_options_t *o, int n);
/* default: 8MB (Cache) */
extern void               db_options_set_cache(db_options_t *o, size_t capacity);
/* default: 4K (Block) */
extern void               db_options_set_block_size(db_options_t *o, size_t s);
extern db_readoptions_t  *db_readoptions_create(void);
extern void               db_readoptions_destroy(db_readoptions_t **roptr);
extern db_writeoptions_t *db_writeoptions_create(void);
extern void               db_writeoptions_destroy(db_writeoptions_t **woptr);
extern void               db_writeoptions_set_sync(db_writeoptions_t *wo, unsigned char v);
extern db_iterator_t     *db_iterator_create(db_t *db, db_readoptions_t *ro);
extern void               db_iterator_destroy(db_iterator_t **itptr);
extern unsigned char      db_iterator_valid(db_iterator_t *it);
extern void               db_iterator_seek_to_first(db_iterator_t *it);
extern void               db_iterator_seek_to_last(db_iterator_t *it);
extern void               db_iterator_seek(db_iterator_t *it, const char *key, size_t klen);
extern void               db_iterator_next(db_iterator_t *it);
extern void               db_iterator_prev(db_iterator_t *it);
extern const char        *db_iterator_key(db_iterator_t *it, size_t *klen);
extern const char        *db_iterator_value(db_iterator_t *it, size_t *vlen);
extern db_writebatch_t   *db_writebatch_create(void);
extern void               db_writebatch_destroy(db_writebatch_t **wbptr);
extern void               db_writebatch_clear(db_writebatch_t *wb);
extern void               db_writebatch_put(db_writebatch_t *wb, const char *key, const char *value);
extern void               db_write(db_t *db, const db_writeoptions_t *wo, db_writebatch_t *wb, char **errptr);
extern void               db_free(void *ptr);
//---------------------------------------------------
extern char* db_get(db_t *db, const db_readoptions_t *ro, const char *key, size_t klen, size_t *vlen, char **errptr);
//---------------------------------------------------

#endif /* DB_INCLUDED */