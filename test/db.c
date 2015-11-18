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

#include <string.h>
#include "db.h"

db_t *db_open(db_options_t *o, const char *name, char **errptr) {
	leveldb_filterpolicy_t *fp = leveldb_filterpolicy_create_bloom(10);

	leveldb_options_set_create_if_missing(o, 1);
	leveldb_options_set_filter_policy(o, fp);
	return leveldb_open(o, name, errptr);
}

void db_close(db_t **dbptr) {
	leveldb_close(*dbptr);
	*dbptr = NULL;
}

void db_put(db_t *db, const db_writeoptions_t *wo,
	const char *key, size_t klen, const char *value, size_t vlen, char **errptr) {
	leveldb_put(db, wo, key, klen, value, vlen, errptr);
}

void db_delete(db_t *db, const db_writeoptions_t *wo, const char *key, size_t klen, char **errptr) {
	leveldb_delete(db, wo, key, klen, errptr);
}

char *db_property_value(db_t *db, const char *propname) {
	return leveldb_property_value(db, propname);
}

void db_compact_range(db_t *db, const char *skey, size_t sklen, const char *lkey, size_t lklen) {
	leveldb_compact_range(db, skey, sklen, lkey, lklen);
}

db_options_t *db_options_create(void) {
	return leveldb_options_create();
}

void db_options_destroy(db_options_t **optr) {
	leveldb_options_destroy(*optr);
	*optr = NULL;
}

void db_options_set_write_buffer_size(db_options_t *o, size_t s) {
	leveldb_options_set_write_buffer_size(o, s);
}

void db_options_set_max_open_files(db_options_t *o, int n) {
	leveldb_options_set_max_open_files(o, n);
}

void db_options_set_cache(db_options_t *o, size_t capacity) {
	leveldb_cache_t *c = leveldb_cache_create_lru(capacity);

	leveldb_options_set_cache(o, c);
}

void db_options_set_block_size(db_options_t *o, size_t s) {
	leveldb_options_set_block_size(o, s);
}

db_readoptions_t *db_readoptions_create(void) {
	return leveldb_readoptions_create();
}

void db_readoptions_destroy(db_readoptions_t **roptr) {
	leveldb_readoptions_destroy(*roptr);
	*roptr = NULL;
}

db_writeoptions_t *db_writeoptions_create(void) {
	return leveldb_writeoptions_create();
}

void db_writeoptions_destroy(db_writeoptions_t **woptr) {
	leveldb_writeoptions_destroy(*woptr);
	*woptr = NULL;
}

void db_writeoptions_set_sync(db_writeoptions_t *wo, unsigned char v) {
	leveldb_writeoptions_set_sync(wo, v);
}

db_iterator_t *db_iterator_create(db_t *db, db_readoptions_t *ro) {
	return leveldb_create_iterator(db, ro);
}

void db_iterator_destroy(db_iterator_t **itptr) {
	leveldb_iter_destroy(*itptr);
	*itptr = NULL;
}

unsigned char db_iterator_valid(db_iterator_t *it) {
	return leveldb_iter_valid(it);
}

void db_iterator_seek_to_first(db_iterator_t *it) {
	leveldb_iter_seek_to_first(it);
}

void db_iterator_seek_to_last(db_iterator_t *it) {
	leveldb_iter_seek_to_last(it);
}

void db_iterator_seek(db_iterator_t *it, const char *key, size_t klen) {
	leveldb_iter_seek(it, key, klen);
}

void db_iterator_next(db_iterator_t *it) {
	leveldb_iter_next(it);
}

void db_iterator_prev(db_iterator_t *it) {
	leveldb_iter_prev(it);
}

const char *db_iterator_key(db_iterator_t *it, size_t *klen) {
	return leveldb_iter_key(it, klen);
}

const char *db_iterator_value(db_iterator_t *it, size_t *vlen) {
	return leveldb_iter_value(it, vlen);
}

db_writebatch_t *db_writebatch_create(void) {
	return leveldb_writebatch_create();
}

void db_writebatch_destroy(db_writebatch_t **wbptr) {
	leveldb_writebatch_destroy(*wbptr);
	*wbptr = NULL;
}

void db_writebatch_clear(db_writebatch_t *wb) {
	leveldb_writebatch_clear(wb);
}

void db_writebatch_put(db_writebatch_t *wb, const char *key, const char *value) {
	leveldb_writebatch_put(wb, key, strlen(key), value, strlen(value));
}

void db_write(db_t *db, const db_writeoptions_t *wo, db_writebatch_t *wb, char **errptr) {
	leveldb_write(db, wo, wb, errptr);
}

void db_free(void *ptr) {
	leveldb_free(ptr);
}

//---------------------------------------------------------
char* db_get(db_t *db, const db_readoptions_t *ro, const char *key, size_t klen, size_t *vlen, char **errptr)
{
	leveldb_get(db, ro, key, klen, vlen, errptr);
}
//---------------------------------------------------------