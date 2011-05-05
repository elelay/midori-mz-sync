/*
 Copyright (C) 2007-2009 Christian Dywan <christian@twotoasts.de>
 Copyright (C) 2009 Dale Whittaker <dayul@users.sf.net>
 Copyright (C) 2009 Alexander Butenko <a.butenka@gmail.com>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#include "sokoke.h"

#if HAVE_CONFIG_H
    #include <config.h>
#endif

#include "midori-stock.h"

#if HAVE_UNISTD_H
    #include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>

#ifdef GDK_WINDOWING_X11
    #include <gdk/gdkx.h>
#endif

#include <gdk/gdkkeysyms.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <glib/gstdio.h>

#if HAVE_LIBIDN
    #include <stringprep.h>
    #include <punycode.h>
    #include <idna.h>
#endif

#ifdef HAVE_HILDON_FM
    #include <hildon/hildon-file-chooser-dialog.h>
#endif

#if HAVE_HILDON
    #include <libosso.h>
    #include <hildon/hildon.h>
    #include <hildon-mime.h>
    #include <hildon-uri.h>
#endif

#if !GTK_CHECK_VERSION(2, 12, 0)

void
gtk_widget_set_has_tooltip (GtkWidget* widget,
                            gboolean   has_tooltip)
{
    /* Do nothing */
}

void
gtk_widget_set_tooltip_text (GtkWidget*   widget,
                             const gchar* text)
{
    if (text && *text)
    {
        static GtkTooltips* tooltips = NULL;
        if (G_UNLIKELY (!tooltips))
            tooltips = gtk_tooltips_new ();
        gtk_tooltips_set_tip (tooltips, widget, text, NULL);
    }
}

void
gtk_tool_item_set_tooltip_text (GtkToolItem* toolitem,
                                const gchar* text)
{
    if (text && *text)
    {
        static GtkTooltips* tooltips = NULL;
        if (G_UNLIKELY (!tooltips))
            tooltips = gtk_tooltips_new ();

        gtk_tool_item_set_tooltip (toolitem, tooltips, text, NULL);
    }
}

#endif

static gchar*
sokoke_js_string_utf8 (JSStringRef js_string)
{
    size_t size_utf8;
    gchar* string_utf8;

    g_return_val_if_fail (js_string, NULL);

    size_utf8 = JSStringGetMaximumUTF8CStringSize (js_string);
    string_utf8 = g_new (gchar, size_utf8);
    JSStringGetUTF8CString (js_string, string_utf8, size_utf8);
    return string_utf8;
}

gchar*
sokoke_js_script_eval (JSContextRef js_context,
                       const gchar* script,
                       gchar**      exception)
{
    gchar* value;
    JSStringRef js_value_string;

    g_return_val_if_fail (js_context, FALSE);
    g_return_val_if_fail (script, FALSE);

    JSStringRef js_script = JSStringCreateWithUTF8CString (script);
    JSValueRef js_exception = NULL;
    JSValueRef js_value = JSEvaluateScript (js_context, js_script,
        JSContextGetGlobalObject (js_context), NULL, 0, &js_exception);
    JSStringRelease (js_script);

    if (!js_value)
    {
        JSStringRef js_message = JSValueToStringCopy (js_context,
                                                      js_exception, NULL);
        value = sokoke_js_string_utf8 (js_message);
        if (exception)
            *exception = value;
        else
        {
            g_warning ("%s", value);
            g_free (value);
        }
        JSStringRelease (js_message);
        return NULL;
    }

    js_value_string = JSValueToStringCopy (js_context, js_value, NULL);
    value = sokoke_js_string_utf8 (js_value_string);
    JSStringRelease (js_value_string);
    return value;
}

static void
sokoke_message_dialog_response_cb (GtkWidget* dialog,
                                   gint       response,
                                   gpointer   data)
{
    gtk_widget_destroy (dialog);
}

void
sokoke_message_dialog (GtkMessageType message_type,
                       const gchar*   short_message,
                       const gchar*   detailed_message,
                       gboolean       modal)
{
    GtkWidget* dialog = gtk_message_dialog_new (
        NULL, 0, message_type,
        #if HAVE_HILDON
        GTK_BUTTONS_NONE,
        #else
        GTK_BUTTONS_OK,
        #endif
        "%s", short_message);
    gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                              "%s", detailed_message);
    if (modal)
    {
        gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);
    }
    else
    {
        g_signal_connect (dialog, "response",
                          G_CALLBACK (sokoke_message_dialog_response_cb), NULL);
        gtk_widget_show (dialog);
    }

}

/**
 * sokoke_show_uri_with_mime_type:
 * @screen: a #GdkScreen, or %NULL
 * @uri: the URI to show
 * @mime_type: a MIME type
 * @timestamp: the timestamp of the event
 * @error: the location of a #GError, or %NULL
 *
 * Shows the specified URI with an appropriate application,
 * as though it had the specified MIME type.
 *
 * On Maemo, hildon_mime_open_file_with_mime_type() is used.
 *
 * See also: sokoke_show_uri().
 *
 * Return value: %TRUE on success, %FALSE if an error occurred
 **/
gboolean
sokoke_show_uri_with_mime_type (GdkScreen*   screen,
                                const gchar* uri,
                                const gchar* mime_type,
                                guint32      timestamp,
                                GError**     error)
{
    gboolean success;
    #if HAVE_HILDON
    osso_context_t* osso;
    DBusConnection* dbus;

    osso = osso_initialize (PACKAGE_NAME, PACKAGE_VERSION, FALSE, NULL);
    if (!osso)
    {
        g_print ("Failed to initialize libosso\n");
        return FALSE;
    }

    dbus = (DBusConnection *) osso_get_dbus_connection (osso);
    if (!dbus)
    {
        osso_deinitialize (osso);
        g_print ("Failed to get dbus connection from osso context\n");
        return FALSE;
    }

    success = (hildon_mime_open_file_with_mime_type (dbus,
               uri, mime_type) == 1);
    osso_deinitialize (osso);
    #else
    GFile* file = g_file_new_for_uri (uri);
    gchar* content_type;
    GAppInfo* app_info;
    GList* files;
    gpointer context;

    content_type = g_content_type_from_mime_type (mime_type);
    app_info = g_app_info_get_default_for_type (content_type,
        !g_str_has_prefix (uri, "file://"));
    g_free (content_type);
    files = g_list_prepend (NULL, file);
    #if GTK_CHECK_VERSION (2, 14, 0)
    context = gdk_app_launch_context_new ();
    gdk_app_launch_context_set_screen (context, screen);
    gdk_app_launch_context_set_timestamp (context, timestamp);
    #else
    context = g_app_launch_context_new ();
    #endif

    success = g_app_info_launch (app_info, files, context, error);

    g_object_unref (app_info);
    g_list_free (files);
    g_object_unref (file);
    #endif

    return success;
}

static void
sokoke_open_with_response_cb (GtkWidget* dialog,
                              gint       response,
                              GtkEntry*  entry)
{
    if (response == GTK_RESPONSE_ACCEPT)
    {
        const gchar* command = gtk_entry_get_text (entry);
        const gchar* uri = g_object_get_data (G_OBJECT (dialog), "uri");
        sokoke_spawn_program (command, uri);
    }
    gtk_widget_destroy (dialog);
}

static GAppInfo*
sokoke_default_for_uri (const gchar* uri,
                        gchar**      scheme_ptr)
{
    gchar* scheme;
    GAppInfo* info;

    scheme = g_uri_parse_scheme (uri);
    if (scheme_ptr != NULL)
        *scheme_ptr = scheme;
    if (!scheme)
        return NULL;

    info = g_app_info_get_default_for_uri_scheme (scheme);
    #if !GLIB_CHECK_VERSION (2, 28, 0)
    if (!info)
    {
        gchar* type = g_strdup_printf ("x-scheme-handler/%s", scheme);
        info = g_app_info_get_default_for_type (type, FALSE);
        g_free (type);
    }
    #endif
    if (info != NULL && scheme_ptr != NULL)
        g_free (scheme);
    return info;

}

/**
 * sokoke_show_uri:
 * @screen: a #GdkScreen, or %NULL
 * @uri: the URI to show
 * @timestamp: the timestamp of the event
 * @error: the location of a #GError, or %NULL
 *
 * Shows the specified URI with an appropriate application. This
 * supports xdg-open, exo-open and gnome-open as fallbacks if
 * GIO doesn't do the trick.
 * x-scheme-handler is supported for GLib < 2.28 as of 0.3.3.
 *
 * On Maemo, hildon_uri_open() is used.
 *
 * Return value: %TRUE on success, %FALSE if an error occurred
 **/
gboolean
sokoke_show_uri (GdkScreen*   screen,
                 const gchar* uri,
                 guint32      timestamp,
                 GError**     error)
{
    #if HAVE_HILDON
    HildonURIAction* action = hildon_uri_get_default_action_by_uri (uri, NULL);
    return hildon_uri_open (uri, action, error);

    #elif defined (G_OS_WIN32)

    const gchar* fallbacks [] = { "explorer" };
    gsize i;
    GAppInfo *app_info;
    GFile *file;
    gchar *free_uri;

    g_return_val_if_fail (GDK_IS_SCREEN (screen) || !screen, FALSE);
    g_return_val_if_fail (uri != NULL, FALSE);
    g_return_val_if_fail (!error || !*error, FALSE);

    file = g_file_new_for_uri (uri);
    app_info = g_file_query_default_handler (file, NULL, error);

    if (app_info != NULL)
    {
        GdkAppLaunchContext *context;
        gboolean result;
        GList l;

        context = gdk_app_launch_context_new ();
        gdk_app_launch_context_set_screen (context, screen);
        gdk_app_launch_context_set_timestamp (context, timestamp);

        l.data = (char *)file;
        l.next = l.prev = NULL;
        result = g_app_info_launch (app_info, &l, (GAppLaunchContext*)context, error);

        g_object_unref (context);
        g_object_unref (app_info);
        g_object_unref (file);

        if (result)
            return TRUE;
    }
    else
        g_object_unref (file);

    free_uri = g_filename_from_uri (uri, NULL, NULL);
    if (free_uri)
    {
        gchar *quoted = g_shell_quote (free_uri);
        uri = quoted;
        g_free (free_uri);
        free_uri = quoted;
    }

    for (i = 0; i < G_N_ELEMENTS (fallbacks); i++)
    {
        gchar* command = g_strconcat (fallbacks[i], " ", uri, NULL);
        gboolean result = g_spawn_command_line_async (command, error);
        g_free (command);
        if (result)
        {
            g_free (free_uri);
            return TRUE;
        }
        if (error)
            *error = NULL;
    }

    g_free (free_uri);

    return FALSE;

    #else

    #if !GLIB_CHECK_VERSION (2, 28, 0)
    GAppInfo* info;
    gchar* scheme;
    #endif
    const gchar* fallbacks [] = { "xdg-open", "exo-open", "gnome-open" };
    gsize i;
    GtkWidget* dialog;
    GtkWidget* box;
    gchar* filename;
    gchar* ms;
    GtkWidget* entry;

    g_return_val_if_fail (GDK_IS_SCREEN (screen) || !screen, FALSE);
    g_return_val_if_fail (uri != NULL, FALSE);
    g_return_val_if_fail (!error || !*error, FALSE);

    sokoke_recursive_fork_protection (uri, TRUE);

    #if GTK_CHECK_VERSION (2, 14, 0)
    if (gtk_show_uri (screen, uri, timestamp, error))
        return TRUE;
    #else
    if (g_app_info_launch_default_for_uri (uri, NULL, NULL))
        return TRUE;
    #endif

    #if !GLIB_CHECK_VERSION (2, 28, 0)
    info = sokoke_default_for_uri (uri, &scheme);
    if (info)
    {
        gchar* argument = g_strdup (&uri[scheme - uri]);
        GList* uris = g_list_prepend (NULL, argument);
        if (g_app_info_launch_uris (info, uris, NULL, NULL))
        {
            g_list_free (uris);
            g_free (scheme);
            g_object_unref (info);
            return TRUE;
        }
        g_list_free (uris);
        g_free (scheme);
        g_object_unref (info);
    }
    #endif

    for (i = 0; i < G_N_ELEMENTS (fallbacks); i++)
    {
        gchar* command = g_strconcat (fallbacks[i], " ", uri, NULL);
        gboolean result = g_spawn_command_line_async (command, error);
        g_free (command);
        if (result)
            return TRUE;
        if (error)
            *error = NULL;
    }

    dialog = gtk_dialog_new_with_buttons (_("Open with"), NULL, 0,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        GTK_STOCK_OPEN, GTK_RESPONSE_ACCEPT, NULL);
    box = gtk_dialog_get_content_area (GTK_DIALOG (dialog));
    if (g_str_has_prefix (uri, "file:///"))
        filename = g_filename_from_uri (uri, NULL, NULL);
    else
        filename = g_strdup (uri);
    ms = g_strdup_printf (_("Choose an application or command to open \"%s\":"),
                          filename);
    gtk_box_pack_start (GTK_BOX (box), gtk_label_new (ms), TRUE, FALSE, 4);
    g_free (ms);
    entry = gtk_entry_new ();
    gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
    gtk_box_pack_start (GTK_BOX (box), entry, TRUE, FALSE, 4);
    g_signal_connect (dialog, "response",
                      G_CALLBACK (sokoke_open_with_response_cb), entry);
    g_object_set_data_full (G_OBJECT (dialog), "uri",
                            filename, (GDestroyNotify)g_free);
    gtk_widget_show_all (dialog);
    gtk_dialog_set_default_response (GTK_DIALOG (dialog), GTK_RESPONSE_ACCEPT);
    gtk_widget_grab_focus (entry);

    return TRUE;
    #endif
}

gboolean
sokoke_spawn_program (const gchar* command,
                      const gchar* argument)
{
    GError* error;

    g_return_val_if_fail (command != NULL, FALSE);
    g_return_val_if_fail (argument != NULL, FALSE);

    if (!g_strstr_len (argument, 8, "://")
     && !g_str_has_prefix (argument, "about:"))
    {
        gboolean success;

        #if HAVE_HILDON
        osso_context_t* osso;
        DBusConnection* dbus;

        osso = osso_initialize (PACKAGE_NAME, PACKAGE_VERSION, FALSE, NULL);
        if (!osso)
        {
            sokoke_message_dialog (GTK_MESSAGE_ERROR,
                                   _("Could not run external program."),
                                   "Failed to initialize libosso", FALSE);
            return FALSE;
        }

        dbus = (DBusConnection *) osso_get_dbus_connection (osso);
        if (!dbus)
        {
            osso_deinitialize (osso);
            sokoke_message_dialog (GTK_MESSAGE_ERROR,
                                   _("Could not run external program."),
                                   "Failed to get dbus connection from osso context", FALSE);
            return FALSE;
        }

        error = NULL;
        /* FIXME: This is not correct, find a proper way to do this */
        success = (osso_application_top (osso, command, argument) == OSSO_OK);
        osso_deinitialize (osso);
        #else
        GAppInfo* info;
        GFile* file;
        GList* files;

        info = g_app_info_create_from_commandline (command,
            NULL, G_APP_INFO_CREATE_NONE, NULL);
        file = g_file_new_for_commandline_arg (argument);
        files = g_list_append (NULL, file);

        error = NULL;
        success = g_app_info_launch (info, files, NULL, &error);
        g_object_unref (file);
        g_list_free (files);
        #endif

        if (!success)
        {
            sokoke_message_dialog (GTK_MESSAGE_ERROR,
                _("Could not run external program."),
                error ? error->message : "", FALSE);
            if (error)
                g_error_free (error);
            return FALSE;
        }
    }
    else
    {
        /* FIXME: Implement Hildon specific version */
        gchar* uri_format;
        gchar* argument_quoted;
        gchar* command_ready;
        gchar** argv;

        if ((uri_format = strstr (command, "%u")))
            uri_format[1] = 's';

        argument_quoted = g_shell_quote (argument);
        if (strstr (command, "%s"))
            command_ready = g_strdup_printf (command, argument_quoted);
        else
            command_ready = g_strconcat (command, " ", argument_quoted, NULL);
        g_free (argument_quoted);

        error = NULL;
        if (!g_shell_parse_argv (command_ready, NULL, &argv, &error))
        {
            sokoke_message_dialog (GTK_MESSAGE_ERROR,
                                   _("Could not run external program."),
                                   error->message, FALSE);
            g_error_free (error);
            g_free (command_ready);
            return FALSE;
        }
        g_free (command_ready);

        error = NULL;
        if (!g_spawn_async (NULL, argv, NULL,
            (GSpawnFlags)G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
            NULL, NULL, NULL, &error))
        {
            sokoke_message_dialog (GTK_MESSAGE_ERROR,
                                   _("Could not run external program."),
                                   error->message, FALSE);
            g_error_free (error);
        }

        g_strfreev (argv);
    }

    return TRUE;
}

void
sokoke_spawn_app (const gchar* uri,
                  gboolean     private)
{
    const gchar* executable = sokoke_get_argv (NULL)[0];
    /* "midori"
       "/usr/bin/midori"
       "c:/Program Files/Midori/bin/midori.exe" */
    gchar* quoted = g_shell_quote (executable);
    gchar* command;
    if (private)
        command = g_strconcat (quoted, " -c ", sokoke_set_config_dir (NULL),
                                       " -p", NULL);
    else
        command = g_strconcat (quoted, " -a", NULL);
    g_free (quoted);
    sokoke_spawn_program (command, uri);
    g_free (command);
}

/**
 * sokoke_hostname_from_uri:
 * @uri: an URI string
 * @path: location of a string, or %NULL
 *
 * Returns the hostname of the specified URI.
 *
 * If there is a path, it is stored in @path.
 *
 * Return value: a newly allocated hostname
 **/
gchar*
sokoke_hostname_from_uri (const gchar* uri,
                          gchar**      path)
{
    gchar* hostname;

    if ((hostname = strchr (uri, '/')))
    {
        gchar* pathname;
        if (hostname[1] == '/')
            hostname += 2;
        if ((pathname = strchr (hostname, '/')))
        {
            if (path != NULL)
                *path = pathname;
            return g_strndup (hostname, pathname - hostname);
        }
        else
            return g_strdup (hostname);
    }

    return g_strdup (uri);
}

/**
 * sokoke_hostname_to_ascii:
 * @uri: an URI string
 *
 * The specified hostname is encoded if it is not ASCII.
 *
 * If no IDN support is available at compile time,
 * the hostname will be returned unaltered.
 *
 * Return value: a newly allocated hostname
 **/
static gchar*
sokoke_hostname_to_ascii (const gchar* hostname)
{
    #ifdef HAVE_LIBSOUP_2_27_90
    return g_hostname_to_ascii (hostname);
    #elif HAVE_LIBIDN
    uint32_t* q;
    char* encoded;
    int rc;

    if ((q = stringprep_utf8_to_ucs4 (hostname, -1, NULL)))
    {
        rc = idna_to_ascii_4z (q, &encoded, IDNA_ALLOW_UNASSIGNED);
        free (q);
        if (rc == IDNA_SUCCESS)
            return encoded;
    }
    #endif
    return g_strdup (hostname);
}

/**
 * sokoke_uri_to_ascii:
 * @uri: an URI string
 *
 * The specified URI is parsed and the hostname
 * part of it is encoded if it is not ASCII.
 *
 * If no IDN support is available at compile time,
 * the URI will be returned unaltered.
 *
 * Return value: a newly allocated URI
 **/
gchar*
sokoke_uri_to_ascii (const gchar* uri)
{
    gchar* proto = NULL;
    gchar* path = NULL;
    gchar* hostname;
    gchar* encoded;

    if (strchr (uri, '/') && (proto = strchr (uri, ':')))
    {
        gulong offset;
        gchar* buffer;

        offset = g_utf8_pointer_to_offset (uri, proto);
        buffer = g_malloc0 (offset + 1);
        g_utf8_strncpy (buffer, uri, offset);
        proto = buffer;
    }

    hostname = sokoke_hostname_from_uri (uri, &path);
    encoded = sokoke_hostname_to_ascii (hostname);

    if (encoded)
    {
        gchar* res = g_strconcat (proto ? proto : "", proto ? "://" : "",
                                  encoded, path, NULL);
        g_free (encoded);
        return res;
    }
    g_free (hostname);
    return g_strdup (uri);
}

static gchar*
sokoke_idn_to_punycode (gchar* uri)
{
    #if HAVE_LIBIDN
    gchar* result = sokoke_uri_to_ascii (uri);
    g_free (uri);
    return result;
    #else
    return uri;
    #endif
}

/**
 * sokoke_search_uri:
 * @uri: a search URI with or without %s
 * @keywords: keywords
 *
 * Takes a search engine URI and inserts the specified
 * keywords. The @keywords are percent encoded. If the
 * search URI contains a %s they keywords are inserted
 * in that place, otherwise appended to the URI.
 *
 * Return value: a newly allocated search URI
 **/
gchar* sokoke_search_uri (const gchar* uri,
                          const gchar* keywords)
{
    gchar* escaped;
    gchar* search;

    g_return_val_if_fail (keywords != NULL, NULL);

    if (!uri)
        return g_strdup (keywords);

    escaped = g_uri_escape_string (keywords, ":/", TRUE);
    if (strstr (uri, "%s"))
        search = g_strdup_printf (uri, escaped);
    else
        search = g_strconcat (uri, escaped, NULL);
    g_free (escaped);
    return search;
}

static void
sokoke_resolve_hostname_cb (SoupAddress *address,
                            guint        status,
                            gpointer     data)
{
    if (status == SOUP_STATUS_OK)
        *(gint *)data = 1;
    else
        *(gint *)data = 2;
}

/**
 * sokoke_resolve_hostname
 * @hostname: a string typed by a user
 *
 * Takes a string that was typed by a user,
 * resolves the hostname, and returns the status.
 *
 * Return value: %TRUE if is a valid host, else %FALSE
 **/
gboolean
sokoke_resolve_hostname (const gchar* hostname)
{
    gchar* uri;
    gint host_resolved = 0;

    uri = g_strconcat ("http://", hostname, NULL);
    if (sokoke_prefetch_uri (NULL, uri, sokoke_resolve_hostname_cb,
                             &host_resolved))
    {
        GTimer* timer = g_timer_new ();
        while (!host_resolved && g_timer_elapsed (timer, NULL) < 10)
            g_main_context_iteration (NULL, FALSE);
        g_timer_destroy (timer);
    }
    g_free (uri);
    return host_resolved == 1 ? TRUE : FALSE;
}

gboolean
sokoke_external_uri (const gchar* uri)
{
    GAppInfo* info;

    if (!uri || !strncmp (uri, "http", 4)
             || !strncmp (uri, "file", 4)
             || !strncmp (uri, "geo", 3)
             || !strncmp (uri, "about:", 6))
        return FALSE;

    info = sokoke_default_for_uri (uri, NULL);
    if (info)
        g_object_unref (info);
    return info != NULL;
}

/**
 * sokoke_magic_uri:
 * @uri: a string typed by a user
 *
 * Takes a string that was typed by a user,
 * guesses what it is, and returns an URI.
 *
 * If it was a search, %NULL will be returned.
 *
 * Return value: a newly allocated URI, or %NULL
 **/
gchar*
sokoke_magic_uri (const gchar* uri)
{
    gchar** parts;
    gchar* search;

    g_return_val_if_fail (uri, NULL);

    /* Just return if it's a javascript: or mailto: uri */
    if (!strncmp (uri, "javascript:", 11)
     || !strncmp (uri, "mailto:", 7)
     || sokoke_external_uri (uri)
     || !strncmp (uri, "data:", 5)
     || !strncmp (uri, "about:", 6))
        return g_strdup (uri);
    /* Add file:// if we have a local path */
    if (g_path_is_absolute (uri))
        return g_strconcat ("file://", uri, NULL);
    /* Parse geo URI geo:48.202778,16.368472;crs=wgs84;u=40 as a location */
    if (!strncmp (uri, "geo:", 4))
    {
        gchar* comma;
        gchar* semicolon;
        gchar* latitude;
        gchar* longitude;
        gchar* geo;

        comma = strchr (&uri[4], ',');
        /* geo:latitude,longitude[,altitude][;u=u][;crs=crs] */
        if (!(comma && *comma))
            return g_strdup (uri);
        semicolon = strchr (comma + 1, ';');
        if (!semicolon)
            semicolon = strchr (comma + 1, ',');
        latitude = g_strndup (&uri[4], comma - &uri[4]);
        if (semicolon)
            longitude = g_strndup (comma + 1, semicolon - comma - 1);
        else
            longitude = g_strdup (comma + 1);
        geo = g_strdup_printf ("http://www.openstreetmap.org/?mlat=%s&mlon=%s",
            latitude, longitude);
        g_free (latitude);
        g_free (longitude);
        return geo;
    }
    /* Do we have a protocol? */
    if (g_strstr_len (uri, 8, "://"))
        return sokoke_idn_to_punycode (g_strdup (uri));

    /* Do we have an IP address? */
    if (g_ascii_isdigit (uri[0]) && g_strstr_len (uri, 4, "."))
        return g_strconcat ("http://", uri, NULL);
    search = NULL;
    if (!strchr (uri, ' ') &&
        ((search = strchr (uri, ':')) || (search = strchr (uri, '@'))) &&
        search[0] && !g_ascii_isalpha (search[1]))
        return sokoke_idn_to_punycode (g_strconcat ("http://", uri, NULL));
    if ((!strcmp (uri, "localhost") || strchr (uri, '/'))
      && sokoke_resolve_hostname (uri))
        return g_strconcat ("http://", uri, NULL);
    if (!search)
    {
        parts = g_strsplit (uri, ".", 0);
        if (parts[0] && parts[1])
        {
            if (!(parts[1][1] == '\0' && !g_ascii_isalpha (parts[1][0])))
                if (!strchr (parts[0], ' ') && !strchr (parts[1], ' '))
                {
                    search = g_strconcat ("http://", uri, NULL);
                    g_strfreev (parts);
                   return sokoke_idn_to_punycode (search);
                }
        }
        g_strfreev (parts);
    }
    return NULL;
}

/**
 * sokoke_uri_unescape_string:
 * @uri: an URI string
 *
 * Unescape @uri if needed, and pass through '+' and '%20'.
 *
 * Return value: a newly allocated URI
 **/
gchar*
sokoke_uri_unescape_string (const gchar* uri)
{
    if (strchr (uri,'%') || strchr (uri, ' '))
    {
        /* Preserve %20 for pasting URLs into other windows */
        gchar* unescaped = g_uri_unescape_string (uri, "+");
        gchar* spaced = sokoke_replace_variables (unescaped, " ", "%20", NULL);
        g_free (unescaped);
        return spaced;
    }

    return g_strdup (uri);
}

/**
 * sokoke_format_uri_for_display:
 * @uri: an URI string
 *
 * Formats an URI for display, for instance by converting
 * percent encoded characters and by decoding punycode.
 *
 * Return value: a newly allocated URI
 **/
gchar*
sokoke_format_uri_for_display (const gchar* uri)
{
    if (uri && g_str_has_prefix (uri, "http://"))
    {
        gchar* unescaped = sokoke_uri_unescape_string (uri);
        #ifdef HAVE_LIBSOUP_2_27_90
        gchar* path = NULL;
        gchar* hostname;
        gchar* decoded;

        if (!unescaped)
            return g_strdup (uri);
        else if (!g_utf8_validate (unescaped, -1, NULL))
        {
            g_free (unescaped);
            return g_strdup (uri);
        }

        hostname = sokoke_hostname_from_uri (unescaped, &path);
        decoded = g_hostname_to_unicode (hostname);

        if (decoded)
        {
            gchar* result = g_strconcat ("http://", decoded, path, NULL);
            g_free (unescaped);
            g_free (decoded);
            g_free (hostname);
            return result;
        }
        g_free (hostname);
        return unescaped;
        #elif HAVE_LIBIDN
        gchar* decoded;

        if (!unescaped)
            return g_strdup (uri);
        else if (!g_utf8_validate (unescaped, -1, NULL))
        {
            g_free (unescaped);
            return g_strdup (uri);
        }

        if (!idna_to_unicode_8z8z (unescaped, &decoded, 0) == IDNA_SUCCESS)
            return unescaped;
        g_free (unescaped);
        return decoded;
        #else
        return unescaped;
        #endif
    }
    return g_strdup (uri);
}

void
sokoke_combo_box_add_strings (GtkComboBox* combobox,
                              const gchar* label_first, ...)
{
    const gchar* label;

    /* Add a number of strings to a combobox, terminated with NULL
       This works only for text comboboxes */
    va_list args;
    va_start (args, label_first);

    for (label = label_first; label; label = va_arg (args, const gchar*))
        gtk_combo_box_append_text (combobox, label);

    va_end (args);
}

void sokoke_widget_set_visible (GtkWidget* widget, gboolean visible)
{
    /* Show or hide the widget */
    if (visible)
        gtk_widget_show (widget);
    else
        gtk_widget_hide (widget);
}

void
sokoke_container_show_children (GtkContainer* container)
{
    /* Show every child but not the container itself */
    gtk_container_foreach (container, (GtkCallback)(gtk_widget_show_all), NULL);
}

typedef enum
{
    SOKOKE_DESKTOP_UNTESTED,
    SOKOKE_DESKTOP_XFCE,
    SOKOKE_DESKTOP_OSX,
    SOKOKE_DESKTOP_UNKNOWN
} SokokeDesktop;

static SokokeDesktop
sokoke_get_desktop (void)
{
    #if HAVE_OSX
    return SOKOKE_DESKTOP_OSX;
    #elif defined (GDK_WINDOWING_X11)
    static SokokeDesktop desktop = SOKOKE_DESKTOP_UNTESTED;
    if (G_UNLIKELY (desktop == SOKOKE_DESKTOP_UNTESTED))
    {
        desktop = SOKOKE_DESKTOP_UNKNOWN;

        /* Are we running in Xfce >= 4.8? */
        if (!g_strcmp0 (g_getenv ("DESKTOP_SESSION"), "xfce"))
        {
            desktop = SOKOKE_DESKTOP_XFCE;
        }
        else
        {
        /* Are we running in Xfce <= 4.6? */
        GdkDisplay* display = gdk_display_get_default ();
        Display* xdisplay = GDK_DISPLAY_XDISPLAY (display);
        Window root_window = RootWindow (xdisplay, 0);
        Atom save_mode_atom = gdk_x11_get_xatom_by_name_for_display (
            display, "_DT_SAVE_MODE");
        Atom actual_type;
        int actual_format;
        unsigned long n_items, bytes;
        gchar* value;
        int status = XGetWindowProperty (xdisplay, root_window,
            save_mode_atom, 0, (~0L),
            False, AnyPropertyType, &actual_type, &actual_format,
            &n_items, &bytes, (unsigned char**)&value);
        if (status == Success)
        {
            if (n_items == 6 && !strncmp (value, "xfce4", 6))
                desktop = SOKOKE_DESKTOP_XFCE;
            XFree (value);
        }
        }
    }

    return desktop;
    #else
    return SOKOKE_DESKTOP_UNKNOWN;
    #endif
}

/**
 * sokoke_xfce_header_new:
 * @icon: an icon name
 * @title: the title of the header
 *
 * Creates an Xfce style header *if* Xfce is running.
 *
 * Return value: A #GtkWidget or %NULL
 *
 * Since 0.1.2 @icon may be NULL, and a default is used.
 **/
GtkWidget*
sokoke_xfce_header_new (const gchar* icon,
                        const gchar* title)
{
    g_return_val_if_fail (title, NULL);

    /* Create an xfce header with icon and title
       This returns NULL if the desktop is not Xfce */
    if (sokoke_get_desktop () == SOKOKE_DESKTOP_XFCE)
    {
        GtkWidget* entry;
        GtkStyle* style;
        gchar* markup;
        GtkWidget* xfce_heading;
        GtkWidget* hbox;
        GtkWidget* image;
        GtkWidget* label;
        GtkWidget* vbox;
        GtkWidget* separator;

        xfce_heading = gtk_event_box_new ();
        entry = gtk_entry_new ();
        style = gtk_widget_get_style (entry);
        gtk_widget_modify_bg (xfce_heading, GTK_STATE_NORMAL,
            &style->base[GTK_STATE_NORMAL]);
        hbox = gtk_hbox_new (FALSE, 12);
        gtk_container_set_border_width (GTK_CONTAINER (hbox), 6);
        if (icon)
            image = gtk_image_new_from_icon_name (icon, GTK_ICON_SIZE_DIALOG);
        else
            image = gtk_image_new_from_stock (GTK_STOCK_PREFERENCES,
                GTK_ICON_SIZE_DIALOG);
        gtk_box_pack_start (GTK_BOX (hbox), image, FALSE, FALSE, 0);
        label = gtk_label_new (NULL);
        gtk_widget_modify_fg (label, GTK_STATE_NORMAL
         , &style->text[GTK_STATE_NORMAL]);
        markup = g_strdup_printf ("<span size='large' weight='bold'>%s</span>",
                                  title);
        gtk_label_set_markup (GTK_LABEL (label), markup);
        gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
        gtk_container_add (GTK_CONTAINER (xfce_heading), hbox);
        g_free (markup);
        gtk_widget_destroy (entry);

        vbox = gtk_vbox_new (FALSE, 0);
        gtk_box_pack_start (GTK_BOX (vbox), xfce_heading, FALSE, FALSE, 0);

        separator = gtk_hseparator_new ();
        gtk_box_pack_start (GTK_BOX (vbox), separator, FALSE, FALSE, 0);

        return vbox;
    }
    return NULL;
}

void
sokoke_widget_set_pango_font_style (GtkWidget* widget,
                                    PangoStyle style)
{
    /* Conveniently change the pango font style
       For some reason we need to reset if we actually want the normal style */
    if (style == PANGO_STYLE_NORMAL)
        gtk_widget_modify_font (widget, NULL);
    else
    {
        PangoFontDescription* font_description = pango_font_description_new ();
        pango_font_description_set_style (font_description, PANGO_STYLE_ITALIC);
        gtk_widget_modify_font (widget, font_description);
        pango_font_description_free (font_description);
    }
}

static gboolean
sokoke_on_entry_focus_in_event (GtkEntry*      entry,
                                GdkEventFocus* event,
                                gpointer       userdata)
{
    gint has_default = GPOINTER_TO_INT (
        g_object_get_data (G_OBJECT (entry), "sokoke_has_default"));
    if (has_default)
    {
        gtk_entry_set_text (entry, "");
        g_object_set_data (G_OBJECT (entry), "sokoke_has_default",
                           GINT_TO_POINTER (0));
        sokoke_widget_set_pango_font_style (GTK_WIDGET (entry),
                                            PANGO_STYLE_NORMAL);
    }
    return FALSE;
}

static gboolean
sokoke_on_entry_focus_out_event (GtkEntry*      entry,
                                 GdkEventFocus* event,
                                 gpointer       userdata)
{
    const gchar* text = gtk_entry_get_text (entry);
    if (text && !*text)
    {
        const gchar* default_text = (const gchar*)g_object_get_data (
            G_OBJECT (entry), "sokoke_default_text");
        gtk_entry_set_text (entry, default_text);
        g_object_set_data (G_OBJECT (entry),
                           "sokoke_has_default", GINT_TO_POINTER (1));
        sokoke_widget_set_pango_font_style (GTK_WIDGET (entry),
                                            PANGO_STYLE_ITALIC);
    }
    return FALSE;
}

static void
sokoke_on_entry_drag_data_received (GtkEntry*       entry,
                                    GdkDragContext* drag_context,
                                    gint            x,
                                    gint            y,
                                    guint           timestamp,
                                    gpointer        user_data)
{
    sokoke_on_entry_focus_in_event (entry, NULL, NULL);
}

void
sokoke_entry_set_default_text (GtkEntry*    entry,
                               const gchar* default_text)
{
    /* Note: The default text initially overwrites any previous text */
    gchar* old_value = g_object_get_data (G_OBJECT (entry),
                                          "sokoke_default_text");
    if (!old_value)
    {
        g_object_set_data (G_OBJECT (entry), "sokoke_has_default",
                           GINT_TO_POINTER (1));
        sokoke_widget_set_pango_font_style (GTK_WIDGET (entry),
                                            PANGO_STYLE_ITALIC);
        gtk_entry_set_text (entry, default_text);
        g_signal_connect (entry, "drag-data-received",
            G_CALLBACK (sokoke_on_entry_drag_data_received), NULL);
        g_signal_connect (entry, "focus-in-event",
            G_CALLBACK (sokoke_on_entry_focus_in_event), NULL);
        g_signal_connect (entry, "focus-out-event",
           G_CALLBACK (sokoke_on_entry_focus_out_event), NULL);
    }
    else if (!gtk_widget_has_focus (GTK_WIDGET (entry)))
    {
        gint has_default = GPOINTER_TO_INT (
            g_object_get_data (G_OBJECT (entry), "sokoke_has_default"));
        if (has_default)
        {
            gtk_entry_set_text (entry, default_text);
            sokoke_widget_set_pango_font_style (GTK_WIDGET (entry),
                                                PANGO_STYLE_ITALIC);
        }
    }
    g_object_set_data (G_OBJECT (entry), "sokoke_default_text",
                       (gpointer)default_text);
}

gchar*
sokoke_key_file_get_string_default (GKeyFile*    key_file,
                                    const gchar* group,
                                    const gchar* key,
                                    const gchar* default_value,
                                    GError**     error)
{
    gchar* value = g_key_file_get_string (key_file, group, key, error);
    return value == NULL ? g_strdup (default_value) : value;
}

gint
sokoke_key_file_get_integer_default (GKeyFile*    key_file,
                                     const gchar* group,
                                     const gchar* key,
                                     const gint   default_value,
                                     GError**     error)
{
    if (!g_key_file_has_key (key_file, group, key, NULL))
        return default_value;
    return g_key_file_get_integer (key_file, group, key, error);
}

gdouble
sokoke_key_file_get_double_default (GKeyFile*     key_file,
                                    const gchar*  group,
                                    const gchar*  key,
                                    const gdouble default_value,
                                    GError**      error)
{
    if (!g_key_file_has_key (key_file, group, key, NULL))
        return default_value;
    return g_key_file_get_double (key_file, group, key, error);
}

gboolean
sokoke_key_file_get_boolean_default (GKeyFile*      key_file,
                                     const gchar*   group,
                                     const gchar*   key,
                                     const gboolean default_value,
                                     GError**       error)
{
    if (!g_key_file_has_key (key_file, group, key, NULL))
        return default_value;
    return g_key_file_get_boolean (key_file, group, key, error);
}

gchar**
sokoke_key_file_get_string_list_default (GKeyFile*     key_file,
                                         const gchar*  group,
                                         const gchar*  key,
                                         gsize*        length,
                                         gchar**       default_value,
                                         gsize*        default_length,
                                         GError*       error)
{
    gchar** value = g_key_file_get_string_list (key_file, group, key, length, NULL);
    if (!value)
    {
        value = g_strdupv (default_value);
        if (length)
            *length = *default_length;
    }
    return value;
}

gboolean
sokoke_key_file_save_to_file (GKeyFile*    key_file,
                              const gchar* filename,
                              GError**     error)
{
    gchar* data;
    gboolean success = FALSE;

    data = g_key_file_to_data (key_file, NULL, error);
    if (!data)
        return FALSE;

    success = g_file_set_contents (filename, data, -1, error);
    g_free (data);
    return success;
}

void
sokoke_widget_get_text_size (GtkWidget*   widget,
                             const gchar* text,
                             gint*        width,
                             gint*        height)
{
    PangoLayout* layout = gtk_widget_create_pango_layout (widget, text);
    pango_layout_get_pixel_size (layout, width, height);
    g_object_unref (layout);
}

/**
 * sokoke_action_create_popup_menu_item:
 * @action: a #GtkAction
 *
 * Creates a menu item from an action, just like
 * gtk_action_create_menu_item(), but it won't
 * display an accelerator.
 *
 * Note: This menu item is not a proxy and will
 *       not reflect any changes to the action.
 *
 * Return value: a new #GtkMenuItem
 **/
GtkWidget*
sokoke_action_create_popup_menu_item (GtkAction* action)
{
    GtkWidget* menuitem;
    GtkWidget* icon;
    gchar* label;
    gchar* stock_id;
    gchar* icon_name;
    gboolean sensitive;
    gboolean visible;

    g_return_val_if_fail (GTK_IS_ACTION (action), NULL);

    if (KATZE_IS_ARRAY_ACTION (action))
        return gtk_action_create_menu_item (action);

    g_object_get (action,
                  "label", &label,
                  "stock-id", &stock_id,
                  "icon-name", &icon_name,
                  "sensitive", &sensitive,
                  "visible", &visible,
                  NULL);
    if (GTK_IS_TOGGLE_ACTION (action))
    {
        menuitem = gtk_check_menu_item_new_with_mnemonic (label);
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (menuitem),
            gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)));
        if (GTK_IS_RADIO_ACTION (action))
            gtk_check_menu_item_set_draw_as_radio (GTK_CHECK_MENU_ITEM (menuitem),
                                                   TRUE);
    }
    else if (stock_id)
    {
        if (label)
        {
            menuitem = gtk_image_menu_item_new_with_mnemonic (label);
            icon = gtk_image_new_from_stock (stock_id, GTK_ICON_SIZE_MENU);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), icon);
        }
        else
            menuitem = gtk_image_menu_item_new_from_stock (stock_id, NULL);
    }
    else
    {
        menuitem = gtk_image_menu_item_new_with_mnemonic (label);
        if (icon_name)
        {
            icon = gtk_image_new_from_icon_name (icon_name, GTK_ICON_SIZE_MENU);
            gtk_image_menu_item_set_image (GTK_IMAGE_MENU_ITEM (menuitem), icon);
        }
    }
    gtk_widget_set_sensitive (menuitem, sensitive);
    sokoke_widget_set_visible (menuitem, visible);
    gtk_widget_set_no_show_all (menuitem, TRUE);
    g_signal_connect_swapped (menuitem, "activate",
                              G_CALLBACK (gtk_action_activate), action);

    return menuitem;
}

/**
 * sokoke_time_t_to_julian:
 * @timestamp: a time_t timestamp value
 *
 * Calculates a unix timestamp to a julian day value.
 *
 * Return value: an integer.
 **/
gint64
sokoke_time_t_to_julian (const time_t* timestamp)
{
    GDate* date;
    gint64 julian;

    date = g_date_new ();

    g_date_set_time_t (date, *timestamp);
    julian = (gint64)g_date_get_julian (date);

    g_date_free (date);

    return julian;
}

/**
 * sokoke_days_between:
 * @day1: a time_t timestamp value
 * @day2: a time_t timestamp value
 *
 * Calculates the number of days between two timestamps.
 *
 * Return value: an integer.
 **/
gint
sokoke_days_between (const time_t* day1,
                     const time_t* day2)
{
    GDate* date1;
    GDate* date2;
    gint age;

    date1 = g_date_new ();
    date2 = g_date_new ();

    g_date_set_time_t (date1, *day1);
    g_date_set_time_t (date2, *day2);

    age = g_date_days_between (date1, date2);

    g_date_free (date1);
    g_date_free (date2);

    return age;
}

/**
 * sokoke_register_stock_items:
 *
 * Registers several custom stock items used throughout Midori.
 **/
void
sokoke_register_stock_items (void)
{
    GtkIconSource* icon_source;
    GtkIconSet* icon_set;
    GtkIconFactory* factory;
    gsize i;

    typedef struct
    {
        const gchar* stock_id;
        const gchar* label;
        GdkModifierType modifier;
        guint keyval;
        const gchar* fallback;
    } FatStockItem;
    static FatStockItem items[] =
    {
        { STOCK_EXTENSION, NULL, 0, 0, GTK_STOCK_CONVERT },
        { STOCK_IMAGE, NULL, 0, 0, GTK_STOCK_ORIENTATION_PORTRAIT },
        { STOCK_WEB_BROWSER, NULL, 0, 0, "gnome-web-browser" },
        { STOCK_NEWS_FEED, NULL, 0, 0, GTK_STOCK_INDEX },
        { STOCK_SCRIPT, NULL, 0, 0, GTK_STOCK_EXECUTE },
        { STOCK_STYLE, NULL, 0, 0, GTK_STOCK_SELECT_COLOR },
        { STOCK_TRANSFER, NULL, 0, 0, GTK_STOCK_SAVE },

        { STOCK_BOOKMARK,       N_("_Bookmark"), 0, 0, GTK_STOCK_FILE },
        { STOCK_BOOKMARKS,      N_("_Bookmarks"), GDK_CONTROL_MASK | GDK_SHIFT_MASK, GDK_B, GTK_STOCK_DIRECTORY },
        { STOCK_BOOKMARK_ADD,   N_("Add Boo_kmark"), 0, 0, GTK_STOCK_ADD },
        { STOCK_CONSOLE,        N_("_Console"), 0, 0, GTK_STOCK_DIALOG_WARNING },
        { STOCK_EXTENSIONS,     N_("_Extensions"), 0, 0, GTK_STOCK_CONVERT },
        { STOCK_HISTORY,        N_("_History"), GDK_CONTROL_MASK | GDK_SHIFT_MASK, GDK_H, GTK_STOCK_SORT_ASCENDING },
        { STOCK_HOMEPAGE,       N_("_Homepage"), 0, 0, GTK_STOCK_HOME },
        { STOCK_SCRIPTS,        N_("_Userscripts"), 0, 0, GTK_STOCK_EXECUTE },
        { STOCK_TAB_NEW,        N_("New _Tab"), 0, 0, GTK_STOCK_ADD },
        { STOCK_TRANSFERS,      N_("_Transfers"), GDK_CONTROL_MASK | GDK_SHIFT_MASK, GDK_J, GTK_STOCK_SAVE },
        { STOCK_PLUGINS,        N_("Netscape p_lugins"), 0, 0, GTK_STOCK_CONVERT },
        { STOCK_USER_TRASH,     N_("_Closed Tabs"), 0, 0, "gtk-undo-ltr" },
        { STOCK_WINDOW_NEW,     N_("New _Window"), 0, 0, GTK_STOCK_ADD },
        { GTK_STOCK_DIRECTORY,  N_("New _Folder"), 0, 0, NULL },
    };

    factory = gtk_icon_factory_new ();
    for (i = 0; i < G_N_ELEMENTS (items); i++)
    {
        icon_set = gtk_icon_set_new ();
        icon_source = gtk_icon_source_new ();
        if (items[i].fallback)
        {
            gtk_icon_source_set_icon_name (icon_source, items[i].fallback);
            items[i].fallback = NULL;
            gtk_icon_set_add_source (icon_set, icon_source);
        }
        gtk_icon_source_set_icon_name (icon_source, items[i].stock_id);
        gtk_icon_set_add_source (icon_set, icon_source);
        gtk_icon_source_free (icon_source);
        gtk_icon_factory_add (factory, items[i].stock_id, icon_set);
        gtk_icon_set_unref (icon_set);
    }
    gtk_stock_add_static ((GtkStockItem*)items, G_N_ELEMENTS (items));
    gtk_icon_factory_add_default (factory);
    g_object_unref (factory);

    #if HAVE_HILDON
    /* Maemo doesn't theme stock icons. So we map platform icons
        to stock icons. These are all monochrome toolbar icons. */
    typedef struct
    {
        const gchar* stock_id;
        const gchar* icon_name;
    } CompatItem;
    static CompatItem compat_items[] =
    {
        { GTK_STOCK_ADD,        "general_add" },
        { GTK_STOCK_BOLD,       "general_bold" },
        { GTK_STOCK_CLOSE,      "general_close_b" },
        { GTK_STOCK_DELETE,     "general_delete" },
        { GTK_STOCK_DIRECTORY,  "general_toolbar_folder" },
        { GTK_STOCK_FIND,       "general_search" },
        { GTK_STOCK_FULLSCREEN, "general_fullsize_b" },
        { GTK_STOCK_GO_BACK,    "general_back" },
        { GTK_STOCK_GO_FORWARD, "general_forward" },
        { GTK_STOCK_GO_UP,      "filemanager_folder_up" },
        { GTK_STOCK_GOTO_FIRST, "pdf_viewer_first_page" },
        { GTK_STOCK_GOTO_LAST,  "pdf_viewer_last_page" },
        { GTK_STOCK_INFO,       "general_information" },
        { GTK_STOCK_ITALIC,     "general_italic" },
        { GTK_STOCK_JUMP_TO,    "general_move_to_folder" },
        { GTK_STOCK_PREFERENCES,"general_settings" },
        { GTK_STOCK_REFRESH,    "general_refresh" },
        { GTK_STOCK_SAVE,       "notes_save" },
        { GTK_STOCK_STOP,       "general_stop" },
        { GTK_STOCK_UNDERLINE,  "notes_underline" },
        { GTK_STOCK_ZOOM_IN,    "pdf_zoomin" },
        { GTK_STOCK_ZOOM_OUT,   "pdf_zoomout" },
    };

    factory = gtk_icon_factory_new ();
    for (i = 0; i < G_N_ELEMENTS (compat_items); i++)
    {
        icon_set = gtk_icon_set_new ();
        icon_source = gtk_icon_source_new ();
        gtk_icon_source_set_icon_name (icon_source, compat_items[i].icon_name);
        gtk_icon_set_add_source (icon_set, icon_source);
        gtk_icon_source_free (icon_source);
        gtk_icon_factory_add (factory, compat_items[i].stock_id, icon_set);
        gtk_icon_set_unref (icon_set);
    }
    gtk_icon_factory_add_default (factory);
    g_object_unref (factory);
    #endif
}

/**
 * sokoke_set_config_dir:
 * @new_config_dir: an absolute path, or %NULL
 *
 * Retrieves and/ or sets the base configuration folder.
 *
 * "/" means no configuration is saved.
 *
 * Return value: the configuration folder, or %NULL
 **/
const gchar*
sokoke_set_config_dir (const gchar* new_config_dir)
{
    static gchar* config_dir = NULL;

    if (config_dir)
        return config_dir;

    if (!new_config_dir)
        config_dir = g_build_filename (g_get_user_config_dir (),
                                       PACKAGE_NAME, NULL);
    else
    {
        g_return_val_if_fail (g_path_is_absolute (new_config_dir), NULL);
        katze_assign (config_dir, g_strdup (new_config_dir));
    }

    return config_dir;
}

/**
 * sokoke_remove_path:
 * @path: an absolute path
 * @ignore_errors: keep removing even if an error occurred
 *
 * Removes the file at @path or the folder including any
 * child folders and files if @path is a folder.
 *
 * If @ignore_errors is %TRUE and @path is a folder with
 * children, one of which can't be removed, remaining
 * children will be deleted nevertheless
 * If @ignore_errors is %FALSE and @path is a folder, the
 * removal process will cancel immediately.
 *
 * Return value: %TRUE on success, %FALSE if an error occurred
 **/
gboolean
sokoke_remove_path (const gchar* path,
                    gboolean     ignore_errors)
{
    GDir* dir = g_dir_open (path, 0, NULL);
    const gchar* name;

    if (!dir)
        return g_remove (path) == 0;

    while ((name = g_dir_read_name (dir)))
    {
        gchar* sub_path = g_build_filename (path, name, NULL);
        if (!sokoke_remove_path (sub_path, ignore_errors) && !ignore_errors)
            return FALSE;
        g_free (sub_path);
    }

    g_dir_close (dir);
    g_rmdir (path);
    return TRUE;
}

/**
 * sokoke_find_config_filename:
 * @folder: a subfolder
 * @filename: a filename or relative path
 *
 * Looks for the specified filename in the system config
 * directories, depending on the platform.
 *
 * Return value: a full path
 **/
gchar*
sokoke_find_config_filename (const gchar* folder,
                             const gchar* filename)
{
    const gchar* const* config_dirs = g_get_system_config_dirs ();
    guint i = 0;
    const gchar* config_dir;
    gchar* path;

    if (!folder)
        folder = "";

    while ((config_dir = config_dirs[i++]))
    {
        path = g_build_filename (config_dir, PACKAGE_NAME, folder, filename, NULL);
        if (g_access (path, F_OK) == 0)
            return path;
        g_free (path);
    }

    #ifdef G_OS_WIN32
    config_dir = g_win32_get_package_installation_directory_of_module (NULL);
    path = g_build_filename (config_dir, "etc", "xdg", PACKAGE_NAME, folder, filename, NULL);
    if (g_access (path, F_OK) == 0)
        return path;
    g_free (path);
    #endif

    return g_build_filename (SYSCONFDIR, "xdg", PACKAGE_NAME, folder, filename, NULL);
}

/**
 * sokoke_find_data_filename:
 * @filename: a filename or relative path
 *
 * Looks for the specified filename in the system data
 * directories, depending on the platform.
 *
 * Return value: a newly allocated full path
 **/
gchar*
sokoke_find_data_filename (const gchar* filename)
{
    const gchar* const* data_dirs = g_get_system_data_dirs ();
    guint i = 0;
    const gchar* data_dir;
    gchar* path;

    path = g_build_filename (g_get_user_data_dir (), filename, NULL);
    if (g_access (path, F_OK) == 0)
        return path;
    g_free (path);

    while ((data_dir = data_dirs[i++]))
    {
        path = g_build_filename (data_dir, filename, NULL);
        if (g_access (path, F_OK) == 0)
            return path;
        g_free (path);
    }
    return g_build_filename (MDATADIR, filename, NULL);
}

/**
 * sokoke_get_argv:
 * @argument_vector: %NULL
 *
 * Retrieves the argument vector passed at program startup.
 *
 * Return value: the argument vector
 **/
gchar**
sokoke_get_argv (gchar** argument_vector)
{
    static gchar** stored_argv = NULL;

    if (!stored_argv)
        stored_argv = g_strdupv (argument_vector);

    return stored_argv;
}

#if !WEBKIT_CHECK_VERSION (1, 1, 14)
static void
res_server_handler_cb (SoupServer*        res_server,
                       SoupMessage*       msg,
                       const gchar*       path,
                       GHashTable*        query,
                       SoupClientContext* client,
                       gpointer           data)
{
    if (g_str_has_prefix (path, "/res"))
    {
        gchar* filename = g_build_filename ("midori", path, NULL);
        gchar* filepath = sokoke_find_data_filename (filename);
        gchar* contents;
        gsize length;

        g_free (filename);
        if (g_file_get_contents (filepath, &contents, &length, NULL))
        {
            gchar* content_type = g_content_type_guess (filepath, (guchar*)contents,
                                                        length, NULL);
            gchar* mime_type = g_content_type_get_mime_type (content_type);
            g_free (content_type);
            soup_message_set_response (msg, mime_type, SOUP_MEMORY_TAKE,
                                       contents, length);
            g_free (mime_type);
            soup_message_set_status (msg, 200);
        }
        else
            soup_message_set_status (msg, 404);
        g_free (filepath);
    }
    else if (g_str_has_prefix (path, "/stock/"))
    {
        GtkIconTheme* icon_theme = gtk_icon_theme_get_default ();
        const gchar* icon_name = &path[7] ? &path[7] : "";
        gint icon_size = 22;
        GdkPixbuf* icon;
        gchar* contents;
        gsize length;

        if (g_ascii_isalpha (icon_name[0]))
            icon_size = strstr (icon_name, "dialog") ? 48 : 22;
        else if (g_ascii_isdigit (icon_name[0]))
        {
            guint i = 0;
            while (icon_name[i])
                if (icon_name[i++] == '/')
                {
                    gchar* size = g_strndup (icon_name, i - 1);
                    icon_size = atoi (size);
                    g_free (size);
                    icon_name = &icon_name[i];
                }
        }

        icon = gtk_icon_theme_load_icon (icon_theme, icon_name,
            icon_size, 0, NULL);
        if (!icon)
            icon = gtk_icon_theme_load_icon (icon_theme, "gtk-missing-image",
                icon_size, 0, NULL);

        gdk_pixbuf_save_to_buffer (icon, &contents, &length, "png", NULL, NULL);
        g_object_unref (icon);
        soup_message_set_response (msg, "image/png", SOUP_MEMORY_TAKE,
                                   contents, length);
        soup_message_set_status (msg, 200);
    }
    else
    {
        soup_message_set_status (msg, 404);
    }
}

SoupServer*
sokoke_get_res_server (void)
{
    static SoupServer* res_server = NULL;
    SoupAddress* addr = NULL;

    if (G_UNLIKELY (!res_server))
    {
        addr = soup_address_new ("localhost", SOUP_ADDRESS_ANY_PORT);
        soup_address_resolve_sync (addr, NULL);
        res_server = soup_server_new ("interface", addr, NULL);
        g_object_unref (addr);
        soup_server_add_handler (res_server, "/",
                                 res_server_handler_cb, NULL, NULL);
        soup_server_run_async (res_server);
    }

    return res_server;
}
#endif

gchar*
sokoke_replace_variables (const gchar* template,
                          const gchar* variable_first, ...)
{
    gchar* result = g_strdup (template);
    const gchar* variable;

    va_list args;
    va_start (args, variable_first);

    for (variable = variable_first; variable; variable = va_arg (args, const gchar*))
    {
        const gchar* value = va_arg (args, const gchar*);
        GRegex* regex = g_regex_new (variable, 0, 0, NULL);
        gchar* replaced = result;
        result = g_regex_replace_literal (regex, replaced, -1, 0, value, 0, NULL);
        g_free (replaced);
        g_regex_unref (regex);
    }

    va_end (args);

    return result;
}

/**
 * sokoke_window_activate_key:
 * @window: a #GtkWindow
 * @event: a #GdkEventKey
 *
 * Attempts to activate they key from the event, much
 * like gtk_window_activate_key(), including keys
 * that gtk_accelerator_valid() considers invalid.
 *
 * Return value: %TRUE on success
 **/
gboolean
sokoke_window_activate_key (GtkWindow*   window,
                            GdkEventKey* event)
{
    gchar *accel_name;
    GQuark accel_quark;
    GObject* object;
    GSList *slist;

    if (gtk_window_activate_key (window, event))
        return TRUE;

    /* Hack to allow Ctrl + Shift + Tab */
    if (event->keyval == 65056)
        event->keyval = GDK_Tab;

    /* We don't use gtk_accel_groups_activate because it refuses to
        activate anything that gtk_accelerator_valid doesn't like. */
    accel_name = gtk_accelerator_name (event->keyval, (event->state & gtk_accelerator_get_default_mod_mask ()));
    accel_quark = g_quark_from_string (accel_name);
    g_free (accel_name);
    object = G_OBJECT (window);

    for (slist = gtk_accel_groups_from_object (object); slist; slist = slist->next)
        if (gtk_accel_group_activate (slist->data, accel_quark,
                                      object, event->keyval, event->state))
            return TRUE;

    return FALSE;
}

/**
 * sokoke_gtk_action_count_modifiers:
 * @action: a #GtkAction
 *
 * Counts the number of modifiers in the accelerator
 * belonging to the action.
 *
 * Return value: the number of modifiers
 **/
guint
sokoke_gtk_action_count_modifiers (GtkAction* action)
{
    GtkAccelKey key;
    gint mods, cmods = 0;
    const gchar* accel_path;

    g_return_val_if_fail (GTK_IS_ACTION (action), 0);

    accel_path = gtk_action_get_accel_path (action);
    if (accel_path)
        if (gtk_accel_map_lookup_entry (accel_path, &key))
        {
            mods = key.accel_mods;
            while (mods)
            {
                if (1 & mods >> 0)
                    cmods++;
                mods = mods >> 1;
            }
        }
    return cmods;
}

/**
 * sokoke_file_chooser_dialog_new:
 * @title: a window title, or %NULL
 * @window: a parent #GtkWindow, or %NULL
 * @action: a #GtkFileChooserAction
 *
 * Creates a new file chooser dialog, as appropriate for
 * the platform, with buttons according to the @action.
 *
 * The positive response is %GTK_RESPONSE_OK.
 *
 * Return value: a new #GtkFileChooser
 **/
GtkWidget*
sokoke_file_chooser_dialog_new (const gchar*         title,
                                GtkWindow*           window,
                                GtkFileChooserAction action)
{
    const gchar* stock_id = GTK_STOCK_OPEN;
    GtkWidget* dialog;

    if (action == GTK_FILE_CHOOSER_ACTION_SAVE)
        stock_id = GTK_STOCK_SAVE;
    #ifdef HAVE_HILDON_FM
    dialog = hildon_file_chooser_dialog_new (window, action);
    #else
    dialog = gtk_file_chooser_dialog_new (title, window, action,
        GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
        stock_id, GTK_RESPONSE_OK, NULL);
    gtk_window_set_icon_name (GTK_WINDOW (dialog), stock_id);
    #endif
    return dialog;
}

/**
 * sokoke_prefetch_uri:
 * @settings: a #MidoriWebSettings instance, or %NULL
 * @uri: an URI string
 *
 * Attempts to prefetch the specified URI, that is
 * it tries to resolve the hostname in advance.
 *
 * Return value: %TRUE on success
 **/
gboolean
sokoke_prefetch_uri (MidoriWebSettings*  settings,
                     const char*         uri,
                     SoupAddressCallback callback,
                     gpointer            user_data)
{
    #define MAXHOSTS 50
    static gchar* hosts = NULL;
    static gint host_count = G_MAXINT;

    SoupURI* s_uri;

    if (!uri)
        return FALSE;

    if (settings && !katze_object_get_boolean (settings, "enable-dns-prefetching"))
        return FALSE;

    s_uri = soup_uri_new (uri);
    if (!s_uri || !s_uri->host)
        return FALSE;

    #if GLIB_CHECK_VERSION (2, 22, 0)
    if (g_hostname_is_ip_address (s_uri->host))
    #else
    if (g_ascii_isdigit (s_uri->host[0]) && g_strstr_len (s_uri->host, 4, "."))
    #endif
    {
        soup_uri_free (s_uri);
        return FALSE;
    }
    if (!g_str_has_prefix (uri, "http"))
    {
        soup_uri_free (s_uri);
        return FALSE;
    }

    if (!hosts ||
        !g_regex_match_simple (s_uri->host, hosts,
                               G_REGEX_CASELESS, G_REGEX_MATCH_NOTEMPTY))
    {
        SoupAddress* address;
        gchar* new_hosts;

        address = soup_address_new (s_uri->host, SOUP_ADDRESS_ANY_PORT);
        soup_address_resolve_async (address, 0, 0, callback, user_data);
        g_object_unref (address);

        if (host_count > MAXHOSTS)
        {
            katze_assign (hosts, g_strdup (""));
            host_count = 0;
        }
        host_count++;
        new_hosts = g_strdup_printf ("%s|%s", hosts, s_uri->host);
        katze_assign (hosts, new_hosts);
    }
    else if (callback)
        callback (NULL, SOUP_STATUS_OK, user_data);
    soup_uri_free (s_uri);
    return TRUE;
}

/**
 * sokoke_recursive_fork_protection
 * @uri: the URI to check
 * @set_uri: if TRUE the URI will be saved
 *
 * Protects against recursive invokations of the Midori executable
 * with the same URI.
 *
 * As an example, consider having an URI starting with 'tel://'. You
 * could attempt to open it with sokoke_show_uri. In turn, 'exo-open'
 * might be called. Now quite possibly 'exo-open' is unable to handle
 * 'tel://' and might well fall back to 'midori' as default browser.
 *
 * To protect against this scenario, call this function with the
 * URI and %TRUE before calling any external tool.
 * #MidoriApp calls sokoke_recursive_fork_protection() with %FALSE
 * and bails out if %FALSE is returned.
 *
 * Return value: %TRUE if @uri is new, %FALSE on recursion
 **/
gboolean
sokoke_recursive_fork_protection (const gchar* uri,
                                  gboolean     set_uri)
{
    static gchar* fork_uri = NULL;
    if (set_uri)
        katze_assign (fork_uri, g_strdup (uri));
    return g_strcmp0 (fork_uri, uri) == 0 ? FALSE : TRUE;
}

/* Provide a new way for SoupSession to assume an 'Accept-Language'
   string automatically from the return value of g_get_language_names(),
   properly formatted according to RFC2616.
   Copyright (C) 2009 Mario Sanchez Prada <msanchez@igalia.com>
   Copyright (C) 2009 Dan Winship <danw@gnome.org>
   Mostly copied from libSoup 2.29, coding style adjusted */

/* Converts a language in POSIX format and to be RFC2616 compliant    */
/* Based on code from epiphany-webkit (ephy_langs_append_languages()) */
static gchar *
sokoke_posix_lang_to_rfc2616 (const gchar *language)
{
    if (!strchr (language, '.') && !strchr (language, '@') && language[0] != 'C')
        /* change to lowercase and '_' to '-' */
        return g_strdelimit (g_ascii_strdown (language, -1), "_", '-');

    return NULL;
}

/* Adds a quality value to a string (any value between 0 and 1). */
static gchar *
sokoke_add_quality_value (const gchar *str,
                          float        qvalue)
{
    if ((qvalue >= 0.0) && (qvalue <= 1.0))
    {
        int qv_int = (qvalue * 1000 + 0.5);
        return g_strdup_printf ("%s;q=%d.%d",
                                str, (int) (qv_int / 1000), qv_int % 1000);
    }

    return g_strdup (str);
}

/* Returns a RFC2616 compliant languages list from system locales */
gchar *
sokoke_accept_languages (const gchar* const * lang_names)
{
    GArray *langs_garray = NULL;
    char *cur_lang = NULL;
    char *prev_lang = NULL;
    char **langs_array;
    char *langs_str;
    float delta;
    int i, n_lang_names;

    /* Calculate delta for setting the quality values */
    n_lang_names = g_strv_length ((gchar **)lang_names);
    delta = 0.999 / (n_lang_names - 1);

    /* Build the array of languages */
    langs_garray = g_array_new (TRUE, FALSE, sizeof (char*));
    for (i = 0; lang_names[i] != NULL; i++)
    {
        cur_lang = sokoke_posix_lang_to_rfc2616 (lang_names[i]);

        /* Apart from getting a valid RFC2616 compliant
           language, also get rid of extra variants */
        if (cur_lang && (!prev_lang ||
           (!strcmp (prev_lang, cur_lang) || !strstr (prev_lang, cur_lang))))
        {

            gchar *qv_lang = NULL;

            /* Save reference for further comparison */
            prev_lang = cur_lang;

            /* Add the quality value and append it */
            qv_lang = sokoke_add_quality_value (cur_lang, 1 - i * delta);
            g_array_append_val (langs_garray, qv_lang);
        }
    }

    /* Fallback: add "en" if list is empty */
    if (langs_garray->len == 0)
    {
        gchar* fallback = g_strdup ("en");
        g_array_append_val (langs_garray, fallback);
    }

    langs_array = (char **) g_array_free (langs_garray, FALSE);
    langs_str = g_strjoinv (", ", langs_array);

    return langs_str;
}

/**
 * sokoke_register_privacy_item:
 * @name: the name of the privacy item
 * @label: a user visible, localized label
 * @clear: a callback clearing data
 *
 * Registers an item to clear data, either via the
 * Clear Private Data dialogue or when Midori quits.
 *
 * Return value: a #GList if all arguments are %NULL
 **/
GList*
sokoke_register_privacy_item (const gchar* name,
                              const gchar* label,
                              GCallback    clear)
{
    static GList* items = NULL;
    SokokePrivacyItem* item;

    if (name == NULL && label == NULL && clear == NULL)
        return items;

    g_return_val_if_fail (name != NULL, NULL);
    g_return_val_if_fail (label != NULL, NULL);
    g_return_val_if_fail (clear != NULL, NULL);

    item = g_new (SokokePrivacyItem, 1);
    item->name = g_strdup (name);
    item->label = g_strdup (label);
    item->clear = clear;
    items = g_list_append (items, item);
    return NULL;
}

void
sokoke_widget_copy_clipboard (GtkWidget*   widget,
                              const gchar* text)
{
    GdkDisplay* display = gtk_widget_get_display (widget);
    GtkClipboard* clipboard;

    clipboard = gtk_clipboard_get_for_display (display, GDK_SELECTION_CLIPBOARD);
    gtk_clipboard_set_text (clipboard, text, -1);
    clipboard = gtk_clipboard_get_for_display (display, GDK_SELECTION_PRIMARY);
    gtk_clipboard_set_text (clipboard, text, -1);
}

gchar*
sokoke_build_thumbnail_path (const gchar* name)
{
    gchar* path = NULL;
    if (name != NULL)
    {
        gchar* checksum = g_compute_checksum_for_string (G_CHECKSUM_MD5, name, -1);
        gchar* filename = g_strdup_printf ("%s.png", checksum);

        path = g_build_filename (g_get_user_cache_dir (), "midori", "thumbnails",
                                 filename, NULL);

        g_free (filename);
        g_free (checksum);
    }
    return path;
}

gchar*
midori_download_prepare_tooltip_text (WebKitDownload* download)
{
    gdouble* last_time;
    guint64* last_size;
    gint hour = 3600, min = 60;
    gint hours_left, minutes_left, seconds_left;
    guint64 total_size = webkit_download_get_total_size (download);
    guint64 current_size  = webkit_download_get_current_size (download);
    gdouble time_elapsed = webkit_download_get_elapsed_time (download);
    gdouble time_estimated, time_diff;
    gchar* current, *total, *download_speed;
    gchar* hours_str, *minutes_str, *seconds_str;
    GString* tooltip = g_string_new (NULL);

    time_diff = time_elapsed / current_size;
    time_estimated = (total_size - current_size) * time_diff;

    hours_left = time_estimated / hour;
    minutes_left = (time_estimated - (hours_left * hour)) / min;
    seconds_left = (time_estimated - (hours_left * hour) - (minutes_left * min));

    hours_str = g_strdup_printf (ngettext ("%d hour", "%d hours", hours_left), hours_left);
    minutes_str = g_strdup_printf (ngettext ("%d minute", "%d minutes", minutes_left), minutes_left);
    seconds_str = g_strdup_printf (ngettext ("%d second", "%d seconds", seconds_left), seconds_left);

    current = g_format_size_for_display (current_size);
    total = g_format_size_for_display (total_size);
    last_time = g_object_get_data (G_OBJECT (download), "last-time");
    last_size = g_object_get_data (G_OBJECT (download), "last-size");

    /* i18n: Download tooltip (size): 4KB of 43MB */
    g_string_append_printf (tooltip, _("%s of %s"), current, total);
    g_free (current);
    g_free (total);

    if (time_elapsed != *last_time)
        download_speed = g_format_size_for_display (
                (current_size - *last_size) / (time_elapsed - *last_time));
    else
        /* i18n: Unknown number of bytes, used for transfer rate like ?B/s */
        download_speed = g_strdup (_("?B"));

    /* i18n: Download tooltip (transfer rate): (130KB/s) */
    g_string_append_printf (tooltip, _(" (%s/s)"), download_speed);
    g_free (download_speed);

    if (time_estimated > 0)
    {
        gchar* eta;
        if (hours_left > 0)
            eta = g_strdup_printf ("%s, %s", hours_str, minutes_str);
        else if (minutes_left >= 10)
            eta = g_strdup_printf ("%s", minutes_str);
        else if (minutes_left < 10 && minutes_left > 0)
            eta = g_strdup_printf ("%s, %s", minutes_str, seconds_str);
        else if (seconds_left > 0)
            eta = g_strdup_printf ("%s", seconds_str);
        else
            eta = g_strdup ("");
        /* i18n: Download tooltip (estimated time) : - 1 hour, 5 minutes remaning */
        g_string_append_printf (tooltip, _(" - %s remaining"), eta);
        g_free (eta);
    }

    g_free (hours_str);
    g_free (seconds_str);
    g_free (minutes_str);

    if (time_elapsed - *last_time > 5.0)
    {
        *last_time = time_elapsed;
        *last_size = current_size;
    }

    return g_string_free (tooltip, FALSE);
}

