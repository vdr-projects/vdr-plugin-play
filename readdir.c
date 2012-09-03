///
///	@file readdir.c		@brief directory reading module
///
///	Copyright (c) 2012 by Johns.  All Rights Reserved.
///
///	Contributor(s):
///
///	License: AGPLv3
///
///	This program is free software: you can redistribute it and/or modify
///	it under the terms of the GNU Affero General Public License as
///	published by the Free Software Foundation, either version 3 of the
///	License.
///
///	This program is distributed in the hope that it will be useful,
///	but WITHOUT ANY WARRANTY; without even the implied warranty of
///	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
///	GNU Affero General Public License for more details.
///
///	$Id$
//////////////////////////////////////////////////////////////////////////////

#define __USE_ZZIPLIB			///< zip archives support
#define __USE_AVFS			///< A Virtual File System support

#include <sys/types.h>
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

#include <dirent.h>

#include <libintl.h>
#define _(str) gettext(str)		///< gettext shortcut
#define _N(str) str			///< gettext_noop shortcut

#ifdef USE_ZZIPLIB
#include <zzip/lib.h>
#endif
#ifdef USE_AVFS
#include <virtual.h>
#else
#define virt_stat	stat		///< universal stat
#define virt_fstat	fstat		///< universal fstat
#define virt_opendir	opendir		///< universal opendir
#define virt_readdir	readdir		///< universal readdir
#define virt_closedir	closedir	///< universal closedir
#endif

#include "misc.h"
#include "readdir.h"

//////////////////////////////////////////////////////////////////////////////
//	Variables
//////////////////////////////////////////////////////////////////////////////

const char ConfigShowHiddenFiles = 0;	///< config show hidden files
static const char *BaseDir;		///< current directory
static const NameFilter *NameFilters;	///< current name filter table

//////////////////////////////////////////////////////////////////////////////
//	Functions
//////////////////////////////////////////////////////////////////////////////

/**
**	Check if filename is a directory.
**
**	@param filename	path and file name
**
**	@retval	true	directory
**	@retval false	no directory
*/
int IsDirectory(const char *filename)
{
    struct stat stat_buf;

    if (virt_stat(filename, &stat_buf) < 0) {
	Error("play/readdir: can't stat '%s': %s\n", filename,
	    strerror(errno));
	return -1;
    }
    return S_ISDIR(stat_buf.st_mode);
}

/**
**	Check if filename is an archive.
**
**	@param filename	path and file name
**
**	@retval	true	archive
**	@retval false	no archive
*/
int IsArchive(const char *filename)
{
#ifdef USE_AVFS

    /**
    **	Table of supported archive suffixes.
    */
    static const NameFilter ArchiveFilters[] = {
#define FILTER(x) { sizeof(x) - 1, x }
	FILTER(".cbz"),
	FILTER(".cbr"),
	FILTER(".zip"),
	FILTER(".rar"),
	FILTER(".tar"),
	FILTER(".tar.gz"),
	FILTER(".tgz"),
#undef FILTER
	{0, NULL}
    };
    int i;
    int len;

    len = strlen(filename);
    for (i = 0; ArchiveFilters[i].String; ++i) {
	if (len >= ArchiveFilters[i].Length
	    && !strcasecmp(filename + len - ArchiveFilters[i].Length,
		ArchiveFilters[i].String)) {
	    return 1;
	}
    }
#else
    (void)filename;
#endif
    return 0;
}

/**
**	Filter for scandir, only directories.
**
**	@param dirent	current directory entry
**
**	@returns true if the @p dirent is a directories.
*/
static int FilterIsDirectory(const struct dirent *dirent)
{
    char *tmp;
    int dir;
    size_t len;

    len = _D_EXACT_NAMLEN(dirent);
    if (len && dirent->d_name[0] == '.') {
	// hide hidden files
	if (!ConfigShowHiddenFiles) {
	    return 0;
	}
	// ignore . and ..
	if (len == 1 || (len == 2 && dirent->d_name[1] == '.')) {
	    return 0;
	}
    }
#ifdef _DIRENT_HAVE_D_TYPE
    if (dirent->d_type == DT_DIR) {	// only directories files
	return 1;
#ifdef DT_LNK
    } else if (dirent->d_type == DT_LNK) {	// symbolic link
#endif
    } else if (dirent->d_type != DT_UNKNOWN) {	// no looser filesystem
	return 0;
    }
#endif

    // DT_UNKOWN or DT_LNK
    tmp = (char *)malloc(strlen(BaseDir) + strlen(dirent->d_name) + 1);
    stpcpy(stpcpy(tmp, BaseDir), dirent->d_name);
    dir = IsDirectory(tmp);
    free(tmp);
    return dir;
}

/**
**	Filter for scandir, only files.
**
**	@param dirent	current directory entry
**
**	@returns true if the @p dirent is a video.
*/
static int FilterIsFile(const struct dirent *dirent)
{
    char *tmp;
    int dir;
    int len;
    int i;

    len = _D_EXACT_NAMLEN(dirent);
    if (len && dirent->d_name[0] == '.') {
	if (!ConfigShowHiddenFiles) {	// hide hidden files
	    return 0;
	}
    }
    // look through name filter table
    if (NameFilters) {
	for (i = 0; NameFilters[i].String; ++i) {
	    if (len >= NameFilters[i].Length
		&& !strcasecmp(dirent->d_name + len - NameFilters[i].Length,
		    NameFilters[i].String)) {
		goto found;
	    }
	}
	// no file name matched
	return 0;
    }
  found:

#ifdef _DIRENT_HAVE_D_TYPE
    if (dirent->d_type == DT_REG) {	// only regular files
	return 1;
#ifdef DT_LNK
    } else if (dirent->d_type == DT_LNK) {	// symbolic link
#endif
    } else if (dirent->d_type != DT_UNKNOWN) {	// no looser filesystem
	return 0;
    }
#endif

    // DT_UNKOWN or DT_LNK
    tmp = (char *)malloc(strlen(BaseDir) + strlen(dirent->d_name) + 1);
    stpcpy(stpcpy(tmp, BaseDir), dirent->d_name);
    dir = IsDirectory(tmp);
    free(tmp);
    return !dir;
}

/**
**	ScanDirectory qsort compare function.
**
**	@param s1	table index 1
**	@param s2	table index 2
**
**	@returns an integer less than, equal to, or greater than zero if s1
**	is found, respectively, to be less than, to match, or be greater
**	than s2.
*/
static int q_cmp(const void *s1, const void *s2)
{
    return strcmp(*(char *const *)s1, *(char *const *)s2);
}

/**
**	Scan a directory for matching entries.
**
**	@param name		directory path and name
**	@param flag_dir		only directories or files
**	@param filter		list of name suffix filters
**	@param[out] namelist	list of matching names in directory
**
**	@retval <0	if any error occurs
**	@retval 0	empty directory, no errors occurs
**	@retval n	number of files
**
**	@todo support reading and sorting files and directories
**	@todo flag disable sort
*/
int ScanDirectory(const char *name, int flag_dir, const NameFilter * filter,
    char ***namelist)
{
    DIR *dir;
    struct dirent *entry;
    struct stat stat_buf;
    int n;
    char **names;
    int arraysz;
    int save;

    Debug(3, "play/scandir: scan directory '%s'\n", name);

    // FIXME: threads remove global variables
    BaseDir = name;
    NameFilters = filter;

    if (!(dir = virt_opendir(name))) {
	Error("play/scandir: can't open dir '%s': %s\n", name,
	    strerror(errno));
	return -1;
    }
    if (virt_fstat(dirfd(dir), &stat_buf) < 0) {
	Error("play/scandir: can't stat dir '%s': %s\n", name,
	    strerror(errno));
	return -1;
    }
    // approximate size of name array
    if (stat_buf.st_size) {
	arraysz = stat_buf.st_size / (3 * 8);
    } else {
	arraysz = 16;
    }
    if (!(names = malloc(arraysz * sizeof(*names)))) {
	Error("play/scandir: dir '%s': out of memory\n", name);
	return -1;
    }

    n = 0;
    errno = 0;
    while ((entry = virt_readdir(dir))) {
	int len;
	char *tmp;

	// skip hidden files, wrong kind, wrong suffix
	if (flag_dir ? !FilterIsDirectory(entry) : !FilterIsFile(entry)) {
	    continue;
	}

	len = _D_ALLOC_NAMLEN(entry);
	if (!(tmp = malloc(len))) {
	    Error("play/scandir: dir '%s': out of memory\n", name);
	    break;
	}
	memcpy(tmp, entry->d_name, len);

	if (++n >= arraysz) {		// array full
	    char **new;

	    if (virt_fstat(dirfd(dir), &stat_buf) < 0) {
		Error("play/scandir: can't stat dir '%s': %s\n", name,
		    strerror(errno));
		--n;
		break;
	    }
	    // dir size grown and valid
	    if (stat_buf.st_size / (3 * 4) > arraysz) {
		arraysz = stat_buf.st_size / (3 * 4);
	    } else {
		arraysz *= 2;
	    }
	    if (!(new = realloc(names, arraysz * sizeof(*names)))) {
		Error("play/scandir: dir '%s': out of memory\n", name);
		--n;
		break;
	    }
	    names = new;
	}
	names[n - 1] = tmp;
    }

    save = errno;
    virt_closedir(dir);

    if (save) {				// error happened
	while (n > 0) {			// free used memory
	    free(names[--n]);
	}
	free(names);
	errno = save;
	*namelist = NULL;
	return -1;
    }
    // sort names
    qsort(names, n, sizeof(*names), (int (*)(const void *,
		const void *))q_cmp);
    *namelist = names;

    return n;
}

/**
**	Read directory for menu.
**
**	@param name	directory path and name
**	@param flag_dir	only directories or files
**	@param filter	list of name suffix filters
**	@param cb_add	call back to handle directory entries
**	@param opaque	privat parameter for the call back
**
**	@retval <0	if any error occurs
**	@retval false	if no errors occurs
*/
int ReadDirectory(const char *name, int flag_dir, const NameFilter * filter,
    void (*cb_add) (void *, const char *), void *opaque)
{
    int i;
    int n;
    char **names;

    n = ScanDirectory(name, flag_dir, filter, &names);
    if (n >= 0) {

	for (i = 0; i < n; ++i) {	// add names to menu
	    cb_add(opaque, names[i]);
	    free(names[i]);
	}

	free(names);
    }

    return n;
}
