/*
 Copyright (C) 2008-2010 Christian Dywan <christian@twotoasts.de>
 Copyright (C) 2011 Alexander Butenko <a.butenka@gmail.com>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#if HAVE_CONFIG_H
    #include <config.h>
#endif

#include "katze-http-cookies-sqlite.h"

#include <stdlib.h>
#ifdef HAVE_UNISTD_H
    #include <unistd.h>
#endif
#include <glib/gi18n.h>
#include <libsoup/soup.h>
#include <gtk/gtk.h>
#include <glib/gstdio.h>
#include <sqlite3.h>

#define QUERY_ALL "SELECT id, name, value, host, path, expiry, lastAccessed, isSecure, isHttpOnly FROM moz_cookies;"
#define CREATE_TABLE "CREATE TABLE moz_cookies (id INTEGER PRIMARY KEY, name TEXT, value TEXT, host TEXT, path TEXT,expiry INTEGER, lastAccessed INTEGER, isSecure INTEGER, isHttpOnly INTEGER)"
#define QUERY_INSERT "INSERT INTO moz_cookies VALUES(NULL, %Q, %Q, %Q, %Q, %d, NULL, %d, %d);"
#define QUERY_DELETE "DELETE FROM moz_cookies WHERE name=%Q AND host=%Q;"

enum {
    COL_ID,
    COL_NAME,
    COL_VALUE,
    COL_HOST,
    COL_PATH,
    COL_EXPIRY,
    COL_LAST_ACCESS,
    COL_SECURE,
    COL_HTTP_ONLY,
    N_COL,
};

struct _KatzeHttpCookiesSqlite
{
    GObject parent_instance;
    gchar* filename;
    SoupCookieJar* jar;
    sqlite3 *db;
    guint counter;
};

struct _KatzeHttpCookiesSqliteClass
{
    GObjectClass parent_class;
};

static void
katze_http_cookies_sqlite_session_feature_iface_init (SoupSessionFeatureInterface *iface,
                                                      gpointer                     data);

G_DEFINE_TYPE_WITH_CODE (KatzeHttpCookiesSqlite, katze_http_cookies_sqlite, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SOUP_TYPE_SESSION_FEATURE,
                         katze_http_cookies_sqlite_session_feature_iface_init));

/* Cookie jar saving into sqlite database
   Copyright (C) 2008 Diego Escalante Urrelo
   Copyright (C) 2009 Collabora Ltd.
   Mostly copied from libSoup 2.30, coding style retained */

static void
try_create_table (sqlite3 *db)
{
    char *error = NULL;

    if (sqlite3_exec (db, CREATE_TABLE, NULL, NULL, &error)) {
        g_warning ("Failed to execute query: %s", error);
        sqlite3_free (error);
    }
}

static void
exec_query_with_try_create_table (sqlite3*    db,
                                  const char* sql,
                                  int         (*callback)(void*,int,char**,char**),
                                  void        *argument)
{
    char *error = NULL;
    gboolean try_create = TRUE;

try_exec:
    if (sqlite3_exec (db, sql, callback, argument, &error)) {
        if (try_create) {
            try_create = FALSE;
            try_create_table (db);
            sqlite3_free (error);
            error = NULL;
            goto try_exec;
        } else {
            g_warning ("Failed to execute query: %s", error);
            sqlite3_free (error);
        }
    }
}

static int
callback (void *data, int argc, char **argv, char **colname)
{
    SoupCookie *cookie = NULL;
    SoupCookieJar *jar = SOUP_COOKIE_JAR (data);

    char *name, *value, *host, *path;
    gint64 expire_time;
    time_t now;
    int max_age;
    gboolean http_only = FALSE, secure = FALSE;

    now = time (NULL);

    name = argv[COL_NAME];
    value = argv[COL_VALUE];
    host = argv[COL_HOST];
    path = argv[COL_PATH];
    expire_time = g_ascii_strtoull (argv[COL_EXPIRY], NULL, 10);

    if (now >= expire_time)
        return 0;
    max_age = (expire_time - now <= G_MAXINT ? expire_time - now : G_MAXINT);

    http_only = (g_strcmp0 (argv[COL_HTTP_ONLY], "1") == 0);
    secure = (g_strcmp0 (argv[COL_SECURE], "1") == 0);

    cookie = soup_cookie_new (name, value, host, path, max_age);

    if (secure)
        soup_cookie_set_secure (cookie, TRUE);
    if (http_only)
        soup_cookie_set_http_only (cookie, TRUE);

    soup_cookie_jar_add_cookie (jar, cookie);

    return 0;
}

/* Follows sqlite3 convention; returns TRUE on error */
static gboolean
katze_http_cookies_sqlite_open_db (KatzeHttpCookiesSqlite* http_cookies)
{
    char *error = NULL;

   if (sqlite3_open (http_cookies->filename, &http_cookies->db)) {
        sqlite3_close (http_cookies->db);
        g_warning ("Can't open %s", http_cookies->filename);
        return TRUE;
    }

    if (sqlite3_exec (http_cookies->db, "PRAGMA synchronous = OFF; PRAGMA secure_delete = 1;", NULL, NULL, &error)) {
        g_warning ("Failed to execute query: %s", error);
        sqlite3_free (error);
    }

    return FALSE;
}

static void
katze_http_cookies_sqlite_load (KatzeHttpCookiesSqlite* http_cookies)
{
    if (http_cookies->db == NULL) {
        if (katze_http_cookies_sqlite_open_db (http_cookies))
            return;
    }

    exec_query_with_try_create_table (http_cookies->db, QUERY_ALL, callback, http_cookies->jar);
}
static void
katze_http_cookies_sqlite_jar_changed_cb (SoupCookieJar*    jar,
                                          SoupCookie*       old_cookie,
                                          SoupCookie*       new_cookie,
                                          KatzeHttpCookiesSqlite* http_cookies)
{
    GObject* settings;
    char *query;
    time_t expires = 0; /* Avoid warning */

    if (http_cookies->db == NULL) {
        if (katze_http_cookies_sqlite_open_db (http_cookies))
            return;
    }

    if (new_cookie && new_cookie->expires)
    {
        gint age;

        expires = soup_date_to_time_t (new_cookie->expires);
        settings = g_object_get_data (G_OBJECT (jar), "midori-settings");
        age = katze_object_get_int (settings, "maximum-cookie-age");
        if (age > 0)
        {
            SoupDate* max_date = soup_date_new_from_now (
                   age * SOUP_COOKIE_MAX_AGE_ONE_DAY);
            if (soup_date_to_time_t (new_cookie->expires)
                > soup_date_to_time_t (max_date))
                   soup_cookie_set_expires (new_cookie, max_date);
        }
        else
        {
            /* An age of 0 to SoupCookie means already-expired
            A user choosing 0 days probably expects 1 hour. */
            soup_cookie_set_max_age (new_cookie, SOUP_COOKIE_MAX_AGE_ONE_HOUR);
        }
    }

    if (g_getenv ("MIDORI_COOKIES_DEBUG") != NULL)
        http_cookies->counter++;

    if (old_cookie) {
        query = sqlite3_mprintf (QUERY_DELETE,
                     old_cookie->name,
                     old_cookie->domain);
        exec_query_with_try_create_table (http_cookies->db, query, NULL, NULL);
        sqlite3_free (query);
    }

    if (new_cookie && new_cookie->expires) {

        query = sqlite3_mprintf (QUERY_INSERT,
                     new_cookie->name,
                     new_cookie->value,
                     new_cookie->domain,
                     new_cookie->path,
                     expires,
                     new_cookie->secure,
                     new_cookie->http_only);
        exec_query_with_try_create_table (http_cookies->db, query, NULL, NULL);
        sqlite3_free (query);
    }
}

static void
katze_http_cookies_sqlite_attach (SoupSessionFeature* feature,
                                  SoupSession*        session)
{
    KatzeHttpCookiesSqlite* http_cookies = (KatzeHttpCookiesSqlite*)feature;
    const gchar* filename = g_object_get_data (G_OBJECT (feature), "filename");
    SoupSessionFeature* jar = soup_session_get_feature (session, SOUP_TYPE_COOKIE_JAR);
    g_return_if_fail (jar != NULL);
    g_return_if_fail (filename != NULL);
    katze_assign (http_cookies->filename, g_strdup (filename));
    http_cookies->jar = g_object_ref (jar);
    katze_http_cookies_sqlite_open_db (http_cookies);
    katze_http_cookies_sqlite_load (http_cookies);
    g_signal_connect (jar, "changed",
        G_CALLBACK (katze_http_cookies_sqlite_jar_changed_cb), feature);

}

static void
katze_http_cookies_sqlite_detach (SoupSessionFeature* feature,
                                  SoupSession*        session)
{
    KatzeHttpCookiesSqlite* http_cookies = (KatzeHttpCookiesSqlite*)feature;
    katze_assign (http_cookies->filename, NULL);
    katze_object_assign (http_cookies->jar, NULL);
    sqlite3_close (http_cookies->db);
}

static void
katze_http_cookies_sqlite_session_feature_iface_init (SoupSessionFeatureInterface *iface,
                                                      gpointer                     data)
{
    iface->attach = katze_http_cookies_sqlite_attach;
    iface->detach = katze_http_cookies_sqlite_detach;
}

static void
katze_http_cookies_sqlite_finalize (GObject* object)
{
    katze_http_cookies_sqlite_detach ((SoupSessionFeature*)object, NULL);
}

static void
katze_http_cookies_sqlite_class_init (KatzeHttpCookiesSqliteClass* class)
{
    GObjectClass* gobject_class = (GObjectClass*)class;
    gobject_class->finalize = katze_http_cookies_sqlite_finalize;
}

static void
katze_http_cookies_sqlite_init (KatzeHttpCookiesSqlite* http_cookies)
{
    http_cookies->filename = NULL;
    http_cookies->jar = NULL;
    http_cookies->db = NULL;
    http_cookies->counter = 0;
}
