/*
 Copyright (C) 2012 Eric Le Lay <elelay@macports.org>

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 See the file COPYING for the full license text.
*/

#include <string.h>
#include <stdio.h>

#include <openssl/sha.h>
#include <math.h>

#include "decode.h"

char* encode_sha1_base32(const char* email){
	unsigned char sha1_digest[20+1] = {0};
	char* username;
	char* username_lower;
	gssize username_len;
	
	// first, SHA1 of email
	SHA_CTX sha1_ctx;
	SHA1_Init(&sha1_ctx);
	SHA1_Update(&sha1_ctx,email,strlen(email));
	SHA1_Final(sha1_digest,&sha1_ctx);

	
	username_len = base32_size(20);
	username = g_strnfill(username_len,0);
	
	// then, base32 encode
	g_assert(base32_encode(username, username_len, sha1_digest, 20) == username_len);
	
	username_lower = g_ascii_strdown(username, username_len);
	g_free(username);
	return username_lower;
}

void from_user_friendly_base32(char* s, gsize len){
	int i;
	for(i=0;i<len;i++){
		if(s[i] == '8'){
			s[i] = 'L';
		}else if(s[i] == '9'){
			s[i] = 'O';
		}else{
			s[i] = g_ascii_toupper(s[i]);
		}
	}
}

void to_user_friendly_base32(char* s, gsize len){
	int i;
	for(i=0;i<len;i++){
		if(s[i] == 'l'){
			s[i] = '8';
		}else if(s[i] == 'o'){
			s[i] = '9';
		}
	}
}

char* hex_encode(const guchar* s, gsize len){
	char* dest = g_malloc(len * 2 + 1); // 2 hex per byte
	int i;
	
	for(i=0;i<len;i++){
		//printf("%02x", s[i]);
		sprintf(dest+i*2, "%02x", s[i]);
	}
	//printf("\n");
	dest[len * 2] = '\0';
	return dest;
}

guchar hdecone(const char c){
	switch(c){
	case '0': return 0;
	case '1': return 1;
	case '2': return 2;
	case '3': return 3;
	case '4': return 4;
	case '5': return 5;
	case '6': return 6;
	case '7': return 7;
	case '8': return 8;
	case '9': return 9;
	case 'a': case 'A': return 10;
	case 'b': case 'B': return 11;
	case 'c': case 'C': return 12;
	case 'd': case 'D': return 13;
	case 'e': case 'E': return 14;
	case 'f': case 'F': return 15;
	default: return 255;
	}
}

guchar* hex_decode(const char* s, gsize* len){
	*len = strlen(s) / 2;
	guchar* dest = g_malloc(*len); // 2 hex per byte
	int i;
	
	for(i=0;i<*len;i++){
		guchar h = hdecone(s[i*2]);
		guchar l = hdecone(s[i*2+1]);
		if(h == 255 || l == 255){
			return NULL;
		}else{
			dest[i] = (h << 4) | l;
		}
	}
	return dest;
}
static const char base32_alphabet[33] = {
	'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H',
	'I', 'J', 'K', 'L', 'M', 'N', 'O', 'P',
	'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X',
	'Y', 'Z', '2', '3', '4', '5', '6', '7', '='
};

gsize
base32_encode(char *dst, gsize size, const guint8* data, gsize len)
{
	gsize i = 0;
	char *q = dst;

	while (i < len) {
		gsize available;
		guint8 tmp_in[5];
		guint8 tmp_out[8] = {0,};
		int j;
		available = len - i;
		
		for(j=0;j<5;j++){
			if(j<available){
			tmp_in[j] = data[i++];
			}else{
				tmp_in[j] = 0;
			}
		}
		// fill it bit by bit because I'm not good with hex.
		int ii;
		for(ii = 0;ii < 40 ; ii++){
		    // it's (7 - x) and (5 - x) because it's upper bits first (e.g. when ii=0, write 5th bit of tmp_out[0])
		    tmp_out[ii / 5] = tmp_out[ii / 5] | (((tmp_in[ ii / 8 ] & (0x1 << (7 - (ii % 8)))) >> (7 - (ii % 8)) ) << (4 - (ii % 5)));
		}
		switch(available){
		case 1:
		    tmp_out[2] = 32;
		    tmp_out[3] = 32;
		case 2:
		    tmp_out[4] = 32;
		case 3:
		    tmp_out[5] = 32;
		    tmp_out[6] = 32;
		case 4:
		    tmp_out[7] = 32;
		}
		
		for(ii = 0 ; ii < 8 ; ii++){
		    *(q++) = base32_alphabet[tmp_out[ii]];
		}
		
	}

	return q - dst;
}

unsigned char dc(unsigned char c){
	switch(c){
	case 'A': return 0;
	case 'B': return 1;
	case 'C': return 2;
	case 'D': return 3;
	case 'E': return 4;
	case 'F': return 5;
	case 'G': return 6;
	case 'H': return 7;
	case 'I': return 8;
	case 'J': return 9;
	case 'K': return 10;
	case 'L': return 11;
	case 'M': return 12;
	case 'N': return 13;
	case 'O': return 14;
	case 'P': return 15;
	case 'Q': return 16;
	case 'R': return 17;
	case 'S': return 18;
	case 'T': return 19;
	case 'U': return 20;
	case 'V': return 21;
	case 'W': return 22;
	case 'X': return 23;
	case 'Y': return 24;
	case 'Z': return 25;
	case '2': return 26;
	case '3': return 27;
	case '4': return 28;
	case '5': return 29;
	case '6': return 30;
	case '7': return 31;
	default: return 32;
	}
}

gsize
base32_decode(guint8 *dst, gsize size, const char* data, gsize len)
{
	guint8 *q = dst;
	gsize i;

	i = 0;

	while (i < len) {

		gsize available;
		guint8 tmp_in[5] = {0,};
		guint8 tmp_out[8] = {0,};
		int j;
		available = len - i;
		
		for(j=0;j<8;j++){
			if(j<available){
			    tmp_in[j] = dc(data[i++]);
			    if(tmp_in[j] == 32){
			        available = j;
			        // don't need to read the rest of the data, but have to increment i not to have another cycle
			        i = len;
			        break;
			    }
			}
		}

		
		// fill it bit by bit because I'm not good with hex.
		int bavailable = available >= 8 ? 40 : (available * 5);
		int ii;
		for(ii = 0;ii < bavailable ; ii++){
		    // it's (7 - x) and (5 - x) because it's upper bits first (e.g. when ii=0, write 5th bit of tmp_out[0])
		    tmp_out[ii / 8] = tmp_out[ii / 8] | (((tmp_in[ ii / 5 ] & (0x1 << (4 - (ii % 5)))) >> (4 - (ii % 5)) ) << (7 - (ii % 8)));
		}
		// multiple of 8 bits only, rest was 0 (e.g. 1 byte to encode => 2 letters => available == 2 but I wan't only to output 1 = (2 * 5) / 8)
		available = (bavailable / 8);
		for(ii = 0 ; ii < 5 && ii < available ; ii++){
		    *(q++) = tmp_out[ii];
		}
		
	}

	return q - dst;
}

gsize base32_size(gsize len){
	return (ceil(len / 5) * 8 );
}

