/*  Sarien - A Sierra AGI resource interpreter engine
 *  Copyright (C) 1999,2001 Stuart George and Claudio Matsuoka
 *  
 *  $Id$
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; see docs/COPYING for further details.
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <dirent.h>

#include "sarien.h"
#include "agi.h"


/* poor man's glob()
 *
 * For our needs, using glob() is overkill. Not all platforms have
 * glob, and glob performs case-sensitive matches we don't want.
 */
static char *match (char *p, int f)
{
	char *slash, *dir, *pattern, s[MAX_PATH];
	DIR *d;
	struct dirent *e;
	int i, j;
	static char path[MAX_PATH];

	*path = 0;

	strcpy (s, p);
	slash = strrchr (s, '/');
	if (slash) {
		*slash = 0;
		pattern = slash + 1;
		dir = s; 
	} else {
		pattern = s;
		dir = ".";
	}
	
	_D ("dir=%s, pattern=%s", dir, pattern);

	/* empty pattern given */
	if (!*pattern)
		return 0;

	d = opendir (dir);

	while ((e = readdir (d)) != NULL) {
		/* check backwards */
		i = strlen (e->d_name) - 1;
		j = strlen (pattern) - 1;

		for (; i >= 0 && j >= 0; i--, j--) {
			if (pattern[j] == '*') {
				i = j = -1;
				break;
			}
			if (tolower (e->d_name[i]) != tolower(pattern[j]))
				break;
		}
		/* exact match */
		if (i < 0 && j < 0) {
			if (f) {
				strcpy (path, dir);
				strcat (path, "/");
				strcat (path, e->d_name);
			} else {
				strcpy (path, e->d_name);
			}
			return path;
		}
	}

	return NULL;
}


int file_exists (char *fname)
{
#if 1
	_D ("(fname=%s)", fname);
	return match (fname, 0) != NULL;
#else
	int rc;
	glob_t pglob;

	_D ("(\"%s\")", fname);
	rc = glob (fname, GLOB_ERR | GLOB_NOSORT, NULL, &pglob);
	if (!rc && pglob.gl_pathc <= 0)
		rc = -1;

	globfree (&pglob);

	return rc;
#endif
}


char* file_name (char *fname)
{
#if 1
	_D ("(fname=%s)", fname);
	return match (fname, 0);
#else
	int rc;
	glob_t pglob;
	char *s, *r = NULL;

	_D ("(\"%s\")", fname);
	rc = glob (fname, GLOB_ERR | GLOB_NOSORT, NULL, &pglob);
	if (!rc) {
		r = strdup (pglob.gl_pathv[0]);
		_D ("found name \"%s\"", r);
	}

	globfree (&pglob);

	if ((s = strrchr (r, '/')))
		r = s + 1;

	return r;
#endif
}


char *fixpath (int flag, char *fname)
{
	static char path[MAX_PATH];
	char *s;

	_D ("(flag = %d, fname = \"%s\")", flag, fname);
	_D ("game.dir = %s", game.dir);

	strcpy (path, game.dir);
	if (*path && path[strlen (path) - 1] != '/')
		strcat (path, "/");
	if (flag)
		strcat (path, game.name);

	strcat (path, fname);

	if (path[strlen (path) - 1] == '.')
		path[strlen (path) - 1] = 0;

	if ((s = match (path, 1)) != NULL)
		strcpy (path, s);
	else
		path[0] = 0;
	
	_D ("fixed path = %s", path);

	return path;
}


char *get_current_directory ()
{
	return (".");
}

