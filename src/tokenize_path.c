#include <string.h>
#include <stdlib.h>
#include "tokenize_path.h"

/**
tokenize_path_free() is the only publically-defined way to deallocate
a string array created by tokenize_path().  It must be called exactly
once for each return value from tokenize_path(). Failing to call it
will result in a memory leak.

If (!p) then this function does nothing.  Passing a pointer which was
not returned from tokenize_path() will result in undefined behaviour.

After calling this, p's contents are invalid.

*/
void tokenize_path_free( char ** p )
{
  if( p )
  { 
    /* Free the tokenized strings (a single string, actually): */
    free( *(p-1) );
    /* Free p from its REAL starting point. */
    free( p-1 );
  }
}

/**
tokenize_path_is_separator() is the default predicate function for
tokenize_path(). It returns 1 if (c == '/'), else it returns 0.

*/
int tokenize_path_is_separator( int c )
{
  return (c == '/');
}
/**
   tokenize_path() takes a string, assumed to be a delimited
   null-terminated path-style string (like a path to a file), and
   tokenizes it into its component parts. The 'out' parameter (if not
   null) is set to the number of tokenized items (may be 0).

   The third argument is a unary predicate function which takes
   a single character and returns true only if that character
   is a "separator character". If the 3rd argument is 0 then
   tokenize_path_is_separator() is used.

   The function returns a list of strings (or 0) which must be freed
   via tokenize_path_free() because the internal allocation of the
   return result is a bit tricky (to minimize on allocations).  DO NOT
   pass the return result to free(), as that will cause undefined
   behaviour. Because the returned array is null-terminated, the second
   parameter is normally not needed because the array can safely
   be looped over without knowing its length in advance. Nonetheless,
   having the count before looping may be useful for some cases.

However,
   the returned array

   The returned string array is always null-terminated, to simplify
   looping over it. The function returns null if the input string is
   null, empty, or contains only separator characters.

   Tokenizing behaviour:

   - It assumes that ALL non-separator chars are entry names.

   - It treats runs of multiple separators chars as a single
   separator, NOT as a series of empty tokens.

   - It has no knowledge of relative or absolute paths, so
   "." and ".." are considered to be normal entries.

   - The returned strings are non-const, but the caller must not
   change their sizes or reallocate them at different memory
   addresses. The only legal way to deallocate them is with
   tokenize_path_free(). Changing the string content IS is legal.

   e.g.:

   "/path/to/nowhere" and "path/to///nowhere/" both parse to:

   Parses to: { "path", "to", "nowhere" }

   "/./../"

   Parses to: { ".", ".." }

   "http://foo.com/bar"

   Parses to: { "http:", "foo.com", "bar" }

   (Note that those arrays all have an implicit NULL entry as their
   last element. )


*/
char ** tokenize_path( char const * in,
		       int * out,
		       int (*predicate)( int )
		       )
{ /* Author: sgbeal@googlemail.com. License: Public Domain. */
  int ignored;
  if( ! out ) out = &ignored;
  *out = 0;
  typedef int (*sep_f)( int );
  sep_f is_sep = (predicate ? predicate : tokenize_path_is_separator);
  int inlen = strlen(in);
  if( (! in) || (0==inlen) ) return 0;
  char * cp = malloc( inlen + 1 );
  /**
     We make a copy because:
      
     Our algorithm is to replace separators with 0 in our copy, and
     use that copy as our return value. This allows us to avoid
     allocating a new string for each returned result.
  */
  strcpy( cp, in );
  /**
     buffsize = the largest possible number of return result we can
     have, plus 1 (to allow for truncated division). The maximum size
     is determined based on worst-case scenario: a list of single
     characters, each separated by one separators, e.g.  "/1/1/1/1/1"
  */
  const int buffsize = inlen / 2 + 1;
  /* 'starts' stores the starting point of each path component
     substring of 'cp'. When we slice up 'cp' below, starts[x]
     will be set to point to a particular position within 'cp'.
     That allows us to avoid allocating/copying each element
     separately.
  */
  char * starts[buffsize];
  int i = 0;
  for( i = 0; i < buffsize; ++i ) starts[i] = 0;

  char * curs = cp;
  for( curs = cp; is_sep(*curs); ++curs );
  /* ^^^ We skip leading separators so we can easily
     mark where the first entry string actually begins.
  */
  if( '\0' == curs )
  {
    free( cp );
    return 0;
  }
  char * mark = curs; /* placeholder for holding the head addr of strings. */
  int count = 0; /* total number of elements we end up tokenizing. */
  int started = 0; /* toggled when we enter a new path element. */
  for( ; *curs != '\0'; ++curs )
  {
    /** Replace '/' with '\0'... */
    if( is_sep(*curs) )
    {
      *curs = '\0';
      mark = curs+1;
      started = 0;
      continue;
    }
    if( ! started )
    { /** Start a new path element... */
      starts[count] = mark;
      started = 1;
      ++count;
    }
  }
  if( ! starts[0] )
  {
    free( cp );
    return 0;
  }
  cp[inlen] = '\0';
  char ** ret = calloc( count + 2, sizeof(char*) );
  /* We over-allocate by 2 entries. The first one holds the address of
   'cp' and the last one is set to 0 to simplify looping over the
   array. */
  *out = count;
  ret[0] = cp;
  ++ret;
  /**
     We're going to hide that [0] entry from the caller. Instead, we
     use that to hold the address of 'cp'. In tokenize_path_free()
     we release both that string and (ret-1).
  */
  for( i = 0; i < count; ++i )
  {
    ret[i] = starts[i];
  }
  ret[count] = 0;
  return ret;
}

extern void cgi_printf(const char *zFormat,...);
/**
render_linked_path() takes a root path and a /unix/style/path and
renders (using cgi_printf()) a clickable list of the entries in the
path. If path is null it does nothing. If root is null it is treated
as an empty string.

Example:

render_linked_path( "/AAA", "b/c/d" );

It would render a list similar to the following,
but think of the text in [brackets] as hyperlinked:

  [b]/[c]/[d]

  Each element is linked to a path like so:

  b: root/b
  c: root/b/c
  d: root/b/c/d

If root is null then the 'root/' part is not applied.

*/
void render_linked_path( char const * root,
                         char const * path )
{
  int count = 0;
  char ** toks = tokenize_path( path, &count, 0 );
  if( ! toks ) return;
  char const * t = 0;
  int pos = 0;
  for( t = toks[pos]; t; t = toks[++pos] )
  {
    cgi_printf( "<a href='" );
    if( root )
    {
      cgi_printf( "%s/", root );
    }
    int bpos = 0;
    for( ; bpos < pos; ++bpos )
    {
      cgi_printf("%s/", toks[bpos] );
    }
    cgi_printf("%s'>%s</a>", t, t );
    if( pos != (count-1) )
    {
      cgi_printf("/");
    }
  }
  tokenize_path_free( toks );
}

#if 0 /* set to 1 to compile a test app. */ 
#include <stdio.h>

static int sep_char = '?';
static int is_sep_char( int c )
{
  return c == sep_char;
}
int main( int argc, char ** argv )
{

  int count = 0;
  sep_char = ( (argc>2) ? (argv[2])[0] : '/');
  printf( "sep_char==%c\n",sep_char);
  char ** l = tokenize_path( argc==1 ? 0 : argv[1],
                             &count,
                             is_sep_char );
  printf( "parsed path: count=%d\n", count );
  if( ! count )
  {
    printf("error: path didn't parse :(\n");
    return 1;
  }
  char * x;
  int i = 0;
  for( x = l[0]; x; x = l[++i] )
  {
    printf( "\t%s\n", x );
  }
  tokenize_path_free( l );
  printf( "Bye!\n");
  return 0;
}
#endif
