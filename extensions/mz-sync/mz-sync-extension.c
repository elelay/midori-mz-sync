/* :noTabs=true:mode=c:tabSize=4:indentSize=4:folding=explicit:
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/
#include "mz-sync-extension.h"

#include <midori/midori.h>
#include <time.h>

#include "sync.h"
#include "js.h"

#define EXTENSION_NAME "Mozilla Sync Panel"

#define MZ_SYNC_EXTENSION_GET_PRIVATE(obj) (G_TYPE_INSTANCE_GET_PRIVATE ((obj), MZ_SYNC_TYPE_EXTENSION, MzSyncExtensionPrivate))

typedef struct {
    SYNC_CTX ctx;
    KatzeArray* roots;

    guint source_id;
    gboolean is_running;
    gboolean is_stopped;
	
    time_t last_sync_time;
    gboolean last_status_was_error;
    
} MzSyncExtensionPrivate;


struct _MzSyncExtension
{
    MidoriExtension parent_instance;

    MzSyncExtensionPrivate* priv;
};

struct _MzSyncExtensionClass
{
    MidoriExtensionClass parent_class;
};

G_DEFINE_TYPE (MzSyncExtension, mz_sync_extension, MIDORI_TYPE_EXTENSION);


enum
{
    SYNC_STARTED,
    SYNC_ENDED,
    
    LAST_SIGNAL
};

static guint signals[LAST_SIGNAL];

static void
mz_sync_extension_init (MzSyncExtension* self)
{
	// private state
	MzSyncExtensionPrivate* priv;
	
    self->priv = priv = MZ_SYNC_EXTENSION_GET_PRIVATE(self);
    
    memset(&(priv->ctx),0,sizeof(SYNC_CTX));
    priv->roots = katze_array_new (KATZE_TYPE_ARRAY);
    priv->source_id = 0;
    priv->is_running = FALSE;
    priv->is_stopped = FALSE;
    priv->last_sync_time = 0;
    priv->last_status_was_error = TRUE;

}

static void
mz_sync_extension_finalize (GObject* object)
{
    MzSyncExtension* self = MZ_SYNC_EXTENSION (object);

	MzSyncExtensionPrivate* priv = self->priv;
    
    //TODO: unref any priv member here 
    g_object_unref (priv->roots);
}

static void
mz_sync_extension_class_init (MzSyncExtensionClass* class)
{
    GObjectClass* gobject_class;

    signals[SYNC_STARTED] = g_signal_new (
        "sync-started",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        NULL,
        NULL,
        g_cclosure_marshal_VOID__VOID,
        G_TYPE_NONE, 0);

    signals[SYNC_ENDED] = g_signal_new (
        "sync-ended",
        G_TYPE_FROM_CLASS (class),
        (GSignalFlags)(G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
        0,
        NULL,
        NULL,
        g_cclosure_marshal_VOID__POINTER,
        G_TYPE_NONE, 1,
        G_TYPE_POINTER);

    gobject_class = G_OBJECT_CLASS (class);
    gobject_class->finalize = mz_sync_extension_finalize;

    g_type_class_add_private (class, sizeof (MzSyncExtensionPrivate));
}

/* access to private */
KatzeArray*
mz_sync_extension_get_roots (MzSyncExtension* extension)
{
    g_return_val_if_fail (MZ_SYNC_IS_EXTENSION (extension), NULL);
	
    return extension->priv->roots;
}

time_t
mz_sync_extension_get_last_sync (MzSyncExtension* extension)
{
    g_return_val_if_fail (MZ_SYNC_IS_EXTENSION (extension), 0);
	
    return extension->priv->last_sync_time;
}

gboolean
mz_sync_extension_get_last_status_was_error (MzSyncExtension* extension)
{
    g_return_val_if_fail (MZ_SYNC_IS_EXTENSION (extension), TRUE);
	
    return extension->priv->last_status_was_error;
}

/* implementation */
static void
mz_sync_dialog_response_cb (GtkWidget* dialog,
                         gint       response,
                         gpointer   data)
{
    gtk_widget_destroy (dialog);
}
static void
show_error_dialog (GError* e)
{
    GtkWidget* dialog;

    dialog = gtk_message_dialog_new (
        NULL, 0, GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
        _("Error"));
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
            _("Error: %s"), e->message);
    gtk_window_set_title (GTK_WINDOW (dialog), EXTENSION_NAME);
    gtk_widget_show (dialog);
    g_signal_connect (dialog, "response",
            G_CALLBACK (mz_sync_dialog_response_cb), NULL);
    g_clear_error(&e);
}


static void
add_bookmarks_rec(KatzeArray* parent, SyncItem* itm);

static void
update_bookmarks_rec(KatzeArray* current, GPtrArray* newer, gboolean delete_missing, gboolean add_new, gboolean modify, MzSyncStatus* status);

gboolean
mz_sync_extension_sync (MzSyncExtension* extension)
{
	
	MzSyncExtensionPrivate* priv = extension->priv;
	
	SYNC_CTX* ctx;
	MzSyncStatus status = {0};
	time_t sync_start_time;

    if (!priv->is_running && !priv->is_stopped)
    {
		priv->is_running = TRUE;
		ctx = &(priv->ctx);

		g_signal_emit_by_name(extension, "sync-started");
		if(ctx->bulk_key == NULL){
			g_set_error(&(status.error), MY_SYNC_ERROR, MY_SYNC_ERROR_FAILED, "not set correctly...");
			status.success = FALSE;
		}else{
			
			sync_start_time = time(NULL);
			// 1) get the collections to see if anything has changed
			// 2) get the bookmarks
			// 3) figure out what has changed and update
			
			
			// 1) get the collections to see if anything has changed

			JSObjectRef collections = list_collections(ctx, priv->last_sync_time, &(status.error));
			if(collections == NULL){
				if(g_error_matches(status.error, MY_SYNC_ERROR, MY_SYNC_ERROR_NOT_MODIFIED)){
					g_clear_error(&(status.error));
					status.success = TRUE;
				}else{
					status.success = FALSE;
				}
			} else {
			
				double bookmarks_ts = get_double_prop(ctx->ctx, collections, "bookmarks", &(status.error));
				if(status.error != NULL){
					status.success = FALSE;
				}else{
					if(bookmarks_ts < priv->last_sync_time) {
						status.success = TRUE;
					} else {
						
						// 2) get the bookmarks
						
						GPtrArray* bookmarks = get_bookmarks(ctx, &(status.error));
						if(bookmarks == NULL){
							status.success = FALSE;
							if( status.error != NULL){
								fprintf (stderr, "Error: %s\n", status.error->message);
								show_error_dialog(status.error);
							}else{
								fprintf (stderr, "no bookmarks !\n");
							}
						}else{
							gboolean delete_missing = mz_sync_extension_get_download_delete(extension);
							gboolean add = mz_sync_extension_get_download_add(extension);
							gboolean modify = mz_sync_extension_get_download_modify(extension);
							
							update_bookmarks_rec(priv->roots, bookmarks, delete_missing, add, modify, &status);
							
							g_ptr_array_unref(bookmarks);
							status.success = TRUE;
						}
					}
				}
			}
			priv->last_status_was_error = !status.success;
			if(status.success){
				priv->last_sync_time = sync_start_time;
			}
		}
		
		g_signal_emit_by_name(extension, "sync-ended", &status);
		g_clear_error(&(status.error));
    }
    priv->is_running = FALSE;
    return TRUE;
}

static void
add_bookmarks_rec(KatzeArray* parent, SyncItem* itm)
{
	int i;
	
	if(itm->type == SYNC_ITEM_FOLDER){
		SYNC_FOLDER* actual = itm->actual;
		if(actual == NULL){
			g_error("invalid folder: '%s'\n", itm->id);
		}else{

            KatzeArray* feed;
            
            feed = katze_array_new (KATZE_TYPE_ARRAY);
            katze_item_set_token (KATZE_ITEM (feed), itm->id);
            katze_item_set_uri (KATZE_ITEM (feed), NULL);
            katze_item_set_name (KATZE_ITEM (feed), itm->id);
            katze_item_set_text (KATZE_ITEM (feed), actual->title);
            katze_array_add_item (parent, feed);

			//printf("%s '%s' %f\n", itm->id, actual->title, itm->modified);
			
			// new, so insertAt == -1
			for(i = 0 ; i < actual->children->len; i++){
				add_bookmarks_rec(feed, g_ptr_array_index(actual->children, i));
			}
		}
	}else if(itm->type == SYNC_ITEM_BOOKMARK){
		SYNC_BOOKMARK* actual = itm->actual;
		if(actual == NULL){
			g_error("invalid bookmark: '%s'\n", itm->id);
		}else{
			printf("%s %s %s %f\n", itm->id, actual->title, actual->bmkURI, itm->modified);
		}
		// ignore place:* bookmarks as they are not real bookmarks but ff specific
		if(strncmp(actual->bmkURI, "place:", 6)){

            KatzeItem* child;
    
            child = katze_item_new ();
            katze_item_set_uri (child, actual->bmkURI);
            katze_item_set_token (KATZE_ITEM (child), itm->id);
            katze_item_set_name (KATZE_ITEM (child), itm->id);
            katze_item_set_text (KATZE_ITEM (child), actual->title);

            katze_array_add_item (parent, child);
        }

	}else if(itm->type == SYNC_ITEM_SEPARATOR){
		//printf("_______________________________\n");
	}else{
		g_error("new type of bookmark !");
	}
}

static void
update_bookmarks_rec(KatzeArray* current, GPtrArray* newer,
	gboolean delete_missing, gboolean add_new, gboolean modify,
	MzSyncStatus* status)
{
	GList* l;
	KatzeItem* child;
	SyncItem* itm;
	int deleted_cnt = 0;
	int current_len = katze_array_get_length(current);
	
	KATZE_ARRAY_FOREACH_ITEM_L (child, current, l)
	{
		//printf("considering %s %s\n",child->token, child->text);
		itm = get_sync_item_by_id(newer, child->token);
		if(itm == NULL){
			deleted_cnt++;
			
			if(delete_missing){
				printf("deleting %s\t%s\n", child->token, child->uri);
				katze_array_remove_item(current, child);
				status->deleted++;
			}else{
				printf("not deleting %s\t%s\n", child->token, child->uri);
			}
		}else{
			
			if(itm->type == SYNC_ITEM_FOLDER){
				SYNC_FOLDER* actual = itm->actual;
				if(actual == NULL){
					g_error("invalid folder: '%s'\n", itm->id);
				}else{
					if(!KATZE_IS_ARRAY(child)){
						// it was a bookmark and is now a folder
						if(delete_missing){
							printf("deleting because it's now an array %s\t%s\n", child->token, child->uri);
							katze_array_remove_item(current, child);
							status->deleted++;
						}else{
							printf("not deleting even if it's now an array %s\t%s\n", child->token, child->uri);
							GError* tmp_error = NULL;
							// this is an ugly case that I try to handle the best way
							// depending on the choices of the user
							// so I modify the local id, so it appears as deleted even if it
							// has not been deleted
							// FIXME: the new id could be non unique...
							child->name = child->token = generate_id(&tmp_error);
						}
					}else{
						gboolean modified = FALSE;
						if(strcmp(child->name, itm->id)){
							if(modify){
								printf("updating name of %s: '%s' to '%s'\n", child->token, child->name, itm->id);
								katze_item_set_name (child, itm->id);
								modified = TRUE;
							}else{
								printf("not updating name of %s: '%s' to '%s'\n", child->token, child->name, itm->id);
							}
						}
						
						if(strcmp(child->text, actual->title)){
							if(modify){
								printf("updating text of %s: '%s' to '%s'\n", child->token, child->text, actual->title);
								katze_item_set_text (child, actual->title);
								modified = TRUE;
							}else{
								printf("not updating text of %s: '%s' to '%s'\n", child->token, child->text, actual->title);
							}
						}
						
						if(modified){
							status->modified++;
						}
						
						update_bookmarks_rec(KATZE_ARRAY(child), actual->children, delete_missing, add_new, modify, status);
					}
				}
			}else if(itm->type == SYNC_ITEM_BOOKMARK){
				SYNC_BOOKMARK* actual = itm->actual;
				gboolean modified = FALSE;
				
				if(actual == NULL){
					g_error("invalid bookmark: '%s'\n", itm->id);
				}
				
				if(strcmp(child->uri, actual->bmkURI)){
					if(modify){
						printf("updating uri of %s: '%s' to '%s'\n", child->token, child->uri, actual->bmkURI);
						katze_item_set_uri (child, actual->bmkURI);
						modified = TRUE;
					}else{
						printf("not updating uri of %s: '%s' to '%s'\n", child->token, child->uri, actual->bmkURI);
					}
				}
				
				if(strcmp(child->name, itm->id)){
					if(modify){
						printf("updating name of %s: '%s' to '%s'\n", child->token, child->name, itm->id);
						katze_item_set_name (child, itm->id);
						modified = TRUE;
					}else{
						printf("not updating name of %s: '%s' to '%s'\n", child->token, child->name, itm->id);
					}
				}							
				
				if(strcmp(child->text, actual->title)){
					if(modify){
						printf("updating text of %s: '%s' to '%s'\n", child->token, child->text, actual->title);
						katze_item_set_text (child, actual->title);
						modified = TRUE;
					}else{
						printf("not updating text of %s: '%s' to '%s'\n", child->token, child->text, actual->title);
					}
				}
				
				if(modified){
					status->modified++;
				}
				
			}else if(itm->type == SYNC_ITEM_SEPARATOR){
				if(delete_missing){
					printf("deleting because it's now a separator %s\t%s\n", child->token, child->uri);
					katze_array_remove_item(current, child);
					status->deleted++;
				}else{
					printf("not deleting even if it's now a separator %s\t%s\n", child->token, child->uri);
				}
			}else{
				g_error("new type of bookmark !");
			}

			
		}
	}
	g_free(l);
	
	
	// now, the added ones ;-)
	g_assert(newer->len + deleted_cnt >= current_len);
	
	if(newer->len + deleted_cnt != current_len){
		int i;
		for(i=0; i < newer->len; i++){
			itm = g_ptr_array_index(newer, i);
			// ignore separators,
			// ignore place:* bookmarks as they are not real bookmarks but ff specific
			if(itm->type == SYNC_ITEM_FOLDER
				|| (itm->type == SYNC_ITEM_BOOKMARK
					&& (strncmp(((SYNC_BOOKMARK*)itm->actual)->bmkURI, "place:", 6) != 0)))
			{
				child  = katze_array_find_token(current, itm->id);
				if(child == NULL){
					if(add_new){
						printf("adding %s (%u)\n", itm->id, itm->type);
						add_bookmarks_rec(current, itm);
						status->added++;
					}else{
						printf("not adding %s\n", itm->id);
					}
				}
			}
		}
	}
}


void
mz_sync_deactivate_cb (MzSyncExtension* extension, gpointer ignored)
{
	g_signal_handlers_disconnect_by_func (extension,
		mz_sync_deactivate_cb, NULL);
	
	
	MzSyncExtensionPrivate* priv = extension->priv;
	
	if (priv->source_id){
		g_source_remove (priv->source_id);
		priv->source_id = 0;
	}
	priv->is_stopped = TRUE;
	if(!priv->is_running){
		free_sync(&(priv->ctx));
	}
}

void
mz_sync_extension_activate_cb (MzSyncExtension* extension)
{

    mz_sync_extension_reset (extension);
    
    g_signal_connect (extension, "deactivate",
        G_CALLBACK (mz_sync_deactivate_cb), NULL); 
    
}

void
mz_sync_extension_reset (MzSyncExtension* extension)
{
    SYNC_CTX* ctx;
    KatzeArray* roots;
    gsize i;
    const gchar* user;
    const gchar* pass;
    const gchar* key;
    const gchar* server;
    const gchar* sync_name;
    MzSyncExtensionPrivate* priv;
    int interval;
    GError* err;
	time_t sync_start_time;
    
    priv = extension->priv;
    
    ctx = &(priv->ctx);
    free_sync(ctx);
    init_sync(ctx, MZ_SYNC_USER_AGENT);
    
    user = mz_sync_extension_get_user (extension);
    pass = mz_sync_extension_get_pass (extension);
    key = mz_sync_extension_get_key (extension);
    server = mz_sync_extension_get_server (extension);
    sync_name = mz_sync_extension_get_sync_name (extension);
    interval = mz_sync_extension_get_interval (extension);
    
    g_assert (user && pass && key && server);
    
    err = NULL;
    
	roots = priv->roots;
	if(!katze_array_is_empty(roots)){
		katze_array_clear(roots);
	}
    if(!set_user(ctx, server, user, pass, &err) ){
        if( err != NULL){
            fprintf (stderr, "Error: %s\n", err->message);
            show_error_dialog(err);
        }
    } else {
		g_clear_error(&err);
		
		if(! verify_storage(ctx, &err) ){
			if( err != NULL){
				fprintf (stderr, "Error: %s\n", err->message);
				show_error_dialog(err);
			}
		} else {
			g_clear_error(&err);
			
			if(! set_master_key(ctx, key, &err) ){
				if( err != NULL){
					fprintf (stderr, "Error: %s\n", err->message);
					show_error_dialog(err);
				}
			} else {
				g_clear_error(&err);
				
				if(! client_exists(ctx, sync_name, &err) ){
					if( err == NULL){
						fprintf (stderr, "Warning: client not declared on the server!\n");
					}else{
						fprintf (stderr, "Error: %s\n", err->message);
						show_error_dialog(err);
						return;
					}
				}
				
				g_clear_error(&err);
				
                sync_start_time = time(NULL);
                // TODO: load cached copy for fast display...
				GPtrArray* bookmarks = get_bookmarks(ctx, &err);
				if(bookmarks == NULL){
					if( err != NULL){
						fprintf (stderr, "Error: %s\n", err->message);
						show_error_dialog(err);
					}else{
						fprintf (stderr, "no bookmarks !\n");
					}
                    priv->last_status_was_error = TRUE;
				} else {
					g_clear_error(&err);
					
					for(i=0;i<bookmarks->len;i++){
						add_bookmarks_rec(roots, g_ptr_array_index(bookmarks, i));
					}
					
					g_ptr_array_unref(bookmarks);

                    priv->last_status_was_error = FALSE;
                    priv->last_sync_time = sync_start_time;
				}
			}
		}
	}
    
	if (priv->source_id){
		g_source_remove (priv->source_id);
		priv->source_id = 0;
	}
    if(interval > 0){
		priv->source_id = g_timeout_add_seconds (interval * 60,
			(GSourceFunc) mz_sync_extension_sync, extension);
	}
}

void
mz_sync_extension_install_prefs(MzSyncExtension * self) {
	// set preferences
    midori_extension_install_string (MIDORI_EXTENSION(self), "user", "toto@titi.fr");
    midori_extension_install_string (MIDORI_EXTENSION(self), "pass", "azertyuiop");
    midori_extension_install_string (MIDORI_EXTENSION(self), "key", "h-r345c-69w4h-kqsfa-y8n6h-h5c8y");
    midori_extension_install_string (MIDORI_EXTENSION(self), "server", "http://localhost:5000");
    midori_extension_install_string (MIDORI_EXTENSION(self), "sync_name", "Midori MZ-SYNC extension");
    midori_extension_install_integer (MIDORI_EXTENSION(self), "interval", 5);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "sandboxed", TRUE);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "sync_bookmarks", TRUE);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "upload_delete", TRUE);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "upload_add", TRUE);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "upload_modify", TRUE);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "download_delete", TRUE);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "download_add", TRUE);
    midori_extension_install_boolean (MIDORI_EXTENSION(self), "download_modify", TRUE);
}
