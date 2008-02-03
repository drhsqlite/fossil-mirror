/*
** Copyright (c) 2007 D. Richard Hipp
** Copyright (c) 2008 Stephan Beal
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License as published by the Free Software Foundation; either
** version 2 of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** General Public License for more details.
**
** You should have received a copy of the GNU General Public
** License along with this library; if not, write to the
** Free Software Foundation, Inc., 59 Temple Place - Suite 330,
** Boston, MA  02111-1307, USA.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** Implementation of the several string formatting routines.
*/
#include <assert.h>
#include "config.h"
#include "string_xform.h"

#if INTERFACE
/**
string_unary_xform_f is a typedef for funcs with the following policy:

They accept a const string which they then transform into some other
form. They return a transformed copy, which the caller is responsible
for freeing.

The intention of this is to provide a way for a generic query routine
to format specific column data (e.g. transform an object ID into a
link to that object).
*/
typedef char * (*string_unary_xform_f)( char const * );

#endif // INTERFACE

#if 0
/** A no-op transformer which can be used as a placeholder.

Conforms to the string_unary_xform_f typedef's policies.
*/
char * strxform_copy( char const * uuid )
{
  int len = strlen(uuid) + 1;
  char * ret = (char *) malloc( len );
  ret[len] = '\0';
  strncpy( ret, uuid, len-1 );
  return ret;
}
#endif

/**
Returns a hyperlink to uuid.

Conforms to the string_unary_xform_f typedef's policies.
*/
char * strxform_link_to_uuid( char const * uuid )
{
  const int offset = 10;
  char shortname[offset+1];
  shortname[offset] = '\0';
  memcpy( shortname, uuid, offset );
  return mprintf("<tt><a href='%s/vinfo/%s'><span style='font-size:1.5em'>"
                 "%s</span>%s</a></tt>",
                 g.zBaseURL, uuid, shortname, uuid+offset );
}

/** Returns a hyperlink to the given tag.

Conforms to the string_unary_xform_f typedef's policies.
*/
char * strxform_link_to_tagid( char const * tagid )
{
  return mprintf( "<a href='%s/tagview?tagid=%s'>%s</a>",
                  g.zBaseURL, tagid, tagid );
}

/** Returns a hyperlink to the named tag.

Conforms to the string_unary_xform_f typedef's policies.
*/
char * strxform_link_to_tagname( char const * tagid )
{
  return mprintf( "<a href='%s/tagview/%s'>%s</a>",
                  g.zBaseURL, tagid, tagid );
}



