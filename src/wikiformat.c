/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public
** License version 2 as published by the Free Software Foundation.
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
** This file contains code to do formatting of wiki text.
*/
#include <assert.h>
#include "config.h"
#include "wikiformat.h"

#if INTERFACE
/*
** Allowed wiki transformation operations
*/
#define WIKI_NOFOLLOW       0x001
#define WIKI_HTML           0x002
#endif


/*
** These are the only markup attributes allowed.
*/
#define ATTR_ALIGN              0x000001
#define ATTR_ALT                0x000002
#define ATTR_BGCOLOR            0x000004
#define ATTR_BORDER             0x000008
#define ATTR_CELLPADDING        0x000010
#define ATTR_CELLSPACING        0x000020
#define ATTR_CLEAR              0x000040
#define ATTR_COLOR              0x000080
#define ATTR_COLSPAN            0x000100
#define ATTR_COMPACT            0x000200
#define ATTR_FACE               0x000400
#define ATTR_HEIGHT             0x000800
#define ATTR_HREF               0x001000
#define ATTR_HSPACE             0x002000
#define ATTR_ID                 0x004000
#define ATTR_ROWSPAN            0x008000
#define ATTR_SIZE               0x010000
#define ATTR_SRC                0x020000
#define ATTR_START              0x040000
#define ATTR_TYPE               0x080000
#define ATTR_VALIGN             0x100000
#define ATTR_VALUE              0x200000
#define ATTR_VSPACE             0x400000
#define ATTR_WIDTH              0x800000

static const struct AllowedAttribute {
  const char *zName;
  unsigned int iMask;
} aAttribute[] = {
  { "align",         ATTR_ALIGN,          },
  { "alt",           ATTR_ALT,            },
  { "bgcolor",       ATTR_BGCOLOR,        },
  { "border",        ATTR_BORDER,         },
  { "cellpadding",   ATTR_CELLPADDING,    },
  { "cellspacing",   ATTR_CELLSPACING,    },
  { "clear",         ATTR_CLEAR,          },
  { "color",         ATTR_COLOR,          },
  { "colspan",       ATTR_COLSPAN,        },
  { "compact",       ATTR_COMPACT,        },
  { "face",          ATTR_FACE,           },
  { "height",        ATTR_HEIGHT,         },
  { "href",          ATTR_HREF,           },
  { "hspace",        ATTR_HSPACE,         },
  { "id",            ATTR_ID,             },
  { "rowspan",       ATTR_ROWSPAN,        },
  { "size",          ATTR_SIZE,           },
  { "src",           ATTR_SRC,            },
  { "start",         ATTR_START,          },
  { "type",          ATTR_TYPE,           },
  { "valign",        ATTR_VALIGN,         },
  { "value",         ATTR_VALUE,          },
  { "vspace",        ATTR_VSPACE,         },
  { "width",         ATTR_WIDTH,          },
};

/*
** Use binary search to locate a tag in the aAttribute[] table.
*/
static int findAttr(const char *z){
  int i, c, first, last;
  first = 0;
  last = sizeof(aAttribute)/sizeof(aAttribute[0]) - 1;
  while( first<=last ){
    i = (first+last)/2;
    c = strcmp(aAttribute[i].zName, z);
    if( c==0 ){
      return aAttribute[i].iMask;
    }else if( c<0 ){
      first = i+1;
    }else{
      last = i-1;
    }
  }
  return 0;
}



/*
** Allowed markup.
**
** Except for MARKUP_INVALID, this must all be in alphabetical order
** and in numerical sequence.  The first markup type must be zero.
** The value for MARKUP_XYZ must correspond to the <xyz> entry 
** in aAllowedMarkup[].
*/
#define MARKUP_INVALID         255
#define MARKUP_A                 0
#define MARKUP_ADDRESS           1
#define MARKUP_BIG               2
#define MARKUP_BLOCKQUOTE        3
#define MARKUP_B                 4
#define MARKUP_BR                5
#define MARKUP_CENTER            6
#define MARKUP_CITE              7
#define MARKUP_CODE              8
#define MARKUP_DD                9
#define MARKUP_DFN              10
#define MARKUP_DL               11
#define MARKUP_DT               12
#define MARKUP_EM               13
#define MARKUP_FONT             14
#define MARKUP_H1               15
#define MARKUP_H2               16
#define MARKUP_H3               17
#define MARKUP_H4               18
#define MARKUP_H5               19
#define MARKUP_H6               20
#define MARKUP_HR               21
#define MARKUP_IMG              22
#define MARKUP_I                23
#define MARKUP_KBD              24
#define MARKUP_LI               25
#define MARKUP_NOBR             26
#define MARKUP_NOWIKI           27
#define MARKUP_OL               28
#define MARKUP_P                29
#define MARKUP_PRE              30
#define MARKUP_SAMP             31
#define MARKUP_SMALL            32
#define MARKUP_S                33
#define MARKUP_STRIKE           34
#define MARKUP_STRONG           35
#define MARKUP_SUB              36
#define MARKUP_SUP              37
#define MARKUP_TABLE            38
#define MARKUP_TD               39
#define MARKUP_TH               40
#define MARKUP_TR               41
#define MARKUP_TT               42
#define MARKUP_UL               43
#define MARKUP_U                44
#define MARKUP_VAR              45
#define MARKUP_VERBATIM         46

/*
** The various markup is divided into the following types:
*/
#define MUTYPE_SINGLE      0x0001   /* <img>, <br>, or <hr> */
#define MUTYPE_BLOCK       0x0002   /* Forms a new paragraph. ex: <p>, <h2> */
#define MUTYPE_FONT        0x0004   /* Font changes. ex: <b>, <font>, <sub> */
#define MUTYPE_LINK        0x0008   /* Hyperlink: <a> */
#define MUTYPE_LIST        0x0010   /* Lists.  <ol>, <ul>, or <dl> */
#define MUTYPE_LI          0x0020   /* List items.  <li>, <dd>, <dt> */
#define MUTYPE_TABLE       0x0040   /* <table> */
#define MUTYPE_TR          0x0080   /* <tr> */
#define MUTYPE_TD          0x0100   /* <td> or <th> */
#define MUTYPE_SPECIAL     0x0200   /* <nowiki> or <verbatim> */
#define MUTYPE_HYPERLINK   0x0400   /* <a> */

#define MUTYPE_STACK  (MUTYPE_BLOCK | MUTYPE_FONT | MUTYPE_LIST | MUTYPE_TABLE)

static const struct AllowedMarkup {
  const char *zName;       /* Name of the markup */
  char iCode;              /* The MARKUP_* code */
  short int iType;         /* The MUTYPE_* code */
  int allowedAttr;         /* Allowed attributes on this markup */
} aMarkup[] = {
 { "a",             MARKUP_A,            MUTYPE_HYPERLINK,     ATTR_HREF },
 { "address",       MARKUP_ADDRESS,      MUTYPE_BLOCK,         0  },
 { "big",           MARKUP_BIG,          MUTYPE_FONT,          0  },
 { "blockquote",    MARKUP_BLOCKQUOTE,   MUTYPE_BLOCK,         0  },
 { "b",             MARKUP_B,            MUTYPE_FONT,          0  },
 { "br",            MARKUP_BR,           MUTYPE_SINGLE,        ATTR_CLEAR  },
 { "center",        MARKUP_CENTER,       MUTYPE_BLOCK,         0  },
 { "cite",          MARKUP_CITE,         MUTYPE_FONT,          0  },
 { "code",          MARKUP_CODE,         MUTYPE_FONT,          0  },
 { "dd",            MARKUP_DD,           MUTYPE_LI,            0  },
 { "dfn",           MARKUP_DFN,          MUTYPE_FONT,          0  },
 { "dl",            MARKUP_DL,           MUTYPE_LIST,          ATTR_COMPACT },
 { "dt",            MARKUP_DT,           MUTYPE_LI,            0  },
 { "em",            MARKUP_EM,           MUTYPE_FONT,          0  },
 { "font",          MARKUP_FONT,         MUTYPE_FONT,
                    ATTR_COLOR|ATTR_FACE|ATTR_SIZE   },
 { "h1",            MARKUP_H1,           MUTYPE_BLOCK,         ATTR_ALIGN  },
 { "h2",            MARKUP_H2,           MUTYPE_BLOCK,         ATTR_ALIGN  },
 { "h3",            MARKUP_H3,           MUTYPE_BLOCK,         ATTR_ALIGN  },
 { "h4",            MARKUP_H4,           MUTYPE_BLOCK,         ATTR_ALIGN  },
 { "h5",            MARKUP_H5,           MUTYPE_BLOCK,         ATTR_ALIGN  },
 { "h6",            MARKUP_H6,           MUTYPE_BLOCK,         ATTR_ALIGN  },
 { "hr",            MARKUP_HR,           MUTYPE_SINGLE,        
                    ATTR_ALIGN|ATTR_COLOR|ATTR_SIZE|ATTR_WIDTH  },
 { "img",           MARKUP_IMG,          MUTYPE_SINGLE,        
                    ATTR_ALIGN|ATTR_ALT|ATTR_BORDER|ATTR_HEIGHT|
                    ATTR_HSPACE|ATTR_SRC|ATTR_VSPACE|ATTR_WIDTH  },
 { "i",             MARKUP_I,            MUTYPE_FONT,          0  },
 { "kbd",           MARKUP_KBD,          MUTYPE_FONT,          0  },
 { "li",            MARKUP_LI,           MUTYPE_LI,            
                    ATTR_TYPE|ATTR_VALUE  },
 { "nobr",          MARKUP_NOBR,         MUTYPE_FONT,          0  },
 { "nowiki",        MARKUP_NOWIKI,       MUTYPE_SPECIAL,       0  },
 { "ol",            MARKUP_OL,           MUTYPE_LIST,          
                    ATTR_START|ATTR_TYPE|ATTR_COMPACT  },
 { "p",             MARKUP_P,            MUTYPE_BLOCK,         ATTR_ALIGN  },
 { "pre",           MARKUP_PRE,          MUTYPE_BLOCK,         0  },
 { "samp",          MARKUP_SAMP,         MUTYPE_FONT,          0  },
 { "small",         MARKUP_SMALL,        MUTYPE_FONT,          0  },
 { "s",             MARKUP_S,            MUTYPE_FONT,          0  },
 { "strike",        MARKUP_STRIKE,       MUTYPE_FONT,          0  },
 { "strong",        MARKUP_STRONG,       MUTYPE_FONT,          0  },
 { "sub",           MARKUP_SUB,          MUTYPE_FONT,          0  },
 { "sup",           MARKUP_SUP,          MUTYPE_FONT,          0  },
 { "table",         MARKUP_TABLE,        MUTYPE_TABLE,         
                    ATTR_ALIGN|ATTR_BGCOLOR|ATTR_BORDER|ATTR_CELLPADDING|
                    ATTR_CELLSPACING|ATTR_HSPACE|ATTR_VSPACE  },
 { "td",            MARKUP_TD,           MUTYPE_TD,            
                    ATTR_ALIGN|ATTR_BGCOLOR|ATTR_COLSPAN|
                    ATTR_ROWSPAN|ATTR_VALIGN  },
 { "th",            MARKUP_TH,           MUTYPE_TD,
                    ATTR_ALIGN|ATTR_BGCOLOR|ATTR_COLSPAN|
                    ATTR_ROWSPAN|ATTR_VALIGN  },
 { "tr",            MARKUP_TR,           MUTYPE_TR, 
                    ATTR_ALIGN|ATTR_BGCOLOR||ATTR_VALIGN  },
 { "tt",            MARKUP_TT,           MUTYPE_FONT,          0  },
 { "ul",            MARKUP_UL,           MUTYPE_LIST,          
                    ATTR_TYPE|ATTR_COMPACT  },
 { "u",             MARKUP_U,            MUTYPE_FONT,          0  },
 { "var",           MARKUP_VAR,          MUTYPE_FONT,          0  },
 { "verbatim",      MARKUP_VERBATIM,     MUTYPE_SPECIAL,       ATTR_ID },
};

/*
** Use binary search to locate a tag in the aMarkup[] table.
*/
static int findTag(const char *z){
  int i, c, first, last;
  first = 0;
  last = sizeof(aMarkup)/sizeof(aMarkup[0]) - 1;
  while( first<=last ){
    i = (first+last)/2;
    c = strcmp(aMarkup[i].zName, z);
    if( c==0 ){
      assert( aMarkup[i].iCode==i );
      return i;
    }else if( c<0 ){
      first = i+1;
    }else{
      last = i-1;
    }
  }
  return MARKUP_INVALID;
}

/*
** Token types
*/
#define TOKEN_MARKUP        1    /* <...> */
#define TOKEN_CHARACTER     2    /* "&" or "<" not part of markup */
#define TOKEN_LINK          3    /* [...] */
#define TOKEN_PARAGRAPH     4    /* blank lines */
#define TOKEN_NEWLINE       5    /* A single "\n" */
#define TOKEN_BULLET        6    /*  "  *  " */
#define TOKEN_ENUM          7    /*  "  \(?\d+[.)]?  " */
#define TOKEN_INDENT        8    /*  "   " */
#define TOKEN_TEXT          9    /* None of the above */

/*
** State flags
*/
#define AT_NEWLINE        0x001  /* At start of a line */
#define AT_PARAGRAPH      0x002  /* At start of a paragraph */
#define ALLOW_WIKI        0x004  /* Allow wiki markup */
#define FONT_MARKUP_ONLY  0x008  /* Only allow MUTYPE_FONT markup */
#define IN_LIST           0x010  /* Within <ul> */

/*
** z points to a "<" character.  Check to see if this is the start of
** a valid markup.  If it is, return the total number of characters in
** the markup including the initial "<" and the terminating ">".  If
** it is not well-formed markup, return 0.
*/
static int markupLength(const char *z){
  int n = 1;
  int inparen = 0;
  if( z[n]=='/' ){ n++; }
  if( !isalpha(z[n]) ) return 0;
  while( isalpha(z[n]) ){ n++; }
  if( z[n]!='>' && !isspace(z[n]) ) return 0;
  while( z[n] && (z[n]!='>' || inparen) ){
    if( z[n]=='"' ){
      inparen = !inparen;
    }
  }
  if( z[n]!='>' ) return 0;
  return n+1;
}

/*
** z points to a "\n" character.  Check to see if this newline is
** followed by one or more blank lines.  If it is, return the number
** of characters through the closing "\n".  If not, return 0.
*/
static int paragraphBreakLength(const char *z){
  int i, n;
  int nNewline = 1;
  for(i=1, n=0; isspace(z[i]); i++){
    if( z[i]=='\n' ){
      nNewline++;
      n = i;
    }
  }
  if( nNewline>=2 ){
    return n+1;
  }else{
    return 0;
  }
}

/*
** Return the number of characters until the next "interesting"
** characters.
**
** Interesting characters are:
**
**      <
**      &
**      \n
**      [
**
** The "[" and "\n" are only considered interesting if the "useWiki"
** flag is set.
*/
static int textLength(const char *z, int useWiki){
  int n = 0;
  int c;
  while( (c = z[0])!=0 && c!='<' && c!='&' &&
               (useWiki==0 || (c!='[' && c!='\n')) ){
    n++;
    z++;
  }
  return n; 
}

/*
** Return true if z[] begins with an HTML character element.
*/
static int isElement(const char *z){
  int i;
  assert( z[0]=='&' );
  if( z[1]=='#' ){
    for(i=2; isdigit(z[i]); i++){}
    return i>2 && z[i]==';';
  }else{
    for(i=1; isalpha(z[i]); i++){}
    return i>1 && z[i]==';';
  }
}

/*
** Check to see if the z[] string is the beginning of a wiki bullet.
** If it is, return the length of the bullet text.  Otherwise return 0.
*/
static int bulletLength(const char *z){
  int i, n;
  n = 0;
  i = 0;
  while( z[n]==' ' || z[n]=='\t' ){
    if( z[n]=='\t' ) i++;
    i++;
    n++;
  }
  if( i<2 || z[n]!='*' ) return 0;
  n++;
  i = 0;
  while( z[n]==' ' || z[n]=='\t' ){
    if( z[n]=='\t' ) i++;
    i++;
    n++;
  }
  if( i<2 || isspace(z[n]) ) return 0;
  return n;
}

/*
** Check to see if the z[] string is the beginning of an indented
** paragraph.  If it is, return the length of the indent.  Otherwise
** return 0.
*/
static int indentLength(const char *z){
  int i, n;
  n = 0;
  i = 0;
  while( z[n]==' ' || z[n]=='\t' ){
    if( z[n]=='\t' ) i++;
    i++;
    n++;
  }
  if( i<2 || isspace(z[n]) ) return 0;
  return n;
}

/*
** Check to see if the z[] string is a wiki hyperlink.  If it is,
** return the length of the hyperlink.  Otherwise return 0.
*/
static int linkLength(const char *z){
  int n;
  assert( z[0]=='[' );
  for(n=0; z[n] && z[n]!=']'; n++){}
  if( z[n]==']' ){
    return n+1;
  }else{
    return 0;
  }
}


/*
** z points to the start of a token.  Return the number of
** characters in that token.  Write the token type into *pTokenType.
*/
static int nextToken(const char *z, int state, int *pTokenType){
  int n;
  if( z[0]=='<' ){
    n = markupLength(z);
    if( n>0 ){
      *pTokenType = TOKEN_MARKUP;
      return n;
    }else{
      *pTokenType = TOKEN_CHARACTER;
      return 1;
    }
  }
  if( z[0]=='&' && !isElement(z) ){
    *pTokenType = TOKEN_CHARACTER;
    return 1;
  }
  if( (state & ALLOW_WIKI)!=0 ){
    if( z[0]=='\n' ){
      n = paragraphBreakLength(z);
      if( n>0 ){
        *pTokenType = TOKEN_PARAGRAPH;
        return n;
      }else if( isspace(z[1]) ){
        *pTokenType = TOKEN_NEWLINE;
        return 1;
      }
    }
    if( (state & AT_NEWLINE)!=0 /* && (state & (AT_PARAGRAPH|IN_LIST))!=0 */
             && isspace(z[0]) ){
      n = bulletLength(z);
      if( n>0 ){
        *pTokenType = TOKEN_BULLET;
        return n;
      }
#if 0
      n = enumLength(z);
      if( n>0 ){
        *pTokenType = TOKEN_ENUM;
        return n;
      }
#endif
    }
    if( (state & AT_PARAGRAPH)!=0 && isspace(z[0]) ){
      n = indentLength(z);
      if( n>0 ){
        *pTokenType = TOKEN_INDENT;
        return n;
      }
    }
    if( z[0]=='[' && (n = linkLength(z))>0 ){
      *pTokenType = TOKEN_LINK;
      return n;
    }
  }
  *pTokenType = TOKEN_TEXT;
  return 1 + textLength(z+1, state & ALLOW_WIKI);
}

/*
** A single markup is parsed into an instance of the following
** structure.
*/
typedef struct ParsedMarkup ParsedMarkup;
struct ParsedMarkup {
  unsigned char endTag;   /* True if </...> instead of <...> */
  unsigned char iCode;    /* MARKUP_* */
  unsigned char nAttr;    /* Number of attributes */
  unsigned short iType;   /* MUTYPE_* */
  struct {
    unsigned char iCode;     /* ATTR_* */
    char *zValue;            /* Argument to this attribute.  Might be NULL */
    char cTerm;              /* Original argument termination character */
  } aAttr[10];
};

/*
** z[] is an HTML markup element - something that begins with '<'.
** Parse this element into the p structure.
**
** The content of z[] might be modified by converting characters 
** to lowercase and by inserting some "\000" characters.
*/
static void parseMarkup(ParsedMarkup *p, char *z){
  int i, c;
  char *zTag, *zValue;
  int seen = 0;

  if( z[1]=='/' ){
    p->endTag = 1;
    i = 2;
  }else{
    p->endTag = 0;
    i = 1;
  }
  zTag = &z[i];
  while( isalnum(z[i]) ){ 
    z[i] = tolower(z[i]);
    i++;
  }
  c = z[i];
  z[i] = 0;
  p->iCode = findTag(zTag);
  p->iType = aMarkup[p->iCode].iType;
  p->nAttr = 0;
  z[i] = c;
  while( isspace(z[i]) ){ i++; }
  while( p->nAttr<8 && isalpha(z[i]) ){
    zTag = &z[i];
    while( isalnum(z[i]) ){ 
      z[i] = tolower(z[i]);
      i++;
    }
    c = z[i];
    z[i] = 0;
    p->aAttr[p->nAttr].iCode = findAttr(zTag);
    z[i] = c;
    while( isspace(z[i]) ){ z++; }
    if( z[i]!='=' ){
      p->aAttr[p->nAttr].zValue = 0;
      p->aAttr[p->nAttr].cTerm = 0;
      c = 0;
    }else{
      i++;
      while( isspace(z[i]) ){ z++; }
      if( z[i]=='"' ){
        i++;
        zValue = &z[i];
        while( z[i] && z[i]!='"' ){ i++; }
      }else{
        zValue = &z[i];
        while( !isspace(z[i]) && z[i]!='>' ){ z++; }
      }
      p->aAttr[p->nAttr].zValue = zValue;
      p->aAttr[p->nAttr].cTerm = c = z[i];
      z[i] = 0;
      i++;
    }
    if( p->aAttr[p->nAttr].iCode!=0 && (seen & p->aAttr[p->nAttr].iCode)==0 ){
      seen |= p->aAttr[p->nAttr].iCode;
      p->nAttr++;
    }
    if( c=='>' ) break;
  }
}

/*
** Render markup on the given blob.
*/
static void renderMarkup(Blob *pOut, ParsedMarkup *p){
  int i;
  if( p->endTag ){
    blob_appendf(pOut, "</%s>", aMarkup[p->iCode].zName);
  }else{
    blob_appendf(pOut, "<%s", aMarkup[p->iCode].zName);
    for(i=0; i<p->nAttr; i++){
      blob_appendf(pOut, " %s", aAttribute[p->aAttr[i].iCode]);
      if( p->aAttr[i].zValue ){
        blob_appendf(pOut, "=\"%s\"", p->aAttr[i].zValue);
      }
    }
    blob_append(pOut, ">", 1);
  }
}

/*
** When the markup was parsed, some "\000" may have been inserted.
** This routine restores to those "\000" values back to their
** original content.
*/
static void unparseMarkup(ParsedMarkup *p){
  int i, n;
  for(i=0; i<p->nAttr; i++){
    char *z = p->aAttr[i].zValue;
    if( z==0 ) continue;
    n = strlen(z);
    z[n] = p->aAttr[i].cTerm;
  }
}

/*
** Current state of the rendering engine
*/
typedef struct Renderer Renderer;
struct Renderer {
  Blob *pOut;                 /* Output appended to this blob */
  int state;                  /* Flag that govern rendering */
  int inVerbatim;             /* True in <verbatim> mode */
  int preVerbState;           /* Value of state prior to verbatim */
  const char *zVerbatimId;    /* The id= attribute of <verbatim> */
  int nStack;                 /* Number of elements on the stack */
  int nAlloc;                 /* Space allocated for aStack */
  unsigned char *aStack;      /* Open markup stack */
};

/*
** Pop a single element off of the stack.  As the element is popped,
** output its end tag.
*/
static void popStack(Renderer *p){
  if( p->nStack ){
    p->nStack--;
    blob_appendf(p->pOut, "</%s>", aMarkup[p->aStack[p->nStack]].zName);
  }
}

/*
** Push a new markup value onto the stack.  Enlarge the stack
** if necessary.
*/
static void pushStack(Renderer *p, int elem){
  if( p->nStack>=p->nAlloc ){
    p->nAlloc = p->nAlloc*2 + 100;
    p->aStack = realloc(p->aStack, p->nAlloc);
    if( p->aStack==0 ){
      fossil_panic("out of memory");
    }
  }
  p->aStack[p->nStack++] = elem;
}

/*
** Pop the stack until the top-most iTag element is removed.
** If there is no iTag element on the stack, this routine
** is a no-op.
*/
static void popStackToTag(Renderer *p, int iTag){
  int i;
  for(i=p->nStack-1; i>=0 && p->aStack[i]!=iTag; i--){}
  if( i<0 ) return;
  while( p->nStack>i ){
    popStack(p);
  }
}

/*
** Pop the stack until the top-most element of the stack
** is an element that matches the type in iMask.  Return
** true on success.  If the stack does not have an element
** that matches iMask, then leave the stack unchanged and
** return false.
*/
static int backupToType(Renderer *p, int iMask){
  int i;
  for(i=p->nStack-1; i>=0 && (aMarkup[p->aStack[i]].iType&iMask)==0; i--){}
  if( i<0 ) return 0;
  i++;
  while( p->nStack>i ){
    popStack(p);
  }
  return 1;
}

/*
** Add missing markup in preparation for writing text.
**
** "Missing" markup are things like start tags for table rows
** or table columns or paragraphs that are omitted from input.
*/
static void addMissingMarkup(Renderer *p){
  /* TBD */
}

/*
** Resolve a hyperlink.  The argument is the content of the [...]
** in the wiki.  Append the URL to the given blob.
*/
static void resolveHyperlink(const char *zTarget, Blob *pOut){
  blob_appendf(pOut, "http://www.fossil-scm.org/test-%T", zTarget);
}

/*
** Check to see if the given parsed markup is the correct
** </verbatim> tag.
*/
static int endVerbatim(Renderer *p, ParsedMarkup *pMarkup){
  char *z;
  assert( p->inVerbatim );
  if( pMarkup->iCode!=MARKUP_VERBATIM ) return 0;
  if( !pMarkup->endTag ) return 0;
  if( p->zVerbatimId==0 ) return 1;
  if( pMarkup->nAttr!=1 ) return 0;
  z = pMarkup->aAttr[0].zValue;
  return strcmp(z, p->zVerbatimId)==0;
}

/*
** Return the MUTYPE for the top of the stack.
*/
static int stackTopType(Renderer *p){
  if( p->nStack<=0 ) return 0;
  return aMarkup[p->aStack[p->nStack-1]].iType;
}

/*
** Convert the wiki in z[] into html in the renderer p.  The
** renderer has already been initialized.
**
** This routine will probably modify the content of z[].
*/
static void wiki_render(Renderer *p, char *z){
  int tokenType;
  ParsedMarkup markup;
  int n;

  while( z[0] ){
    n = nextToken(z, p->state, &tokenType);
    p->state &= ~(AT_NEWLINE|AT_PARAGRAPH);
    switch( tokenType ){
      case TOKEN_PARAGRAPH: {
        blob_append(p->pOut, "\n\n<p>", -1);
        p->state |= AT_PARAGRAPH|AT_NEWLINE;
        popStackToTag(p, MARKUP_P);
        break;
      }
      case TOKEN_NEWLINE: {
        blob_append(p->pOut, "\n", 1);
        p->state |= AT_NEWLINE;
        break;
      }
      case TOKEN_BULLET: {
        if( backupToType(p, MUTYPE_LIST)==0 ){
          pushStack(p, MARKUP_UL);
          blob_append(p->pOut, "<ul>", 4);
        }
        pushStack(p, MARKUP_LI);
        blob_append(p->pOut, "<li>", 4);
        break;
      }
      case TOKEN_CHARACTER: {
        if( z[0]=='<' ){
          blob_append(p->pOut, "&lt;", 4);
        }else if( z[0]=='&' ){
          blob_append(p->pOut, "&amp;", 5);
        }
        break;
      }
      case TOKEN_LINK: {
        char *zTarget;
        char *zDisplay = 0;
        int i;
        int savedState;
        addMissingMarkup(p);
        zTarget = &z[1];
        for(i=1; z[i] && z[i]!=']'; i++){
          if( z[i]=='|' && zDisplay==0 ){
            zDisplay = &z[i+1];
            z[i] = 0;
          }
        }
        z[i] = 0;
        if( zDisplay==0 ){
          zDisplay = zTarget;
        }else{
          while( isspace(*zDisplay) ) zDisplay++;
        }
        blob_append(p->pOut, "<a href=\"", -1);
        resolveHyperlink(zTarget, p->pOut);
        blob_append(p->pOut, "\">", -1);
        savedState = p->state;
        p->state &= ~ALLOW_WIKI;
        p->state |= FONT_MARKUP_ONLY;
        wiki_render(p, zDisplay);
        p->state = savedState;
        blob_append(p->pOut, "</a>", 4);
        break;
      }
      case TOKEN_TEXT: {
        addMissingMarkup(p);
        blob_append(p->pOut, z, n);
        break;
      }
      case TOKEN_MARKUP: {
        parseMarkup(&markup, z);
        if( p->inVerbatim ){
          if( endVerbatim(p, &markup) ){
            p->inVerbatim = 0;
            p->state = p->preVerbState;
            blob_append(p->pOut, "</pre>", 6);
          }else{
            unparseMarkup(&markup);
            blob_append(p->pOut, "&lt;", 4);
            n = 1;
          }
        }else if( markup.iCode==MARKUP_INVALID ){
          blob_append(p->pOut, "&lt;", 4);
          n = 1;
        }else if( (markup.iType&MUTYPE_FONT)==0
                    && (p->state & FONT_MARKUP_ONLY)!=0 ){
          /* Do nothing */
        }else if( markup.iCode==MARKUP_NOWIKI ){
          if( markup.endTag ){
            p->state |= ALLOW_WIKI;
          }else{
            p->state &= ALLOW_WIKI;
          }
        }else if( markup.endTag ){
          popStackToTag(p, markup.iCode);
        }else if( markup.iCode==MARKUP_VERBATIM ){
          if( markup.nAttr==1 ){
            p->zVerbatimId = markup.aAttr[0].zValue;
          }else{
            p->zVerbatimId = 0;
          }
          p->inVerbatim = 1;
          p->preVerbState = p->state;
          p->state &= ~ALLOW_WIKI;
          blob_append(p->pOut, "<pre>", 5);
        }else if( markup.iType==MUTYPE_LI ){
          if( backupToType(p, MUTYPE_LIST)==0 ){
            pushStack(p, MARKUP_UL);
            blob_append(p->pOut, "<ul>", 4);
          }
          pushStack(p, MARKUP_LI);
          renderMarkup(p->pOut, &markup);
        }else if( markup.iType==MUTYPE_TR ){
          if( backupToType(p, MUTYPE_TABLE) ){
            pushStack(p, MARKUP_TR);
            renderMarkup(p->pOut, &markup);
          }
        }else if( markup.iType==MUTYPE_TD ){
          if( backupToType(p, MUTYPE_TABLE|MUTYPE_TR) ){
            if( stackTopType(p)==MUTYPE_TABLE ){
              pushStack(p, MARKUP_TR);
              blob_append(p->pOut, "<tr>", 4);
            }
            pushStack(p, markup.iCode);
            renderMarkup(p->pOut, &markup);
          }
        }else{
          if( (markup.iType & MUTYPE_STACK )!=0 ){
            pushStack(p, markup.iCode);
          }
          renderMarkup(p->pOut, &markup);
        }
        break;
      }
    }
    z += n;
  }
}


/*
** Transform the text in the pIn blob.  Write the results
** into the pOut blob.  The pOut blob should already be
** initialized.  The output is merely appended to pOut.
**
** The transformations carried out depend on the ops flag:
**
** WIKI_NOFOLLOW
**
**     * Add the nofollow attribute to external links
**
** WIKI_HTML
**
**     * Convert wiki into HTML
**     * Remove <nowiki> and <verbatium>
**     * Convert & into &amp;
**     * Unrecognized markup and markup within <verbatim>
**       is converted into &lt;...&gt;
**     * Unauthorized attributes on markup are removed
*/
void wiki_convert(Blob *pIn, Blob *pOut, int ops){
  char *z;
  int n;
  Renderer renderer;
  
  memset(&renderer, 0, sizeof(renderer));
  renderer.state = ALLOW_WIKI|AT_NEWLINE|AT_PARAGRAPH;
  renderer.pOut = pOut;

  z = blob_str(pIn);
  wiki_render(&renderer, z);
  while( renderer.nStack ){
    popStack(&renderer);
  }
  blob_append(pOut, "\n", 1);
  free(renderer.aStack);
}


/*
** COMMAND: test-wiki-render
*/
void test_wiki_render(void){
  Blob in, out;
  if( g.argc!=3 ) usage("FILE");
  blob_zero(&out);
  blob_read_from_file(&in, g.argv[2]);
  wiki_convert(&in, &out, WIKI_HTML);
  blob_write_to_file(&out, "-");
}
