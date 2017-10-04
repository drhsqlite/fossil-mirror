/*
** Copyright (c) 2007 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)

** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code to do formatting of wiki text.
*/
#include "config.h"
#include <assert.h>
#include "wikiformat.h"

#if INTERFACE
/*
** Allowed wiki transformation operations
*/
#define WIKI_HTMLONLY       0x001  /* HTML markup only.  No wiki */
#define WIKI_INLINE         0x002  /* Do not surround with <p>..</p> */
#define WIKI_NOBLOCK        0x004  /* No block markup of any kind */
#define WIKI_BUTTONS        0x008  /* Allow sub-menu buttons */
#define WIKI_NOBADLINKS     0x010  /* Ignore broken hyperlinks */
#define WIKI_LINKSONLY      0x020  /* No markup.  Only decorate links */
#endif


/*
** These are the only markup attributes allowed.
*/
enum allowed_attr_t {
  ATTR_ALIGN = 1,
  ATTR_ALT,
  ATTR_BGCOLOR,
  ATTR_BORDER,
  ATTR_CELLPADDING,
  ATTR_CELLSPACING,
  ATTR_CLASS,
  ATTR_CLEAR,
  ATTR_COLOR,
  ATTR_COLSPAN,
  ATTR_COMPACT,
  ATTR_FACE,
  ATTR_HEIGHT,
  ATTR_HREF,
  ATTR_HSPACE,
  ATTR_ID,
  ATTR_LINKS,
  ATTR_NAME,
  ATTR_ROWSPAN,
  ATTR_SIZE,
  ATTR_SRC,
  ATTR_START,
  ATTR_STYLE,
  ATTR_TARGET,
  ATTR_TYPE,
  ATTR_VALIGN,
  ATTR_VALUE,
  ATTR_VSPACE,
  ATTR_WIDTH
};

enum amsk_t {
  AMSK_ALIGN        = 0x00000001,
  AMSK_ALT          = 0x00000002,
  AMSK_BGCOLOR      = 0x00000004,
  AMSK_BORDER       = 0x00000008,
  AMSK_CELLPADDING  = 0x00000010,
  AMSK_CELLSPACING  = 0x00000020,
  AMSK_CLASS        = 0x00000040,
  AMSK_CLEAR        = 0x00000080,
  AMSK_COLOR        = 0x00000100,
  AMSK_COLSPAN      = 0x00000200,
  AMSK_COMPACT      = 0x00000400,
  /* re-use         = 0x00000800, */
  AMSK_FACE         = 0x00001000,
  AMSK_HEIGHT       = 0x00002000,
  AMSK_HREF         = 0x00004000,
  AMSK_HSPACE       = 0x00008000,
  AMSK_ID           = 0x00010000,
  AMSK_LINKS        = 0x00020000,
  AMSK_NAME         = 0x00040000,
  AMSK_ROWSPAN      = 0x00080000,
  AMSK_SIZE         = 0x00100000,
  AMSK_SRC          = 0x00200000,
  AMSK_START        = 0x00400000,
  AMSK_STYLE        = 0x00800000,
  AMSK_TARGET       = 0x01000000,
  AMSK_TYPE         = 0x02000000,
  AMSK_VALIGN       = 0x04000000,
  AMSK_VALUE        = 0x08000000,
  AMSK_VSPACE       = 0x10000000,
  AMSK_WIDTH        = 0x20000000
};

static const struct AllowedAttribute {
  const char *zName;
  unsigned int iMask;
} aAttribute[] = {
  /* These indexes MUST line up with their
     corresponding allowed_attr_t enum values.
  */
  { 0, 0 },
  { "align",         AMSK_ALIGN          },
  { "alt",           AMSK_ALT            },
  { "bgcolor",       AMSK_BGCOLOR        },
  { "border",        AMSK_BORDER         },
  { "cellpadding",   AMSK_CELLPADDING    },
  { "cellspacing",   AMSK_CELLSPACING    },
  { "class",         AMSK_CLASS          },
  { "clear",         AMSK_CLEAR          },
  { "color",         AMSK_COLOR          },
  { "colspan",       AMSK_COLSPAN        },
  { "compact",       AMSK_COMPACT        },
  { "face",          AMSK_FACE           },
  { "height",        AMSK_HEIGHT         },
  { "href",          AMSK_HREF           },
  { "hspace",        AMSK_HSPACE         },
  { "id",            AMSK_ID             },
  { "links",         AMSK_LINKS          },
  { "name",          AMSK_NAME           },
  { "rowspan",       AMSK_ROWSPAN        },
  { "size",          AMSK_SIZE           },
  { "src",           AMSK_SRC            },
  { "start",         AMSK_START          },
  { "style",         AMSK_STYLE          },
  { "target",        AMSK_TARGET         },
  { "type",          AMSK_TYPE           },
  { "valign",        AMSK_VALIGN         },
  { "value",         AMSK_VALUE          },
  { "vspace",        AMSK_VSPACE         },
  { "width",         AMSK_WIDTH          },
};

/*
** Use binary search to locate a tag in the aAttribute[] table.
*/
static int findAttr(const char *z){
  int i, c, first, last;
  first = 1;
  last = count(aAttribute) - 1;
  while( first<=last ){
    i = (first+last)/2;
    c = fossil_strcmp(aAttribute[i].zName, z);
    if( c==0 ){
      return i;
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
#define MARKUP_INVALID            0
#define MARKUP_A                  1
#define MARKUP_ADDRESS            2
#define MARKUP_HTML5_ARTICLE      3
#define MARKUP_HTML5_ASIDE        4
#define MARKUP_B                  5
#define MARKUP_BIG                6
#define MARKUP_BLOCKQUOTE         7
#define MARKUP_BR                 8
#define MARKUP_CENTER             9
#define MARKUP_CITE               10
#define MARKUP_CODE               11
#define MARKUP_COL                12
#define MARKUP_COLGROUP           13
#define MARKUP_DD                 14
#define MARKUP_DFN                15
#define MARKUP_DIV                16
#define MARKUP_DL                 17
#define MARKUP_DT                 18
#define MARKUP_EM                 19
#define MARKUP_FONT               20
#define MARKUP_HTML5_FOOTER       21
#define MARKUP_H1                 22
#define MARKUP_H2                 23
#define MARKUP_H3                 24
#define MARKUP_H4                 25
#define MARKUP_H5                 26
#define MARKUP_H6                 27
#define MARKUP_HTML5_HEADER       28
#define MARKUP_HR                 29
#define MARKUP_I                  30
#define MARKUP_IMG                31
#define MARKUP_KBD                32
#define MARKUP_LI                 33
#define MARKUP_HTML5_NAV          34
#define MARKUP_NOBR               35
#define MARKUP_NOWIKI             36
#define MARKUP_OL                 37
#define MARKUP_P                  38
#define MARKUP_PRE                39
#define MARKUP_S                  40
#define MARKUP_SAMP               41
#define MARKUP_HTML5_SECTION      42
#define MARKUP_SMALL              43
#define MARKUP_SPAN               44
#define MARKUP_STRIKE             45
#define MARKUP_STRONG             46
#define MARKUP_SUB                47
#define MARKUP_SUP                48
#define MARKUP_TABLE              49
#define MARKUP_TBODY              50
#define MARKUP_TD                 51
#define MARKUP_TFOOT              52
#define MARKUP_TH                 53
#define MARKUP_THEAD              54
#define MARKUP_TITLE              55
#define MARKUP_TR                 56
#define MARKUP_TT                 57
#define MARKUP_U                  58
#define MARKUP_UL                 59
#define MARKUP_VAR                60
#define MARKUP_VERBATIM           61

/*
** The various markup is divided into the following types:
*/
#define MUTYPE_SINGLE      0x0001   /* <img>, <br>, or <hr> */
#define MUTYPE_BLOCK       0x0002   /* Forms a new paragraph. ex: <p>, <h2> */
#define MUTYPE_FONT        0x0004   /* Font changes. ex: <b>, <font>, <sub> */
#define MUTYPE_LIST        0x0010   /* Lists.  <ol>, <ul>, or <dl> */
#define MUTYPE_LI          0x0020   /* List items.  <li>, <dd>, <dt> */
#define MUTYPE_TABLE       0x0040   /* <table> */
#define MUTYPE_TR          0x0080   /* <tr> */
#define MUTYPE_TD          0x0100   /* <td> or <th> */
#define MUTYPE_SPECIAL     0x0200   /* <nowiki> or <verbatim> */
#define MUTYPE_HYPERLINK   0x0400   /* <a> */

/*
** These markup types must have an end tag.
*/
#define MUTYPE_STACK  (MUTYPE_BLOCK | MUTYPE_FONT | MUTYPE_LIST | MUTYPE_TABLE)

/*
** This markup types are allowed for "inline" text.
*/
#define MUTYPE_INLINE (MUTYPE_FONT | MUTYPE_HYPERLINK)

static const struct AllowedMarkup {
  const char *zName;       /* Name of the markup */
  char iCode;              /* The MARKUP_* code */
  short int iType;         /* The MUTYPE_* code */
  int allowedAttr;         /* Allowed attributes on this markup */
} aMarkup[] = {
 { 0,               MARKUP_INVALID,      0,                    0  },
 { "a",             MARKUP_A,            MUTYPE_HYPERLINK,
                    AMSK_HREF|AMSK_NAME|AMSK_CLASS|AMSK_TARGET|AMSK_STYLE },
 { "address",       MARKUP_ADDRESS,      MUTYPE_BLOCK,         AMSK_STYLE },
 { "article",       MARKUP_HTML5_ARTICLE, MUTYPE_BLOCK,
                                            AMSK_ID|AMSK_CLASS|AMSK_STYLE },
 { "aside",         MARKUP_HTML5_ASIDE,  MUTYPE_BLOCK,
                                            AMSK_ID|AMSK_CLASS|AMSK_STYLE },

 { "b",             MARKUP_B,            MUTYPE_FONT,          AMSK_STYLE },
 { "big",           MARKUP_BIG,          MUTYPE_FONT,          AMSK_STYLE },
 { "blockquote",    MARKUP_BLOCKQUOTE,   MUTYPE_BLOCK,         AMSK_STYLE },
 { "br",            MARKUP_BR,           MUTYPE_SINGLE,        AMSK_CLEAR },
 { "center",        MARKUP_CENTER,       MUTYPE_BLOCK,         AMSK_STYLE },
 { "cite",          MARKUP_CITE,         MUTYPE_FONT,          AMSK_STYLE },
 { "code",          MARKUP_CODE,         MUTYPE_FONT,          AMSK_STYLE },
 { "col",           MARKUP_COL,          MUTYPE_SINGLE,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_COLSPAN|AMSK_WIDTH|AMSK_STYLE },
 { "colgroup",      MARKUP_COLGROUP,     MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_COLSPAN|AMSK_WIDTH|AMSK_STYLE},
 { "dd",            MARKUP_DD,           MUTYPE_LI,            AMSK_STYLE },
 { "dfn",           MARKUP_DFN,          MUTYPE_FONT,          AMSK_STYLE },
 { "div",           MARKUP_DIV,          MUTYPE_BLOCK,
                    AMSK_ID|AMSK_CLASS|AMSK_STYLE },
 { "dl",            MARKUP_DL,           MUTYPE_LIST,
                    AMSK_COMPACT|AMSK_STYLE },
 { "dt",            MARKUP_DT,           MUTYPE_LI,            AMSK_STYLE },
 { "em",            MARKUP_EM,           MUTYPE_FONT,          AMSK_STYLE },
 { "font",          MARKUP_FONT,         MUTYPE_FONT,
                    AMSK_COLOR|AMSK_FACE|AMSK_SIZE|AMSK_STYLE },
 { "footer",        MARKUP_HTML5_FOOTER, MUTYPE_BLOCK,
                                            AMSK_ID|AMSK_CLASS|AMSK_STYLE },

 { "h1",            MARKUP_H1,           MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "h2",            MARKUP_H2,           MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "h3",            MARKUP_H3,           MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "h4",            MARKUP_H4,           MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "h5",            MARKUP_H5,           MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "h6",            MARKUP_H6,           MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },

 { "header",        MARKUP_HTML5_HEADER, MUTYPE_BLOCK,
                                            AMSK_ID|AMSK_CLASS|AMSK_STYLE },

 { "hr",            MARKUP_HR,           MUTYPE_SINGLE,
                    AMSK_ALIGN|AMSK_COLOR|AMSK_SIZE|AMSK_WIDTH|
                    AMSK_STYLE|AMSK_CLASS  },
 { "i",             MARKUP_I,            MUTYPE_FONT,          AMSK_STYLE },
 { "img",           MARKUP_IMG,          MUTYPE_SINGLE,
                    AMSK_ALIGN|AMSK_ALT|AMSK_BORDER|AMSK_HEIGHT|
                    AMSK_HSPACE|AMSK_SRC|AMSK_VSPACE|AMSK_WIDTH|AMSK_STYLE  },
 { "kbd",           MARKUP_KBD,          MUTYPE_FONT,          AMSK_STYLE },
 { "li",            MARKUP_LI,           MUTYPE_LI,
                    AMSK_TYPE|AMSK_VALUE|AMSK_STYLE  },
 { "nav",           MARKUP_HTML5_NAV,    MUTYPE_BLOCK,
                                            AMSK_ID|AMSK_CLASS|AMSK_STYLE },
 { "nobr",          MARKUP_NOBR,         MUTYPE_FONT,          0  },
 { "nowiki",        MARKUP_NOWIKI,       MUTYPE_SPECIAL,       0  },
 { "ol",            MARKUP_OL,           MUTYPE_LIST,
                    AMSK_START|AMSK_TYPE|AMSK_COMPACT|AMSK_STYLE  },
 { "p",             MARKUP_P,            MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "pre",           MARKUP_PRE,          MUTYPE_BLOCK,         AMSK_STYLE },
 { "s",             MARKUP_S,            MUTYPE_FONT,          AMSK_STYLE },
 { "samp",          MARKUP_SAMP,         MUTYPE_FONT,          AMSK_STYLE },
 { "section",       MARKUP_HTML5_SECTION, MUTYPE_BLOCK,
                                            AMSK_ID|AMSK_CLASS|AMSK_STYLE },
 { "small",         MARKUP_SMALL,        MUTYPE_FONT,          AMSK_STYLE },
 { "span",          MARKUP_SPAN,         MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "strike",        MARKUP_STRIKE,       MUTYPE_FONT,          AMSK_STYLE },
 { "strong",        MARKUP_STRONG,       MUTYPE_FONT,          AMSK_STYLE },
 { "sub",           MARKUP_SUB,          MUTYPE_FONT,          AMSK_STYLE },
 { "sup",           MARKUP_SUP,          MUTYPE_FONT,          AMSK_STYLE },
 { "table",         MARKUP_TABLE,        MUTYPE_TABLE,
                    AMSK_ALIGN|AMSK_BGCOLOR|AMSK_BORDER|AMSK_CELLPADDING|
                    AMSK_CELLSPACING|AMSK_HSPACE|AMSK_VSPACE|AMSK_CLASS|
                    AMSK_STYLE  },
 { "tbody",         MARKUP_TBODY,        MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "td",            MARKUP_TD,           MUTYPE_TD,
                    AMSK_ALIGN|AMSK_BGCOLOR|AMSK_COLSPAN|
                    AMSK_ROWSPAN|AMSK_VALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "tfoot",         MARKUP_TFOOT,        MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "th",            MARKUP_TH,           MUTYPE_TD,
                    AMSK_ALIGN|AMSK_BGCOLOR|AMSK_COLSPAN|
                    AMSK_ROWSPAN|AMSK_VALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "thead",         MARKUP_THEAD,        MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
 { "title",         MARKUP_TITLE,        MUTYPE_BLOCK, 0 },
 { "tr",            MARKUP_TR,           MUTYPE_TR,
                    AMSK_ALIGN|AMSK_BGCOLOR|AMSK_VALIGN|AMSK_CLASS|AMSK_STYLE },
 { "tt",            MARKUP_TT,           MUTYPE_FONT,          AMSK_STYLE },
 { "u",             MARKUP_U,            MUTYPE_FONT,          AMSK_STYLE },
 { "ul",            MARKUP_UL,           MUTYPE_LIST,
                    AMSK_TYPE|AMSK_COMPACT|AMSK_STYLE  },
 { "var",           MARKUP_VAR,          MUTYPE_FONT,          AMSK_STYLE },
 { "verbatim",      MARKUP_VERBATIM,     MUTYPE_SPECIAL,
                    AMSK_ID|AMSK_TYPE },
};

void show_allowed_wiki_markup( void ){
  int i; /* loop over allowedAttr */
  for( i=1 ; i<=count(aMarkup) - 1 ; i++ ){
    @ &lt;%s(aMarkup[i].zName)&gt;
  }
}

/*
** Use binary search to locate a tag in the aMarkup[] table.
*/
static int findTag(const char *z){
  int i, c, first, last;
  first = 1;
  last = count(aMarkup) - 1;
  while( first<=last ){
    i = (first+last)/2;
    c = fossil_strcmp(aMarkup[i].zName, z);
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
#define TOKEN_MARKUP        1  /* <...> */
#define TOKEN_CHARACTER     2  /* "&" or "<" not part of markup */
#define TOKEN_LINK          3  /* [...] */
#define TOKEN_PARAGRAPH     4  /* blank lines */
#define TOKEN_NEWLINE       5  /* A single "\n" */
#define TOKEN_BUL_LI        6  /*  "  *  " */
#define TOKEN_NUM_LI        7  /*  "  #  " */
#define TOKEN_ENUM          8  /*  "  \(?\d+[.)]?  " */
#define TOKEN_INDENT        9  /*  "   " */
#define TOKEN_RAW           10 /* Output exactly (used when wiki-use-html==1) */
#define TOKEN_TEXT          11 /* None of the above */

/*
** State flags.  Save the lower 16 bits for the WIKI_* flags.
*/
#define AT_NEWLINE          0x0010000  /* At start of a line */
#define AT_PARAGRAPH        0x0020000  /* At start of a paragraph */
#define ALLOW_WIKI          0x0040000  /* Allow wiki markup */
#define ALLOW_LINKS         0x0080000  /* Allow [...] hyperlinks */
#define FONT_MARKUP_ONLY    0x0100000  /* Only allow MUTYPE_FONT markup */
#define INLINE_MARKUP_ONLY  0x0200000  /* Allow only "inline" markup */
#define IN_LIST             0x0400000  /* Within wiki <ul> or <ol> */

/*
** Current state of the rendering engine
*/
typedef struct Renderer Renderer;
struct Renderer {
  Blob *pOut;                 /* Output appended to this blob */
  int state;                  /* Flag that govern rendering */
  unsigned renderFlags;       /* Flags from the client */
  int wikiList;               /* Current wiki list type */
  int inVerbatim;             /* True in <verbatim> mode */
  int preVerbState;           /* Value of state prior to verbatim */
  int wantAutoParagraph;      /* True if a <p> is desired */
  int inAutoParagraph;        /* True if within an automatic paragraph */
  const char *zVerbatimId;    /* The id= attribute of <verbatim> */
  int nStack;                 /* Number of elements on the stack */
  int nAlloc;                 /* Space allocated for aStack */
  struct sStack {
    short iCode;                 /* Markup code */
    short allowWiki;             /* ALLOW_WIKI if wiki allowed before tag */
    const char *zId;             /* ID attribute or NULL */
  } *aStack;
};

/*
** Return TRUE if HTML should be used as the sole markup language for wiki.
**
** On first invocation, this routine consults the "wiki-use-html" setting.
** It caches the result for subsequent invocations, under the assumption
** that the setting will not change.
*/
static int wikiUsesHtml(void){
  static int r = -1;
  if( r<0 ) r = db_get_boolean("wiki-use-html", 0);
  return r;
}

/*
** z points to a "<" character.  Check to see if this is the start of
** a valid markup.  If it is, return the total number of characters in
** the markup including the initial "<" and the terminating ">".  If
** it is not well-formed markup, return 0.
*/
int htmlTagLength(const char *z){
  int n = 1;
  int inparen = 0;
  int c;
  if( z[n]=='/' ){ n++; }
  if( !fossil_isalpha(z[n]) ) return 0;
  while( fossil_isalnum(z[n]) || z[n]=='-' ){ n++; }
  c = z[n];
  if( c=='/' && z[n+1]=='>' ){ return n+2; }
  if( c!='>' && !fossil_isspace(c) ) return 0;
  while( (c = z[n])!=0 && (c!='>' || inparen) ){
    if( c==inparen ){
      inparen = 0;
    }else if( inparen==0 && (c=='"' || c=='\'') ){
      inparen = c;
    }
    n++;
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
  for(i=1, n=0; fossil_isspace(z[i]); i++){
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
** The "[" is only considered if flags contain ALLOW_LINKS or ALLOW_WIKI.
** The "\n" is only considered interesting if the flags constains ALLOW_WIKI.
*/
static int textLength(const char *z, int flags){
  int n = 0;
  int c, x1, x2;

  if( flags & ALLOW_WIKI ){
    x1 = '[';
    x2 = '\n';
  }else if( flags & ALLOW_LINKS ){
    x1 = '[';
    x2 = 0;
  }else{
    x1 = x2 = 0;
  }
  while( (c = z[0])!=0 && c!='<' && c!='&' && c!=x1 && c!=x2 ){
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
    for(i=2; fossil_isdigit(z[i]); i++){}
    return i>2 && z[i]==';';
  }else{
    for(i=1; fossil_isalpha(z[i]); i++){}
    return i>1 && z[i]==';';
  }
}

/*
** Check to see if the z[] string is the beginning of a wiki list item.
** If it is, return the length of the bullet text.  Otherwise return 0.
*/
static int listItemLength(const char *z, const char listChar){
  int i, n;
  n = 0;
  i = 0;
  while( z[n]==' ' || z[n]=='\t' ){
    if( z[n]=='\t' ) i++;
    i++;
    n++;
  }
  if( i<2 || z[n]!=listChar ) return 0;
  n++;
  i = 0;
  while( z[n]==' ' || z[n]=='\t' ){
    if( z[n]=='\t' ) i++;
    i++;
    n++;
  }
  if( i<2 || fossil_isspace(z[n]) ) return 0;
  return n;
}

/*
** Check to see if the z[] string is the beginning of a enumeration value.
** If it is, return the length of the bullet text.  Otherwise return 0.
**
** Syntax:
**    *  a tab or two or more spaces
**    *  one or more digits
**    *  optional "."
**    *  another tab or two ore more spaces.
**
*/
static int enumLength(const char *z){
  int i, n;
  n = 0;
  i = 0;
  while( z[n]==' ' || z[n]=='\t' ){
    if( z[n]=='\t' ) i++;
    i++;
    n++;
  }
  if( i<2 ) return 0;
  for(i=0; fossil_isdigit(z[n]); i++, n++){}
  if( i==0 ) return 0;
  if( z[n]=='.' ){
    n++;
  }
  i = 0;
  while( z[n]==' ' || z[n]=='\t' ){
    if( z[n]=='\t' ) i++;
    i++;
    n++;
  }
  if( i<2 || fossil_isspace(z[n]) ) return 0;
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
  if( i<2 || fossil_isspace(z[n]) ) return 0;
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
** Get the next wiki token.
**
** z points to the start of a token.  Return the number of
** characters in that token.  Write the token type into *pTokenType.
*/
static int nextWikiToken(const char *z, Renderer *p, int *pTokenType){
  int n;
  if( z[0]=='<' ){
    n = htmlTagLength(z);
    if( n>0 ){
      *pTokenType = TOKEN_MARKUP;
      return n;
    }else{
      *pTokenType = TOKEN_CHARACTER;
      return 1;
    }
  }
  if( z[0]=='&' && (p->inVerbatim || !isElement(z)) ){
    *pTokenType = TOKEN_CHARACTER;
    return 1;
  }
  if( (p->state & ALLOW_WIKI)!=0 ){
    if( z[0]=='\n' ){
      n = paragraphBreakLength(z);
      if( n>0 ){
        *pTokenType = TOKEN_PARAGRAPH;
        return n;
      }else if( fossil_isspace(z[1]) ){
        *pTokenType = TOKEN_NEWLINE;
        return 1;
      }
    }
    if( (p->state & AT_NEWLINE)!=0 && fossil_isspace(z[0]) ){
      n = listItemLength(z, '*');
      if( n>0 ){
        *pTokenType = TOKEN_BUL_LI;
        return n;
      }
      n = listItemLength(z, '#');
      if( n>0 ){
        *pTokenType = TOKEN_NUM_LI;
        return n;
      }
      n = enumLength(z);
      if( n>0 ){
        *pTokenType = TOKEN_ENUM;
        return n;
      }
    }
    if( (p->state & AT_PARAGRAPH)!=0 && fossil_isspace(z[0]) ){
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
  }else if( (p->state & ALLOW_LINKS)!=0 && z[0]=='[' && (n = linkLength(z))>0 ){
    *pTokenType = TOKEN_LINK;
    return n;
  }
  *pTokenType = TOKEN_TEXT;
  return 1 + textLength(z+1, p->state);
}

/*
** Parse only Wiki links, return everything else as TOKEN_RAW.
**
** z points to the start of a token.  Return the number of
** characters in that token. Write the token type into *pTokenType.
*/
static int nextRawToken(const char *z, Renderer *p, int *pTokenType){
  int n;
  if( z[0]=='[' && (n = linkLength(z))>0 ){
    *pTokenType = TOKEN_LINK;
    return n;
  }
  *pTokenType = TOKEN_RAW;
  return 1 + textLength(z+1, p->state);
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
    unsigned char iACode;    /* ATTR_* */
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
static int parseMarkup(ParsedMarkup *p, char *z){
  int i, j, c;
  int iACode;
  char *zValue;
  int seen = 0;
  char zTag[100];

  if( z[1]=='/' ){
    p->endTag = 1;
    i = 2;
  }else{
    p->endTag = 0;
    i = 1;
  }
  j = 0;
  while( fossil_isalnum(z[i]) ){
    if( j<sizeof(zTag)-1 ) zTag[j++] = fossil_tolower(z[i]);
    i++;
  }
  zTag[j] = 0;
  p->iCode = findTag(zTag);
  p->iType = aMarkup[p->iCode].iType;
  p->nAttr = 0;
  c = 0;
  if( z[i]=='-' ){
    p->aAttr[0].iACode = iACode = ATTR_ID;
    i++;
    p->aAttr[0].zValue = &z[i];
    while( fossil_isalnum(z[i]) ){ i++; }
    p->aAttr[0].cTerm = c = z[i];
    z[i++] = 0;
    p->nAttr = 1;
    if( c=='>' ) return 0;
  }
  while( fossil_isspace(z[i]) ){ i++; }
  while( c!='>' && p->nAttr<8 && fossil_isalpha(z[i]) ){
    int attrOk;    /* True to preserve attribute.  False to ignore it */
    j = 0;
    while( fossil_isalnum(z[i]) ){
      if( j<sizeof(zTag)-1 ) zTag[j++] = fossil_tolower(z[i]);
      i++;
    }
    zTag[j] = 0;
    p->aAttr[p->nAttr].iACode = iACode = findAttr(zTag);
    attrOk = iACode!=0 && (seen & aAttribute[iACode].iMask)==0;
    while( fossil_isspace(z[i]) ){ z++; }
    if( z[i]!='=' ){
      p->aAttr[p->nAttr].zValue = 0;
      p->aAttr[p->nAttr].cTerm = 0;
      c = 0;
    }else{
      i++;
      while( fossil_isspace(z[i]) ){ z++; }
      if( z[i]=='"' ){
        i++;
        zValue = &z[i];
        while( z[i] && z[i]!='"' ){ i++; }
      }else if( z[i]=='\'' ){
        i++;
        zValue = &z[i];
        while( z[i] && z[i]!='\'' ){ i++; }
      }else{
        zValue = &z[i];
        while( !fossil_isspace(z[i]) && z[i]!='>' ){ z++; }
      }
      if( attrOk ){
        p->aAttr[p->nAttr].zValue = zValue;
        p->aAttr[p->nAttr].cTerm = c = z[i];
        z[i] = 0;
      }
      i++;
    }
    if( attrOk ){
      seen |= aAttribute[iACode].iMask;
      p->nAttr++;
    }
    while( fossil_isspace(z[i]) ){ i++; }
    if( z[i]=='>' || (z[i]=='/' && z[i+1]=='>') ) break;
  }
  return seen;
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
      blob_appendf(pOut, " %s", aAttribute[p->aAttr[i].iACode].zName);
      if( p->aAttr[i].zValue ){
        const char *zVal = p->aAttr[i].zValue;
        if( p->aAttr[i].iACode==ATTR_SRC && zVal[0]=='/' ){
          blob_appendf(pOut, "=\"%s%s\"", g.zTop, zVal);
        }else{
          blob_appendf(pOut, "=\"%s\"", zVal);
        }
      }
    }
    if (p->iType & MUTYPE_SINGLE){
      blob_append(pOut, " /", 2);
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
    if( p->aAttr[i].cTerm ){
      n = strlen(z);
      z[n] = p->aAttr[i].cTerm;
    }
  }
}

/*
** Return the value of attribute attrId.  Return NULL if there is no
** ID attribute.
*/
static const char *attributeValue(ParsedMarkup *p, int attrId){
  int i;
  for(i=0; i<p->nAttr; i++){
    if( p->aAttr[i].iACode==attrId ){
      return p->aAttr[i].zValue;
    }
  }
  return 0;
}

/*
** Return the ID attribute for markup.  Return NULL if there is no
** ID attribute.
*/
static const char *markupId(ParsedMarkup *p){
  return attributeValue(p, ATTR_ID);
}

/*
** Check markup pMarkup to see if it is a hyperlink with class "button"
** that is follows by simple text and an </a> only.  Example:
**
**     <a class="button" href="../index.wiki">Index</a>
**
** If the markup matches this pattern, and if the WIKI_BUTTONS flag was
** passed to wiki_convert(), then transform this link into a submenu
** button, skip the text, and set *pN equal to the total length of the
** text through the end of </a> and return true.  If the markup does
** not match or if WIKI_BUTTONS is not set, then make no changes to *pN
** and return false.
*/
static int isButtonHyperlink(
  Renderer *p,              /* Renderer state */
  ParsedMarkup *pMarkup,    /* Potential button markup */
  const char *z,            /* Complete text of Wiki */
  int *pN                   /* Characters of z[] consumed */
){
  const char *zClass;
  const char *zHref;
  char *zTag;
  int i, j;
  if( (p->state & WIKI_BUTTONS)==0 ) return 0;
  zClass = attributeValue(pMarkup, ATTR_CLASS);
  if( zClass==0 ) return 0;
  if( fossil_strcmp(zClass, "button")!=0 ) return 0;
  zHref = attributeValue(pMarkup, ATTR_HREF);
  if( zHref==0 ) return 0;
  i = *pN;
  while( z[i] && z[i]!='<' ){ i++; }
  if( fossil_strnicmp(&z[i], "</a>",4)!=0 ) return 0;
  for(j=*pN; fossil_isspace(z[j]); j++){}
  zTag = mprintf("%.*s", i-j, &z[j]);
  j = (int)strlen(zTag);
  while( j>0 && fossil_isspace(zTag[j-1]) ){ j--; }
  if( j==0 ) return 0;
  style_submenu_element(zTag, "%s", zHref);
  *pN = i+4;
  return 1;
}

/*
** Pop a single element off of the stack.  As the element is popped,
** output its end tag if it is not a </div> tag.
*/
static void popStack(Renderer *p){
  if( p->nStack ){
    int iCode;
    p->nStack--;
    iCode = p->aStack[p->nStack].iCode;
    if( (iCode!=MARKUP_DIV || p->aStack[p->nStack].zId==0) && p->pOut ){
      blob_appendf(p->pOut, "</%s>", aMarkup[iCode].zName);
    }
  }
}

/*
** Push a new markup value onto the stack.  Enlarge the stack
** if necessary.
*/
static void pushStackWithId(Renderer *p, int elem, const char *zId, int w){
  if( p->nStack>=p->nAlloc ){
    p->nAlloc = p->nAlloc*2 + 100;
    p->aStack = fossil_realloc(p->aStack, p->nAlloc*sizeof(p->aStack[0]));
  }
  p->aStack[p->nStack].iCode = elem;
  p->aStack[p->nStack].zId = zId;
  p->aStack[p->nStack].allowWiki = w;
  p->nStack++;
}
static void pushStack(Renderer *p, int elem){
  pushStackWithId(p, elem, 0, 0);
}

/*
** Pop the stack until the top-most iTag element is removed.
** If there is no iTag element on the stack, this routine
** is a no-op.
*/
static void popStackToTag(Renderer *p, int iTag){
  int i;
  for(i=p->nStack-1; i>=0; i--){
    if( p->aStack[i].iCode!=iTag ) continue;
    if( p->aStack[i].zId ) continue;
    break;
  }
  if( i<0 ) return;
  while( p->nStack>i ){
    popStack(p);
  }
}

/*
** Attempt to find a find a tag of type iTag with id zId.  Return -1
** if not found.  If found, return its stack level.
*/
static int findTagWithId(Renderer *p, int iTag, const char *zId){
  int i;
  assert( zId!=0 );
  for(i=p->nStack-1; i>=0; i--){
    if( p->aStack[i].iCode!=iTag ) continue;
    if( p->aStack[i].zId==0 ) continue;
    if( fossil_strcmp(zId, p->aStack[i].zId)!=0 ) continue;
    break;
  }
  return i;
}

/*
** Pop the stack until the top-most element of the stack
** is an element that matches the type in iMask.  Return
** code of the markup element that is on left on top of the stack.
** If the stack does not have an element
** that matches iMask, then leave the stack unchanged and
** return false (MARKUP_INVALID).
*/
static int backupToType(Renderer *p, int iMask){
  int i;
  for(i=p->nStack-1; i>=0; i--){
    if( aMarkup[p->aStack[i].iCode].iType & iMask ) break;
  }
  if( i<0 ) return 0;
  i++;
  while( p->nStack>i ){
    popStack(p);
  }
  return p->aStack[i-1].iCode;
}

/*
** Begin a new paragraph if that something that is needed.
*/
static void startAutoParagraph(Renderer *p){
  if( p->wantAutoParagraph==0 ) return;
  if( p->state & WIKI_LINKSONLY ) return;
  if( p->wikiList==MARKUP_OL || p->wikiList==MARKUP_UL ) return;
  blob_append(p->pOut, "<p>", -1);
  p->wantAutoParagraph = 0;
  p->inAutoParagraph = 1;
}

/*
** End a paragraph if we are in one.
*/
static void endAutoParagraph(Renderer *p){
  if( p->inAutoParagraph ){
    p->inAutoParagraph = 0;
  }
}

/*
** If the input string corresponds to an existing baseline,
** return true.
*/
static int is_valid_hname(const char *z){
  int n = strlen(z);
  if( n<4 || n>HNAME_MAX ) return 0;
  if( !validate16(z, n) ) return 0;
  return 1;
}

/*
** Return TRUE if a hash name corresponds to an artifact in this
** repository.
*/
static int in_this_repo(const char *zUuid){
  static Stmt q;
  int rc;
  int n;
  char zU2[HNAME_MAX+1];
  db_static_prepare(&q,
     "SELECT 1 FROM blob WHERE uuid>=:u AND uuid<:u2"
  );
  db_bind_text(&q, ":u", zUuid);
  n = (int)strlen(zUuid);
  if( n>=sizeof(zU2) ) n = sizeof(zU2)-1;
  memcpy(zU2, zUuid, n);
  zU2[n-1]++;
  zU2[n] = 0;
  db_bind_text(&q, ":u2", zU2);
  rc = db_step(&q);
  db_reset(&q);
  return rc==SQLITE_ROW;
}

/*
** zTarget is guaranteed to be a UUID.  It might be the UUID of a ticket.
** If it is, store in *pClosed a true or false depending on whether or not
** the ticket is closed and return true. If zTarget
** is not the UUID of a ticket, return false.
*/
static int is_ticket(
  const char *zTarget,    /* Ticket UUID */
  int *pClosed            /* True if the ticket is closed */
){
  static Stmt q;
  static int once = 1;
  int n;
  int rc;
  char zLower[UUID_SIZE+1];
  char zUpper[UUID_SIZE+1];
  n = strlen(zTarget);
  memcpy(zLower, zTarget, n+1);
  canonical16(zLower, n+1);
  memcpy(zUpper, zLower, n+1);
  zUpper[n-1]++;
  if( once ){
    const char *zClosedExpr = db_get("ticket-closed-expr", "status='Closed'");
    db_static_prepare(&q,
      "SELECT %s FROM ticket "
      " WHERE tkt_uuid>=:lwr AND tkt_uuid<:upr",
      zClosedExpr /*safe-for-%s*/
    );
    once = 0;
  }
  db_bind_text(&q, ":lwr", zLower);
  db_bind_text(&q, ":upr", zUpper);
  if( db_step(&q)==SQLITE_ROW ){
    rc = 1;
    *pClosed = db_column_int(&q, 0);
  }else{
    rc = 0;
  }
  db_reset(&q);
  return rc;
}

/*
** Return a pointer to the name part of zTarget (skipping the "wiki:" prefix
** if there is one) if zTarget is a valid wiki page name.  Return NULL if
** zTarget names a page that does not exist.
*/
static const char *validWikiPageName(Renderer *p, const char *zTarget){
  if( strncmp(zTarget, "wiki:", 5)==0
      && wiki_name_is_wellformed((const unsigned char*)zTarget) ){
    return zTarget+5;
  }
  if( strcmp(zTarget, "Sandbox")==0 ) return zTarget;
  if( wiki_name_is_wellformed((const unsigned char *)zTarget)
   && ((p->state & WIKI_NOBADLINKS)==0 ||
        db_exists("SELECT 1 FROM tag WHERE tagname GLOB 'wiki-%q'"
                  " AND (SELECT value FROM tagxref WHERE tagid=tag.tagid"
                  " ORDER BY mtime DESC LIMIT 1) > 0", zTarget))
  ){
    return zTarget;
  }
  return 0;
}

/*
** Resolve a hyperlink.  The zTarget argument is the content of the [...]
** in the wiki.  Append to the output string whatever text is appropriate
** for opening the hyperlink.  Write into zClose[0...nClose-1] text that will
** close the markup.
**
** If this routine determines that no hyperlink should be generated, then
** set zClose[0] to 0.
**
** Actually, this routine might or might not append the hyperlink, depending
** on current rendering rules: specifically does the current user have
** "History" permission.
**
**    [http://www.fossil-scm.org/]
**    [https://www.fossil-scm.org/]
**    [ftp://www.fossil-scm.org/]
**    [mailto:fossil-users@lists.fossil-scm.org]
**
**    [/path]
**
**    [./relpath]
**
**    [WikiPageName]
**    [wiki:WikiPageName]
**
**    [0123456789abcdef]
**
**    [#fragment]
**
**    [2010-02-27 07:13]
*/
static void openHyperlink(
  Renderer *p,            /* Rendering context */
  const char *zTarget,    /* Hyperlink target; text within [...] */
  char *zClose,           /* Write hyperlink closing text here */
  int nClose,             /* Bytes available in zClose[] */
  const char *zOrig       /* Complete document text */
){
  const char *zTerm = "</a>";
  const char *z;

  assert( nClose>=20 );
  if( strncmp(zTarget, "http:", 5)==0
   || strncmp(zTarget, "https:", 6)==0
   || strncmp(zTarget, "ftp:", 4)==0
   || strncmp(zTarget, "mailto:", 7)==0
  ){
    blob_appendf(p->pOut, "<a href=\"%s\">", zTarget);
  }else if( zTarget[0]=='/' ){
    blob_appendf(p->pOut, "<a href=\"%R%h\">", zTarget);
  }else if( zTarget[0]=='.'
         && (zTarget[1]=='/' || (zTarget[1]=='.' && zTarget[2]=='/'))
         && (p->state & WIKI_LINKSONLY)==0 ){
    blob_appendf(p->pOut, "<a href=\"%h\">", zTarget);
  }else if( zTarget[0]=='#' ){
    blob_appendf(p->pOut, "<a href=\"%h\">", zTarget);
  }else if( is_valid_hname(zTarget) ){
    int isClosed = 0;
    if( strlen(zTarget)<=UUID_SIZE && is_ticket(zTarget, &isClosed) ){
      /* Special display processing for tickets.  Display the hyperlink
      ** as crossed out if the ticket is closed.
      */
      if( isClosed ){
        if( g.perm.Hyperlink ){
          blob_appendf(p->pOut,
             "%z<span class=\"wikiTagCancelled\">[",
             href("%R/info/%s",zTarget)
          );
          zTerm = "]</span></a>";
        }else{
          blob_appendf(p->pOut,"<span class=\"wikiTagCancelled\">[");
          zTerm = "]</span>";
        }
      }else{
        if( g.perm.Hyperlink ){
          blob_appendf(p->pOut,"%z[", href("%R/info/%s", zTarget));
          zTerm = "]</a>";
        }else{
          blob_appendf(p->pOut, "[");
          zTerm = "]";
        }
      }
    }else if( !in_this_repo(zTarget) ){
      if( (p->state & (WIKI_LINKSONLY|WIKI_NOBADLINKS))!=0 ){
        zTerm = "";
      }else{
        blob_appendf(p->pOut, "<span class=\"brokenlink\">[");
        zTerm = "]</span>";
      }
    }else if( g.perm.Hyperlink ){
      blob_appendf(p->pOut, "%z[",href("%R/info/%s", zTarget));
      zTerm = "]</a>";
    }else{
      zTerm = "";
    }
  }else if( strlen(zTarget)>=10 && fossil_isdigit(zTarget[0]) && zTarget[4]=='-'
            && db_int(0, "SELECT datetime(%Q) NOT NULL", zTarget) ){
    blob_appendf(p->pOut, "<a href=\"%R/timeline?c=%T\">", zTarget);
  }else if( (z = validWikiPageName(p, zTarget))!=0 ){
    blob_appendf(p->pOut, "<a href=\"%R/wiki?name=%T\">", z);
  }else if( zTarget>=&zOrig[2] && !fossil_isspace(zTarget[-2]) ){
    /* Probably an array subscript in code */
    zTerm = "";
  }else if( (p->state & (WIKI_NOBADLINKS|WIKI_LINKSONLY))!=0 ){
    zTerm = "";
  }else{
    blob_appendf(p->pOut, "<span class=\"brokenlink\">[%h]", zTarget);
    zTerm = "</span>";
  }
  assert( strlen(zTerm)<nClose );
  sqlite3_snprintf(nClose, zClose, "%s", zTerm);
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
  return fossil_strcmp(z, p->zVerbatimId)==0;
}

/*
** Return the MUTYPE for the top of the stack.
*/
static int stackTopType(Renderer *p){
  if( p->nStack<=0 ) return 0;
  return aMarkup[p->aStack[p->nStack-1].iCode].iType;
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
  int inlineOnly = (p->state & INLINE_MARKUP_ONLY)!=0;
  int wikiHtmlOnly = (p->state & (WIKI_HTMLONLY | WIKI_LINKSONLY))!=0;
  int linksOnly = (p->state & WIKI_LINKSONLY)!=0;
  char *zOrig = z;

  /* Make sure the attribute constants and names still align
  ** following changes in the attribute list. */
  assert( fossil_strcmp(aAttribute[ATTR_WIDTH].zName, "width")==0 );

  while( z[0] ){
    if( wikiHtmlOnly ){
      n = nextRawToken(z, p, &tokenType);
    }else{
      n = nextWikiToken(z, p, &tokenType);
    }
    p->state &= ~(AT_NEWLINE|AT_PARAGRAPH);
    switch( tokenType ){
      case TOKEN_PARAGRAPH: {
        if( inlineOnly ){
          /* blob_append(p->pOut, " &para; ", -1); */
          blob_append(p->pOut, " &nbsp;&nbsp; ", -1);
        }else{
          if( p->wikiList ){
            popStackToTag(p, p->wikiList);
            p->wikiList = 0;
          }
          endAutoParagraph(p);
          blob_append(p->pOut, "\n\n", 1);
          p->wantAutoParagraph = 1;
        }
        p->state |= AT_PARAGRAPH|AT_NEWLINE;
        break;
      }
      case TOKEN_NEWLINE: {
        blob_append(p->pOut, "\n", 1);
        p->state |= AT_NEWLINE;
        break;
      }
      case TOKEN_BUL_LI: {
        if( inlineOnly ){
          blob_append(p->pOut, " &bull; ", -1);
        }else{
          if( p->wikiList!=MARKUP_UL ){
            if( p->wikiList ){
              popStackToTag(p, p->wikiList);
            }
            endAutoParagraph(p);
            pushStack(p, MARKUP_UL);
            blob_append(p->pOut, "<ul>", 4);
            p->wikiList = MARKUP_UL;
          }
          popStackToTag(p, MARKUP_LI);
          startAutoParagraph(p);
          pushStack(p, MARKUP_LI);
          blob_append(p->pOut, "<li>", 4);
        }
        break;
      }
      case TOKEN_NUM_LI: {
        if( inlineOnly ){
          blob_append(p->pOut, " # ", -1);
        }else{
          if( p->wikiList!=MARKUP_OL ){
            if( p->wikiList ){
              popStackToTag(p, p->wikiList);
            }
            endAutoParagraph(p);
            pushStack(p, MARKUP_OL);
            blob_append(p->pOut, "<ol>", 4);
            p->wikiList = MARKUP_OL;
          }
          popStackToTag(p, MARKUP_LI);
          startAutoParagraph(p);
          pushStack(p, MARKUP_LI);
          blob_append(p->pOut, "<li>", 4);
        }
        break;
      }
      case TOKEN_ENUM: {
        if( inlineOnly ){
          blob_appendf(p->pOut, " (%d) ", atoi(z));
        }else{
          if( p->wikiList!=MARKUP_OL ){
            if( p->wikiList ){
              popStackToTag(p, p->wikiList);
            }
            endAutoParagraph(p);
            pushStack(p, MARKUP_OL);
            blob_append(p->pOut, "<ol>", 4);
            p->wikiList = MARKUP_OL;
          }
          popStackToTag(p, MARKUP_LI);
          startAutoParagraph(p);
          pushStack(p, MARKUP_LI);
          blob_appendf(p->pOut, "<li value=\"%d\">", atoi(z));
        }
        break;
      }
      case TOKEN_INDENT: {
        if( !inlineOnly ){
          assert( p->wikiList==0 );
          pushStack(p, MARKUP_BLOCKQUOTE);
          blob_append(p->pOut, "<blockquote>", -1);
          p->wantAutoParagraph = 0;
          p->wikiList = MARKUP_BLOCKQUOTE;
        }
        break;
      }
      case TOKEN_CHARACTER: {
        startAutoParagraph(p);
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
        int i, j;
        int savedState;
        char zClose[20];
        char cS1 = 0;
        int iS1 = 0;

        startAutoParagraph(p);
        zTarget = &z[1];
        for(i=1; z[i] && z[i]!=']'; i++){
          if( z[i]=='|' && zDisplay==0 ){
            zDisplay = &z[i+1];
            for(j=i; j>0 && fossil_isspace(z[j-1]); j--){}
            iS1 = j;
            cS1 = z[j];
            z[j] = 0;
          }
        }
        z[i] = 0;
        if( zDisplay==0 ){
          zDisplay = zTarget;
        }else{
          while( fossil_isspace(*zDisplay) ) zDisplay++;
        }
        openHyperlink(p, zTarget, zClose, sizeof(zClose), zOrig);
        if( linksOnly || zClose[0]==0 || p->inVerbatim ){
          if( cS1 ) z[iS1] = cS1;
          if( zClose[0]!=']' ){
            blob_appendf(p->pOut, "[%h]%s", zTarget, zClose);
          }else{
            blob_appendf(p->pOut, "%h%s", zTarget, zClose);
          }
        }else{
          savedState = p->state;
          p->state &= ~ALLOW_WIKI;
          p->state |= FONT_MARKUP_ONLY;
          wiki_render(p, zDisplay);
          p->state = savedState;
          blob_append(p->pOut, zClose, -1);
        }
        break;
      }
      case TOKEN_TEXT: {
        int i;
        for(i=0; i<n && fossil_isspace(z[i]); i++){}
        if( i<n ) startAutoParagraph(p);
        blob_append(p->pOut, z, n);
        break;
      }
      case TOKEN_RAW: {
        if( linksOnly ){
          htmlize_to_blob(p->pOut, z, n);
        }else{
          blob_append(p->pOut, z, n);
        }
        break;
      }
      case TOKEN_MARKUP: {
        const char *zId;
        int iDiv;
        int mAttr = parseMarkup(&markup, z);

        /* Convert <title> to <h1 align='center'> */
        if( markup.iCode==MARKUP_TITLE && !p->inVerbatim ){
          markup.iCode = MARKUP_H1;
          markup.nAttr = 1;
          markup.aAttr[0].iACode = AMSK_ALIGN;
          markup.aAttr[0].zValue = "center";
          markup.aAttr[0].cTerm = 0;
        }

        /* Markup of the form </div id=ID> where there is a matching
        ** ID somewhere on the stack.  Exit any contained verbatim.
        ** Pop the stack up to the matching <div>.  Discard the </div>
        */
        if( markup.iCode==MARKUP_DIV && markup.endTag &&
             (zId = markupId(&markup))!=0 &&
             (iDiv = findTagWithId(p, MARKUP_DIV, zId))>=0
        ){
          if( p->inVerbatim ){
            p->inVerbatim = 0;
            p->state = p->preVerbState;
            blob_append(p->pOut, "</pre>", 6);
          }
          while( p->nStack>iDiv+1 ) popStack(p);
          if( p->aStack[iDiv].allowWiki ){
            p->state |= ALLOW_WIKI;
          }else{
            p->state &= ~ALLOW_WIKI;
          }
          assert( p->nStack==iDiv+1 );
          p->nStack--;
        }else

        /* If within <verbatim id=ID> ignore everything other than
        ** </verbatim id=ID> and the </dev id=ID2> above.
        */
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
        }else

        /* Render invalid markup literally.  The markup appears in the
        ** final output as plain text.
        */
        if( markup.iCode==MARKUP_INVALID ){
          unparseMarkup(&markup);
          startAutoParagraph(p);
          blob_append(p->pOut, "&lt;", 4);
          n = 1;
        }else

        /* If the markup is not font-change markup ignore it if the
        ** font-change-only flag is set.
        */
        if( (markup.iType&MUTYPE_FONT)==0 && (p->state & FONT_MARKUP_ONLY)!=0 ){
          /* Do nothing */
        }else

        if( markup.iCode==MARKUP_NOWIKI ){
          if( markup.endTag ){
            p->state |= ALLOW_WIKI;
          }else{
            p->state &= ~ALLOW_WIKI;
          }
        }else

        /* Ignore block markup for in-line rendering.
        */
        if( inlineOnly && (markup.iType&MUTYPE_INLINE)==0 ){
          /* Do nothing */
        }else

        /* Generate end-tags */
        if( markup.endTag ){
          popStackToTag(p, markup.iCode);
        }else

        /* Push <div> markup onto the stack together with the id=ID attribute.
        */
        if( markup.iCode==MARKUP_DIV && (mAttr & ATTR_ID)!=0 ){
          pushStackWithId(p, markup.iCode, markupId(&markup),
                          (p->state & ALLOW_WIKI)!=0);
        }else

        /* Enter <verbatim> processing.  With verbatim enabled, all other
        ** markup other than the corresponding end-tag with the same ID is
        ** ignored.
        */
        if( markup.iCode==MARKUP_VERBATIM ){
          int ii, vAttrDidAppend=0;
          p->zVerbatimId = 0;
          p->inVerbatim = 1;
          p->preVerbState = p->state;
          p->state &= ~ALLOW_WIKI;
          for(ii=0; ii<markup.nAttr; ii++){
            if( markup.aAttr[ii].iACode == ATTR_ID ){
              p->zVerbatimId = markup.aAttr[ii].zValue;
            }else if( markup.aAttr[ii].iACode==ATTR_TYPE ){
              blob_appendf(p->pOut, "<pre name='code' class='%s'>",
                markup.aAttr[ii].zValue);
              vAttrDidAppend=1;
            }else if( markup.aAttr[ii].iACode==ATTR_LINKS
                   && !is_false(markup.aAttr[ii].zValue) ){
              p->state |= ALLOW_LINKS;
            }
          }
          if( !vAttrDidAppend ) {
            endAutoParagraph(p);
            blob_append(p->pOut, "<pre class='verbatim'>",-1);
          }
          p->wantAutoParagraph = 0;
        }else
        if( markup.iType==MUTYPE_LI ){
          if( backupToType(p, MUTYPE_LIST)==0 ){
            endAutoParagraph(p);
            pushStack(p, MARKUP_UL);
            blob_append(p->pOut, "<ul>", 4);
          }
          pushStack(p, MARKUP_LI);
          renderMarkup(p->pOut, &markup);
        }else
        if( markup.iType==MUTYPE_TR ){
          if( backupToType(p, MUTYPE_TABLE) ){
            pushStack(p, MARKUP_TR);
            renderMarkup(p->pOut, &markup);
          }
        }else
        if( markup.iType==MUTYPE_TD ){
          if( backupToType(p, MUTYPE_TABLE|MUTYPE_TR) ){
            if( stackTopType(p)==MUTYPE_TABLE ){
              pushStack(p, MARKUP_TR);
              blob_append(p->pOut, "<tr>", 4);
            }
            pushStack(p, markup.iCode);
            renderMarkup(p->pOut, &markup);
          }
        }else
        if( markup.iType==MUTYPE_HYPERLINK ){
          if( !isButtonHyperlink(p, &markup, z, &n) ){
            popStackToTag(p, markup.iCode);
            startAutoParagraph(p);
            renderMarkup(p->pOut, &markup);
            pushStack(p, markup.iCode);
          }
        }else
        {
          if( markup.iType==MUTYPE_FONT ){
            startAutoParagraph(p);
          }else if( markup.iType==MUTYPE_BLOCK || markup.iType==MUTYPE_LIST ){
            p->wantAutoParagraph = 0;
          }
          if(   markup.iCode==MARKUP_HR
             || markup.iCode==MARKUP_H1
             || markup.iCode==MARKUP_H2
             || markup.iCode==MARKUP_H3
             || markup.iCode==MARKUP_H4
             || markup.iCode==MARKUP_H5
             || markup.iCode==MARKUP_P
          ){
            endAutoParagraph(p);
          }
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
** If pOut is NULL, then the output is appended to the CGI
** reply.
*/
void wiki_convert(Blob *pIn, Blob *pOut, int flags){
  Renderer renderer;

  memset(&renderer, 0, sizeof(renderer));
  renderer.renderFlags = flags;
  renderer.state = ALLOW_WIKI|AT_NEWLINE|AT_PARAGRAPH|flags;
  if( flags & WIKI_NOBLOCK ){
    renderer.state |= INLINE_MARKUP_ONLY;
  }
  if( flags & WIKI_INLINE ){
    renderer.wantAutoParagraph = 0;
  }else{
    renderer.wantAutoParagraph = 1;
  }
  if( wikiUsesHtml() ){
    renderer.state |= WIKI_HTMLONLY;
  }
  if( pOut ){
    renderer.pOut = pOut;
  }else{
    renderer.pOut = cgi_output_blob();
  }

  blob_to_utf8_no_bom(pIn, 0);
  wiki_render(&renderer, blob_str(pIn));
  endAutoParagraph(&renderer);
  while( renderer.nStack ){
    popStack(&renderer);
  }
  blob_append(renderer.pOut, "\n", 1);
  free(renderer.aStack);
}

/*
** Send a string as wiki to CGI output.
*/
void wiki_write(const char *zIn, int flags){
  Blob in;
  blob_init(&in, zIn, -1);
  wiki_convert(&in, 0, flags);
  blob_reset(&in);
}

/*
** COMMAND: test-wiki-render
**
** Usage: %fossil test-wiki-render FILE [OPTIONS]
**
** Options:
**    --buttons        Set the WIKI_BUTTONS flag
**    --htmlonly       Set the WIKI_HTMLONLY flag
**    --linksonly      Set the WIKI_LINKSONLY flag
**    --nobadlinks     Set the WIKI_NOBADLINKS flag
**    --inline         Set the WIKI_INLINE flag
**    --noblock        Set the WIKI_NOBLOCK flag
*/
void test_wiki_render(void){
  Blob in, out;
  int flags = 0;
  if( find_option("buttons",0,0)!=0 ) flags |= WIKI_BUTTONS;
  if( find_option("htmlonly",0,0)!=0 ) flags |= WIKI_HTMLONLY;
  if( find_option("linksonly",0,0)!=0 ) flags |= WIKI_LINKSONLY;
  if( find_option("nobadlinks",0,0)!=0 ) flags |= WIKI_NOBADLINKS;
  if( find_option("inline",0,0)!=0 ) flags |= WIKI_INLINE;
  if( find_option("noblock",0,0)!=0 ) flags |= WIKI_NOBLOCK;
  verify_all_options();
  if( g.argc!=3 ) usage("FILE");
  blob_zero(&out);
  blob_read_from_file(&in, g.argv[2]);
  wiki_convert(&in, &out, flags);
  blob_write_to_file(&out, "-");
}

/*
** Search for a <title>...</title> at the beginning of a wiki page.
** Return true (nonzero) if a title is found.  Return zero if there is
** not title.
**
** If a title is found, initialize the pTitle blob to be the content
** of the title and initialize pTail to be the text that follows the
** title.
*/
int wiki_find_title(Blob *pIn, Blob *pTitle, Blob *pTail){
  char *z;
  int i;
  int iStart;
  blob_to_utf8_no_bom(pIn, 0);
  z = blob_str(pIn);
  for(i=0; fossil_isspace(z[i]); i++){}
  if( z[i]!='<' ) return 0;
  i++;
  if( strncmp(&z[i],"title>", 6)!=0 ) return 0;
  for(iStart=i+6; fossil_isspace(z[iStart]); iStart++){}
  for(i=iStart; z[i] && (z[i]!='<' || strncmp(&z[i],"</title>",8)!=0); i++){}
  if( strncmp(&z[i],"</title>",8)!=0 ){
    blob_init(pTitle, 0, 0);
    blob_init(pTail, &z[iStart], -1);
    return 1;
  }
  if( i-iStart>0 ){
    blob_init(pTitle, &z[iStart], i-iStart);
  }else{
    blob_init(pTitle, 0, 0);
  }
  blob_init(pTail, &z[i+8], -1);
  return 1;
}

/*
** Parse text looking for wiki hyperlinks in one of the formats:
**
**       [target]
**       [target|...]
**
** Where "target" can be either an artifact ID prefix or a wiki page
** name.  For each such hyperlink found, add an entry to the
** backlink table.
*/
void wiki_extract_links(
  char *z,           /* The wiki text from which to extract links */
  int srcid,         /* srcid field for new BACKLINK table entries */
  int srctype,       /* srctype field for new BACKLINK table entries */
  double mtime,      /* mtime field for new BACKLINK table entries */
  int replaceFlag,   /* True first delete prior BACKLINK entries */
  int flags          /* wiki parsing flags */
){
  Renderer renderer;
  int tokenType;
  ParsedMarkup markup;
  int n;
  int inlineOnly;
  int wikiHtmlOnly = 0;

  memset(&renderer, 0, sizeof(renderer));
  renderer.state = ALLOW_WIKI|AT_NEWLINE|AT_PARAGRAPH;
  if( flags & WIKI_NOBLOCK ){
    renderer.state |= INLINE_MARKUP_ONLY;
  }
  if( wikiUsesHtml() ){
    renderer.state |= WIKI_HTMLONLY;
    wikiHtmlOnly = 1;
  }
  inlineOnly = (renderer.state & INLINE_MARKUP_ONLY)!=0;
  if( replaceFlag ){
    db_multi_exec("DELETE FROM backlink WHERE srctype=%d AND srcid=%d",
                  srctype, srcid);
  }

  while( z[0] ){
    if( wikiHtmlOnly ){
      n = nextRawToken(z, &renderer, &tokenType);
    }else{
      n = nextWikiToken(z, &renderer, &tokenType);
    }
    switch( tokenType ){
      case TOKEN_LINK: {
        char *zTarget;
        int i, c;
        char zLink[42];

        zTarget = &z[1];
        for(i=0; zTarget[i] && zTarget[i]!='|' && zTarget[i]!=']'; i++){}
        while(i>1 && zTarget[i-1]==' '){ i--; }
        c = zTarget[i];
        zTarget[i] = 0;
        if( is_valid_hname(zTarget) ){
          memcpy(zLink, zTarget, i+1);
          canonical16(zLink, i);
          db_multi_exec(
             "REPLACE INTO backlink(target,srctype,srcid,mtime)"
             "VALUES(%Q,%d,%d,%g)", zLink, srctype, srcid, mtime
          );
        }
        zTarget[i] = c;
        break;
      }
      case TOKEN_MARKUP: {
        const char *zId;
        int iDiv;
        parseMarkup(&markup, z);

        /* Markup of the form </div id=ID> where there is a matching
        ** ID somewhere on the stack.  Exit the verbatim if were are in
        ** it.  Pop the stack up to the matching <div>.  Discard the
        ** </div>
        */
        if( markup.iCode==MARKUP_DIV && markup.endTag &&
             (zId = markupId(&markup))!=0 &&
             (iDiv = findTagWithId(&renderer, MARKUP_DIV, zId))>=0
        ){
          if( renderer.inVerbatim ){
            renderer.inVerbatim = 0;
            renderer.state = renderer.preVerbState;
          }
          while( renderer.nStack>iDiv+1 ) popStack(&renderer);
          if( renderer.aStack[iDiv].allowWiki ){
            renderer.state |= ALLOW_WIKI;
          }else{
            renderer.state &= ~ALLOW_WIKI;
          }
          renderer.nStack--;
        }else

        /* If within <verbatim id=ID> ignore everything other than
        ** </verbatim id=ID> and the </dev id=ID2> above.
        */
        if( renderer.inVerbatim ){
          if( endVerbatim(&renderer, &markup) ){
            renderer.inVerbatim = 0;
            renderer.state = renderer.preVerbState;
          }else{
            n = 1;
          }
        }else

        /* Render invalid markup literally.  The markup appears in the
        ** final output as plain text.
        */
        if( markup.iCode==MARKUP_INVALID ){
          n = 1;
        }else

        /* If the markup is not font-change markup ignore it if the
        ** font-change-only flag is set.
        */
        if( (markup.iType&MUTYPE_FONT)==0 &&
                            (renderer.state & FONT_MARKUP_ONLY)!=0 ){
          /* Do nothing */
        }else

        if( markup.iCode==MARKUP_NOWIKI ){
          if( markup.endTag ){
            renderer.state |= ALLOW_WIKI;
          }else{
            renderer.state &= ~ALLOW_WIKI;
          }
        }else

        /* Ignore block markup for in-line rendering.
        */
        if( inlineOnly && (markup.iType&MUTYPE_INLINE)==0 ){
          /* Do nothing */
        }else

        /* Generate end-tags */
        if( markup.endTag ){
          popStackToTag(&renderer, markup.iCode);
        }else

        /* Push <div> markup onto the stack together with the id=ID attribute.
        */
        if( markup.iCode==MARKUP_DIV ){
          pushStackWithId(&renderer, markup.iCode, markupId(&markup),
                          (renderer.state & ALLOW_WIKI)!=0);
        }else

        /* Enter <verbatim> processing.  With verbatim enabled, all other
        ** markup other than the corresponding end-tag with the same ID is
        ** ignored.
        */
        if( markup.iCode==MARKUP_VERBATIM ){
          int vAttrIdx;
          renderer.zVerbatimId = 0;
          renderer.inVerbatim = 1;
          renderer.preVerbState = renderer.state;
          renderer.state &= ~ALLOW_WIKI;
          for (vAttrIdx = 0; vAttrIdx < markup.nAttr; vAttrIdx++){
            if( markup.aAttr[vAttrIdx].iACode == ATTR_ID ){
              renderer.zVerbatimId = markup.aAttr[0].zValue;
            }
          }
          renderer.wantAutoParagraph = 0;
        }

        /* Restore the input text to its original configuration
        */
        unparseMarkup(&markup);
        break;
      }
      default: {
        break;
      }
    }
    z += n;
  }
  free(renderer.aStack);
}

/*
** Get the next HTML token.
**
** z points to the start of a token.  Return the number of
** characters in that token.
*/
static int nextHtmlToken(const char *z){
  int n;
  char c;
  if( (c=z[0])=='<' ){
    n = htmlTagLength(z);
    if( n<=0 ) n = 1;
  }else if( fossil_isspace(c) ){
    for(n=1; z[n] && fossil_isspace(z[n]); n++){}
  }else if( c=='&' ){
    n = z[1]=='#' ? 2 : 1;
    while( fossil_isalnum(z[n]) ) n++;
    if( z[n]==';' ) n++;
  }else{
    n = 1;
    for(n=1; 1; n++){
      if( (c = z[n]) > '<' ) continue;
      if( c=='<' || c=='&' || fossil_isspace(c) || c==0 ) break;
    }
  }
  return n;
}

/*
** Attempt to reformat messy HTML to be easily readable by humans.
**
**    *  Try to keep lines less than 80 characters in length
**    *  Collapse white space into a single space
**    *  Put a blank line before:
**          <blockquote><center><code><hN><p><pre><table>
**    *  Put a newline after <br> and <hr>
**    *  Start each of the following elements on a new line:
**          <address><cite><dd><div><dl><dt><li><ol><samp>
**          <tbody><td><tfoot><th><thead><tr><ul>
**
** Except, do not do any reformatting inside of <pre>...</pre>
*/
void htmlTidy(const char *zIn, Blob *pOut){
  int n;
  int nPre = 0;
  int iCur = 0;
  int wantSpace = 0;
  int omitSpace = 1;
  while( zIn[0] ){
    n = nextHtmlToken(zIn);
    if( zIn[0]=='<' && n>1 ){
      int i, j;
      int isCloseTag;
      int eTag;
      int eType;
      char zTag[32];
      isCloseTag = zIn[1]=='/';
      for(i=0, j=1+isCloseTag; i<30 && fossil_isalnum(zIn[j]); i++, j++){
         zTag[i] = fossil_tolower(zIn[j]);
      }
      zTag[i] = 0;
      eTag = findTag(zTag);
      eType = aMarkup[eTag].iType;
      if( eTag==MARKUP_PRE ){
        if( isCloseTag ){
          nPre--;
          blob_append(pOut, zIn, n);
          zIn += n;
          if( nPre==0 ){ blob_append(pOut, "\n", 1); iCur = 0; }
          continue;
        }else{
          if( iCur && nPre==0 ){ blob_append(pOut, "\n", 1); iCur = 0; }
          nPre++;
        }
      }else if( eType & (MUTYPE_BLOCK|MUTYPE_TABLE) ){
        if( !isCloseTag && nPre==0 && blob_size(pOut)>0 ){
          blob_append(pOut, "\n\n", 1 + (iCur>0));
          iCur = 0;
        }
        wantSpace = 0;
        omitSpace = 1;
      }else if( (eType & (MUTYPE_LIST|MUTYPE_LI|MUTYPE_TR|MUTYPE_TD))!=0
             || eTag==MARKUP_HR
      ){
        if( nPre==0 && (!isCloseTag || (eType&MUTYPE_LIST)!=0) && iCur>0 ){
          blob_append(pOut, "\n", 1);
          iCur = 0;
        }
        wantSpace = 0;
        omitSpace = 1;
      }
      if( wantSpace && nPre==0 ){
        if( iCur+n+1>=80 ){
          blob_append(pOut, "\n", 1);
          iCur = 0;
        }else{
          blob_append(pOut, " ", 1);
          iCur++;
        }
      }
      blob_append(pOut, zIn, n);
      iCur += n;
      wantSpace = 0;
      if( eTag==MARKUP_BR || eTag==MARKUP_HR ){
        blob_append(pOut, "\n", 1);
        iCur = 0;
      }
    }else if( fossil_isspace(zIn[0]) ){
      if( nPre ){
        blob_append(pOut, zIn, n);
      }else{
        wantSpace = !omitSpace;
      }
    }else{
      if( wantSpace && nPre==0 ){
        if( iCur+n+1>=80 ){
          blob_append(pOut, "\n", 1);
          iCur = 0;
        }else{
          blob_append(pOut, " ", 1);
          iCur++;
        }
      }
      blob_append(pOut, zIn, n);
      iCur += n;
      wantSpace = omitSpace = 0;
    }
    zIn += n;
  }
  if( iCur ) blob_append(pOut, "\n", 1);
}

/*
** COMMAND: test-html-tidy
**
** Run the htmlTidy() routine on the content of all files named on
** the command-line and write the results to standard output.
*/
void test_html_tidy(void){
  Blob in, out;
  int i;

  for(i=2; i<g.argc; i++){
    blob_read_from_file(&in, g.argv[i]);
    blob_zero(&out);
    htmlTidy(blob_str(&in), &out);
    blob_reset(&in);
    fossil_puts(blob_str(&out), 0);
    blob_reset(&out);
  }
}

/*
** Remove all HTML markup from the input text.  The output written into
** pOut is pure text.
**
** Put the title on the first line, if there is any <title> markup.
** If there is no <title>, then create a blank first line.
*/
void html_to_plaintext(const char *zIn, Blob *pOut){
  int n;
  int i, j;
  int inTitle = 0;          /* True between <title>...</title> */
  int seenText = 0;         /* True after first non-whitespace seen */
  int nNL = 0;              /* Number of \n characters at the end of pOut */
  int nWS = 0;              /* True if pOut ends with whitespace */
  while( fossil_isspace(zIn[0]) ) zIn++;
  while( zIn[0] ){
    n = nextHtmlToken(zIn);
    if( zIn[0]=='<' && n>1 ){
      int isCloseTag;
      int eTag;
      int eType;
      char zTag[32];
      isCloseTag = zIn[1]=='/';
      for(i=0, j=1+isCloseTag; i<30 && fossil_isalnum(zIn[j]); i++, j++){
         zTag[i] = fossil_tolower(zIn[j]);
      }
      zTag[i] = 0;
      eTag = findTag(zTag);
      eType = aMarkup[eTag].iType;
      if( eTag==MARKUP_INVALID && fossil_strnicmp(zIn,"<style",6)==0 ){
        zIn += n;
        while( zIn[0] ){
          n = nextHtmlToken(zIn);
          if( fossil_strnicmp(zIn, "</style",7)==0 ) break;
          zIn += n;
        }
        if( zIn[0]=='<' ) zIn += n;
        continue;
      }
      if( eTag==MARKUP_TITLE ){
        inTitle = !isCloseTag;
      }
      if( !isCloseTag && seenText && (eType & (MUTYPE_BLOCK|MUTYPE_TABLE))!=0 ){
        if( nNL==0 ){
          blob_append(pOut, "\n", 1);
          nNL++;
        }
        nWS = 1;
      }
    }else if( fossil_isspace(zIn[0]) ){
      if( seenText ){
        nNL = 0;
        if( !inTitle ){ /* '\n' -> ' ' within <title> */
          for(i=0; i<n; i++) if( zIn[i]=='\n' ) nNL++;
        }
        if( !nWS ){
          blob_append(pOut, nNL ? "\n" : " ", 1);
          nWS = 1;
        }
      }
    }else if( zIn[0]=='&' ){
      char c = '?';
      if( zIn[1]=='#' ){
        int x = atoi(&zIn[1]);
        if( x>0 && x<=127 ) c = x;
      }else{
        static const struct { int n; char c; char *z; } aEntity[] = {
           { 5, '&', "&amp;"   },
           { 4, '<', "&lt;"    },
           { 4, '>', "&gt;"    },
           { 6, ' ', "&nbsp;"  },
        };
        int jj;
        for(jj=0; jj<count(aEntity); jj++){
          if( aEntity[jj].n==n && strncmp(aEntity[jj].z,zIn,n)==0 ){
            c = aEntity[jj].c;
            break;
          }
        }
      }
      if( fossil_isspace(c) ){
        if( nWS==0 && seenText ) blob_append(pOut, &c, 1);
        nWS = 1;
        nNL = c=='\n';
      }else{
        if( !seenText && !inTitle ) blob_append(pOut, "\n", 1);
        seenText = 1;
        nNL = nWS = 0;
        blob_append(pOut, &c, 1);
      }
    }else{
      if( !seenText && !inTitle ) blob_append(pOut, "\n", 1);
      seenText = 1;
      nNL = nWS = 0;
      blob_append(pOut, zIn, n);
    }
    zIn += n;
  }
  if( nNL==0 ) blob_append(pOut, "\n", 1);
}

/*
** COMMAND: test-html-to-text
**
** Usage: %fossil test-html-to-text FILE ...
**
** Read all files named on the command-line.  Convert the file
** content from HTML to text and write the results on standard
** output.
**
** This command is intended as a test and debug interface for
** the html_to_plaintext() routine.
*/
void test_html_to_text(void){
  Blob in, out;
  int i;

  for(i=2; i<g.argc; i++){
    blob_read_from_file(&in, g.argv[i]);
    blob_zero(&out);
    html_to_plaintext(blob_str(&in), &out);
    blob_reset(&in);
    fossil_puts(blob_str(&out), 0);
    blob_reset(&out);
  }
}
