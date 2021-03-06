/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */

/*
 *  GThumb
 *
 *  Copyright (C) 2001-2008 Free Software Foundation, Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <gio/gio.h>
#include "glib-utils.h"

#define MAX_PATTERNS 128
#define BUFFER_SIZE_FOR_SNIFFING 32


/* gobject utils*/


gpointer
_g_object_ref (gpointer object)
{
	if (object != NULL)
		return g_object_ref (object);
	else
		return NULL;
}


void
_g_object_unref (gpointer object)
{
	if (object != NULL)
		g_object_unref (object);
}


void
_g_clear_object (gpointer p)
{
	gpointer *object_p = (gpointer *) p;

	if ((object_p != NULL) && (*object_p != NULL)) {
		g_object_unref (*object_p);
		*object_p = NULL;
	}
}


GList *
_g_object_list_ref (GList *list)
{
	GList *new_list;

	if (list == NULL)
		return NULL;

	new_list = g_list_copy (list);
	g_list_foreach (new_list, (GFunc) g_object_ref, NULL);

	return new_list;
}


void
_g_object_list_unref (GList *list)
{
	g_list_foreach (list, (GFunc) _g_object_unref, NULL);
	g_list_free (list);
}


typedef GList GObjectList;


G_DEFINE_BOXED_TYPE (GObjectList,
		     g_object_list,
		     (GBoxedCopyFunc) _g_object_list_ref,
		     (GBoxedFreeFunc) _g_object_list_unref)


GEnumValue *
_g_enum_type_get_value (GType enum_type,
			int   value)
{
	GEnumClass *class;
	GEnumValue *enum_value;

	class = G_ENUM_CLASS (g_type_class_ref (enum_type));
	enum_value = g_enum_get_value (class, value);
	g_type_class_unref (class);

	return enum_value;
}


GEnumValue *
_g_enum_type_get_value_by_nick (GType       enum_type,
				const char *nick)
{
	GEnumClass *class;
	GEnumValue *enum_value;

	class = G_ENUM_CLASS (g_type_class_ref (enum_type));
	enum_value = g_enum_get_value_by_nick (class, nick);
	g_type_class_unref (class);

	return enum_value;
}


/* idle callback */


IdleCall*
idle_call_new (DataFunc func,
	       gpointer data)
{
	IdleCall *call = g_new0 (IdleCall, 1);
	call->func = func;
	call->data = data;
	return call;
}


void
idle_call_free (IdleCall *call)
{
	g_free (call);
}


static gboolean
idle_call_exec_cb (gpointer data)
{
	IdleCall *call = data;
	(*call->func) (call->data);
	return FALSE;
}


guint
idle_call_exec (IdleCall *call,
		gboolean  use_idle_cb)
{
	if (use_idle_cb)
		return g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
					idle_call_exec_cb,
					call,
					(GDestroyNotify) idle_call_free);
	else {
		(*call->func) (call->data);
		idle_call_free (call);
		return 0;
	}
}


guint
call_when_idle (DataFunc  func,
		gpointer  data)
{
	return idle_call_exec (idle_call_new (func, data), TRUE);
}


typedef struct {
	gpointer       object;
	ReadyCallback  ready_func;
	gpointer       user_data;
	GError        *error;
	guint          id;
} ObjectReadyData;


static gboolean
exec_object_ready_func (gpointer user_data)
{
	ObjectReadyData *data = user_data;

	g_source_remove (data->id);
	data->ready_func (G_OBJECT (data->object), data->error, data->user_data);
	g_free (data);

	return FALSE;
}


void
object_ready_with_error (gpointer       object,
			 ReadyCallback  ready_func,
			 gpointer       user_data,
			 GError        *error)
{
	ObjectReadyData *data;

	data = g_new0 (ObjectReadyData, 1);

	data->object = object;
	data->ready_func = ready_func;
	data->user_data = user_data;
	data->error = error;
	data->id = g_idle_add (exec_object_ready_func, data);
}


typedef struct {
	ReadyFunc  ready_func;
	gpointer   user_data;
	GError    *error;
	guint      id;
} ReadyData;


static gboolean
exec_ready_func (gpointer user_data)
{
	ReadyData *data = user_data;

	g_source_remove (data->id);
	data->ready_func (data->error, data->user_data);
	g_free (data);

	return FALSE;
}


void
ready_with_error (ReadyFunc  ready_func,
		  gpointer   user_data,
		  GError    *error)
{
	ReadyData *data;

	data = g_new0 (ReadyData, 1);
	data->ready_func = ready_func;
	data->user_data = user_data;
	data->error = error;
	data->id = g_idle_add (exec_ready_func, data);
}


/* debug */


void
debug (const char *file,
       int         line,
       const char *function,
       const char *format,
       ...)
{
#ifdef DEBUG
	static   gboolean first_time = 0;
	static   gboolean print_debug_info = 0;
	va_list  args;
	char    *str;

	if (! first_time) {
		first_time = 1;
		if (g_getenv ("GTHUMB_DEBUG"))
			print_debug_info = 1;
	}

	if (! print_debug_info)
		return;

	g_return_if_fail (format != NULL);

	va_start (args, format);
	str = g_strdup_vprintf (format, args);
	va_end (args);

	g_fprintf (stderr, "[%s] %s:%d (%s):\n\t%s\n", g_get_prgname(), file, line, function, str);

	g_free (str);
#endif
}


void
performance (const char *file,
	     int         line,
	     const char *function,
	     const char *format,
	     ...)
{
#ifdef DEBUG
	va_list args;
	char *formatted, *str, *filename;

	filename = g_path_get_basename (file);

	va_start (args, format);
	formatted = g_strdup_vprintf (format, args);
	va_end (args);

	str = g_strdup_printf ("MARK: %s: (%s:%d) [%s] : %s", g_get_prgname(), filename, line, function, formatted);
	g_free (formatted);

	access (str, F_OK);
	g_free (str);
	g_free (filename);
#endif
}


/* GTimeVal utils */


/* taken from the glib function g_date_strftime */
char *
struct_tm_strftime (struct tm   *tm,
		    const char  *format)
{
	gsize   locale_format_len = 0;
	char   *locale_format;
	GError *error = NULL;
	gsize   tmpbufsize;
	char   *tmpbuf;
	gsize   tmplen;
	char   *retval;

	locale_format = g_locale_from_utf8 (format, -1, NULL, &locale_format_len, &error);
	if (error != NULL) {
		g_warning (G_STRLOC "Error converting format to locale encoding: %s\n", error->message);
		g_error_free (error);
		return NULL;
	}

	tmpbufsize = MAX (128, locale_format_len * 2);
	while (TRUE) {
		tmpbuf = g_malloc (tmpbufsize);

		/* Set the first byte to something other than '\0', to be able to
		 * recognize whether strftime actually failed or just returned "".
		 */
		tmpbuf[0] = '\1';
		tmplen = strftime (tmpbuf, tmpbufsize, locale_format, tm);

		if ((tmplen == 0) && (tmpbuf[0] != '\0')) {
			g_free (tmpbuf);
			tmpbufsize *= 2;

			if (tmpbufsize > 65536) {
				g_warning (G_STRLOC "Maximum buffer size for gth_datetime_strftime exceeded: giving up\n");
				g_free (locale_format);
				return NULL;
			}
		}
		else
			break;
	}
	g_free (locale_format);

	retval = g_locale_to_utf8 (tmpbuf, tmplen, NULL, NULL, &error);
	if (error != NULL) {
		g_warning (G_STRLOC "Error converting results of strftime to UTF-8: %s\n", error->message);
		g_error_free (error);
		return NULL;
	}

	g_free (tmpbuf);

	return retval;
}


int
_g_time_val_cmp (GTimeVal *a,
		 GTimeVal *b)
{
	if (a->tv_sec == b->tv_sec) {
		if (a->tv_usec == b->tv_usec)
			return 0;
		else
			return a->tv_usec > b->tv_usec ? 1 : -1;
	}
	else if (a->tv_sec > b->tv_sec)
		return 1;
	else
		return -1;
}


void
_g_time_val_reset (GTimeVal *time_)
{
	time_->tv_sec = 0;
	time_->tv_usec = 0;
}


gboolean
_g_time_val_from_exif_date (const char *exif_date,
			    GTimeVal   *time_)
{
	struct tm tm;
	long   val;

	g_return_val_if_fail (time_ != NULL, FALSE);

	if (exif_date == NULL)
		return FALSE;

	while (g_ascii_isspace (*exif_date))
		exif_date++;

	if (*exif_date == '\0')
		return FALSE;

	if (! g_ascii_isdigit (*exif_date))
		return FALSE;

	tm.tm_isdst = -1;

	/* YYYY */

	val = g_ascii_strtoull (exif_date, (char **)&exif_date, 10);
	tm.tm_year = val - 1900;

	if (*exif_date != ':')
		return FALSE;

	/* MM */

	exif_date++;
	tm.tm_mon = g_ascii_strtoull (exif_date, (char **)&exif_date, 10) - 1;

	if (*exif_date != ':')
		return FALSE;

	/* DD */

	exif_date++;
	tm.tm_mday = g_ascii_strtoull (exif_date, (char **)&exif_date, 10);

  	if (*exif_date != ' ')
		return FALSE;

  	/* hh */

  	val = g_ascii_strtoull (exif_date, (char **)&exif_date, 10);
  	tm.tm_hour = val;

  	if (*exif_date != ':')
		return FALSE;

  	/* mm */

	exif_date++;
	tm.tm_min = g_ascii_strtoull (exif_date, (char **)&exif_date, 10);

	if (*exif_date != ':')
		return FALSE;

      	/* ss */

	exif_date++;
	tm.tm_sec = strtoul (exif_date, (char **)&exif_date, 10);

	time_->tv_sec = mktime (&tm);
	time_->tv_usec = 0;

	/* usec */

	if ((*exif_date == ',') || (*exif_date == '.')) {
		glong mul = 100000;

		while (g_ascii_isdigit (*++exif_date)) {
			time_->tv_usec += (*exif_date - '0') * mul;
			mul /= 10;
		}
	}

	while (g_ascii_isspace (*exif_date))
		exif_date++;

	return *exif_date == '\0';
}


char *
_g_time_val_to_exif_date (GTimeVal *time_)
{
	char      *retval;
	struct tm *tm;
	time_t     secs;

	g_return_val_if_fail (time_->tv_usec >= 0 && time_->tv_usec < G_USEC_PER_SEC, NULL);

	secs = time_->tv_sec;
	tm = localtime (&secs);
	retval = g_strdup_printf ("%4d:%02d:%02d %02d:%02d:%02d",
				  tm->tm_year + 1900,
				  tm->tm_mon + 1,
				  tm->tm_mday,
				  tm->tm_hour,
				  tm->tm_min,
				  tm->tm_sec);

	return retval;
}


static int
_g_time_get_timezone_offset (struct tm *tm)
{
	int offset = 0;

#if defined (HAVE_TM_GMTOFF)
	offset = tm->tm_gmtoff;
#elif defined (HAVE_TIMEZONE)
	if (tm->tm_isdst > 0) {
  #if defined (HAVE_ALTZONE)
		offset = -altzone;
  #else /* !defined (HAVE_ALTZONE) */
		offset = -timezone + 3600;
  #endif
	} else
		offset = -timezone;
#endif

	return offset;
}


char *
_g_time_val_to_xmp_date (GTimeVal *time_)
{
	time_t     secs;
	struct tm *tm;
	int        offset;
	char      *retval;

	g_return_val_if_fail (time_->tv_usec >= 0 && time_->tv_usec < G_USEC_PER_SEC, NULL);

	secs = time_->tv_sec;
	tm = localtime (&secs);
	offset = _g_time_get_timezone_offset (tm);
	retval = g_strdup_printf ("%4d-%02d-%02dT%02d:%02d:%02d%+03d:%02d",
				  tm->tm_year + 1900,
				  tm->tm_mon + 1,
				  tm->tm_mday,
				  tm->tm_hour,
				  tm->tm_min,
				  tm->tm_sec,
				  offset / 3600,
				  offset % 3600);

	return retval;
}


char *
_g_time_val_strftime (GTimeVal   *time_,
		      const char *format)
{
	time_t     secs;
	struct tm *tm;

	if ((format == NULL) || (*format == '\0'))
		format = DEFAULT_STRFTIME_FORMAT;

	if (strcmp (format, "%q") == 0)
		format = "%Y-%m-%d";

	secs = time_->tv_sec;
	tm = localtime (&secs);
	return struct_tm_strftime (tm, format);
}


/* Bookmark file utils */


void
_g_bookmark_file_clear (GBookmarkFile *bookmark)
{
	char **uris;
	int    i;

	uris = g_bookmark_file_get_uris (bookmark, NULL);
	for (i = 0; uris[i] != NULL; i++)
		g_bookmark_file_remove_item (bookmark, uris[i], NULL);
	g_strfreev (uris);
}


void
_g_bookmark_file_add_uri (GBookmarkFile *bookmark,
			  const char    *uri)
{
	g_bookmark_file_set_is_private (bookmark, uri, TRUE);
	g_bookmark_file_add_application (bookmark, uri, NULL, NULL);
}


void
_g_bookmark_file_set_uris (GBookmarkFile *bookmark,
			   GList         *uri_list)
{
	GList *scan;

	_g_bookmark_file_clear (bookmark);
	for (scan = uri_list; scan; scan = scan->next)
		_g_bookmark_file_add_uri (bookmark, scan->data);
}


/* String utils */


void
_g_strset (char       **s,
	   const char  *value)
{
	if (*s == value)
		return;

	if (*s != NULL) {
		g_free (*s);
		*s = NULL;
	}

	if (value != NULL)
		*s = g_strdup (value);
}


char *
_g_strdup_with_max_size (const char *s,
			 int         max_size)
{
	char *result;
	int   l = strlen (s);

	if (l > max_size) {
		char *first_half;
		char *second_half;
		int   offset;
		int   half_max_size = max_size / 2 + 1;

		first_half = g_strndup (s, half_max_size);
		offset = half_max_size + l - max_size;
		second_half = g_strndup (s + offset, half_max_size);

		result = g_strconcat (first_half, "…", second_half, NULL);

		g_free (first_half);
		g_free (second_half);
	} else
		result = g_strdup (s);

	return result;
}


/**
 * example 1 : "xxx##yy#" --> [0] = xxx
 *                            [1] = ##
 *                            [2] = yy
 *                            [3] = #
 *                            [4] = NULL
 *
 * example 2 : ""         --> [0] = NULL
 **/
char **
_g_get_template_from_text (const char *utf8_template)
{
	const char  *chunk_start = utf8_template;
	char       **str_vect;
	GList       *str_list = NULL, *scan;
	int          n = 0;

	if (utf8_template == NULL)
		return NULL;

	while (*chunk_start != 0) {
		gunichar    ch;
		gboolean    reading_sharps;
		char       *chunk;
		const char *chunk_end;
		int         chunk_len = 0;

		reading_sharps = (g_utf8_get_char (chunk_start) == '#');
		chunk_end = chunk_start;

		ch = g_utf8_get_char (chunk_end);
		while (reading_sharps
		       && (*chunk_end != 0)
		       && (ch == '#')) {
			chunk_end = g_utf8_next_char (chunk_end);
			ch = g_utf8_get_char (chunk_end);
			chunk_len++;
		}

		ch = g_utf8_get_char (chunk_end);
		while (! reading_sharps
		       && (*chunk_end != 0)
		       && (*chunk_end != '#')) {
			chunk_end = g_utf8_next_char (chunk_end);
			ch = g_utf8_get_char (chunk_end);
			chunk_len++;
		}

		chunk = _g_utf8_strndup (chunk_start, chunk_len);
		str_list = g_list_prepend (str_list, chunk);
		n++;

		chunk_start = chunk_end;
	}

	str_vect = g_new (char*, n + 1);

	str_vect[n--] = NULL;
	for (scan = str_list; scan; scan = scan->next)
		str_vect[n--] = scan->data;

	g_list_free (str_list);

	return str_vect;
}


char *
_g_get_name_from_template (char **utf8_template,
			   int    n)
{
	GString *s;
	int      i;
	char    *result;

	s = g_string_new (NULL);

	for (i = 0; utf8_template[i] != NULL; i++) {
		const char *chunk = utf8_template[i];
		gunichar    ch = g_utf8_get_char (chunk);

		if (ch != '#')
			g_string_append (s, chunk);
		else {
			char *s_n;
			int   s_n_len;
			int   sharps_len = g_utf8_strlen (chunk, -1);

			s_n = g_strdup_printf ("%d", n);
			s_n_len = strlen (s_n);

			while (s_n_len < sharps_len) {
				g_string_append_c (s, '0');
				sharps_len--;
			}

			g_string_append (s, s_n);
			g_free (s_n);
		}
	}

	result = s->str;
	g_string_free (s, FALSE);

	return result;
}


char *
_g_replace (const char *str,
	    const char *from_str,
	    const char *to_str)
{
	char    **tokens;
	int       i;
	GString  *gstr;

	if (str == NULL)
		return NULL;

	if (from_str == NULL)
		return g_strdup (str);

	if (strcmp (str, from_str) == 0)
		return g_strdup (to_str);

	tokens = g_strsplit (str, from_str, -1);

	gstr = g_string_new (NULL);
	for (i = 0; tokens[i] != NULL; i++) {
		g_string_append (gstr, tokens[i]);
		if ((to_str != NULL) && (tokens[i+1] != NULL))
			g_string_append (gstr, to_str);
	}

	g_strfreev (tokens);

	return g_string_free (gstr, FALSE);
}


char *
_g_replace_pattern (const char *utf8_text,
		    gunichar    pattern,
		    const char *value)
{
	const char *s;
	GString    *r;
	char       *r_str;

	if (utf8_text == NULL)
		return NULL;

	if (g_utf8_strchr (utf8_text, -1, '%') == NULL)
		return g_strdup (utf8_text);

	r = g_string_new (NULL);
	for (s = utf8_text; *s != 0; s = g_utf8_next_char (s)) {
		gunichar ch = g_utf8_get_char (s);

		if (ch == '%') {
			s = g_utf8_next_char (s);

			if (*s == 0) {
				g_string_append_unichar (r, ch);
				break;
			}

			ch = g_utf8_get_char (s);
			if (ch == pattern) {
				if (value)
					g_string_append (r, value);
			}
			else {
				g_string_append (r, "%");
				g_string_append_unichar (r, ch);
			}

		} else
			g_string_append_unichar (r, ch);
	}

	r_str = r->str;
	g_string_free (r, FALSE);

	return r_str;
}


int
_g_utf8_first_ascii_space (const char *str)
{
	const char *pos;

	pos = str;
	while (pos != NULL) {
		gunichar c = g_utf8_get_char (pos);
		if (c == 0)
			break;
		if (g_ascii_isspace (c))
			return g_utf8_pointer_to_offset (str, pos);
		pos = g_utf8_next_char (pos);
	}

	return -1;
}


gboolean
_g_utf8_has_prefix (const char  *string,
		    const char  *prefix)
{
	char     *substring;
	gboolean  result;

	if (string == NULL)
		return FALSE;
	if (prefix == NULL)
		return TRUE;

	substring = g_utf8_substring (string, 0, g_utf8_strlen (prefix, -1));
	if (substring == NULL)
		return FALSE;

	result = g_utf8_collate (substring, prefix) == 0;
	g_free (substring);

	return result;
}


char *
_g_utf8_remove_prefix (const char *string,
		       int         prefix_length)
{
	int str_length;

	str_length = g_utf8_strlen (string, -1);
	if (str_length <= prefix_length)
		return NULL;

	return g_utf8_substring (string, prefix_length, str_length);
}


char *
_g_utf8_replace (const char  *string,
		 const char  *pattern,
		 const char  *replacement)
{
	GRegex *regex;
	char   *result;

	if (string == NULL)
		return NULL;

	regex = g_regex_new (pattern, 0, 0, NULL);
	if (regex == NULL)
		return NULL;

	result = g_regex_replace_literal (regex, string, -1, 0, replacement, 0, NULL);

	g_regex_unref (regex);

	return result;
}


char *
_g_utf8_strndup (const char *str,
		 gsize       n)
{
	const char *s = str;
	char       *result;

	while (n && *s) {
		s = g_utf8_next_char (s);
		n--;
	}

	result = g_strndup (str, s - str);

	return result;
}


const char *
_g_utf8_strstr (const char *haystack,
		const char *needle)
{
	const char *s;
	glong       i;
	glong       haystack_len = g_utf8_strlen (haystack, -1);
	glong       needle_len = g_utf8_strlen (needle, -1);
	int         needle_size = strlen (needle);

	s = haystack;
	for (i = 0; i <= haystack_len - needle_len; i++) {
		if (strncmp (s, needle, needle_size) == 0)
			return s;
		s = g_utf8_next_char(s);
	}

	return NULL;
}


char **
_g_utf8_strsplit (const char *string,
		  const char *delimiter,
		  int         max_tokens)
{
	GSList      *string_list = NULL, *slist;
	char       **str_array;
	const char  *s;
	guint        n = 0;
	const char  *remainder;

	g_return_val_if_fail (string != NULL, NULL);
	g_return_val_if_fail (delimiter != NULL, NULL);
	g_return_val_if_fail (delimiter[0] != '\0', NULL);

	if (max_tokens < 1)
		max_tokens = G_MAXINT;

	remainder = string;
	s = _g_utf8_strstr (remainder, delimiter);
	if (s != NULL) {
		gsize delimiter_size = strlen (delimiter);

		while (--max_tokens && (s != NULL)) {
			gsize  size = s - remainder;
			char  *new_string;

			new_string = g_new (char, size + 1);
			strncpy (new_string, remainder, size);
			new_string[size] = 0;

			string_list = g_slist_prepend (string_list, new_string);
			n++;
			remainder = s + delimiter_size;
			s = _g_utf8_strstr (remainder, delimiter);
		}
	}
	if (*string) {
		n++;
		string_list = g_slist_prepend (string_list, g_strdup (remainder));
	}

	str_array = g_new (char*, n + 1);

	str_array[n--] = NULL;
	for (slist = string_list; slist; slist = slist->next)
		str_array[n--] = slist->data;

	g_slist_free (string_list);

	return str_array;
}


char *
_g_utf8_strstrip (const char *str)
{
	if (str == NULL)
		return NULL;
	return g_strstrip (g_strdup (str));
}


gboolean
_g_utf8_all_spaces (const char *utf8_string)
{
	gunichar c;

	if (utf8_string == NULL)
		return TRUE;

	c = g_utf8_get_char (utf8_string);
	while (c != 0) {
		if (! g_unichar_isspace (c))
			return FALSE;
		utf8_string = g_utf8_next_char (utf8_string);
		c = g_utf8_get_char (utf8_string);
	}

	return TRUE;
}


char *
_g_utf8_remove_extension (const char *str)
{
	char *p;
	char *ext;
	char *dest;

	if ((str == NULL) || ! g_utf8_validate (str, -1, NULL))
		return NULL;

	p = (char *) str;
	ext = g_utf8_strrchr (p, -1, g_utf8_get_char ("."));
	dest = g_strdup (p);
	g_utf8_strncpy (dest, p, g_utf8_strlen (p, -1) - g_utf8_strlen (ext, -1));

	return dest;
}


char *
_g_utf8_try_from_any (const char *str)
{
	char *utf8_str;

	if (str == NULL)
		return NULL;

	if (! g_utf8_validate (str, -1, NULL))
		utf8_str = g_locale_to_utf8 (str, -1, NULL, NULL, NULL);
	else
		utf8_str = g_strdup (str);

	return utf8_str;
}


char *
_g_utf8_from_any (const char *str)
{
	char *utf8_str;

	if (str == NULL)
		return NULL;

	utf8_str = _g_utf8_try_from_any (str);
	if (utf8_str == NULL)
		utf8_str = g_strdup (_("(invalid value)"));

	return utf8_str;
}


static int
remove_from_file_list_and_get_position (GList **file_list,
					GFile  *file)
{
	GList *scan;
	int    i = 0;

	for (scan = *file_list; scan; scan = scan->next, i++)
		if (g_file_equal ((GFile *) scan->data, file))
			break;

	if (scan == NULL)
		return -1;

	*file_list = g_list_remove_link (*file_list, scan);

	return i;
}


void
_g_list_reorder (GList  *all_files,
		 GList  *visible_files,
		 GList  *files_to_move,
		 int     dest_pos,
		 int   **new_order_p,
		 GList **new_file_list_p)
{
	GHashTable *positions;
	GList      *new_visible_files;
	GList      *scan;
	int        *new_order;
	int         pos;
	GList      *new_file_list;
	GHashTable *visibles;

	/* save the original positions */

	positions = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, (GDestroyNotify) g_object_unref, NULL);
	for (scan = visible_files, pos = 0; scan; scan = scan->next, pos++)
		g_hash_table_insert (positions, g_object_ref ((GFile *) scan->data), GINT_TO_POINTER (pos));

	/* create the new visible list */

	new_visible_files = g_list_copy (visible_files);

	for (scan = files_to_move; scan; scan = scan->next) {
		int file_pos = remove_from_file_list_and_get_position (&new_visible_files, (GFile *) scan->data);
		if (file_pos < dest_pos)
			dest_pos--;
	}

	for (scan = files_to_move; scan; scan = scan->next) {
		new_visible_files = g_list_insert (new_visible_files, (GFile *) scan->data, dest_pos);
		dest_pos++;
	}

	/* compute the new order */

	new_order = g_new0 (int, g_list_length (new_visible_files));
	for (scan = new_visible_files, pos = 0; scan; scan = scan->next, pos++)
		new_order[pos] = GPOINTER_TO_INT (g_hash_table_lookup (positions, (GFile *) scan->data));

	/* save the new order in the catalog, appending the hidden files at
	 * the end. */

	new_file_list = _g_object_list_ref (new_visible_files);

	visibles = g_hash_table_new_full (g_file_hash, (GEqualFunc) g_file_equal, (GDestroyNotify) g_object_unref, NULL);
	for (scan = new_visible_files; scan; scan = scan->next)
		g_hash_table_insert (visibles, g_object_ref ((GFile *) scan->data), GINT_TO_POINTER (1));

	new_file_list = g_list_reverse (new_file_list);
	for (scan = all_files; scan; scan = scan->next) {
		GFile *file = scan->data;

		if (g_hash_table_lookup (visibles, file) == NULL)
			new_file_list = g_list_prepend (new_file_list, g_object_ref (file));
	}
	new_file_list = g_list_reverse (new_file_list);

	if (new_order_p != NULL)
		*new_order_p = new_order;
	else
		g_free (new_order);

	if (new_file_list_p != NULL)
		*new_file_list_p = new_file_list;
	else
		_g_object_list_unref (new_file_list);

	g_hash_table_destroy (visibles);
	g_list_free (new_visible_files);
	g_hash_table_destroy (positions);
}


GList *
_g_list_insert_list_before (GList *list1,
			    GList *sibling,
			    GList *list2)
{
  if (!list2)
    {
      return list1;
    }
  else if (!list1)
    {
      return list2;
    }
  else if (sibling)
    {
      GList *list2_last = g_list_last (list2);
      if (sibling->prev)
	{
	  sibling->prev->next = list2;
	  list2->prev = sibling->prev;
	  sibling->prev = list2_last;
	  list2_last->next = sibling;
	  return list1;
	}
      else
	{
	  sibling->prev = list2_last;
	  list2_last->next = sibling;
	  return list2;
	}
    }
  else
    {
      return g_list_concat (list1, list2);
    }
}


GHashTable *static_strings = NULL;
static GMutex static_strings_mutex;


const char *
get_static_string (const char *s)
{
	const char *result;

	if (s == NULL)
		return NULL;

	g_mutex_lock (&static_strings_mutex);

	if (static_strings == NULL)
		static_strings = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

	if (! g_hash_table_lookup_extended (static_strings, s, (gpointer) &result, NULL)) {
		result = g_strdup (s);
		g_hash_table_insert (static_strings,
				     (gpointer) result,
				     GINT_TO_POINTER (1));
	}

	g_mutex_unlock (&static_strings_mutex);

	return result;
}


char *
_g_rand_string (int len)
{
	static char *alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	static int   letters_only = 52;
	static int   whole_alphabet = 62;
	char        *s;
	GRand       *rand_gen;
	int          i;

	s = g_malloc (sizeof (char) * (len + 1));
	rand_gen = g_rand_new ();
	for (i = 0; i < len; i++)
		s[i] = alphabet[g_rand_int_range (rand_gen, 0, (i == 0) ? letters_only : whole_alphabet)];
	g_rand_free (rand_gen);
	s[len] = 0;

	return s;
}


int
_g_strv_find (char       **v,
	      const char  *s)
{
	int i;

	for (i = 0; v[i] != NULL; i++) {
		if (strcmp (v[i], s) == 0)
			return i;
	}

	return -1;
}


gboolean
_g_strv_contains (char       **v,
		  const char  *s)
{
	return (_g_strv_find (v, s) >= 0);
}


char **
_g_strv_prepend (char       **str_array,
                 const char  *str)
{
	char **result;
	int    i;
	int    j;

	result = g_new (char *, g_strv_length (str_array) + 1);
	i = 0;
	result[i++] = g_strdup (str);
	for (j = 0; str_array[j] != NULL; j++)
		result[i++] = g_strdup (str_array[j]);
	result[i] = NULL;

	return result;
}


char **
_g_strv_concat (char **strv1,
		char **strv2)
{
	char **result;
	int    i, j;

	result = g_new (char *, g_strv_length (strv1) + g_strv_length (strv2) + 1);
	i = 0;
	for (j = 0; strv1[j] != NULL; j++)
		result[i++] = g_strdup (strv1[j]);
	for (j = 0; strv2[j] != NULL; j++)
		result[i++] = g_strdup (strv2[j]);
	result[i] = NULL;

	return result;
}


gboolean
_g_strv_remove (char       **str_array,
                const char  *str)
{
	int i;
	int j;

	if (str == NULL)
		return FALSE;

	for (i = 0; str_array[i] != NULL; i++)
		if (strcmp (str_array[i], str) == 0)
			break;

	if (str_array[i] == NULL)
		return FALSE;

	for (j = i; str_array[j] != NULL; j++)
		str_array[j] = str_array[j + 1];

	return TRUE;
}


char *
_g_str_remove_suffix (const char *s,
		      const char *suffix)
{
	int s_len;
	int suffix_len;

	if (s == NULL)
		return NULL;
	if (suffix == NULL)
		return g_strdup (s);

	s_len = strlen (s);
	suffix_len = strlen (suffix);

	if (suffix_len >= s_len)
		return g_strdup ("");
	else
		return g_strndup (s, s_len - suffix_len);
}


/* based on glib/glib/gmarkup.c (Copyright 2000, 2003 Red Hat, Inc.)
 * This version does not escape ' and ''. Needed  because IE does not recognize
 * &apos; and &quot; */
void
_g_string_append_for_html (GString    *str,
		 	   const char *text,
		 	   gssize      length)
{
	const gchar *p;
	const gchar *end;
	gunichar     ch;
	int          state = 0;

	p = text;
	end = text + length;

	while (p != end) {
		const char *next;

		next = g_utf8_next_char (p);
		ch = g_utf8_get_char (p);

		switch (state) {
		case 1: /* escaped */
			if ((ch > 127) || ! g_ascii_isprint ((char) ch))
				g_string_append_printf (str, "\\&#%d;", ch);
			else
				g_string_append_unichar (str, ch);
			state = 0;
			break;

		default: /* not escaped */
			switch (*p) {
			case '\\':
				state = 1; /* next character is escaped */
				break;

			case '&':
				g_string_append (str, "&amp;");
				break;

			case '<':
				g_string_append (str, "&lt;");
				break;

			case '>':
				g_string_append (str, "&gt;");
				break;

			case '\n':
				g_string_append (str, "<br />");
				break;

			default:
				if ((ch > 127) ||  ! g_ascii_isprint ((char)ch))
					g_string_append_printf (str, "&#%d;", ch);
				else
					g_string_append_unichar (str, ch);
				state = 0;
				break;
			}
			break;
		}

		p = next;
	}
}


char *
_g_escape_for_html (const char *text,
		    gssize      length)
{
        GString *str;

        g_return_val_if_fail (text != NULL, NULL);

        if (length < 0)
                length = strlen (text);

        /* prealloc at least as long as original text */
        str = g_string_sized_new (length);
        _g_string_append_for_html (str, text, length);

        return g_string_free (str, FALSE);
}


/* Array utils*/


char *
_g_string_array_join (GPtrArray  *array,
		      const char *separator)
{
	GString *s;
	int      i;

	s = g_string_new ("");
	for (i = 0; i < array->len; i++) {
		if ((i > 0) && (separator != NULL))
			g_string_append (s, separator);
		g_string_append (s, g_ptr_array_index (array, i));
	}

	return g_string_free (s, FALSE);
}


/* Regexp utils */

static char **
get_patterns_from_pattern (const char *pattern_string)
{
	char **patterns;
	int    i;

	if (pattern_string == NULL)
		return NULL;

	patterns = _g_utf8_strsplit (pattern_string, ";", MAX_PATTERNS);
	for (i = 0; patterns[i] != NULL; i++) {
		char *p1, *p2;

		p1 = _g_utf8_strstrip (patterns[i]);
		p2 = _g_replace (p1, ".", "\\.");
		patterns[i] = _g_replace (p2, "*", ".*");

		g_free (p2);
		g_free (p1);
	}

	return patterns;
}


GRegex **
get_regexps_from_pattern (const char         *pattern_string,
			  GRegexCompileFlags  compile_options)
{
	char   **patterns;
	GRegex **regexps;
	int      i;

	patterns = get_patterns_from_pattern (pattern_string);
	if (patterns == NULL)
		return NULL;

	regexps = g_new0 (GRegex*, g_strv_length (patterns) + 1);
	for (i = 0; patterns[i] != NULL; i++)
		regexps[i] = g_regex_new (patterns[i],
					  G_REGEX_OPTIMIZE | compile_options,
					  G_REGEX_MATCH_NOTEMPTY,
					  NULL);
	g_strfreev (patterns);

	return regexps;
}


gboolean
string_matches_regexps (GRegex           **regexps,
			const char        *string,
			GRegexMatchFlags   match_options)
{
	gboolean matched;
	int      i;

	if ((regexps == NULL) || (regexps[0] == NULL))
		return TRUE;

	if (string == NULL)
		return FALSE;

	matched = FALSE;
	for (i = 0; regexps[i] != NULL; i++)
		if (g_regex_match (regexps[i], string, match_options, NULL)) {
			matched = TRUE;
			break;
	}

	return matched;
}


void
free_regexps (GRegex **regexps)
{
	int i;

	if (regexps == NULL)
		return;

	for (i = 0; regexps[i] != NULL; i++)
		g_regex_unref (regexps[i]);
	g_free (regexps);
}


/* URI utils  */


const char *
get_home_uri (void)
{
	static char *home_uri = NULL;

	if (home_uri == NULL) {
		const char *path;
		char       *uri;

		path = g_get_home_dir ();
		uri = g_uri_escape_string (path, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, TRUE);

		home_uri = g_strconcat ("file://", uri, NULL);

		g_free (uri);
	}

	return home_uri;
}


int
uricmp (const char *uri1,
	const char *uri2)
{
	if (uri1 == NULL) {
		if (uri2 == NULL)
			return 0;
		else
			return -1;
	}

	if (uri2 == NULL) {
		if (uri1 == NULL)
			return 0;
		else
			return 1;
	}

	return g_strcmp0 (uri1, uri2);
}


gboolean
same_uri (const char *uri1,
	  const char *uri2)
{
	return uricmp (uri1, uri2) == 0;
}


void
_g_string_list_free (GList *string_list)
{
	if (string_list == NULL)
		return;
	g_list_foreach (string_list, (GFunc) g_free, NULL);
	g_list_free (string_list);

}


GList *
_g_string_list_dup (GList *string_list)
{
	GList *new_list = NULL;
	GList *scan;

	for (scan = string_list; scan; scan = scan->next)
		new_list = g_list_prepend (new_list, g_strdup (scan->data));

	return g_list_reverse (new_list);
}


char **
_g_string_list_to_strv (GList *string_list)
{
	char  **strv;
	GList  *scan;
	int     i;

	strv = g_new0 (char *, g_list_length (string_list) + 1);
	for (scan = string_list, i = 0; scan; scan = scan->next)
		strv[i++] = g_strdup ((char *)scan->data);
	strv[i++] = NULL;

	return strv;
}


typedef GList GStringList;


G_DEFINE_BOXED_TYPE (GStringList,
		     g_string_list,
		     (GBoxedCopyFunc) _g_string_list_dup,
		     (GBoxedFreeFunc) _g_string_list_free)


GList *
get_file_list_from_url_list (char *url_list)
{
	GList *list = NULL;
	int    i;
	char  *url_start, *url_end;

	url_start = url_list;
	while (url_start[0] != '\0') {
		char *url;

		if (strncmp (url_start, "file:", 5) == 0) {
			url_start += 5;
			if ((url_start[0] == '/')
			    && (url_start[1] == '/')) url_start += 2;
		}

		i = 0;
		while ((url_start[i] != '\0')
		       && (url_start[i] != '\r')
		       && (url_start[i] != '\n')) i++;
		url_end = url_start + i;

		url = g_strndup (url_start, url_end - url_start);
		list = g_list_prepend (list, url);

		url_start = url_end;
		i = 0;
		while ((url_start[i] != '\0')
		       && ((url_start[i] == '\r')
			   || (url_start[i] == '\n'))) i++;
		url_start += i;
	}

	return g_list_reverse (list);
}


const char *
_g_uri_get_basename (const char *uri)
{
	register char   *base;
	register gssize  last_char;

	if (uri == NULL)
		return NULL;

	if (uri[0] == '\0')
		return "";

	last_char = strlen (uri) - 1;

	if (uri[last_char] == G_DIR_SEPARATOR)
		return "";

	base = g_utf8_strrchr (uri, -1, G_DIR_SEPARATOR);
	if (! base)
		return uri;

	return base + 1;
}


const char *
_g_uri_get_file_extension (const char *uri)
{
	char *p;

	if (uri == NULL)
		return NULL;

	p = strrchr (uri, '.');
	if (p == NULL)
		return NULL;

	if (p != uri) {
		char *p2;

		p2 = p - 1;
		while ((*p2 != '.') && (p2 != uri))
			p2--;
		if (strncmp (p2, ".tar.", 5) == 0)
			p = p2;
	}

	return p;
}


static gboolean
uri_is_filetype (const char *uri,
		 GFileType   file_type)
{
	gboolean   result = FALSE;
	GFile     *file;
	GFileInfo *info;
	GError    *error = NULL;

	file = g_file_new_for_uri (uri);

	if (! g_file_query_exists (file, NULL)) {
		g_object_unref (file);
		return FALSE;
	}

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, &error);
	if (error == NULL) {
		result = (g_file_info_get_file_type (info) == file_type);
	}
	else {
		g_warning ("Failed to get file type for uri %s: %s", uri, error->message);
		g_error_free (error);
	}

	g_object_unref (info);
	g_object_unref (file);

	return result;
}


gboolean
_g_uri_is_file (const char *uri)
{
	return uri_is_filetype (uri, G_FILE_TYPE_REGULAR);
}


gboolean
_g_uri_is_dir (const char *uri)
{
	return uri_is_filetype (uri, G_FILE_TYPE_DIRECTORY);
}


gboolean
_g_uri_parent_of_uri (const char *dirname,
		      const char *filename)
{
	int dirname_l, filename_l, separator_position;

	if ((dirname == NULL) || (filename == NULL))
		return FALSE;

	dirname_l = strlen (dirname);
	filename_l = strlen (filename);

	if ((dirname_l == filename_l + 1)
	    && (dirname[dirname_l - 1] == '/'))
		return FALSE;

	if ((filename_l == dirname_l + 1)
	    && (filename[filename_l - 1] == '/'))
		return FALSE;

	if (dirname[dirname_l - 1] == '/')
		separator_position = dirname_l - 1;
	else
		separator_position = dirname_l;

	return ((filename_l > dirname_l)
		&& (strncmp (dirname, filename, dirname_l) == 0)
		&& (filename[separator_position] == '/'));
}


char *
_g_uri_get_parent (const char *uri)
{
	int         p;
	const char *ptr = uri;
	char       *new_uri;

	if (uri == NULL)
		return NULL;

	p = strlen (uri) - 1;
	if (p < 0)
		return NULL;

	while ((p > 0) && (ptr[p] != '/'))
		p--;
	if ((p == 0) && (ptr[p] == '/'))
		p++;
	new_uri = g_strndup (uri, (guint)p);

	return new_uri;
}


char *
_g_uri_remove_extension (const char *uri)
{
	const char *ext;

	if (uri == NULL)
		return NULL;

	ext = _g_uri_get_file_extension (uri);
	if (ext == NULL)
		return g_strdup (uri);
	else
		return g_strndup (uri, strlen (uri) - strlen (ext));
}


char *
_g_build_uri (const char *base, ...)
{
	va_list     args;
	const char *child;
	GString    *uri;

	uri = g_string_new (base);

	va_start (args, base);
	while ((child = va_arg (args, const char *)) != NULL) {
		if (! g_str_has_suffix (uri->str, "/") && ! g_str_has_prefix (child, "/"))
			g_string_append (uri, "/");
		g_string_append (uri, child);
	}
	va_end (args);

	return g_string_free (uri, FALSE);
}


char *
_g_uri_get_scheme (const char *uri)
{
	const char *idx;

	idx = strstr (uri, "://");
	if (idx == NULL)
		return NULL;
	else
		return g_strndup (uri, (idx - uri) + 3);
}


const char *
_g_uri_remove_host (const char *uri)
{
	const char *idx, *sep;

	if (uri == NULL)
		return NULL;

	idx = strstr (uri, "://");
	if (idx == NULL)
		return uri;

	idx += 3;
	if (*idx == '\0')
		return "/";

	sep = strstr (idx, "/");
	if (sep == NULL)
		return idx;

	return sep;
}


char *
_g_uri_get_host (const char *uri)
{
	const char *idx;

	idx = strstr (uri, "://");
	if (idx == NULL)
		return g_strdup ("file://");

	idx = strstr (idx + 3, "/");
	if (idx == NULL) {
		char *scheme;

		scheme = _g_uri_get_scheme (uri);
		if (scheme == NULL)
			scheme = g_strdup ("file://");
		return scheme;
	}

	return g_strndup (uri, (idx - uri));
}


/* example 1 : uri      = file:///xxx/yyy/zzz/foo
 *             base     = file:///xxx/www
 *             return   : ../yyy/zzz/foo
 *
 * example 2 : uri      = file:///xxx/yyy/foo
 *             base     = file:///xxx
 *             return   : yyy/foo
 *
 * example 3 : uri      = smb:///xxx/yyy/foo
 *             base     = file://hhh/xxx
 *             return   : smb:///xxx/yyy/foo
 *
 * example 4 : uri      = file://hhh/xxx
 *             base     = file://hhh/xxx
 *             return   : ./
 */
char *
_g_uri_get_relative_path (const char *uri,
			  const char *base)
{
	char      *uri_host;
	char      *base_host;
	gboolean   same_host;
	char      *source_dir;
	char     **source_dir_v;
	char     **base_v;
	GString   *relative_path;
	int        i, j;

	if ((uri == NULL) || (base == NULL))
		return NULL;

	if (strcmp (uri, base) == 0)
		return g_strdup ("./");

	uri_host = _g_uri_get_host (uri);
	base_host = _g_uri_get_host (base);
	same_host = g_str_equal (uri_host, base_host);

	g_free (base_host);
	g_free (uri_host);

	if (! same_host)
		return g_strdup (uri);

	source_dir = _g_uri_get_parent (_g_uri_remove_host (uri));
	source_dir_v = g_strsplit (source_dir, "/", 0);
	base_v = g_strsplit (_g_uri_remove_host (base), "/", 0);

	relative_path = g_string_new (NULL);

	i = 0;
	while ((source_dir_v[i] != NULL)
	       && (base_v[i] != NULL)
	       && (strcmp (source_dir_v[i], base_v[i]) == 0))
	{
		i++;
	}

	j = i;

	while (base_v[i++] != NULL)
		g_string_append (relative_path, "../");

	while (source_dir_v[j] != NULL) {
		g_string_append (relative_path, source_dir_v[j]);
		g_string_append_c (relative_path, '/');
		j++;
	}

	g_string_append (relative_path, _g_uri_get_basename (uri));

	g_strfreev (base_v);
	g_strfreev (source_dir_v);
	g_free (source_dir);

	return g_string_free (relative_path, FALSE);
}


char *
_g_filename_clear_for_file (const char *display_name)
{
	return _g_utf8_replace (display_name, "/", "_");
}


/* GIO utils */


GFile *
_g_file_new_for_display_name (const char *base_uri,
			      const char *display_name,
			      const char *extension)
{
	GFile *base;
	char  *name;
	char  *name_escaped;
	GFile *catalog_file;

	base = g_file_new_for_uri (base_uri);
	name = g_strdup_printf ("%s%s", display_name, extension);
	name_escaped = _g_utf8_replace (name, "/", ".");
	catalog_file = g_file_get_child_for_display_name (base, name_escaped, NULL);

	g_free (name_escaped);
	g_free (name);
	g_object_unref (base);

	return catalog_file;
}


gboolean
_g_file_equal (GFile *file1,
	       GFile *file2)
{
	if ((file1 == NULL) && (file2 == NULL))
		return TRUE;
	if ((file1 == NULL) || (file2 == NULL))
		return FALSE;

	return g_file_equal (file1, file2);
}


char *
_g_file_get_display_name (GFile *file)
{
	char      *name = NULL;
	GFileInfo *file_info;

	file_info = g_file_query_info (file,
				       G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
				       G_FILE_QUERY_INFO_NONE,
				       NULL,
				       NULL);
	if (file_info != NULL) {
		name = g_strdup (g_file_info_get_attribute_string (file_info, G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME));
		g_object_unref (file_info);
	}
	else
		name = g_file_get_parse_name (file);

	return name;
}


GFileType
_g_file_get_standard_type (GFile *file)
{
	GFileType  result;
	GFileInfo *info;
	GError    *error = NULL;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_STANDARD_TYPE, 0, NULL, &error);
	if (error == NULL) {
		result = g_file_info_get_file_type (info);
	}
	else {
		result = G_FILE_ATTRIBUTE_TYPE_INVALID;
		g_error_free (error);
	}

	if (info != NULL)
		g_object_unref (info);

	return result;
}


GFile *
_g_file_get_destination (GFile *source,
		         GFile *source_base,
		         GFile *destination_folder)
{
	char       *source_uri;
	const char *source_suffix;
	char       *destination_folder_uri;
	char       *destination_uri;
	GFile      *destination;

	source_uri = g_file_get_uri (source);
	if (source_base != NULL) {
		char *source_base_uri;

		source_base_uri = g_file_get_uri (source_base);
		source_suffix = source_uri + strlen (source_base_uri);

		g_free (source_base_uri);
	}
	else
		source_suffix = _g_uri_get_basename (source_uri);

	destination_folder_uri = g_file_get_uri (destination_folder);
	destination_uri = g_strconcat (destination_folder_uri, "/", source_suffix, NULL);
	destination = g_file_new_for_uri (destination_uri);

	g_free (destination_uri);
	g_free (destination_folder_uri);
	g_free (source_uri);

	return destination;
}


GFile *
_g_file_get_duplicated (GFile *file)
{
	GString    *new_uri;
	char       *uri;
	char       *uri_noext;
	GRegex     *regex;
	GMatchInfo *match_info;
	GFile      *duplicated;

	new_uri = g_string_new ("");
	uri = g_file_get_uri (file);
	uri_noext = _g_uri_remove_extension (uri);

	regex = g_regex_new ("^(.*)%20\\(([0-9]+)\\)$", 0, 0, NULL);
	g_regex_match (regex, uri_noext, 0, &match_info);
	if (g_match_info_matches (match_info)) {
		char    *word;
		guint64  n;

		word = g_match_info_fetch (match_info, 1);
		g_string_append (new_uri, word);
		g_free (word);

		word = g_match_info_fetch (match_info, 2);
		n = g_ascii_strtoull (word, NULL, 10);
		g_string_append_printf (new_uri, "%%20(%" G_GUINT64_FORMAT ")", n + 1);

		g_free (word);
	}
	else {
		g_string_append (new_uri, uri_noext);
		g_string_append (new_uri, "%20(2)");
	}

	g_string_append (new_uri, _g_uri_get_file_extension (uri));
	duplicated = g_file_new_for_uri (new_uri->str);

	g_match_info_free (match_info);
	g_regex_unref (regex);
	g_free (uri_noext);
	g_free (uri);
	g_string_free (new_uri, TRUE);

	return duplicated;
}


GFile *
_g_file_get_child (GFile *file,
		   ...)
{
	va_list     args;
	const char *name;
	GFile      *child;

	child = g_object_ref (file);

	va_start (args, file);
	while ((name = va_arg (args, const char *)) != NULL) {
		GFile *tmp;

		tmp = g_file_get_child (child, name);
		g_object_unref (child);
		child = tmp;
	}
	va_end (args);

	return child;
}


GIcon *
_g_file_get_icon (GFile *file)
{
	GIcon     *icon = NULL;
	GFileInfo *file_info;

	file_info = g_file_query_info (file,
				       G_FILE_ATTRIBUTE_STANDARD_ICON,
				       G_FILE_QUERY_INFO_NONE,
				       NULL,
				       NULL);
	if (file_info != NULL) {
		icon = (GIcon*) g_object_ref (g_file_info_get_attribute_object (file_info, G_FILE_ATTRIBUTE_STANDARD_ICON));
		g_object_unref (file_info);
	}

	if (icon == NULL)
		icon = g_themed_icon_new ("file");

	return icon;
}

GIcon *
_g_file_get_symbolic_icon (GFile *file)
{
	GIcon     *icon = NULL;
	GFileInfo *file_info;

	file_info = g_file_query_info (file,
				       G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON,
				       G_FILE_QUERY_INFO_NONE,
				       NULL,
				       NULL);
	if (file_info != NULL) {
		icon = (GIcon*) g_object_ref (g_file_info_get_attribute_object (file_info, G_FILE_ATTRIBUTE_STANDARD_SYMBOLIC_ICON));
		g_object_unref (file_info);
	}

	if (icon == NULL)
		icon = g_themed_icon_new ("text-x-generic-symbolic");

	return icon;
}


GList *
_g_file_list_dup (GList *l)
{
	GList *r = NULL, *scan;
	for (scan = l; scan; scan = scan->next)
		r = g_list_prepend (r, g_file_dup ((GFile*) scan->data));
	return g_list_reverse (r);
}


void
_g_file_list_free (GList *l)
{
	GList *scan;
	for (scan = l; scan; scan = scan->next)
		g_object_unref (scan->data);
	g_list_free (l);
}


GList *
_g_file_list_new_from_uri_list (GList *uris)
{
	GList *r = NULL, *scan;
	for (scan = uris; scan; scan = scan->next)
		r = g_list_prepend (r, g_file_new_for_uri ((char*)scan->data));
	return g_list_reverse (r);
}


GList *
_g_file_list_new_from_uriv (char **uris)
{
	GList *r = NULL;
	int    i;

	if (uris == NULL)
		return NULL;

	for (i = 0; uris[i] != NULL; i++)
		r = g_list_prepend (r, g_file_new_for_uri (uris[i]));

	return g_list_reverse (r);
}


GList *
_g_file_list_find_file (GList *l,
			GFile *file)
{
	GList *scan;

	for (scan = l; scan; scan = scan->next)
		if (g_file_equal (file, (GFile *) scan->data))
			return scan;
	return NULL;
}


const char*
_g_file_get_mime_type (GFile    *file,
		       gboolean  fast_file_type)
{
	GFileInfo  *info;
	GError     *err = NULL;
 	const char *result = NULL;

	info = g_file_query_info (file,
				  fast_file_type ?
				  G_FILE_ATTRIBUTE_STANDARD_FAST_CONTENT_TYPE :
				  G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
				  0, NULL, &err);
	if (info == NULL) {
		char *uri;

		uri = g_file_get_uri (file);
		g_warning ("could not get content type for %s: %s", uri, err->message);
		g_free (uri);
		g_clear_error (&err);
	}
	else {
		result = get_static_string (g_content_type_get_mime_type (g_file_info_get_content_type (info)));
		g_object_unref (info);
	}

	return result;
}


void
_g_file_get_modification_time (GFile    *file,
			       GTimeVal *result)
{
	GFileInfo *info;
	GError    *err = NULL;

	result->tv_sec = 0;
	result->tv_usec = 0;

	info = g_file_query_info (file,
				  G_FILE_ATTRIBUTE_TIME_MODIFIED "," G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC,
				  0,
				  NULL,
				  &err);
	if (info != NULL) {
		g_file_info_get_modification_time (info, result);
		g_object_unref (info);
	}
	else {
		char *uri;

		uri = g_file_get_uri (file);
		g_warning ("could not get modification time for %s: %s", uri, err->message);
		g_free (uri);
		g_clear_error (&err);
	}
}


time_t
_g_file_get_mtime (GFile *file)
{
	GTimeVal timeval;

	_g_file_get_modification_time (file, &timeval);
	return (time_t) timeval.tv_sec;
}


int
_g_file_cmp_uris (GFile *a,
		  GFile *b)
{
	char *uri_a;
	char *uri_b;
	int   result;

	uri_a = g_file_get_uri (a);
	uri_b = g_file_get_uri (b);
	result = g_strcmp0 (uri_a, uri_b);

	g_free (uri_b);
	g_free (uri_a);

	return result;
}


gboolean
_g_file_equal_uris (GFile *a,
		    GFile *b)
{
	return _g_file_cmp_uris (a, b) == 0;
}


int
_g_file_cmp_modification_time (GFile *file_a,
			       GFile *file_b)
{
	GTimeVal timeval_a;
	GTimeVal timeval_b;

	_g_file_get_modification_time (file_a, &timeval_a);
	_g_file_get_modification_time (file_b, &timeval_b);

	return _g_time_val_cmp (&timeval_a, &timeval_b);
}


goffset
_g_file_get_size (GFile *file)
{
	GFileInfo *info;
	GError    *err = NULL;
	goffset    size = 0;

	info = g_file_query_info (file, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE, 0, NULL, &err);
	if (info != NULL) {
		size = g_file_info_get_size (info);
		g_object_unref (info);
	}
	else {
		char *uri;

		uri = g_file_get_uri (file);
		g_warning ("could not get size for %s: %s", uri, err->message);
		g_free (uri);
		g_clear_error (&err);
	}

	return size;
}


#define MAX_SYMLINKS 32


static GFile *
resolve_symlinks (GFile   *file,
		  GError **error,
		  int      level)
{
	GFile *resolved;
	char  *path;
	char **names;
	int    i;

	if (level > MAX_SYMLINKS) {
		char *uri;

		uri = g_file_get_uri (file);
		*error = g_error_new (G_IO_ERROR, G_IO_ERROR_TOO_MANY_LINKS, "Too many symbolic links for file: %s.", uri);
		g_free (uri);

		return NULL;
	}

	path = g_file_get_path (file);
	if (path == NULL)
		return g_file_dup (file);

	resolved = g_file_new_for_path (G_DIR_SEPARATOR_S);

	names = g_strsplit (path, G_DIR_SEPARATOR_S, -1);
	for (i = 0; names[i] != NULL; i++) {
		GFile     *child;
		GFileInfo *info;
		GFile     *new_resolved;

		if (strcmp (names[i], "") == 0)
			continue;

		child = g_file_get_child (resolved, names[i]);
		info = g_file_query_info (child, G_FILE_ATTRIBUTE_STANDARD_IS_SYMLINK "," G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET, 0, NULL, error);
		if (info == NULL) {
			g_object_unref (child);
			g_object_unref (resolved);
			resolved = NULL;
			break;
		}

		/* if names[i] isn't a symbolic link add it to the resolved path and continue */

		if (! g_file_info_get_is_symlink (info)) {
			g_object_unref (info);
			g_object_unref (resolved);
			resolved = child;
			continue;
		}

		g_object_unref (child);

		/* names[i] is a symbolic link */

		new_resolved = g_file_resolve_relative_path (resolved, g_file_info_get_symlink_target (info));

		g_object_unref (resolved);
		g_object_unref (info);

		resolved = resolve_symlinks (new_resolved, error, level + 1);

		g_object_unref (new_resolved);
	}

	g_strfreev (names);
	g_free (path);

	return resolved;
}


GFile *
_g_file_resolve_all_symlinks (GFile   *file,
			      GError **error)
{
	return resolve_symlinks (file, error, 0);
}


gboolean
_g_file_has_prefix (GFile *file,
		    GFile *prefix)
{
	char     *file_uri;
	char     *prefix_uri;
	gboolean  result;

	file_uri = g_file_get_uri (file);
	prefix_uri = g_file_get_uri (prefix);
	if (! g_str_has_suffix (prefix_uri, "/")) {
		char *tmp;

		tmp = g_strconcat (prefix_uri, "/", NULL);
		g_free (prefix_uri);
		prefix_uri = tmp;
	}
	result = g_str_has_prefix (file_uri, prefix_uri);

	g_free (prefix_uri);
	g_free (file_uri);

	return result;
}


GFile *
_g_file_append_prefix (GFile      *file,
		       const char *prefix)
{
	char  *uri;
	char  *new_uri;
	GFile *new_file;

	uri = g_file_get_uri (file);
	new_uri = g_strconcat (prefix, uri, NULL);
	new_file = g_file_new_for_uri (new_uri);

	g_free (new_uri);
	g_free (uri);

	return new_file;
}


GFile *
_g_file_append_path (GFile      *file,
		     const char *path)
{
	char  *uri;
	char  *escaped;
	char  *new_uri;
	GFile *new_file;

	if (path == NULL)
		return g_file_dup (file);

	uri = g_file_get_uri (file);
	escaped = g_uri_escape_string (path, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH, FALSE);
	new_uri = _g_build_uri (uri, escaped, NULL);
	new_file = g_file_new_for_uri (new_uri);

	g_free (new_uri);
	g_free (escaped);
	g_free (uri);

	return new_file;
}


static gboolean
attribute_matches_mask (const char *attribute,
			const char *mask)
{
	char *pattern_end;

	pattern_end = g_strstr_len (mask, -1, "*");
	if (pattern_end == NULL)
		return strcmp (attribute, mask) == 0;
	else
		return strncmp (attribute, mask, pattern_end - mask) == 0;
}


gboolean
_g_file_attributes_matches_all (const char *attributes,
			        const char *mask)
{
	gboolean   matches_all_mask = TRUE;
	char     **attributes_v;
	char     **mask_v;
	int        j;

	attributes_v = g_strsplit (attributes, ",", -1);
	mask_v = g_strsplit (mask, ",", -1);
	for (j = 0; matches_all_mask && (attributes_v[j] != NULL); j++) {
		gboolean matches = FALSE;
		int      i;

		for (i = 0; ! matches && (mask_v[i] != NULL); i++) {
			matches = attribute_matches_mask (attributes_v[j], mask_v[i]);
#if 0
			g_print ("attr: %s <=> mask: %s : %d\n", attributes_v[j], mask_v[i], matches);
#endif
		}

		matches_all_mask = matches;
	}

	g_strfreev (mask_v);
	g_strfreev (attributes_v);

	return matches_all_mask;
}


static gboolean
_attributes_matches_mask (const char *attributes,
			  const char *mask)
{
	gboolean   matches = FALSE;
	char     **attributes_v;
	char     **mask_v;
	int        i;

	attributes_v = g_strsplit (attributes, ",", -1);
	mask_v = g_strsplit (mask, ",", -1);
	for (i = 0; ! matches && (mask_v[i] != NULL); i++) {
		int j;

		for (j = 0; ! matches && (attributes_v[j] != NULL); j++) {
			matches = attribute_matches_mask (attributes_v[j], mask_v[i]);
#if 0
			g_print ("attr: %s <=> mask: %s : %d\n", attributes_v[j], mask_v[i], matches);
#endif
		}
	}

	g_strfreev (mask_v);
	g_strfreev (attributes_v);

	return matches;
}


gboolean
_g_file_attributes_matches_any (const char *attributes,
			        const char *mask)
{
	return _attributes_matches_mask (attributes, mask) || _attributes_matches_mask (mask, attributes);
}


gboolean
_g_file_attributes_matches_any_v (const char *attributes,
				  char      **attribute_v)
{
	gboolean matches;
	int      i;

	if (attributes == NULL)
		return FALSE;

	matches = FALSE;
	for (i = 0; ! matches && (attribute_v[i] != NULL); i++)
		matches = _g_file_attributes_matches_any (attributes, attribute_v[i]);

	return matches;
}


/* -- _g_file_info_swap_attributes -- */


typedef struct {
	GFileAttributeType type;
	union {
		char      *string;
		char     **stringv;
		gboolean   boolean;
		guint32    uint32;
		gint32     int32;
		guint64    uint64;
		gint64     int64;
		gpointer   object;
	} v;
} _GFileAttributeValue;


static void
_g_file_attribute_value_free (_GFileAttributeValue *attr_value)
{
	switch (attr_value->type) {
	case G_FILE_ATTRIBUTE_TYPE_STRING:
	case G_FILE_ATTRIBUTE_TYPE_BYTE_STRING:
		g_free (attr_value->v.string);
		break;
#if GLIB_CHECK_VERSION(2,22,0)
	case G_FILE_ATTRIBUTE_TYPE_STRINGV:
		g_strfreev (attr_value->v.stringv);
		break;
#endif
	case G_FILE_ATTRIBUTE_TYPE_OBJECT:
		g_object_unref (attr_value->v.object);
		break;
	default:
		break;
	}

	g_free (attr_value);
}


static _GFileAttributeValue *
_g_file_info_get_value (GFileInfo  *info,
			const char *attr)
{
	_GFileAttributeValue  *attr_value;
	GFileAttributeType     type;
	gpointer               value;
	GFileAttributeStatus   status;

	attr_value = g_new (_GFileAttributeValue, 1);
	attr_value->type = G_FILE_ATTRIBUTE_TYPE_INVALID;

	if (! g_file_info_get_attribute_data (info, attr, &type, &value, &status))
		return attr_value;

	attr_value->type = type;
	switch (type) {
	case G_FILE_ATTRIBUTE_TYPE_INVALID:
		break;
	case G_FILE_ATTRIBUTE_TYPE_STRING:
	case G_FILE_ATTRIBUTE_TYPE_BYTE_STRING:
		attr_value->v.string = g_strdup ((char *) value);
		break;
#if GLIB_CHECK_VERSION(2,22,0)
	case G_FILE_ATTRIBUTE_TYPE_STRINGV:
		attr_value->v.stringv = g_strdupv ((char **) value);
		break;
#endif
	case G_FILE_ATTRIBUTE_TYPE_BOOLEAN:
		attr_value->v.boolean = * ((gboolean *) value);
		break;
	case G_FILE_ATTRIBUTE_TYPE_UINT32:
		attr_value->v.uint32 = * ((guint32 *) value);
		break;
	case G_FILE_ATTRIBUTE_TYPE_INT32:
		attr_value->v.int32 = * ((gint32 *) value);
		break;
	case G_FILE_ATTRIBUTE_TYPE_UINT64:
		attr_value->v.uint64 = * ((guint64 *) value);
		break;
	case G_FILE_ATTRIBUTE_TYPE_INT64:
		attr_value->v.int64 = * ((gint64 *) value);
		break;
	case G_FILE_ATTRIBUTE_TYPE_OBJECT:
		attr_value->v.object = g_object_ref ((GObject *) value);
		break;
	default:
		g_warning ("unknown attribute type: %d", type);
		break;
	}

	return attr_value;
}


static void
_g_file_info_set_value (GFileInfo            *info,
			const char           *attr,
			_GFileAttributeValue *attr_value)
{
	gpointer value = NULL;

	if (attr_value->type == G_FILE_ATTRIBUTE_TYPE_INVALID)
		return;

	switch (attr_value->type) {
	case G_FILE_ATTRIBUTE_TYPE_STRING:
	case G_FILE_ATTRIBUTE_TYPE_BYTE_STRING:
		value = attr_value->v.string;
		break;
#if GLIB_CHECK_VERSION(2,22,0)
	case G_FILE_ATTRIBUTE_TYPE_STRINGV:
		value = attr_value->v.stringv;
		break;
#endif
	case G_FILE_ATTRIBUTE_TYPE_BOOLEAN:
		value = &attr_value->v.boolean;
		break;
	case G_FILE_ATTRIBUTE_TYPE_UINT32:
		value = &attr_value->v.uint32;
		break;
	case G_FILE_ATTRIBUTE_TYPE_INT32:
		value = &attr_value->v.int32;
		break;
	case G_FILE_ATTRIBUTE_TYPE_UINT64:
		value = &attr_value->v.uint64;
		break;
	case G_FILE_ATTRIBUTE_TYPE_INT64:
		value = &attr_value->v.int64;
		break;
	case G_FILE_ATTRIBUTE_TYPE_OBJECT:
		value = attr_value->v.object;
		break;
	default:
		g_warning ("Unknown attribute type: %d", attr_value->type);
		break;
	}

	g_file_info_set_attribute (info, attr, attr_value->type, value);
}


void
_g_file_info_swap_attributes (GFileInfo  *info,
			      const char *attr1,
			      const char *attr2)
{
	_GFileAttributeValue *value1;
	_GFileAttributeValue *value2;

	value1 = _g_file_info_get_value (info, attr1);
	value2 = _g_file_info_get_value (info, attr2);

	_g_file_info_set_value (info, attr1, value2);
	_g_file_info_set_value (info, attr2, value1);

	_g_file_attribute_value_free (value1);
	_g_file_attribute_value_free (value2);
}


void
_g_file_info_set_secondary_sort_order (GFileInfo  *info,
				       gint32      sort_order)
{
	g_file_info_set_attribute_int32 (info, "gth::standard::secondary-sort-order", sort_order);
}


gint32
_g_file_info_get_secondary_sort_order (GFileInfo  *info)
{
	return g_file_info_get_attribute_int32 (info, "gth::standard::secondary-sort-order");
}


void
_g_file_info_update (GFileInfo  *dest_info,
		     GFileInfo  *src_info)
{
	char **attributes;
	int    i;

	if (src_info == NULL)
		return;

	attributes = g_file_info_list_attributes (src_info, NULL);
	for (i = 0; attributes[i] != NULL; i++) {
		GFileAttributeType   type;
		gpointer             value_pp;
		GFileAttributeStatus status;

		if (g_file_info_get_attribute_data (src_info,
						    attributes[i],
						    &type,
						    &value_pp,
						    &status))
		{
			g_file_info_set_attribute (dest_info, attributes[i], type, value_pp);
		}
	}

	g_strfreev (attributes);
}


const char *
_g_content_type_guess_from_name (const char *filename)
{
	if (filename == NULL)
		return NULL;

#if WEBP_IS_UNKNOWN_TO_GLIB
	const char *ext;

	ext = _g_uri_get_file_extension (filename);
	if (g_strcmp0 (ext, ".webp") == 0)
		return "image/webp";
#endif
	return g_content_type_guess (filename, NULL, 0, NULL);
}


gboolean
_g_content_type_is_a (const char *type,
		      const char *supertype)
{
	if (type == NULL)
		return FALSE;
	else
		return g_content_type_is_a (type, supertype);
}


/* -- _g_content_type_get_from_stream -- */


static const char *
get_mime_type_from_magic_numbers (guchar *buffer,
				  gsize   buffer_size)
{
#if ENABLE_MAGIC

	static magic_t magic = NULL;

	if (magic == NULL) {
		magic = magic_open (MAGIC_MIME_TYPE);
		if (magic != NULL)
			magic_load (magic, NULL);
		else
			g_warning ("unable to open magic database");
	}

	if (magic != NULL) {
		const char * mime_type;

		mime_type = magic_buffer (magic, buffer, buffer_size);
		if (mime_type)
			return mime_type;

		g_warning ("unable to detect filetype from magic: %s", magic_error (magic));
	}

#else

	static const struct magic {
		const unsigned int off;
		const unsigned int len;
		const char * const id;
		const char * const mime_type;
	}
	magic_ids [] = {
		/* some magic ids taken from magic/Magdir/archive from the file-4.21 tarball */
		{ 0,  8, "\x89PNG\x0d\x0a\x1a\x0a",		"image/png" },
		{ 0,  4, "MM\x00\x2a",				"image/tiff" },
		{ 0,  4, "II\x2a\x00",				"image/tiff" },
		{ 0,  4, "GIF8",				"image/gif" },
		{ 0,  3, "\xff\xd8\xff",			"image/jpeg" },
		{ 8,  7, "WEBPVP8",				"image/webp" }
	};

	int  i;

	for (i = 0; i < G_N_ELEMENTS (magic_ids); i++) {
		const struct magic * const magic = &magic_ids[i];

		if ((magic->off + magic->len) > buffer_size)
			continue;

		if (! memcmp (buffer + magic->off, magic->id, magic->len))
			return magic->mime_type;
	}

#endif

	return NULL;
}


const char *
_g_content_type_get_from_stream (GInputStream  *istream,
				 GFile         *file,
				 GCancellable  *cancellable,
				 GError       **error)
{
	guchar      buffer[BUFFER_SIZE_FOR_SNIFFING];
	gssize      n = 0;
	gboolean    result_uncertain = FALSE;
	const char *content_type;

	n = g_input_stream_read (istream,
				 buffer,
				 BUFFER_SIZE_FOR_SNIFFING,
				 cancellable,
				 error);
	if (n < 0)
		return NULL;

	content_type = get_mime_type_from_magic_numbers (buffer, n);
	if (content_type == NULL)
		content_type = g_content_type_guess (NULL, buffer, n, &result_uncertain);
	if (result_uncertain)
		content_type = NULL;

	if (((content_type == NULL)
	     || (strcmp (content_type, "application/xml") == 0)
	     || (strcmp (content_type, "image/tiff") == 0))
	    && (file != NULL))
	{
		char *filename;

		filename = g_file_get_basename (file);
		content_type = _g_content_type_guess_from_name (filename);

		g_free (filename);
	}

	g_seekable_seek (G_SEEKABLE (istream), 0, G_SEEK_SET, cancellable, NULL);

	return content_type;
}


gboolean
_g_mime_type_is_image (const char *mime_type)
{
	g_return_val_if_fail (mime_type != NULL, FALSE);
	return (g_content_type_is_a (mime_type, "image/*"));
}


gboolean
_g_mime_type_is_raw (const char *mime_type)
{
        g_return_val_if_fail (mime_type != NULL, FALSE);
        return (g_content_type_is_a (mime_type, "image/x-dcraw"));
}


gboolean
_g_mime_type_is_video (const char *mime_type)
{
	g_return_val_if_fail (mime_type != NULL, FALSE);

	return (g_content_type_is_a (mime_type, "video/*")
		|| (strcmp (mime_type, "application/ogg") == 0)
		|| (strcmp (mime_type, "application/vnd.ms-asf") == 0)
		|| (strcmp (mime_type, "application/vnd.rn-realmedia") == 0));
}


gboolean
_g_mime_type_is_audio (const char *mime_type)
{
	g_return_val_if_fail (mime_type != NULL, FALSE);

	return g_content_type_is_a (mime_type, "audio/*");
}


char *
_g_settings_get_uri (GSettings  *settings,
		     const char *key)
{
	char *value;
	char *uri;

	value = g_settings_get_string (settings, key);
	if ((value == NULL) || (strcmp (value, "") == 0)) {
		g_free (value);
		return NULL;
	}

	uri = _g_replace (value, "file://~", get_home_uri ());

	g_free (value);

	return uri;
}


char *
_g_settings_get_uri_or_special_dir (GSettings      *settings,
		     	     	    const char     *key,
		     	     	    GUserDirectory  directory)
{
	char *uri;

	uri = g_settings_get_string (settings, key);
	if ((uri == NULL) || (strcmp (uri, "~") == 0) || (strcmp (uri, "file://~") == 0)) {
		const char *dir;

		g_free (uri);
		uri = NULL;

		dir = g_get_user_special_dir (directory);
		if (dir != NULL)
			uri = g_filename_to_uri (dir, NULL, NULL);
		if (uri == NULL)
			uri = g_strdup (get_home_uri ());
	}
	else {
		char *tmp = uri;
		uri = _g_replace (tmp, "file://~", get_home_uri ());
		g_free (tmp);
	}

	return uri;
}


void
_g_settings_set_uri (GSettings  *settings,
		     const char *key,
		     const char *uri)
{
	g_settings_set_string (settings, key, uri);
}


void
_g_settings_set_string_list (GSettings  *settings,
			     const char *key,
			     GList      *list)
{
	int     len;
	char  **strv;
	int     i;
	GList  *scan;

	len = g_list_length (list);
	strv = g_new (char *, len + 1);
	for (i = 0, scan = list; scan; scan = scan->next)
		strv[i++] = scan->data;
	strv[i] = NULL;

	g_settings_set_strv (settings, key, (const char *const *) strv);

	g_free (strv);
}


GList *
_g_settings_get_string_list (GSettings  *settings,
			     const char *key)
{
	char **strv;
	int    i;
	GList *list;

	strv = g_settings_get_strv (settings, key);
	list = NULL;
	for (i = 0; strv[i] != NULL; i++)
		list = g_list_prepend (list, g_strdup (strv[i]));

	g_strfreev (strv);

	return g_list_reverse (list);
}


GSettings *
_g_settings_new_if_schema_installed (const char *schema_id)
{
	GSettingsSchema *schema;

	schema = g_settings_schema_source_lookup (g_settings_schema_source_get_default (),
						  schema_id,
						  TRUE);
	if (schema == NULL)
		return NULL;

	g_settings_schema_unref (schema);

	return g_settings_new (schema_id);
}


/* this is totem_time_to_string renamed, thanks to the authors :) */
char *
_g_format_duration_for_display (gint64 msecs)
{
        int sec, min, hour, _time;

        _time = (int) (msecs / 1000);
        sec = _time % 60;
        _time = _time - sec;
        min = (_time % (60*60)) / 60;
        _time = _time - (min * 60);
        hour = _time / (60*60);

        if (hour > 0)
        {
                /* hour:minutes:seconds */
                /* Translators: This is a time format, like "9∶05∶02" for 9
                 * hours, 5 minutes, and 2 seconds. You may change "∶" to
                 * the separator that your locale uses or use "%Id" instead
                 * of "%d" if your locale uses localized digits.
                 */
                return g_strdup_printf (C_("long time format", "%d∶%02d∶%02d"), hour, min, sec);
        }

        /* minutes:seconds */
        /* Translators: This is a time format, like "5∶02" for 5
         * minutes and 2 seconds. You may change "∶" to the
         * separator that your locale uses or use "%Id" instead of
         * "%d" if your locale uses localized digits.
         */
        return g_strdup_printf (C_("short time format", "%d∶%02d"), min, sec);
}


GList *
_g_list_prepend_link (GList *list,
		      GList *link)
{
	link->next = list;
	if (list != NULL) list->prev = link;
	return link;
}


void
_g_error_free (GError *error)
{
	if (error != NULL)
		g_error_free (error);
}


void
toggle_action_activated (GSimpleAction *action,
			 GVariant      *parameter,
			 gpointer       data)
{
	GVariant *state;

	state = g_action_get_state (G_ACTION (action));
	g_action_change_state (G_ACTION (action), g_variant_new_boolean (! g_variant_get_boolean (state)));

	g_variant_unref (state);
}
