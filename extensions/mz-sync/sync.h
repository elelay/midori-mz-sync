/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/
#ifndef SYNC_H
#define SYNC_H

#include <glib.h>
#include <libsoup/soup.h>
#include <JavaScriptCore/JavaScript.h>

#define MY_SYNC_ERROR my_sync_error_quark ()

GQuark my_sync_error_quark (void);

typedef enum {
	MY_SYNC_ERROR_FORMAT,	    /* error with the content from the server (can't parse, interpret it) */
	MY_SYNC_ERROR_COMMUNICATION, /* error getting to the server */
	MY_SYNC_ERROR_WRONG_USER,	/* user not found */
	MY_SYNC_ERROR_WRONG_PASSWORD,	/* unauthorized */
	MY_SYNC_ERROR_BACKOFF,		/* todo: server requested that we stop interacting with it */
	MY_SYNC_ERROR_CAPTCHA,
	MY_SYNC_ERROR_FAILED
} MySyncError;

typedef struct {
	JSGlobalContextRef ctx;
	SoupSession *session;
	const char* end_point;
	const char* enc_user;
	const guchar* master_key;
	const guchar* master_hmac;
	const guchar* bulk_key;
	const guchar* bulk_hmac;
	const char** user_pass;
} SYNC_CTX;

typedef struct {
const char* id;
double modified;
int sortindex;
const char* payload;
int ttl;
} WBO;


typedef struct {
	const char* title;
	const char* bmkURI;
	const char* description;
	GPtrArray* tags;
	const char* keyword;
} SYNC_BOOKMARK;

typedef struct {
	const char* title;
	GPtrArray* children;
} SYNC_FOLDER;

typedef struct {
} SYNC_HISTORY;

typedef struct {
} SYNC_PASSWORD;

typedef enum {                                        
	SYNC_ITEM_FOLDER,
	SYNC_ITEM_BOOKMARK,
	SYNC_ITEM_SEPARATOR,
} SyncItemType;

typedef struct {
	SyncItemType type;
	const char* id;
	double modified;
	gpointer actual;
} SyncItem;

void init_sync(SYNC_CTX* s_ctx);
void free_sync(SYNC_CTX* s_ctx);

void wbo_free(WBO* itm);

WBO* wbo_from_js(SYNC_CTX* s_ctx, JSObjectRef o, int i, GError** err);
const gchar* wbo_to_json(SYNC_CTX* s_ctx, WBO* wbo, GError** err);


gboolean user_exists(SYNC_CTX* s_ctx, const char* email, const char* server_url, GError** err);

gboolean set_user(SYNC_CTX* s_ctx,  const char* server_url, const char* user, const char* pass, GError** err);

gboolean set_master_key(SYNC_CTX* s_ctx, const char* master_key, GError** err);
unsigned char** regen_key_hmac(const char* username, gsize username_len, const char* user_key, GError** err);
gboolean refresh_bulk_keys_from_wbo(SYNC_CTX* ctx, WBO* cryptokeys, GError** err);

JSObjectRef list_collections(SYNC_CTX* s_ctx, GError** err);

GPtrArray* get_collection(SYNC_CTX* s_ctx, const char* collection, GError** err);

WBO* get_record(SYNC_CTX* s_ctx, const char* collection, const char* id, GError** err);

char* decrypt_wbo(SYNC_CTX* s_ctx, const char* collection, WBO* w, GError** err);
const char* encrypt_wbo(SYNC_CTX* s_ctx, const char* collection, const WBO* w, GError** err);


gboolean verify_storage(SYNC_CTX* s_ctx, GError** err);

GPtrArray* get_bookmarks(SYNC_CTX* s_ctx, GError** err);

// modification
gboolean register_user(SYNC_CTX* s_ctx, const char* server_url, const char* email, const char* password, GError** err);
gboolean create_collection(SYNC_CTX* s_ctx, const char* collection, GError** err);
gboolean delete_collection(SYNC_CTX* s_ctx, const char* collection, GError** err);
gboolean add_wbos(SYNC_CTX* s_ctx, const char* collection, WBO* wbos, int cnt, GError** err);
gboolean create_storage(SYNC_CTX* s_ctx, const char* master_key, const char* client_name, GError** err);

// utilities
WBO* get_wbo_by_id(GPtrArray* coll, const char* id);

#endif        //  #ifndef SYNC_H

