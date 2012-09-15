/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/
#include "sync.h"

#include <stdio.h>
#include <string.h>
#include <limits.h>


#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/rand.h>
#include <openssl/err.h>

#include "decode.h"
#include "js.h"

#define SYNC_SUPPORTED_VERSION			5

static gboolean debug = FALSE, quiet = FALSE;

GQuark
my_sync_error_quark (void)
{
	return g_quark_from_static_string ("my-sync-error-quark");
}

// internal functions
char* get_sync_node(SYNC_CTX* s_ctx, const char* username, const char* server_url, GError** err);
GPtrArray* wbo_list_from_json(SYNC_CTX* s_ctx, const char* body, gsize len, GError** err);
unsigned char** generate_bulk_key(GError** err);
gboolean refresh_bulk_keys(SYNC_CTX* ctx, GError** err);
guchar** decrypt_bulk_keys(SYNC_CTX* ctx, const guchar* master_key, const guchar* master_hmac, WBO* keys, GError** err);
const char* encrypt_wbo_int(SYNC_CTX* s_ctx, const guchar* key, const guchar* hmac, const WBO* w, GError** err);
char* decrypt_wbo_int(SYNC_CTX* s_ctx, const guchar* key, const guchar* hmac, const WBO* w, GError** err);
JSObjectRef list_collections_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, time_t if_modified_since, GError** err);
GPtrArray* get_collection_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, GError** err);
WBO* get_record_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, const char* id, GError** err);
gboolean create_collection_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, GError** err);
gboolean delete_collection_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, GError** err);
gboolean add_wbos_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, WBO* wbos, int cnt, GError** err);
void sync_item_free(SyncItem* itm);
void sync_folder_free(SYNC_FOLDER* f);
void sync_bookmark_free(SYNC_BOOKMARK* b);
gboolean create_metaglobal(SYNC_CTX* s_ctx, GError** err);
gboolean create_cryptokeys(SYNC_CTX* s_ctx, const char* master_key, GError** err);
gboolean create_client(SYNC_CTX* s_ctx, const char* name, const char* type, GError** err);

void sync_item_free(SyncItem* itm){
	g_free((gpointer)itm->id);
	if(itm->actual != NULL){
		switch(itm->type){
		case SYNC_ITEM_FOLDER:
			sync_folder_free((SYNC_FOLDER*)itm->actual);
			break;
		case SYNC_ITEM_BOOKMARK:
			sync_bookmark_free((SYNC_BOOKMARK*)itm->actual);
			break;
		case SYNC_ITEM_SEPARATOR:
			g_free(itm->actual);
			break;
		}
	}
	g_free(itm);
}

void sync_folder_free(SYNC_FOLDER* f){
	g_free((gpointer)f->title);
	g_ptr_array_unref(f->children);
	g_free(f);
}

void sync_bookmark_free(SYNC_BOOKMARK* b){
	g_free((gpointer)b->title);
	g_free((gpointer)b->bmkURI);
	g_free((gpointer)b->description);
	g_ptr_array_unref(b->tags);
	//TODO: keyword
	g_free(b);
}


void wbo_free(WBO* itm){
	g_free((gpointer)itm->id);
	g_free((gpointer)itm->payload);
	g_free(itm);
}
                                            
const gchar* wbo_to_json(SYNC_CTX* s_ctx, WBO* wbo, GError** err){
	const gchar* ret;
	JSObjectRef oref = js_create_empty_object(s_ctx->ctx);
	
	if(!set_string_prop(s_ctx->ctx, oref, "id", wbo->id, err)){
		return NULL;
	}
	if(!set_double_prop(s_ctx->ctx, oref, "modified", wbo->modified, err)){
		return NULL;
	}
	//if(wbo->sortindex != INT_MAX){
	//	set_int_property(s_ctx->ctx, "sortindex", wbo->sortindex);
	//}
	if(!set_string_prop(s_ctx->ctx, oref, "payload", wbo->payload, err)){
		return NULL;
	}
	ret = js_to_json(s_ctx->ctx, oref);
	return ret;
}

WBO* wbo_from_js(SYNC_CTX* s_ctx, JSObjectRef o, int i, GError** err){
	WBO * mywbo;
	GError* tmp_error = NULL;
	
	mywbo = g_malloc(sizeof(WBO));
	
	mywbo->id = get_string_prop(s_ctx->ctx, o,"id", &tmp_error);
	
	if(tmp_error != NULL){
		g_set_error(err,
			MY_SYNC_ERROR,
			MY_SYNC_ERROR_FORMAT,
			"can't parse collection[%i].id : %s\n",i, strdup(tmp_error->message));
		g_clear_error(&tmp_error);
		return NULL;
	}
	
	mywbo->modified = get_double_prop(s_ctx->ctx, o, "modified", &tmp_error);
	
	if(tmp_error != NULL){
		g_set_error(err,
			MY_SYNC_ERROR,
			MY_SYNC_ERROR_FORMAT,
			"can't parse collection[%i].modified : %s\n",i ,strdup(tmp_error->message));
		g_clear_error(&tmp_error);
		return NULL;
	}
	
	mywbo->sortindex = get_int_prop(s_ctx->ctx, o, "sortindex", &tmp_error);
	
	if(tmp_error != NULL){
		if(tmp_error->code == MY_JS_ERROR_NO_SUCH_VALUE){
			// simply missing, not too bad
			g_clear_error(&tmp_error);
			mywbo->sortindex = INT_MAX;
			
		}else{
			g_set_error(err,
				MY_SYNC_ERROR,
				MY_SYNC_ERROR_FORMAT,
				"can't parse collection[%i].sortindex : %s\n",i, tmp_error->message);
			g_clear_error(&tmp_error);
			return NULL;
		}
	}
	
	mywbo->payload = get_string_prop(s_ctx->ctx, o, "payload", &tmp_error);
	
	if(tmp_error != NULL){
		g_set_error(err,
			MY_SYNC_ERROR,
			MY_SYNC_ERROR_FORMAT,
			"can't parse collection[%i].payload : %s\n",i, strdup(tmp_error->message));
		g_clear_error(&tmp_error);
		return NULL;
	}
	
	mywbo->ttl = get_int_prop(s_ctx->ctx, o, "ttl", &tmp_error);
	
	if(tmp_error != NULL){
		if(tmp_error->code == MY_JS_ERROR_NO_SUCH_VALUE){
			// simply missing, not too bad
			g_clear_error(&tmp_error);
			mywbo->ttl = 0;
			
		}else{
			g_set_error(err,
				MY_SYNC_ERROR,
				MY_SYNC_ERROR_FORMAT,
				"can't parse collection[%i].ttl : %s\n",i, tmp_error->message);
			g_clear_error(&tmp_error);
			return NULL;
		}
	}
	
	return mywbo;

}

GPtrArray* wbo_list_from_json(SYNC_CTX* s_ctx, const char* body, gsize len, GError** err){
	
	JSObjectRef oref;
	GPtrArray* res = NULL;
	JSValueRef jsv;
	WBO * mywbo;
	GError* tmp_error;
	tmp_error = NULL;
	oref = js_from_json(s_ctx->ctx, body, len, &tmp_error);
	if(tmp_error != NULL) {
		g_propagate_error(err, tmp_error);
		return NULL;
	}else{
		gsize pcnt = get_int_prop(s_ctx->ctx, oref, "length", &tmp_error);
		if(tmp_error != NULL) {
			g_set_error(err,
				MY_SYNC_ERROR,
				MY_SYNC_ERROR_FORMAT,
				"can't parse collection: %s\n",strdup(tmp_error->message));
			g_clear_error(&tmp_error);
			return NULL;
		} else {
			res = g_ptr_array_new_full(pcnt, ((GDestroyNotify)(wbo_free)));
			int i;
			for(i=0;i<pcnt;i++){
				jsv = JSObjectGetPropertyAtIndex(s_ctx->ctx, oref, i, NULL);
				if(jsv == NULL) {
					g_set_error(err,
						MY_SYNC_ERROR,
						MY_SYNC_ERROR_FORMAT,
						"null item in collection[%i]\n",i);
					return NULL;
				} else {
					JSObjectRef o = JSValueToObject(s_ctx->ctx, jsv,NULL);
					mywbo = wbo_from_js(s_ctx, o, i, &tmp_error);
					if(mywbo == NULL){
						g_propagate_error(err, tmp_error);
						return NULL;
					}
					g_ptr_array_add(res,mywbo);
				}
			}
		}
	}
	return res;
}


void log_error_if_verbose(const char* descr,const char* name, SoupMessage* msg){
	const char *header;
	
	printf("Error %s: details follow\n",descr);
	if (msg->status_code == SOUP_STATUS_SSL_FAILED) {
		GTlsCertificateFlags flags;
		
		if (soup_message_get_https_status (msg, NULL, &flags))
			printf ("%s: %d %s (0x%x)\n", name, msg->status_code, msg->reason_phrase, flags);
		else
			printf ("%s: %d %s (no handshake status)\n", name, msg->status_code, msg->reason_phrase);
	} else if (!quiet || SOUP_STATUS_IS_TRANSPORT_ERROR (msg->status_code)) {
		printf ("%s: %d %s\n", name, msg->status_code, msg->reason_phrase);
	} else if (SOUP_STATUS_IS_REDIRECTION (msg->status_code)) {
		header = soup_message_headers_get_one (msg->response_headers,
			"Location");
		if (header) {
			
			if (!debug && !quiet)
				printf ("  -> %s\n", header);
		}
	}
}

/* returns a 12 chars base64 random string */
char* generate_id(GError** err){
	guchar* buf = g_malloc(6);
	gchar* id;
	int success = RAND_pseudo_bytes(buf, 6);
	if(success == -1){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
			"Generating random bytes seems to be unsupported (errcode=%lu)", ERR_get_error());
		g_free(buf);
		return NULL;
	}else{
		id = g_base64_encode(buf, 6);
		g_free(buf);
		return id;
	}
}

char* get_sync_node(SYNC_CTX* s_ctx, const char* username, const char* server_url, GError** err)
{
	GString* url;
	const char *name;
	SoupMessage *msg;
	char *res;
	const char* method;
	
	url = g_string_new(server_url);
	g_string_append_printf(url, "/user/1.0/%s/node/weave",username);
	
	method = SOUP_METHOD_GET;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	soup_session_send_message (s_ctx->session, msg);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		
		res = g_strndup(msg->response_body->data,msg->response_body->length);
		
	} else if(msg->status_code == SOUP_STATUS_NOT_FOUND){
		
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_USER,
			"user %s doesn't exist",username);
		res = NULL;
		
	} else {
		log_error_if_verbose("in get_sync_node",name, msg);
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error in get_sync_node: server replied with %i",msg->status_code);
		res = NULL;
	}
	
	g_string_free(url,TRUE);
	g_object_unref(msg);
	
	return res;
}

gboolean user_exists(SYNC_CTX* s_ctx, const char* username, const char* server_url, GError** err)
{
	GString* url;
	SoupMessage *msg;
	gboolean res;
	const char* method;
	gchar* canned_response;
	
	url = g_string_new(server_url);
	g_string_append_printf(url, "/user/1.0/%s",username);
	
	method = SOUP_METHOD_GET;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	soup_session_send_message (s_ctx->session, msg);
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		// only 0 or 1
		g_assert(msg->response_body->length == 1);
		
		/* if returned 0, then doesn't exist, else exists */
		res = msg->response_body->data[0] == '1';
		
	} else {
		switch(msg->status_code){
			case SOUP_STATUS_CANT_RESOLVE:
				canned_response = "Can't resolve host (the server's name may be wrong)";
				break;
			case SOUP_STATUS_CANT_RESOLVE_PROXY:
				canned_response = "Can't resolve proxy (check your proxy configuration)";
				break;
			case SOUP_STATUS_CANT_CONNECT:
				canned_response = "Can't connect to host (check the server's name and if the server is running; check also your network connection)";
				break;
			case SOUP_STATUS_CANT_CONNECT_PROXY:
				canned_response = "Can't connect to proxy (check your proxy configuration; check also your network connection)";
				break;
			case SOUP_STATUS_SSL_FAILED:
				canned_response = "SSL Failed (establishing a secure connection to the server failed; check your server's certificates by accessing the service from midori)";
				break;
			case SOUP_STATUS_IO_ERROR:
				canned_response = "I/O Error (did you just unplug the network ?)";
				break;
			case SOUP_STATUS_NOT_FOUND:
				canned_response = "404 (maybe you didn't include the full name to the service?)";
				break;
			case SOUP_STATUS_PROXY_AUTHENTICATION_REQUIRED:
				canned_response = "Proxy authentication failed (check your proxy configuration)";
				break;
			default:
				canned_response = NULL;
		}
		if(canned_response == NULL)
		{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
				"error in user_exists: server replied with %i",msg->status_code);
		}
		else
		{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
				"error in user_exists: %s",canned_response);
		}
		res = FALSE;
	}
	
	g_object_unref(msg);
	g_string_free(url,TRUE);
	
	return res;
}

gboolean register_user(SYNC_CTX* s_ctx, const char* server_url, const char* email, const char* password, GError** err)
{
	GString* url;
	GString* body;
	const char *name;
	SoupMessage *msg;
	const char* username;
	char* captcha_challenge;
	char* captcha_response;
	gboolean res;
	GError* tmp_error;
	
	tmp_error = NULL;

	username = encode_sha1_base32(email);

	if(user_exists(s_ctx, username, server_url, &tmp_error)){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_USER,
			"user %s exists already",email);
		res = false;
		g_free((gpointer)username);
		username = NULL;
	}else{
		
		url = g_string_new(server_url);
		g_string_append(url, "/misc/1.0/captcha_html");
		
		msg = soup_message_new (SOUP_METHOD_GET, url->str);
		
		soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
		
		soup_session_send_message (s_ctx->session, msg);
		
		name = soup_message_get_uri (msg)->path;
		
		if (msg->status_code == SOUP_STATUS_OK) {
			
			g_set_error(err,MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
				"captcha required !\n");
			res = FALSE;
			//todo
			captcha_challenge = "TODO";
			captcha_response = "";
			
		} else if (msg->status_code == SOUP_STATUS_NOT_FOUND) {
			
			g_debug("no captcha required");
			captcha_challenge = "";
			captcha_response = "";
			res = TRUE;
			
		} else {
			
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
				"error in register_user (getting captcha): server replied with %i",msg->status_code);
			log_error_if_verbose("in register_user (getting captcha)",name, msg);
			
			res = FALSE;
		}
		
		g_string_free(url,TRUE);
		g_object_unref(msg);
		
		if(!res){
			g_free((gpointer)username);
			username = NULL;
			return FALSE;
		}
		
		
		
		url = g_string_new(server_url);
		g_string_append_printf(url, "/user/1.0/%s", username);

		g_free((gpointer)username);
		username = NULL;
		
		msg = soup_message_new (SOUP_METHOD_PUT, url->str);
		
		soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
		
		body = g_string_new_len("",0);
		
		g_string_sprintf(body,"{\"password\": \"%s\""
			",\"email\": \"%s\""
			",\"captcha-challenge\": \"%s\""
			",\"captcha-response\": \"%s\"}",
			password, email, captcha_challenge, captcha_response
			);
		soup_message_set_request(msg, "text/plain", SOUP_MEMORY_TEMPORARY, body->str, body->len);
		
		soup_session_send_message (s_ctx->session, msg);
		
		g_string_free(body, TRUE);
		
		name = soup_message_get_uri (msg)->path;
		
		if (SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
			
			printf("SUCCESS: user %s created !\n",msg->response_body->data);
			res = TRUE;
			
		} else if (msg->status_code == SOUP_STATUS_BAD_REQUEST) {
			
			if(!g_strcmp0("4",msg->response_body->data)){
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_USER,
					"user %s exists already (just appeared)",email);
				
			}else if(!g_strcmp0("6", msg->response_body->data)){
				log_error_if_verbose("error creating user: Json parse failure", name, msg);
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
					"error creating user %s: server responded Json parse failure", email);
			}else if(!g_strcmp0("12", msg->response_body->data)){
				log_error_if_verbose("error creating user: No email address on file", name, msg);
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
					"error creating user %s: server responded No email address on file", email);
			}else if(!g_strcmp0("7", msg->response_body->data)){
				log_error_if_verbose("error creating user: Missing password field", name, msg);
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
					"error creating user %s: server responded Missing password field", email);
			}else if(!g_strcmp0("9", msg->response_body->data)){
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_PASSWORD,
					"error creating user %s: server responded Requested password not strong enough", email);
			}else if(!g_strcmp0("2", msg->response_body->data)){
				log_error_if_verbose("error creating user: Incorrect or missing captcha", name, msg);
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_CAPTCHA,
					"error creating user %s: server responded Incorrect or missing captcha", email);
			}else {
				log_error_if_verbose("error creating user: unknown error code",name, msg);
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
					"error creating user: unknown error code: %s",msg->response_body->data);
			}
			res = FALSE;
		} else {
			
			log_error_if_verbose("in register_user (putting)",name, msg);
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
				"error creating user: server replied with %i",msg->status_code);
			
			res = FALSE;
		}
		
		g_string_free(url,TRUE);
		g_object_unref(msg);
		
	}
	return res;
}

JSObjectRef list_collections(SYNC_CTX* s_ctx, time_t if_modified_since, GError** err){
	return list_collections_int(s_ctx, s_ctx->enc_user, s_ctx->end_point, if_modified_since, err);
}


JSObjectRef list_collections_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, time_t if_modified_since, GError** err)
{
	GString* url;
	const char *name;
	SoupMessage *msg;
	JSObjectRef res;
	GError* tmp_error = NULL;
	SoupMessageHeaders* headers;
	
	url = g_string_new(end_point);
	g_string_append_printf(url, "1.1/%s/info/collections",username);
	
	
	msg = soup_message_new (SOUP_METHOD_GET, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	if(if_modified_since > 0){
		headers = msg->request_headers;
		SoupDate* modsince = soup_date_new_from_time_t (if_modified_since);
		char* h = soup_date_to_string (modsince, SOUP_DATE_HTTP);
		soup_message_headers_replace(headers, "If-Modified-Since", h);
		soup_date_free(modsince);
		g_free(h);
	}
	
	soup_session_send_message (s_ctx->session, msg);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		
		res = js_from_json(s_ctx->ctx, msg->response_body->data, msg->response_body->length, &tmp_error);

		if(res == NULL){
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"error listing collections: can't parse server response as JSON (%s)", tmp_error->message);
			g_clear_error(&tmp_error);
		}
	} else if(msg->status_code == SOUP_STATUS_NOT_MODIFIED){

		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_NOT_MODIFIED,
			"collections have not been modified since (%lu)", if_modified_since);

		res = NULL;
		
	} else if(msg->status_code == SOUP_STATUS_NOT_FOUND){
		
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
			"error listing collections: collections not found");
		res = NULL;
		
	} else {
		log_error_if_verbose("in list_collections",name, msg);
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error listing collections: server replied with %i",msg->status_code);
		res = NULL;
	}
	
	g_object_unref(msg);
	g_string_free(url,TRUE);
	
	return res;
}

gboolean create_collection(SYNC_CTX* s_ctx, const char* collection, GError** err){
	return create_collection_int(s_ctx, s_ctx->enc_user, s_ctx->end_point, collection, err);
}

// todo: check that collection names are valid
//   	"Collection names may only contain alphanumeric characters, period, underscore and hyphen."
gboolean create_collection_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, GError** err){
	GString* url;
	const char *name;
	SoupMessage *msg;
	gboolean res;
	GString* body;
	GHashTable* content;
	const char* method;          
	
	url = g_string_new(end_point);
	g_string_append_printf(url, "1.1/%s/storage/%s",username,collection);
	
	method = SOUP_METHOD_POST;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	body = g_string_new_len("",0);
	
	content = g_hash_table_new(g_str_hash,g_str_equal);
	g_hash_table_insert(content,(gpointer)"id",g_variant_new_string("000000000001"));
	g_hash_table_insert(content,(gpointer)"sortindex",g_variant_new_int32(0));
	g_hash_table_insert(content,(gpointer)"payload",g_variant_new_string("hello"));
	
	//	printf("[%s]\n",to_json(ctx, content));
	
	g_string_sprintf(body,"[{\"id\": \"000000000001\", \"sortindex\": 0, \"payload\": \"hello\" }]");
	soup_message_set_request(msg, "text/plain", SOUP_MEMORY_TEMPORARY, body->str, body->len);
	
	soup_session_send_message (s_ctx->session, msg);
	
	g_string_free(body, TRUE);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		res = TRUE;
		
	} else {
		log_error_if_verbose("in create_collection",name, msg);
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error creating collection %s: server replied with %i",collection, msg->status_code);
		res = false;
	}
	
	g_string_free(url,TRUE);
	g_object_unref(msg);
	
	return res;	
}


gboolean delete_collection(SYNC_CTX* s_ctx, const char* collection, GError** err){
	return delete_collection_int(s_ctx, s_ctx->enc_user, s_ctx->end_point, collection, err);
}

gboolean delete_collection_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, GError** err){
	GString* url;
	const char *name;
	SoupMessage *msg;
	gboolean res;
	const char* method;          
	
	url = g_string_new(end_point);
	g_string_append_printf(url, "1.1/%s/storage/%s",username,collection);
	
	method = SOUP_METHOD_DELETE;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	soup_session_send_message (s_ctx->session, msg);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		res = TRUE;
		
	} else {
		log_error_if_verbose("in delete_collection",name, msg);
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error deleting collection %s: server replied with %i",collection, msg->status_code);
		res = false;
	}
	
	g_string_free(url,TRUE);
	g_object_unref(msg);
	
	return res;	
}

gboolean add_wbos(SYNC_CTX* s_ctx, const char* collection, WBO* wbos, int cnt, GError** err){
	return add_wbos_int(s_ctx, s_ctx->enc_user, s_ctx->end_point, collection, wbos, cnt, err);
}

// todo: check that collection names are valid
//   	"Collection names may only contain alphanumeric characters, period, underscore and hyphen."
gboolean add_wbos_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, WBO* wbos, int cnt, GError** err){
	GString* url;
	const char *name;
	SoupMessage *msg;
	gboolean res;
	GString* body;
	const gchar* tmp;
	const char* method;    
	int i;
	
	url = g_string_new(end_point);
	g_string_append_printf(url, "1.1/%s/storage/%s",username,collection);
	
	method = SOUP_METHOD_POST;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	body = g_string_sized_new(cnt*500);
	g_string_append(body,"[");
	
	for(i=0;i<cnt;i++){
		WBO* wbo = &(wbos[i]);
		tmp = wbo_to_json(s_ctx, wbo, err);
		if(tmp == NULL){
			return FALSE;
		}
		g_string_append(body, tmp);
		if(i < cnt-1){
			g_string_append(body, ",");
		}
		g_free((gpointer)tmp);
	}
	g_string_append(body,"]");

	soup_message_set_request(msg, "text/plain", SOUP_MEMORY_TEMPORARY, body->str, body->len);
	
	soup_session_send_message (s_ctx->session, msg);
	
	g_string_free(body, TRUE);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		
		JSObjectRef oref;
		GError* tmp_error;
		tmp_error = NULL;
		oref = js_from_json(s_ctx->ctx, msg->response_body->data, msg->response_body->length, &tmp_error);
		if(tmp_error == NULL) {
			
			oref = get_object_prop(s_ctx->ctx, oref, "failed", &tmp_error);
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err, tmp_error, "Error reading response from server:");
				res = FALSE;
			}else{
				GPtrArray* failed = get_prop_names(s_ctx->ctx, oref);
				if(failed->len == 0){
					res = TRUE;
				}else{
					res = FALSE;
					GString* err_msg = g_string_new("Server couldn't save all records: ");

					int i;
					gpointer key;
					GPtrArray* value;
					for(i=0;i<failed->len;i++)
					{
						key = g_ptr_array_index(failed, i),
						value = get_string_array_prop(s_ctx->ctx, oref, key, &tmp_error);
						g_assert(value != NULL);
						int i;
						g_string_append_printf(err_msg, "couldn't save %s: ",(gchar*)key);
						for(i=0;i<value->len;i++){
							g_string_append(err_msg, g_ptr_array_index(value, i));
							if(i < value->len - 1){
								g_string_append(err_msg,", ");
							}
						}
						g_string_append(err_msg, "\n");
					}
					g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED, err_msg->str);
					
					g_string_free(err_msg, TRUE);
				}
				g_ptr_array_unref(failed);
			}
		}else{
			g_propagate_prefixed_error(err, tmp_error, "Error reading response from server:");
			res = FALSE;
		}
		
	} else {
		log_error_if_verbose("in create_collection",name, msg);
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error creating collection %s: server replied with %i",collection, msg->status_code);
		res = FALSE;
	}
	
	g_string_free(url,TRUE);
	g_object_unref(msg);
	
	return res;	
}

WBO* get_record(SYNC_CTX* s_ctx, const char* collection, const char* id, GError** err){
	return get_record_int(s_ctx, s_ctx->enc_user, s_ctx->end_point, collection, id, err);
}

WBO* get_record_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, const char* id, GError** err){
	GString* url;
	const char *name;
	SoupMessage *msg;
	WBO* res;
	const char* method;
	GError* tmp_error = NULL;
	
	url = g_string_new(end_point);
	g_string_append_printf(url, "1.1/%s/storage/%s/%s",username,collection,id);
	
	method = SOUP_METHOD_GET;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	soup_session_send_message (s_ctx->session, msg);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		JSObjectRef oref = js_from_json(s_ctx->ctx, msg->response_body->data, msg->response_body->length, &tmp_error);
		if(tmp_error != NULL) {
			g_propagate_error(err, tmp_error);
			return NULL;
		}
		res = wbo_from_js(s_ctx, oref, 0, err);
	} else {
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error in get_collection (fetching wbo %s/%s): server replied with %i",collection, id, msg->status_code);
		log_error_if_verbose("in get_record",name, msg);
		res = NULL;
	}
	
	g_object_unref(msg);
	g_string_free(url,TRUE);
	
	return res;
}


GPtrArray* get_collection(SYNC_CTX* s_ctx, const char* collection, GError** err){
	return get_collection_int(s_ctx, s_ctx->enc_user, s_ctx->end_point, collection, err);
}


GPtrArray* get_collection_int(SYNC_CTX* s_ctx, const char* username, const char* end_point, const char* collection, GError** err){
	GString* url;
	const char *name;
	SoupMessage *msg;
	GPtrArray* res;
	const char* method;
	
	url = g_string_new(end_point);
	g_string_append_printf(url, "1.1/%s/storage/%s?full=true",username,collection);
	
	method = SOUP_METHOD_GET;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	soup_session_send_message (s_ctx->session, msg);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		res = wbo_list_from_json(s_ctx, msg->response_body->data, msg->response_body->length, err);
	} else {
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error in get_collection (fetching collection %s): server replied with %i",collection, msg->status_code);
		log_error_if_verbose("in get_collection",name, msg);
		res = NULL;
	}
	
	g_object_unref(msg);
	g_string_free(url,TRUE);
	
	return res;
}


static void authenticate_cb(SoupSession *session,
	SoupMessage *msg,
	SoupAuth    *auth,
	gboolean     retrying,
	gpointer user_data)
{
	const char** user_pass = (const char**) user_data;
	const char* user = user_pass[0];
	const char* pass = user_pass[1];
	
	//g_warning("authenticate cb %i %s %s", retrying, user, pass);
	if(retrying){
	 	g_warning("wrong username,password : %s,%s\n",user,pass);
	}else{
	 	soup_auth_authenticate(auth, user, pass);
	}
}


gboolean set_user(SYNC_CTX* s_ctx,  const char* server_url, const char* user, const char* pass, GError** err){
	const char* enc_user = encode_sha1_base32(user);
	const char** user_pass;
	const char* end_point;
	GString* url;
	const char *name;
	SoupMessage *msg;
	gboolean ret;
	GError* tmp;
	
	tmp = NULL;
	// verify user exists
	if(!user_exists(s_ctx, enc_user, server_url, &tmp)){
		if(tmp != NULL){
			g_propagate_prefixed_error(err,tmp, "can't set user: ");
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_USER, "can't set user: user %s doesn't exist on server %s",user, server_url);
		}
		return FALSE;
	}
	
	// verify end point
	end_point = get_sync_node(s_ctx, enc_user, server_url, &tmp);
	
	if(end_point == NULL){
		g_propagate_prefixed_error(err, tmp, "can't set user: no sync server for user %s on server %s (user does exist, though) ",user, server_url);
		return FALSE;
	}
	
	// get collections
	user_pass = malloc(2* sizeof(char*));
	user_pass[0] = enc_user;
	user_pass[1] = pass;
	
	g_signal_connect (s_ctx->session, "authenticate",
		G_CALLBACK (authenticate_cb), user_pass);
	
	
	
	url = g_string_new(end_point);
	g_string_append_printf(url, "1.1/%s/info/collections",enc_user);
	
	msg = soup_message_new (SOUP_METHOD_GET, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	soup_session_send_message (s_ctx->session, msg);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		
		s_ctx->enc_user = enc_user;
		s_ctx->end_point = end_point;
		s_ctx->user_pass = user_pass;
		ret = TRUE;
		
	} else if(msg->status_code == SOUP_STATUS_NOT_FOUND){
		
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_USER,
			"can't set user: user %s doesn't exist",user);
		ret = FALSE;
		
	} else if(msg->status_code == SOUP_STATUS_UNAUTHORIZED){
		
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_USER,
			"can't set user: wrong password for user %s: %s",user, pass);
		ret = FALSE;
	} else {
		log_error_if_verbose("can't set user: unknown error",name, msg);
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error in set_user %s: server replied with %i",user, msg->status_code);
		ret = FALSE;
	}

	g_object_unref(msg);
	g_string_free(url,TRUE);
	
	return ret;
}


unsigned char** regen_key_hmac(const char* username, gsize username_len, const char* user_key, GError** err){
	GRegex* valid;
	char k[27];
	guchar* dst;
	guchar* info;
	HMAC_CTX hctx;
	int retcode;
	unsigned char* key;
	unsigned char* hmac;
	unsigned int key_len;
	gsize info_len;
	unsigned char** ret = NULL;
	
	valid = g_regex_new("[A-K8MN9P-Z2-7](-([A-K8MN9P-Z2-7]{5})){5}", G_REGEX_CASELESS, 0, NULL);
	
	if(g_regex_match( valid,user_key,0,NULL )){
		k[0] = user_key[0];
		int i;
		for(i=0;i<5;i++){
			strncpy(k+1+i*5,user_key+2+i*6,5);
		}
		k[26] = '\0';
		//printf("got key %s from %s\n",k,user_key);
		dst = (guchar*)g_strnfill(16,'\0');
		from_user_friendly_base32(k,26);
		//printf("friendly k is:%s\n", k);
		g_assert(16 == base32_decode((guint8*)dst, 16, k, 26));
		
		// construit encryption et HMAC
		// "Sync-AES_256_CBC-HMAC256" + username + 1
		info_len = 24 + username_len + 1;
		info = g_malloc(info_len);
		strcpy((char*)info,"Sync-AES_256_CBC-HMAC256");
		strcpy((char*)info+24,username);
		info[24 + username_len] = '\1';
		
		// printf("user(%i)=%s\n",(int)strlen(username), username);
		// printf("input to digest=%s\n",g_base64_encode(info, info_len ));
		// printf("prk is:%s\n", g_base64_encode(dst,16));
		
		HMAC_CTX_init(&hctx);
		
		retcode = HMAC_Init_ex(&hctx, dst, 16, EVP_sha256(), NULL);
		g_assert(1 == retcode);// 1 = success, 0 = error
		
		g_assert(1 == HMAC_Update(&hctx, (unsigned char*)info, info_len));
		
		key = g_malloc(32);
		
		g_assert(1 == HMAC_Final(&hctx, key, &key_len));
		g_assert(32 == key_len);
		
		// printf("key is:%s\n", g_base64_encode(key,32));
		
		g_free(info);
		
		// key + "Sync-AES_256_CBC-HMAC256" + username + 1
		info_len = 32 + 24 + username_len + 1;
		info = g_malloc( info_len );
		memcpy(info, key, 32);
		strcpy(((char*)info)+32,"Sync-AES_256_CBC-HMAC256");
		strcpy(((char*)info)+32+24,username);
		info[32 + 24 + username_len] = '\2';
		
		// printf("input to digest=%s\n",g_base64_encode(info,info_len ));
		g_assert(1 == HMAC_Init_ex(&hctx, dst, 16, EVP_sha256(), NULL));
		
		g_assert(1 == HMAC_Update(&hctx, (unsigned char*)info, info_len));
		
		hmac = g_malloc(32);
		
		g_assert(1 == HMAC_Final(&hctx, hmac, &key_len));
		g_assert(32 == key_len);
		
		// printf("hmac is:%s\n", g_base64_encode(hmac,32));
		
		g_free(dst);
		g_free(info);
		ret = g_malloc(sizeof(gpointer) * 2);
		ret[0] = key;
		ret[1] = hmac;
		
		HMAC_CTX_cleanup(&hctx);
		
	}else{
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_WRONG_PASSWORD,
			"invalid key %s (must be sthing like s-fffff-99999-11111-11111-aaaaa)",user_key);
	}
	
	g_regex_unref(valid);
	return ret;
}

guchar* randomBytes(gsize count, GError** err){
	guchar* buf = g_malloc(count);
	int success = RAND_bytes(buf, count);
	if(success == -1){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
			"Generating random bytes seems to be unsupported (errcode=%lu)", ERR_get_error());
		g_free(buf);
		return NULL;
	}else if(success == 0){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
			"Not enough entropy to generate %lu random bytes (errcode=%lu)", count, ERR_get_error());
		g_free(buf);
		return NULL;
	}else{
		return buf;
	}
}

unsigned char** generate_bulk_key(GError** err){
	guchar** ret = g_malloc(sizeof(gpointer) * 2);
	ret[0] = randomBytes(32,err);
	if(ret[0] == NULL){
		g_free(ret);
		return NULL;
	}
	ret[1] = randomBytes(32,err);
	if(ret[1] == NULL){
		g_free(ret[0]);
		g_free(ret);
		return NULL;
	}
	
	return ret;
}

WBO* get_wbo_by_id(GPtrArray* coll, const char* id){
	int i;
	WBO* w;
	for(i=0;i < coll->len; i++){
		w = g_ptr_array_index(coll, i);
		if(g_strcmp0(w->id, id) == 0){
			return w;
		}
	}
	return NULL;
}

SyncItem* get_sync_item_by_id(GPtrArray* coll, const char* id){
	int i;
	SyncItem* w;
	for(i=0;i < coll->len; i++){
		w = g_ptr_array_index(coll, i);
		if(g_strcmp0(w->id, id) == 0){
			return w;
		}
	}
	return NULL;
}

char* decrypt_wbo(SYNC_CTX* s_ctx, const char* collection, WBO* w, GError** err){
	//todo: non default keys
	return decrypt_wbo_int(s_ctx, s_ctx->bulk_key, s_ctx->bulk_hmac, w, err);
}


char* decrypt_wbo_int(SYNC_CTX* s_ctx, const guchar* key, const guchar* hmac_key, const WBO* w, GError** err){
	HMAC_CTX hctx;
	const char* ciphertext64;
	const char* iv64;
	const char* hmachex;
	GError* tmp_error = NULL;
	
	JSObjectRef payload = js_from_json(s_ctx->ctx, w->payload, strlen(w->payload), &tmp_error);
	if(payload == NULL){
		if(tmp_error != NULL){
			g_set_error(err,
				MY_SYNC_ERROR,
				MY_SYNC_ERROR_FORMAT,
				"couldn't decode WBO json payload for %s (%s); cause is %s",w->id, w->payload, tmp_error->message);
			g_clear_error(&tmp_error);
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"couldn't decode WBO json payload for %s (%s)",w->id, w->payload);
		}
		return NULL;
	}else{
		
		ciphertext64 = get_string_prop(s_ctx->ctx, payload, "ciphertext", &tmp_error);
		if(ciphertext64 == NULL){
			if(tmp_error != NULL){
				g_set_error(err,
					MY_SYNC_ERROR,
					MY_SYNC_ERROR_FORMAT,
					"no ciphertext for %s in %s; cause is %s",w->id, w->payload, tmp_error->message);
				g_clear_error(&tmp_error);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"no ciphertext for %s in %s",w->id, w->payload);
			}
			return NULL;
		}
		iv64 = get_string_prop(s_ctx->ctx, payload, "IV", &tmp_error);
		if(iv64 == NULL){
			if(tmp_error != NULL){
				g_set_error(err,
					MY_SYNC_ERROR,
					MY_SYNC_ERROR_FORMAT,
					"no IV for %s in %s; cause is %s",w->id, w->payload, tmp_error->message);
				g_clear_error(&tmp_error);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"no IV for %s in %s",w->id, w->payload);
			}
			return NULL;
		}
		hmachex = get_string_prop(s_ctx->ctx, payload, "hmac", &tmp_error);
		if(hmachex == NULL){
			if(tmp_error != NULL){
				g_set_error(err,
					MY_SYNC_ERROR,
					MY_SYNC_ERROR_FORMAT,
					"no hmac for %s in %s; cause is %s",w->id, w->payload, tmp_error->message);
				g_clear_error(&tmp_error);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"no hmac for %s in %s",w->id, w->payload);
			}
			return NULL;
		}
		
		guchar* ciphertext;
		gsize ciphertext_len;
		guchar* iv;
		gsize iv_len;
		guchar* hmac;
		gsize hmac_len;
		guchar* comp_hmac;
		unsigned int comp_hmac_len;
		char* res;
		
		// printf("key is:%s\n", g_base64_encode(key,32));
		// printf("hmac is:%s\n", g_base64_encode(hmac_key,32));
		
		ciphertext = g_base64_decode(ciphertext64, &ciphertext_len);
		// printf("decoded %lu bytes of cipher from %lu (%s)\n", ciphertext_len, strlen(ciphertext64), ciphertext64);
		
		iv = g_base64_decode(iv64, &iv_len);
		// printf("decoded %lu bytes of IV from %lu (%s)\n", iv_len, strlen(iv64), iv64);
		g_free((gpointer)iv64);
		
		hmac = hex_decode(hmachex, &hmac_len);
		// printf("decoded %lu bytes of hmac from %lu (%s)\n", hmac_len, strlen(hmachex), hmachex);
		
		// hmac
		HMAC_CTX_init(&hctx);
		
		g_assert(1 == HMAC_Init_ex(&hctx, hmac_key, 32, EVP_sha256(), NULL));
		
		g_assert(1 == HMAC_Update(&hctx, (guchar*)ciphertext64, strlen(ciphertext64)));
		
		comp_hmac = g_malloc(32);
		
		g_assert(1 == HMAC_Final(&hctx, comp_hmac, &comp_hmac_len));
		g_assert(32 == comp_hmac_len);
		
		HMAC_CTX_cleanup(&hctx);
		
		// printf("got comp. hmac=%s\n", hex_encode(comp_hmac, comp_hmac_len));
		// printf("got ..... hmac=%s\n", hex_encode(hmac, hmac_len));
		
		if(memcmp(hmac, comp_hmac, hmac_len)){
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"hmac mismatch for %s; corrupt server record or wrong hmac key", w->id);
			g_free(comp_hmac);
			return NULL;
		}
		g_free(comp_hmac);
		comp_hmac = NULL;
		
		// decrypt
		
		
		EVP_CIPHER_CTX hctx = {0};
		EVP_CIPHER_CTX_init(&hctx);
		
		// AES_256_CBC --> CKM_AES_CBC --> CKM_AES_CBC_PAD
		
		g_assert(1 == EVP_DecryptInit_ex(&hctx, EVP_aes_256_cbc(), NULL, key, iv));
		
		guchar* kout = g_malloc0((ciphertext_len+EVP_CIPHER_CTX_block_size(&hctx))*sizeof(unsigned char));
		int kout_len = 0;
		int kout_rem = 0;


		g_assert(1 == EVP_DecryptUpdate(&hctx, kout, &kout_len, ciphertext, ciphertext_len));

		// const gchar *p;
		
		// g_assert (kout_len >= 0);

		// int i;
		// for (i=0, p = (char*)kout; ((p - (char*)kout) < kout_len) && *p; i++, p++)
		// {
		// 	printf("%i\n",i);
		// }
		// exit(-1);
			
		// printf("out deco=%i\n", kout_len);
		g_assert(1 == EVP_DecryptFinal_ex(&hctx, kout+kout_len, &kout_rem));
		// printf("out final deco=%i %s\n", kout_len+kout_rem, kout);
		EVP_CIPHER_CTX_cleanup(&hctx);
		
		kout_len += kout_rem;
		
		kout[kout_len] = '\0';

		// if(kout[0] == '\0'){
		// 	printf("valgrind complains!");
		// }
		
		if(g_utf8_validate((char*) kout, kout_len, NULL)){
			res = g_strndup((char*)kout, kout_len);
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"decoded payload for %s contains an invalid utf8 string", w->id);
			res = NULL;
		}
		
		g_free((gpointer)ciphertext64);
		g_free((gpointer)ciphertext);
		g_free(iv);
		g_free((gpointer)hmachex);
		g_free(hmac);
		g_free(kout);
		
		return res;
	}
}

const char* encrypt_wbo(SYNC_CTX* s_ctx, const char* collection, const WBO* w, GError** err){
	//todo: non default keys
	return encrypt_wbo_int(s_ctx, s_ctx->bulk_key, s_ctx->bulk_hmac, w, err);
}

const char* encrypt_wbo_int(SYNC_CTX* s_ctx, const guchar* key, const guchar* hmac_key, const WBO* w, GError** err){
	HMAC_CTX hctx;
	const char* ciphertext64;
	const guchar* iv;
	char* iv64;
	const char* hmachex;
	GError* tmp_error = NULL;
	const char* res;
	guchar* comp_hmac;
	unsigned int comp_hmac_len;
	
	g_assert(w->payload != NULL);
	gsize payload_len = strlen(w->payload);
	
	// encrypt
	iv = randomBytes(16, &tmp_error);
	if(iv == NULL){
		g_propagate_prefixed_error(err, tmp_error, "Can't encrypt: ");
		return NULL;
	}
	
	EVP_CIPHER_CTX cctx;
	EVP_CIPHER_CTX_init(&cctx);
	
	// AES_256_CBC --> CKM_AES_CBC --> CKM_AES_CBC_PAD
	
	g_assert(1 == EVP_EncryptInit_ex(&cctx, EVP_aes_256_cbc(), NULL, key, iv));
	
	guchar* ciphertext = malloc((payload_len+EVP_CIPHER_CTX_block_size(&cctx)-1));
	int ciphertext_len;
	int ciphertext_rem;
	
	g_assert(1 == EVP_EncryptUpdate(&cctx, ciphertext, &ciphertext_len, (guchar*)w->payload, payload_len));
	//printf("out enco=%i from %lu\n", ciphertext_len, payload_len);
	g_assert(1 == EVP_EncryptFinal_ex(&cctx, ciphertext+ciphertext_len, &ciphertext_rem));
	//printf("out final enco=%i\n", ciphertext_len+ciphertext_rem);
	EVP_CIPHER_CTX_cleanup(&cctx);
	
	ciphertext_len += ciphertext_rem;
	
	ciphertext64 = g_base64_encode(ciphertext, ciphertext_len);
	//printf("encoded %lu bytes of cipher from %i (%s)\n", strlen(ciphertext64), ciphertext_len, ciphertext64);
	
	iv64 = g_base64_encode(iv, 16);
	//printf("encoded %lu bytes of IV from %lu (%s)\n", 16L, strlen(iv64), iv64);
	
	
	// hmac
	HMAC_CTX_init(&hctx);
	
	g_assert(1 == HMAC_Init_ex(&hctx, hmac_key, 32, EVP_sha256(), NULL));
	
	g_assert(1 == HMAC_Update(&hctx, (guchar*)ciphertext64, strlen(ciphertext64)));
	
	comp_hmac = g_malloc(32);
	
	g_assert(1 == HMAC_Final(&hctx, comp_hmac, &comp_hmac_len));
	g_assert(32 == comp_hmac_len);
	
	HMAC_CTX_cleanup(&hctx);
	
	hmachex = hex_encode(comp_hmac, comp_hmac_len);

	//printf("got comp. hmac=%s\n", hmachex);
	
	
	JSObjectRef payload = js_create_empty_object(s_ctx->ctx);
	
	
	set_string_prop(s_ctx->ctx, payload, "ciphertext", ciphertext64, &tmp_error);
	set_string_prop(s_ctx->ctx, payload, "IV", iv64, &tmp_error);
	set_string_prop(s_ctx->ctx, payload, "hmac", hmachex, &tmp_error);
	

	res = js_to_json(s_ctx->ctx, payload);
	
	return res;
}


void init_sync(SYNC_CTX* s_ctx){
	s_ctx->ctx = JSGlobalContextCreate(NULL);	
	s_ctx->session = soup_session_sync_new (
		#ifdef HAVE_LIBSOUP_GNOME
		SOUP_SESSION_ADD_FEATURE_BY_TYPE, SOUP_TYPE_PROXY_RESOLVER_GNOME,
		#endif
		);
	if (debug) {
	    SoupLogger *logger;
	    
	    logger = soup_logger_new (SOUP_LOGGER_LOG_BODY, -1);
	    soup_session_add_feature (s_ctx->session, SOUP_SESSION_FEATURE (logger));
	    g_object_unref (logger);
	}
}

void free_sync(SYNC_CTX* s_ctx){
	if(s_ctx->ctx != NULL){
		JSGlobalContextRelease(s_ctx->ctx);
	}
	if(s_ctx->session != NULL){
		soup_session_abort (s_ctx->session);
		g_object_unref(s_ctx->session);
		s_ctx->session = NULL;
	}
	if(s_ctx->end_point != NULL){
		g_free((gpointer)s_ctx->end_point);
		s_ctx->end_point = NULL;
	}
	if(s_ctx->enc_user != NULL){
		g_free((gpointer)s_ctx->enc_user);
		s_ctx->enc_user = NULL;
	}
	if(s_ctx->master_key != NULL){
		g_free((gpointer)s_ctx->master_key);
		s_ctx->master_key = NULL;
	}
	if(s_ctx->master_hmac != NULL){
		g_free((gpointer)s_ctx->master_hmac);
		s_ctx->master_hmac = NULL;
	}
	if(s_ctx->bulk_key != NULL){
		g_free((gpointer)s_ctx->bulk_key);
		s_ctx->bulk_key = NULL;
	}
	if(s_ctx->bulk_hmac != NULL){
		g_free((gpointer)s_ctx->bulk_hmac);
		s_ctx->bulk_hmac = NULL;
	}
	if(s_ctx->user_pass != NULL){
		g_free((gpointer)s_ctx->user_pass);
		s_ctx->user_pass = NULL;
	}
}


gboolean refresh_bulk_keys(SYNC_CTX* ctx, GError** err) {
	GPtrArray* crypto;
	WBO* keys;
	GError* tmp_error = NULL;
	
	g_assert(ctx->master_key != NULL);
	g_assert(ctx->master_hmac != NULL);
	
	// crypto/keys == {default:
	crypto = get_collection(ctx, "crypto", &tmp_error);
	if(crypto == NULL){
		g_propagate_prefixed_error(err, tmp_error,
			"couldn't fetch bulk keys from server: ");
		return FALSE;
	}
	
	keys = get_wbo_by_id(crypto, "keys");
	if(keys == NULL){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
			"no bulk keys on server");
		g_ptr_array_unref(crypto);
		return FALSE;
	}
	gboolean ret = refresh_bulk_keys_from_wbo(ctx, keys, err);
	g_ptr_array_unref(crypto);
	return ret;
}
	
gboolean refresh_bulk_keys_from_wbo(SYNC_CTX* ctx, WBO* cryptokeys, GError** err) {
	guchar** deckeys;
	deckeys = decrypt_bulk_keys(ctx, ctx->master_key, ctx->master_hmac, cryptokeys, err);
	if(deckeys == NULL){
		ctx->bulk_key = NULL;
		ctx->bulk_hmac = NULL;
		return FALSE;
	}
	ctx->bulk_key = deckeys[0];
	ctx->bulk_hmac = deckeys[1];
	g_free(deckeys);
	
	return TRUE;
}

guchar** decrypt_bulk_keys(SYNC_CTX* ctx, const guchar* master_key, const guchar* master_hmac, WBO* keys, GError** err) {
	GError* tmp_error = NULL;
	
	// decrypt payload
	gchar* deckeys = decrypt_wbo_int(ctx, ctx->master_key, ctx->master_hmac, keys, &tmp_error);
	
	if(deckeys == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"can't decrypt bulk keys: wrong master key or corrupt server records: ");
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
				"can't decrypt bulk keys: wrong master key or corrupt server records");
		}
		return NULL;
	}
	
	// read content
	JSObjectRef bulkKeysObj = js_from_json(ctx->ctx, deckeys, strlen(deckeys), &tmp_error);
	g_free(deckeys);
	if(bulkKeysObj == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"bulk keys content is not JSON (content:%s):", deckeys);
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"bulk keys content is not JSON (content:%s)", deckeys);
		}
		return NULL;
	}
	
	GPtrArray* bulk_key_hmac64 = get_string_array_prop(ctx->ctx, bulkKeysObj, "default", &tmp_error);
	if(bulk_key_hmac64 == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"can't decrypt bulk keys: wrong master key or corrupt server records");
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"expected a key,hmac pair dictionary in a defaults property, but it's not there");
		}
		return NULL;
	} else if(bulk_key_hmac64->len != 2){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
			"expected a key,hmac pair dictionary in a defaults property, but it's not of size 2 but %i", bulk_key_hmac64->len);
		g_ptr_array_unref(bulk_key_hmac64);
		return NULL;
	} else {
		guchar** ret = g_malloc(sizeof(gpointer)*2);
		gsize k_len;
		ret[0] = (guchar*) g_base64_decode(g_ptr_array_index(bulk_key_hmac64,0), &k_len);
		if(k_len != 32){
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"invalid bulk key size (expecting 32): %lu",k_len);
			g_free(ret[0]);
			g_free(ret);
			g_ptr_array_unref(bulk_key_hmac64);
			return NULL;
		} else {
			ret[1] = g_base64_decode(g_ptr_array_index(bulk_key_hmac64,1), &k_len);
			if(k_len != 32){
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"invalid hmac size (expecting 32): %lu",k_len);
				g_free((gpointer)ctx->bulk_hmac);
				g_free(ret[0]);
				g_free(ret[1]);
				g_free(ret);
				g_ptr_array_unref(bulk_key_hmac64);
				return NULL;
			} else {
				g_ptr_array_unref(bulk_key_hmac64);
				return ret;
			}
		}
	}
}

gboolean set_master_key(SYNC_CTX* s_ctx, const char* master_key, GError** err){
	GError* tmp_error = NULL;
	guchar** key_hmac;
	
	key_hmac = regen_key_hmac(s_ctx->enc_user, strlen(s_ctx->enc_user), master_key, err);
	
	if(key_hmac == NULL){
		s_ctx->master_key = NULL;
		s_ctx->master_hmac = NULL;
		return FALSE;
		
	}else{
		s_ctx->master_key = key_hmac[0];
		s_ctx->master_hmac = key_hmac[1];
		
		g_free(key_hmac);
		
		if(refresh_bulk_keys(s_ctx, &tmp_error)){
			return TRUE;
		}else{
			g_propagate_prefixed_error(err, tmp_error,
				"error setting master key: can't refresh bulk keys: ");
			s_ctx->master_key = NULL;
			s_ctx->master_hmac = NULL;
			return FALSE;
		}
	}
}
#define SUP_CNT 3
static const char* sup[SUP_CNT] = { "clients", "bookmarks", "history" };
static const int sup_v[SUP_CNT] = { 1, 2, 1 };


gboolean verify_storage(SYNC_CTX* s_ctx, GError** err){
	GString* url;
	const char *name;
	SoupMessage *msg;
	JSObjectRef res;
	const char* method;
	const char* payload;
	GError* tmp_error = NULL;
	
	url = g_string_new(s_ctx->end_point);
	g_string_append_printf(url, "1.1/%s/storage/meta/global",s_ctx->enc_user);
	
	method = SOUP_METHOD_GET;
	
	msg = soup_message_new (method, url->str);
	
	soup_message_set_flags (msg, SOUP_MESSAGE_NO_REDIRECT);
	
	soup_session_send_message (s_ctx->session, msg);
	
	name = soup_message_get_uri (msg)->path;
	
	if(SOUP_STATUS_IS_SUCCESSFUL(msg->status_code)) {
		res = js_from_json(s_ctx->ctx, msg->response_body->data, msg->response_body->length, &tmp_error);
		if(res == NULL){
			g_propagate_prefixed_error(err, tmp_error, "No meta/global item: ");
		}
	} else {
		log_error_if_verbose("in verify_storage",name, msg);
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_COMMUNICATION,
			"error in verify_storage (fetching meta/global): server replied with %i",msg->status_code);
		res = NULL;
	}
	
	g_object_unref(msg);
	g_string_free(url,TRUE);
	
	
	if(res == NULL){
		return FALSE;
	}
	
	payload = get_string_prop(s_ctx->ctx, res, "payload", &tmp_error);
	if(payload == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"error in verify_storage: no or wrong payload in meta/global: ");
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"error in verify_storage: no or wrong payload in meta/global");
		}
		return FALSE;
	}
	
	JSObjectRef global = js_from_json(s_ctx->ctx, payload, strlen(payload), 
		&tmp_error);
	
	g_free((gpointer)payload);
	payload = NULL;
	
	if(global == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"error in verify_storage: meta/global payload can't be interpreted as JSON: ");
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"error in verify_storage:  meta/global payload can't be interpreted as JSON");
		}                                                                        
		return FALSE;
	}
	
	int version = get_int_prop(s_ctx->ctx, global, "storageVersion", &tmp_error);
	
	if(tmp_error != NULL){
		g_propagate_prefixed_error(err,tmp_error,
			"error in verify_storage: invalid storageVersion in meta/global ");
		return FALSE;
	}
	
	if(version != SYNC_SUPPORTED_VERSION){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
			"storage version not supported: %i (%i supported)", version, SYNC_SUPPORTED_VERSION);
		return FALSE;
	}
	
	JSObjectRef engine;
	JSObjectRef engines = get_object_prop(s_ctx->ctx, global, "engines", &tmp_error);
	
	if(engines == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"error in verify_storage: meta/global engines is incorrect: ");
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"error in verify_storage: no meta/global engines property");
		}
		return FALSE;
	}
	
	int i;
	for(i = 0 ; i < SUP_CNT ; i++){
		engine = get_object_prop(s_ctx->ctx, engines, sup[i], &tmp_error);
		
		if(engine == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in verify_storage: meta/global/engines/%s is incorrect: ", sup[i]);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in verify_storage: meta/global/engines/%s is missing", sup[i]);
			}
			return FALSE;
		}
		
		version = get_int_prop(s_ctx->ctx, engine, "version", &tmp_error);
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"error in verify_storage: meta/global/engines/%s/version is incorrect: ", sup[i]);
			return FALSE;
		}
		
		
		if(version != sup_v[i]){
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
				"%s version not supported: %i (%i supported)", sup[i], version, sup_v[i]);
			return FALSE;
		}
	}
	
	return TRUE;
}

gboolean create_storage(SYNC_CTX* s_ctx, const char* master_key, const char* client_name, GError** err){
	if(!create_metaglobal(s_ctx, err)){
		return FALSE;
	}
	if(!create_cryptokeys(s_ctx, master_key, err)){
		return FALSE;
	}
	return create_client(s_ctx, client_name, "desktop", err);
}


gboolean create_metaglobal(SYNC_CTX* s_ctx, GError** err){
	GError* tmp_error = NULL;
	JSObjectRef global_payload;
	WBO* wbo = g_malloc(sizeof(WBO));
	wbo->id = g_strdup("global");
	const char* id;
	
	id = generate_id(&tmp_error);
	if(id == NULL){
		g_propagate_prefixed_error(err, tmp_error, "Unable to create meta/global record, unable to generate id: ");
		g_free(wbo);
		return FALSE;
	}
	
	global_payload = js_create_empty_object(s_ctx->ctx);
	g_assert(set_string_prop(s_ctx->ctx, global_payload, "syncId", id, err));// uniqueness is not necessary here: we're the first
	g_assert(set_int_prop(s_ctx->ctx, global_payload, "storageVersion", SYNC_SUPPORTED_VERSION, err));
	g_free((gpointer)id);
	
	JSObjectRef engines = js_create_empty_object(s_ctx->ctx);
	
	int i;
	id = generate_id(&tmp_error);// uniqueness is not necessary here: we're the first
	if(id == NULL){
		g_propagate_prefixed_error(err, tmp_error, "Unable to create meta/global record, unable to generate id: ");
		g_free(wbo);
		return FALSE;
	}
	for(i=0;i<SUP_CNT;i++){
		JSObjectRef engine = js_create_empty_object(s_ctx->ctx);
		g_assert (set_int_prop(s_ctx->ctx, engine, "version", sup_v[i], err));
		g_assert (set_string_prop(s_ctx->ctx, engine, "synId", id, err));
		g_assert (set_object_prop(s_ctx->ctx, engines, sup[i], engine, err));
	}
	g_free((gpointer)id);
	g_assert (set_object_prop(s_ctx->ctx, global_payload, "engines", engines, err));
	
	wbo->payload = js_to_json(s_ctx->ctx, global_payload);
	
	if(!add_wbos(s_ctx, "meta", wbo, 1, &tmp_error)){
		g_propagate_prefixed_error(err, tmp_error, "Unable to upload meta/global record: ");
		g_free(wbo);
		return FALSE;
	}
	g_free(wbo);
	return TRUE;
}

gboolean create_cryptokeys(SYNC_CTX* s_ctx, const char* master_key, GError** err){
	GError* tmp_error = NULL;
	JSObjectRef keys_payload;
	WBO* wbo = g_malloc(sizeof(WBO));
	wbo->id = g_strdup("keys");
	
	guchar** key_hmac;
	
	key_hmac = regen_key_hmac(s_ctx->enc_user, strlen(s_ctx->enc_user), master_key, err);
	
	if(key_hmac == NULL){
		s_ctx->master_key = NULL;
		s_ctx->master_hmac = NULL;
		return FALSE;
		
	}else{
		s_ctx->master_key = key_hmac[0];
		s_ctx->master_hmac = key_hmac[1];
		
		g_free(key_hmac);
	}

	keys_payload = js_create_empty_object(s_ctx->ctx);

	guchar** bulk_key_hmac;

	bulk_key_hmac = generate_bulk_key(err);
	if(bulk_key_hmac == NULL){
		s_ctx->bulk_key = NULL;
		s_ctx->bulk_hmac = NULL;
		return FALSE;
		
	}else{
		s_ctx->bulk_key = bulk_key_hmac[0];
		s_ctx->bulk_hmac = bulk_key_hmac[1];
		
		g_free(bulk_key_hmac);
	}
	
	GPtrArray* bulk = g_ptr_array_new_full(2, ((GDestroyNotify)(g_free)));
	const gchar* encoded = g_base64_encode(s_ctx->bulk_key, 32);
	g_ptr_array_add(bulk,(gpointer)encoded);
	encoded = g_base64_encode(s_ctx->bulk_hmac, 32);
	g_ptr_array_add(bulk,(gpointer)encoded);
	
	set_string_array_prop(s_ctx->ctx, keys_payload, "default", bulk, &tmp_error);
	if(tmp_error != NULL){
		g_propagate_error(err, tmp_error);
		wbo_free(wbo);
		return FALSE;
	}
	
	g_ptr_array_unref(bulk);
	
	JSObjectRef empty = js_create_empty_object(s_ctx->ctx);
	
	set_object_prop(s_ctx->ctx, keys_payload, "collections", empty, err);
	
	set_string_prop(s_ctx->ctx, keys_payload, "collection", "crypto", err);

	wbo->payload = js_to_json(s_ctx->ctx, keys_payload);
	
	encoded = encrypt_wbo_int(s_ctx, s_ctx->master_key, s_ctx->master_hmac, wbo, &tmp_error);
	if(encoded == NULL){
		g_assert(tmp_error!=NULL);
		g_propagate_error(err, tmp_error);
		wbo_free(wbo);
		return FALSE;
	}else{
		g_free((gpointer)wbo->payload);
		wbo->payload = encoded;
	}
	
	if(!add_wbos(s_ctx, "crypto", wbo, 1, &tmp_error)){
		g_propagate_prefixed_error(err, tmp_error, "Unable to upload crypto/keys record: ");
		wbo_free(wbo);
		return FALSE;
	}
	
	wbo_free(wbo);
	
	return TRUE;
}

gboolean create_client(SYNC_CTX* s_ctx, const char* client_name, const char* client_type, GError** err){
	GError* tmp_error = NULL;
	const char* encoded;
	JSObjectRef client_payload;
	WBO* wbo = g_malloc(sizeof(WBO));
	wbo->id = generate_id(&tmp_error);
	
	if(wbo->id == NULL){
		g_propagate_prefixed_error(err, tmp_error, "Unable to create client record, unable to generate id: ");
		g_free(wbo);
		return FALSE;
	}
	
	client_payload = js_create_empty_object(s_ctx->ctx);
                     
	if(!set_string_prop(s_ctx->ctx, client_payload, "name", client_name, &tmp_error)){
		g_assert(tmp_error!=NULL);
		g_propagate_prefixed_error(err, tmp_error, "Unable to create client record (invalid name '%s'?): ", client_name);
		wbo_free(wbo);
		return FALSE;
	}

	if(!set_string_prop(s_ctx->ctx, client_payload, "type", client_type, &tmp_error)){
		g_assert(tmp_error!=NULL);
		g_propagate_prefixed_error(err, tmp_error, "Unable to create client record (invalid type '%s'?): ", client_type);
		wbo_free(wbo);
		return FALSE;
	}
	
	GPtrArray* cmds = g_ptr_array_sized_new(0);
	if(!set_string_array_prop(s_ctx->ctx, client_payload, "commands", cmds, &tmp_error)){
		g_assert(tmp_error!=NULL);
		g_propagate_error(err, tmp_error);
		wbo_free(wbo);
		g_ptr_array_unref(cmds);
		return FALSE;
	}
	
	g_ptr_array_unref(cmds);
	
	wbo->payload = js_to_json(s_ctx->ctx, client_payload);
	
	encoded = encrypt_wbo(s_ctx, "clients", wbo, &tmp_error);
	if(encoded == NULL){
		g_assert(tmp_error!=NULL);
		g_propagate_error(err, tmp_error);
		wbo_free(wbo);
		return FALSE;
	}else{
		g_free((gpointer)wbo->payload);
		wbo->payload = encoded;
	}
	
	if(!add_wbos(s_ctx, "clients", wbo, 1, &tmp_error)){
		g_assert(tmp_error!=NULL);
		g_propagate_prefixed_error(err, tmp_error, "Unable to upload client record: ");
		wbo_free(wbo);
		return FALSE;
	}
	
	wbo_free(wbo);
	return TRUE;
}


SyncItem* get_bookmark_rec(SYNC_CTX* s_ctx, GPtrArray* bookmarks_wbo, WBO* itm, JSObjectRef wbo_json, GError** err){
	
	GError* tmp_error = NULL;
	//g_message("get_bookmark_rec(%s)",itm->id);
	g_assert(err == NULL || *err == NULL);
	
	const char* s = get_string_prop(s_ctx->ctx, wbo_json, "id", &tmp_error);
	if(s == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"error in get_bookmark_rec: invalid 'id' property in WBO %s:", itm->id);
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"error in get_bookmark_rec: missing 'id' property in WBO %s", itm->id);
		}
		return NULL;
	}
	
	if( strcmp(itm->id, s) ){
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
			"decrypted WBO has mismatched ids ('%s' and '%s')", itm->id, s);
		g_free((gpointer)s);
		return NULL;
	}
	
	SyncItem* si = g_malloc(sizeof(SyncItem));
	si->id = s;
	si->modified = itm->modified;
	
	s = get_string_prop(s_ctx->ctx, wbo_json, "type", &tmp_error);
	if(s == NULL){
		if(tmp_error != NULL){
			g_propagate_prefixed_error(err,tmp_error,
				"error in get_bookmark_rec: invalid 'type' property in WBO %s:", itm->id);
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				"error in get_bookmark_rec: missing 'type' property in WBO %s", itm->id);
		}
		return NULL;
	}


	if(! strcmp("folder", s) || ! strcmp("livemark", s) ){
		g_free((gpointer)s);

		si->type = SYNC_ITEM_FOLDER;
		si->actual = NULL;
		SYNC_FOLDER* folder = g_malloc(sizeof(SYNC_FOLDER));
		
		folder->title = get_string_prop(s_ctx->ctx, wbo_json, "title", &tmp_error);
		
		if(folder->title == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in get_bookmark_rec: invalid 'title' property in folder %s:", itm->id);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in get_bookmark_rec: missing 'title' property in folder %s", itm->id);
			}
			return si;
		}
		
		GPtrArray* children_ids = get_string_array_prop(s_ctx->ctx, wbo_json, "children", &tmp_error);
		
		if(children_ids == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in get_bookmark_rec: invalid 'childen' property in folder %s with title %s:", itm->id, folder->title);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in get_bookmark_rec: no 'childen' property in folder %s with title %s:", itm->id, folder->title);
			}
			sync_folder_free(folder);
			return si;
		}
		
		int i;
		folder->children = g_ptr_array_new_full(children_ids->len, ((GDestroyNotify)sync_item_free));

		// g_debug("%s :> [",itm->id);		
		// for(i = 0; i< children_ids->len; i++){
		// 	printf("%s ",(const char*)g_ptr_array_index(children_ids, i));
		// }
		// printf("]\n");
		
		for(i = 0; i< children_ids->len; i++){
			const char* child_id = g_ptr_array_index(children_ids, i);
			WBO* child_wbo = get_wbo_by_id(bookmarks_wbo, child_id);
			
			if(child_wbo == NULL){
				//g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
				//return si;
				g_warning("child %s not found (parent is %s)", child_id, itm->id);
				continue;
			}
			
			JSObjectRef child_json = js_from_json(s_ctx->ctx, child_wbo->payload, strlen(child_wbo->payload), &tmp_error);
			if(child_json == NULL){
				if(tmp_error != NULL){
					g_propagate_prefixed_error(err,tmp_error,
						"decrypted WBO is not JSON (id=%s): ", child_wbo->id);
				}else{
					g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
						"decrypted WBO is not JSON (id=%s)", child_wbo->id);
				}
				g_ptr_array_unref(children_ids);
				sync_folder_free(folder);
				return NULL;
			}
			
			gboolean deleted = get_boolean_prop(s_ctx->ctx, child_json, "deleted", &tmp_error);
			if(tmp_error != NULL){
				if(g_error_matches (tmp_error, MY_JS_ERROR, MY_JS_ERROR_NO_SUCH_VALUE))
				{
					g_clear_error(&tmp_error);
				}
				else
				{
					g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
						"error in get_bookmark_rec: invalid 'deleted' property in %s: %s", child_wbo->id, tmp_error->message);
					g_clear_error(&tmp_error);
					g_ptr_array_unref(children_ids);
					sync_folder_free(folder);
					return NULL;
				}
			}
			if(deleted){
				g_warning("item %s has been deleted, ignoring",child_wbo->id);
				continue;
			}
			SyncItem* child = get_bookmark_rec(s_ctx, bookmarks_wbo, child_wbo, child_json, &tmp_error); 
			
			if(child == NULL){
				g_assert(tmp_error != NULL);
				g_propagate_prefixed_error(err,tmp_error,
					"child %s parsed to NULL (parent is %s): ", child_id, itm->id);
				g_ptr_array_unref(children_ids);
				sync_folder_free(folder);
				return NULL;
			}else if(tmp_error != NULL){
				g_warning("Error in child %s: %s", child_id, tmp_error->message);
				g_clear_error(&tmp_error);
			}
			
			g_ptr_array_add(folder->children, child);
		}
		
		si->actual = folder;
		
		g_ptr_array_unref(children_ids);
				
		return si;
		
		
	} else if(! strcmp("bookmark", s) || ! strcmp("query", s) ){
		g_free((gpointer)s);
		
		si->type = SYNC_ITEM_BOOKMARK;
		
		SYNC_BOOKMARK* bookmark = g_malloc(sizeof(SYNC_BOOKMARK));;
		
		bookmark->title = get_string_prop(s_ctx->ctx, wbo_json, "title", &tmp_error);
		
		if(bookmark->title == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in get_bookmark_rec: invalid 'title' property in bookmark %s:", itm->id);
				return NULL;
			}else{
				g_warning("in get_bookmark_rec: missing 'title' property in bookmark %s (%s)", itm->id, itm->payload);
			}
		}
		
		
		bookmark->bmkURI = get_string_prop(s_ctx->ctx, wbo_json, "bmkUri", &tmp_error);
		
		if(bookmark->bmkURI == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in get_bookmark_rec: invalid 'bmkUri' property in bookmark %s with title %s:", itm->id, bookmark->title);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in get_bookmark_rec: missing 'bmkUri' property in bookmark %s with title %s", itm->id, bookmark->title);
			}
			return si;
		}
		
		bookmark->description = get_string_prop(s_ctx->ctx, wbo_json, "description", &tmp_error);
		
		if(bookmark->bmkURI == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in get_bookmark_rec: invalid 'description' property in bookmark %s with title %s:", itm->id, bookmark->title);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in get_bookmark_rec: missing 'description' property in bookmark %s with title %s", itm->id, bookmark->title);
			}
			return si;
		}
		
		bookmark->tags = get_string_array_prop(s_ctx->ctx, wbo_json, "tags", &tmp_error);
		
		if(bookmark->tags == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in get_bookmark_rec: invalid 'tags' property in bookmark %s with title %s:", itm->id, bookmark->title);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in get_bookmark_rec: missing 'tags' property in bookmark %s with title %s", itm->id, bookmark->title);
			}
			return si;
		}
		
		si->actual = bookmark;
		
		return si;
		
	} else if(! strcmp("separator", s)){
		g_free((gpointer)s);

		si->type = SYNC_ITEM_SEPARATOR;
		si->actual = NULL;
		return si;
	} else {
		g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
			"bookmark type not handled: %s", s);
		g_free((gpointer)s);
		
		si->actual = NULL;
		
		return si;
	}
	
}

// the items on the server are a mess: these 3 seem to be the only important ones
#define ROOTS_LEN	3
static const char* roots[ROOTS_LEN] = {"menu","toolbar","unfiled"};

GPtrArray* get_bookmarks(SYNC_CTX* s_ctx, GError** err){
	GError* tmp_error = NULL;
	
	if(!verify_storage(s_ctx, &tmp_error)){
		g_propagate_prefixed_error(err, tmp_error,
			"can't get bookmarks, unsupported storage version");
		return NULL;
	}
	
	GPtrArray* bookmarks_wbo = get_collection(s_ctx, "bookmarks", &tmp_error);
	
	if(bookmarks_wbo == NULL){
		if(tmp_error != NULL){
		g_propagate_prefixed_error(err, tmp_error,
			"can't get bookmarks:");
		}else{
			g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED,
				"No bookmarks collection in storage");
		}
		return NULL;
	}
	
	
	GPtrArray* bookmarks = g_ptr_array_new_full(ROOTS_LEN,((GDestroyNotify)sync_item_free));
	int i;
	
	for(i = 0; i<bookmarks_wbo->len; i++){
		WBO* itm = g_ptr_array_index(bookmarks_wbo, i);
		const char* decrypted = decrypt_wbo(s_ctx, "bookmarks", itm, &tmp_error);
		if(decrypted == NULL){
			g_assert(tmp_error != NULL);
			g_propagate_prefixed_error(err, tmp_error,
				"can't decrypt a bookmark:");
			g_ptr_array_unref(bookmarks_wbo);
			return NULL;
		}
		g_free((gpointer)itm->payload);
		// replace encrypted payload with decrypted
		itm->payload = decrypted;
	}
	
	for(i = 0; i<ROOTS_LEN; i++){
		WBO* itm = get_wbo_by_id(bookmarks_wbo, roots[i]);
		if(itm == NULL){
			g_warning("cant find item %s in the bookmarks", roots[i]);
			continue;
		}
		JSObjectRef wbo_json = js_from_json(s_ctx->ctx, itm->payload, strlen(itm->payload), &tmp_error);
		if(wbo_json == NULL){
			if(tmp_error != NULL){
				g_propagate_prefixed_error(err,tmp_error,
					"error in get_bookmark: decrypted WBO %s payload is not JSON:", itm->id);
			}else{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in get_bookmark: decrypted WBO %s payload is not JSON:", itm->id);
			}
			g_ptr_array_unref(bookmarks_wbo);
			return NULL;
		}
		// const char* parentid = get_string_prop(s_ctx->ctx, wbo_json, "parentid", NULL);
		// g_debug("%s -> %s\n", parentid, itm->id);
		gboolean deleted = get_boolean_prop(s_ctx->ctx, wbo_json, "deleted", &tmp_error);
		if(tmp_error != NULL){
			if(g_error_matches (tmp_error, MY_JS_ERROR, MY_JS_ERROR_NO_SUCH_VALUE))
			{
				g_clear_error(&tmp_error);
			}
			else
			{
				g_set_error(err, MY_SYNC_ERROR, MY_SYNC_ERROR_FORMAT,
					"error in get_bookmark: invalid 'deleted' property in %s: %s", itm->id, tmp_error->message);
				g_clear_error(&tmp_error);
				g_ptr_array_unref(bookmarks_wbo);
				return NULL;
			}
		}
		if(deleted){
			g_warning("item %s has been deleted, ignoring",itm->id);
			continue;
		}
		SyncItem* parsed = get_bookmark_rec(s_ctx, bookmarks_wbo, itm, wbo_json, &tmp_error);
		if(parsed == NULL){
			g_assert(tmp_error != NULL);
			g_propagate_prefixed_error(err,tmp_error,
				"error in get_bookmark:");
			g_ptr_array_unref(bookmarks_wbo);
			return bookmarks;
		}else if(tmp_error != NULL){
			g_warning("error in get_bookmark:%s",tmp_error->message);
			g_clear_error(&tmp_error);
		}
		g_ptr_array_add(bookmarks, parsed);
	}
	g_ptr_array_unref(bookmarks_wbo);
	
	return bookmarks;
}
