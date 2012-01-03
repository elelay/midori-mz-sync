/*
 Copyright (C) 2007-2009 Christian Dywan <christian@twotoasts.de>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#ifndef __MIDORI_ARRAY_H__
#define __MIDORI_ARRAY_H__ 1

#include <sqlite3.h>
#include <katze/katze.h>

gboolean
midori_array_from_file (KatzeArray*  array,
                        const gchar* filename,
                        const gchar* format,
                        GError**     error);

gboolean
midori_array_to_file   (KatzeArray*  array,
                        const gchar* filename,
                        const gchar* format,
                        GError**     error);

KatzeArray*
katze_array_from_statement (sqlite3_stmt* stmt);

KatzeArray*
midori_array_query     (KatzeArray*  array,
                        const gchar* fields,
                        const gchar* condition,
                        const gchar* value);

KatzeArray*
midori_array_query_recursive (KatzeArray*  array,
                              const gchar* fields,
                              const gchar* condition,
                              const gchar* value,
                              gboolean     recursive);

KatzeArray*
katze_array_from_sqlite (sqlite3*     db,
                         const gchar* sqlcmd);

#endif /* !__MIDORI_ARRAY_H__ */
