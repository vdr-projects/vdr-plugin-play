///
///	@file readdir.h		@brief directory reading module header file
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

///
///	Readdir name filter typedef
///
typedef struct __name_filter_
{
    int Length;				///< filter string length
    const char *String;			///< filter string
} NameFilter;

    /// check if filename is a directory
extern int IsDirectory(const char *);

    /// check if filename is an archive
extern int IsArchive(const char *);

    /// scan a directory
extern int ScanDirectory(const char *, int, const NameFilter *, char ***);

    /// read a directory
extern int ReadDirectory(const char *, int, const NameFilter *,
    void (*cb_add) (void *, const char *), void *);
