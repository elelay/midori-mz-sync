/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#ifndef DECODE_H
#define DECODE_H

#include <glib.h>

/* digest string using SHA1 and encode the result to base32, yielding a 32 char string
   result must be freed using g_free
 */
char* encode_sha1_base32(const char* email);

/* replace user-friendly 8 and 9 to o and l and others to upper-case in place*/
void from_user_friendly_base32(char* s, gsize len);

/* replace o and l to more user friendly (no confusion with 0 and L) 8 and 9 in place */
void to_user_friendly_base32(char* s, gsize len);

/* encode len bytes of s to hex */
char* hex_encode(const guchar* s, gsize len);

/* decode len chars of s to bytes */
guchar* hex_decode(const char* s, gsize* len);


/* decode len chars of data (base32, uppercase) to bytes */
gsize base32_decode(guint8* dst, gsize size, const char* data, gsize len);
/* encode len chars of data to base32 (not user-friendly base32) */
gsize base32_encode(char* dst, gsize size, const guint8* data, gsize len);

gsize base32_size(gsize len);


#endif

