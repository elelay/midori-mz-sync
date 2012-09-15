/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <math.h>

#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>

#include <libsoup/soup.h>
#include <JavaScriptCore/JavaScript.h>

#include <glib/gstdio.h>

#include "ClientConfig.h"

#include "decode.h"
#include "js.h"
#include "sync.h"

void print_bookmark_rec(SyncItem* itm, int depth){
	int i;
	for(i=0;i<depth; i++){
		printf(" ");
	}
	if(itm->type == SYNC_ITEM_FOLDER){
		SYNC_FOLDER* actual = itm->actual;
		if(actual == NULL){
			printf("(invalid folder)\n");
		}else{
			printf("%s '%s' %f\n", itm->id, actual->title, itm->modified);
			for(i = 0 ; i < actual->children->len; i++){
				print_bookmark_rec(g_ptr_array_index(actual->children, i), depth+2);
			}
		}
	}else if(itm->type == SYNC_ITEM_BOOKMARK){
		SYNC_BOOKMARK* actual = itm->actual;
		if(actual == NULL){
			printf("(invalid bookmark)\n");
		}else{
			printf("%s %s %s %f\n", itm->id, actual->title, actual->bmkURI, itm->modified);
		}
	}else if(itm->type == SYNC_ITEM_SEPARATOR){
		printf("_______________________________\n");
	}else{
		g_error("new type of bookmark !");
	}
}

gboolean init_stored_ctx(SYNC_CTX* stored_ctx, JSObjectRef colls, GPtrArray* collections, const char* key, GError** err ){
	
	GError* tmp_error = NULL;
	const char* stored_user = get_string_prop(stored_ctx->ctx, colls, "user", err);
	if(stored_user == NULL){
		if( tmp_error != NULL){
			g_propagate_prefixed_error(err, tmp_error, "Missing user top property: ");
		}else {
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED, "Missing user top property");
		}
		return FALSE;
	}
	
	stored_ctx->enc_user= encode_sha1_base32(stored_user);
	
	g_free((gpointer)stored_user);
	
	guchar** sm = regen_key_hmac(stored_ctx->enc_user, strlen(stored_ctx->enc_user), key, &tmp_error);
	if(sm == NULL){
		g_assert(tmp_error != NULL);
		g_propagate_prefixed_error(err, tmp_error, "Error processing master key: ");
		g_clear_error(&tmp_error);
		return FALSE;
	}
	stored_ctx->master_key = sm[0];
	stored_ctx->master_hmac = sm[1];
	g_free(sm);
	
	int coll_cnt = collections->len;
	int i;
	for(i=0;i<coll_cnt;i++){
		JSObjectRef coll = JSValueToObject(stored_ctx->ctx, g_ptr_array_index(collections, i), NULL);
		
		const gchar* name = get_string_prop(stored_ctx->ctx, coll, "name", &tmp_error);
		if(name == NULL){
			g_assert(tmp_error != NULL);
			g_propagate_error(err, tmp_error);
			return FALSE;
		}
		if(!strcmp("crypto",name)){
			GPtrArray* coll_contents = get_array_prop(stored_ctx->ctx, coll, "content", &tmp_error);
			if(coll_contents == NULL){
				g_assert(tmp_error != NULL);
				g_propagate_error(err, tmp_error);
				return FALSE;
			}
			
			g_assert(coll_contents->len == 1);
			
			JSObjectRef keys = JSValueToObject(stored_ctx->ctx, g_ptr_array_index(coll_contents, 0), NULL);
			g_assert(keys!=NULL);
			
			WBO* w = wbo_from_js(stored_ctx, keys, 0, &tmp_error);
			if(w == NULL){
				g_assert( tmp_error != NULL);
				g_propagate_prefixed_error(err, tmp_error, "Error getting stored keys: ");
				return FALSE;
			}
			
			if(!refresh_bulk_keys_from_wbo(stored_ctx, w, &tmp_error)){
				g_assert( tmp_error != NULL);
				g_propagate_prefixed_error(err, tmp_error, "Error decrypting stored keys: ");
				return FALSE;
			}
			wbo_free(w);
		}
		g_free((gpointer)name);
	}
	if(stored_ctx->bulk_key == NULL || stored_ctx->bulk_hmac == NULL){
		g_set_error (err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED, "Missing 'meta' collection in file\n");
		return FALSE;
	}
	return TRUE;
}


gboolean list_bookmarks(const char* server, const char* user, const char* pass, const char* key, GError** err){
	SYNC_CTX ctx = { 0 };
	GError* tmp_error = NULL;
	
	init_sync(&ctx);
	
	if(!set_user(&ctx, server, user, pass, err) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	
	if(! verify_storage(&ctx, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	
	if(! set_master_key(&ctx, key, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	
	GPtrArray* bookmarks = get_bookmarks(&ctx, &tmp_error);
	if(bookmarks == NULL){
		g_propagate_prefixed_error (err, tmp_error, "No bookmarks! ");
		return FALSE;
	}
	
	int i;
	for(i=0;i<bookmarks->len;i++){
		print_bookmark_rec(g_ptr_array_index(bookmarks, i), 0);
	}  
	
	g_ptr_array_unref(bookmarks);
	
	free_sync(&ctx);
	
	return TRUE;
}

gboolean get_data(const char* server, const char* user, const char* pass, const char* key, const char* filename, GError** err){
	SYNC_CTX ctx = { 0 };
	GError* tmp_error = NULL;
	FILE* file;
	
	init_sync(&ctx);
	
	file = g_fopen(filename, "w");
	if(file == NULL){
		g_set_error (err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED, "Error opening file '%s' for writing", filename);
		return FALSE;
	}
	
	if(!set_user(&ctx, server, user, pass, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	if(! verify_storage(&ctx, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	if(! set_master_key(&ctx, key, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	JSObjectRef collections = list_collections(&ctx, 0, &tmp_error);
	if(collections == NULL){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	GPtrArray* names = get_prop_names(ctx.ctx, collections);
	g_assert(names != NULL);
	
	fprintf(file, "{ \"user\": \"%s\",\n",user);
	fprintf(file, " \"collections\": [\n");
	
	int i;
	for(i=0;i<names->len;i++){
		const char* col_name = g_ptr_array_index(names,i);
		fprintf(file, "{ \"name\": \"%s\", \"content\":[\n", col_name);
		
		GPtrArray* test_content;
		
		// get around the fact that mozilla's server returns an empty "meta" collection
		if(!strcmp("meta",col_name)){
			WBO* global = get_record(&ctx, "meta", "global", &tmp_error);
			if(global != NULL){
				test_content = g_ptr_array_new_full(1,((GDestroyNotify)(wbo_free)));
				g_ptr_array_add(test_content, global);
			}
		}else {
			test_content = get_collection(&ctx, col_name, &tmp_error);
		}
		
		
		if( tmp_error != NULL){
			g_ptr_array_unref(test_content);
			g_propagate_error (err, tmp_error);
			return FALSE;
		}
		
		int j;
		for(j = 0; j<test_content->len;j++){
			WBO* wbo = g_ptr_array_index(test_content,j);
			const gchar* jsn = wbo_to_json(&ctx,wbo, &tmp_error);
			if(jsn == NULL){
				g_warning("couldn't serialize wbo %s[%s]: %s\n", col_name, wbo->id, (tmp_error==NULL? "": tmp_error->message));
			}else{
				fprintf(file, jsn);
				if(j < test_content->len - 1){
					fprintf(file, ",");
				}
				fprintf(file, "\n");
				g_free((gpointer)jsn);
			}
			g_clear_error(&tmp_error);
			if(strcmp("meta",col_name) && strcmp("crypto", col_name)){
				const char* plain = decrypt_wbo(&ctx, col_name, wbo, &tmp_error);
				if(plain == NULL){
					g_warning("couldn't decrypt %s[%s]: %s\n", col_name, wbo->id, (tmp_error==NULL? "": tmp_error->message));
				}else{
					g_free((gpointer)plain);
				}
				g_clear_error(&tmp_error);
			}
		}
		fprintf(file, "]}");
		if(i < names->len - 1){
			fprintf(file, ",");
		}
		fprintf(file, "\n");
		
		g_ptr_array_unref(test_content);
	}
	fprintf(file, "]}\n");
	g_ptr_array_unref(names);
	
	g_clear_error(&tmp_error);
	
	fclose(file);
	free_sync(&ctx);
	
	return TRUE;
}

gboolean register_and_create_storage(const char* server, const char* user, const char* pass, const char* key, const char* client_name, GError** err){
	SYNC_CTX ctx = { 0 };
	
	init_sync(&ctx);
	
	if(register_user(&ctx, server, user, pass, err)){
		if(!set_user(&ctx, server, user, pass, err) ){
			return FALSE;
		}
		g_clear_error(err);
		if(!create_storage(&ctx, key, client_name, err)){
			return FALSE;
		}
	}else{
		return FALSE;
	}
	return TRUE;
}


gboolean put_data(const char* server, const char* user, const char* pass, const char* key, const char* filename, gboolean reg, GError** err){
	SYNC_CTX ctx = { 0 };
	SYNC_CTX stored_ctx = { 0 };
	GError* tmp_error = NULL;
	
	init_sync(&stored_ctx);
	init_sync(&ctx);
	
	
	if(!set_user(&ctx, server, user, pass, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	if(! verify_storage(&ctx, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	if(! set_master_key(&ctx, key, &tmp_error) ){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	JSObjectRef colls;
	gsize contents_length = 0;
	gchar* contents = NULL;
	gboolean success = g_file_get_contents(filename, &contents, &contents_length, &tmp_error);
	if(!success){ 
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	
	g_clear_error(&tmp_error);
	colls = js_from_json(stored_ctx.ctx, contents, contents_length, &tmp_error);
	if(colls == NULL){ 
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_free(contents);
	
	JSObjectRef server_collections = list_collections(&ctx, 0, &tmp_error);
	if(server_collections == NULL){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	g_assert(has_prop(ctx.ctx,server_collections,"meta"));
	
	GPtrArray* collections = get_array_prop(stored_ctx.ctx, colls,"collections", &tmp_error);
	if(collections == NULL){
		g_propagate_error (err, tmp_error);
		return FALSE;
	}
	
	
	
	if(reg){
		if(!init_stored_ctx(&stored_ctx, colls, collections, key, &tmp_error )){
			g_propagate_prefixed_error (err, tmp_error, "error Initializing stored crypto: ");
			return FALSE;
		}
	}
	
	int coll_cnt = collections->len;
	int i;
	for(i=0;i<coll_cnt;i++){
		JSObjectRef coll = JSValueToObject(stored_ctx.ctx, g_ptr_array_index(collections, i), NULL);
		
		const gchar* name = get_string_prop(stored_ctx.ctx, coll, "name", &tmp_error);
		if(name == NULL){
			g_propagate_error (err, tmp_error);
			return FALSE;
		}
		
		
		// don't overwrite my precious keys !
		if(reg && (!strcmp("meta", name) || !strcmp("crypto", name)))
		{
			g_free((gpointer)name);
			continue;
		}
		
		GPtrArray* coll_contents = get_array_prop(stored_ctx.ctx, coll, "content", &tmp_error);
		if(coll_contents == NULL){
			g_propagate_error (err, tmp_error);
			g_free((gpointer)name);
			return FALSE;
		}
		
		int j;
		int content_cnt = coll_contents->len;
		WBO* wbos = g_malloc(sizeof(WBO)*content_cnt);
		for(j=0;j<content_cnt;j++){
			
			
			JSObjectRef pre_wbo = JSValueToObject(stored_ctx.ctx, g_ptr_array_index(coll_contents, j), NULL);
			g_assert(pre_wbo!=NULL);
			
			WBO* wbo = wbo_from_js(&stored_ctx, pre_wbo, j,  &tmp_error);
			if(wbo == NULL){
				g_assert( tmp_error != NULL);
				g_warning("unable to load stored wbo %s[%i]: %s\n", name, j, tmp_error->message);
				continue;
			}
			
			// decrypt using stored keys then encrypt using new keys
			if(reg){
				// this is not meta, so it's encrypted
				gchar* decrypted = decrypt_wbo(&stored_ctx, name, wbo, &tmp_error);
				if(decrypted == NULL){
					g_free((gpointer)name);
					
					g_propagate_prefixed_error(err, tmp_error, "unable to decrypt stored wbo using stored bulk key %s[%i]: ", name, j);
					return FALSE;
				}
				g_free((gpointer)wbo->payload);
				wbo->payload = decrypted;
				wbo->payload = encrypt_wbo(&ctx, name, wbo, &tmp_error);
				g_free(decrypted);
				if(wbo->payload == NULL){
					g_free((gpointer)name);

					g_propagate_prefixed_error(err, tmp_error, "unable to re-encrypt stored wbo using bulk key from server %s[%i]: ", name, j);
					return FALSE;
				}
				
			}
			
			// put it in the array
			wbos[j] = *wbo;
			g_free(wbo);
		}
		
		if(has_prop(ctx.ctx, server_collections, name)){
			g_message("%s is on server, deleting\n",name);
			if(!delete_collection(&ctx, name, &tmp_error)){
				g_free((gpointer)name);

				g_propagate_error (err, tmp_error);
				return FALSE;
			}
			g_clear_error(&tmp_error);
		}else{
			g_message("%s is not on server\n",name);
		}
		
		if(!add_wbos(&ctx, name, wbos, content_cnt, &tmp_error)){
			g_free((gpointer)name);

			g_propagate_prefixed_error(err, tmp_error, "unable to save %s: ", name);
			return FALSE;
		}
		
		for(j=0;j<content_cnt;j++){
			WBO* wbo = &(wbos[j]);
			g_free((gpointer)wbo->id);
			g_free((gpointer)wbo->payload);
		}
		g_free(wbos);
		
		g_free((gpointer)name);
	}
	free_sync(&ctx);
	if(reg){
		free_sync(&stored_ctx);
	}
	return TRUE;
}


gboolean test_file(const char* user, const char* key, const char* filename, GError** err){
	SYNC_CTX stored_ctx = { 0 };
	GError* tmp_error = NULL;
	
	init_sync(&stored_ctx);
	
	JSObjectRef colls;
	gsize contents_length = 0;
	gchar* contents = NULL;
	gboolean ret = TRUE;
	
	if(!g_file_get_contents(filename, &contents, &contents_length, &tmp_error))
	{ 
		g_propagate_prefixed_error(err, tmp_error, "Unable read to data file: ");
		return FALSE;
	}
	g_clear_error(&tmp_error);
	
	colls = js_from_json(stored_ctx.ctx, contents, contents_length, &tmp_error);
	g_free(contents);
	if(colls == NULL){
		g_propagate_prefixed_error(err, tmp_error, "Unable parse to data file as JSON: ");
		return FALSE;
	}
	
	int coll_cnt;
	int i;
	
	GPtrArray* collections = get_array_prop(stored_ctx.ctx, colls,"collections", &tmp_error);
	if(collections == NULL){
		g_propagate_prefixed_error(err, tmp_error, "Root object doesn't have collections property: ");
		return FALSE;
	}
	
	if(!init_stored_ctx(&stored_ctx, colls, collections, key, &tmp_error )){
		g_propagate_prefixed_error(err, tmp_error, "error Initializing stored crypto:");
		return FALSE;
	}
	
	coll_cnt = collections->len;
	for(i=0;i<coll_cnt;i++){
		JSObjectRef coll = JSValueToObject(stored_ctx.ctx, g_ptr_array_index(collections, i), NULL);
		
		const gchar* name = get_string_prop(stored_ctx.ctx, coll, "name", &tmp_error);
		if(name == NULL){
			g_propagate_error (err, tmp_error);
			return FALSE;
		}
		
		GPtrArray* coll_contents = get_array_prop(stored_ctx.ctx, coll, "content", &tmp_error);
		if(coll_contents == NULL){
			g_propagate_prefixed_error(err, tmp_error, "Collection '%s' doesn't have content: ", name);
			return FALSE;
		}
		
		int j;
		int content_cnt = coll_contents->len;
		for(j=0;j<content_cnt;j++){
			
			JSObjectRef pre_wbo = JSValueToObject(stored_ctx.ctx, g_ptr_array_index(coll_contents, j), NULL);
			g_assert(pre_wbo!=NULL);
			
			WBO* wbo = wbo_from_js(&stored_ctx, pre_wbo, j,  &tmp_error);
			if(wbo == NULL){
				g_assert( tmp_error != NULL);
				g_warning("unable to load stored wbo %s[%i]: %s\n", name, j, tmp_error->message);
				ret = FALSE;
				continue;
			}
			
			// try decrypting
			// those are not encrypted or encrypted using master key & already tested...
			if((!strcmp("meta", name) || !strcmp("crypto", name)))
			{
				wbo_free(wbo);
				continue;
			}
			gchar* decrypted = decrypt_wbo(&stored_ctx, name, wbo, &tmp_error);
			if(decrypted == NULL){
				g_assert( tmp_error != NULL);
				g_warning("unable to decrypt stored wbo using stored bulk key %s[%i]: %s\n", name, j, tmp_error->message);
				ret = FALSE;
				wbo_free(wbo);
				continue;
			}
			wbo_free(wbo);
			g_free(decrypted);
			
		}
		g_free((gpointer)name);
	}
	free_sync(&stored_ctx);
	return ret;
}

static gboolean verbose = FALSE;

static gboolean get = FALSE;
static gboolean put = FALSE;
static gboolean reg = FALSE;
static gboolean test = FALSE;
static gboolean bookmarks = FALSE;

static gchar* key = "";
static gchar* user = "";
static gchar* server = "";
static gchar* pass = "";
static gchar* filename = "";

static GOptionEntry entries[] =
{
	{ "key", 'k', 0, G_OPTION_ARG_STRING, &key, "Master key to decrypt contents x-xxxxx-xxxxx-xxxxx-xxxxx-xxxxx (get it from Firefox Account Management drop down > My backup key)", NULL },
	{ "user", 'u', 0, G_OPTION_ARG_STRING, &user, "user name on the server (usually email address)", NULL },
	{ "server", 's', 0, G_OPTION_ARG_STRING, &server, "server URL (mozilla's is at https://auth.services.mozilla.com)", NULL },
	{ "pass", 'p', 0, G_OPTION_ARG_STRING, &pass, "user password (choose a strong password so that others don't delete your stuff)", NULL },
	{ "file", 'f', 0, G_OPTION_ARG_STRING, &filename, "file to read from / to write to", NULL },
	{ "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose, "Be verbose", NULL },
	{ "get", 'g', 0, G_OPTION_ARG_NONE, &get, "Download whole content...", NULL },
	{ "put", 't', 0, G_OPTION_ARG_NONE, &put, "Replace existing content...", NULL },
	{ "register", 'r', 0, G_OPTION_ARG_NONE, &reg, "Create a new account", NULL },
	{ "test", 'e', 0, G_OPTION_ARG_NONE, &test, "Test Saved data (decrypt it)", NULL },
	{ "bookmarks", 'b', 0, G_OPTION_ARG_NONE, &bookmarks, "Print out bookmarks", NULL },
	{ NULL }
};

int main (int argc, char *argv[])
{
	GError *error = NULL;
	GOptionContext *context;
	
	g_type_init ();
	
	fprintf(stdout,"%s Version %d.%d\n",
		argv[0],
		CLIENT_VERSION_MAJOR,
		CLIENT_VERSION_MINOR);
	
	context = g_option_context_new ("- download/upload user sync data");
	
	g_option_context_set_summary(context, 
		"Usage:\n"
		"  Client --get -s SERVER -u USER -p PASSWORD -k MASTER_KEY -f OUTPUT_FILE\n"
		"Grab all your data from the server and store it locally as JSON.\n"
		"Content is still encrypted in the file.\n\n"
		
		"  Client --put [--register] -s SERVER -u USER -p PASSWORD -k MASTER_KEY -f INPUT_FILE\n"
		"Put previously saved data on the server.\n"
		"*DELETES ALL THE CONTENTS* before uploading so be careful\n"
		"If you want to put to another account, it's OK, but you have to keep the same master key\n"
		"If you specify --register a new user account will be created\n\n"
		
		"  Client --test -k MASTER_KEY -f INPUT_FILE\n"
		"Read saved data and try to decrypt the contents.\n"
		"Prints messages on error.\n"
		"*YOU REALLY WANT TO RUN THIS BEFORE USING --put*\n\n"
		
		"  Client --bookmarks -s SERVER -u USER -p PASSWORD -k MASTER_KEY\n"
		"Read saved data and prints out the bookmarks as HTML.\n"
		);
	g_option_context_add_main_entries (context, entries, NULL);
	if (!g_option_context_parse (context, &argc, &argv, &error))
	{
		g_print ("option parsing failed: %s\n", error->message);
		exit (1);
	}
	g_option_context_free(context);
	
	GError* err = NULL;
	const char* client_name = g_strdup_printf("mz-sync standalone client %i.%i",
		CLIENT_VERSION_MAJOR, CLIENT_VERSION_MINOR);
	
	if(get){
		
		if(!get_data(server, user, pass, key, filename, &err)){
			if( err != NULL){
				fprintf (stderr, "Error: %s\n", err->message);
			}else{
				fprintf (stderr, "Error getting data!\n");
			}
			return -1;
		}
		
	}else if(put || reg){
		
		if(reg){
			if(!register_and_create_storage(server, user, pass, key, client_name, &err)){
				if( err != NULL){
					fprintf (stderr, "Error registering: %s\n", err->message);
				}else {
					fprintf (stderr, "Error registering!\n");
				}
				return -1;
			}
		}
		g_clear_error(&err);
		
		if(put){
			if(!put_data(server, user, pass, key, filename, reg, &err)){
				if( err != NULL){
					fprintf (stderr, "Error uploading data: %s\n", err->message);
				}else {
					fprintf (stderr, "Error uploading data!\n");
				}
				return -1;
			}
			
		}
		
	}else if(test){
		if(!test_file(user, key, filename, &err)){
			if( err != NULL){
				fprintf (stderr, "Fatal error: %s\n", err->message);
			}else {
				fprintf (stderr, "There were errors!\n");
			}
			return -1;
		}else{
			printf("ALL GOOD !\n");
		}
	}else if(bookmarks){
		if(!list_bookmarks(server, user, pass, key, &err)){
			if( err != NULL){
				fprintf (stderr, "Fatal error listing bookmarks: %s\n", err->message);
			}else {
				fprintf (stderr, "There were errors listing bookmarks!\n");
			}
			return -1;
		}
	}
	return 0;
}
