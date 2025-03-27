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
#define WIKI_HTMLONLY      0x0001  /* HTML markup only.  No wiki */
#define WIKI_INLINE        0x0002  /* Do not surround with <p>..</p> */
/* avalable for reuse:     0x0004  ---  formerly WIKI_NOBLOCK */
#define WIKI_BUTTONS       0x0008  /* Allow sub-menu buttons */
#define WIKI_NOBADLINKS    0x0010  /* Ignore broken hyperlinks */
#define WIKI_LINKSONLY     0x0020  /* No markup.  Only decorate links */
#define WIKI_NEWLINE       0x0040  /* Honor \n - break lines at each \n */
#define WIKI_MARKDOWNLINKS 0x0080  /* Resolve hyperlinks as in markdown */
#define WIKI_SAFE          0x0100  /* Make the result safe for embedding */
#define WIKI_TARGET_BLANK  0x0200  /* Hyperlinks go to a new window */
#define WIKI_NOBRACKET     0x0400  /* Omit extra [..] around hyperlinks */
#define WIKI_ADMIN         0x0800  /* Ignore g.perm.Hyperlink */
#define WIKI_MARK          0x1000  /* Add <mark>..</mark> around problems */

/*
** Return values from wiki_convert
*/
#define RENDER_LINK       0x0001  /* One or more hyperlinks rendered */
#define RENDER_ENTITY     0x0002  /* One or more HTML entities (ex: &lt;) */
#define RENDER_TAG        0x0004  /* One or more HTML tags */
#define RENDER_BLOCKTAG   0x0008  /* One or more HTML block tags (ex: <p>) */
#define RENDER_BLOCK      0x0010  /* Block wiki (paragraphs, etc.) */
#define RENDER_MARK       0x0020  /* Output contains <mark>..</mark> */
#define RENDER_BADLINK    0x0100  /* Bad hyperlink syntax seen */
#define RENDER_BADTARGET  0x0200  /* Bad hyperlink target */
#define RENDER_BADTAG     0x0400  /* Bad HTML tag or tag syntax */
#define RENDER_BADENTITY  0x0800  /* Bad HTML entity syntax */
#define RENDER_BADHTML    0x1000  /* Bad HTML seen */
#define RENDER_ERROR      0x8000  /* Some other kind of error */
/* Composite values: */
#define RENDER_ANYERROR   0x9f00  /* Mask for any kind of error */

#endif /* INTERFACE */


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
  ATTR_TITLE,
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
  AMSK_FACE         = 0x00000800,
  AMSK_HEIGHT       = 0x00001000,
  AMSK_HREF         = 0x00002000,
  AMSK_HSPACE       = 0x00004000,
  AMSK_ID           = 0x00008000,
  AMSK_LINKS        = 0x00010000,
  AMSK_NAME         = 0x00020000,
  AMSK_ROWSPAN      = 0x00040000,
  AMSK_SIZE         = 0x00080000,
  AMSK_SRC          = 0x00100000,
  AMSK_START        = 0x00200000,
  AMSK_STYLE        = 0x00400000,
  AMSK_TARGET       = 0x00800000,
  AMSK_TITLE        = 0x01000000,
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
  { "title",         AMSK_TITLE          },
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
** in aMarkup[].
*/
enum markup_t {
  MARKUP_INVALID = 0,
  MARKUP_A,
  MARKUP_ABBR,
  MARKUP_ADDRESS,
  MARKUP_HTML5_ARTICLE,
  MARKUP_HTML5_ASIDE,
  MARKUP_B,
  MARKUP_BIG,
  MARKUP_BLOCKQUOTE,
  MARKUP_BR,
  MARKUP_CENTER,
  MARKUP_CITE,
  MARKUP_CODE,
  MARKUP_COL,
  MARKUP_COLGROUP,
  MARKUP_DD,
  MARKUP_DEL,
  MARKUP_DETAILS,
  MARKUP_DFN,
  MARKUP_DIV,
  MARKUP_DL,
  MARKUP_DT,
  MARKUP_EM,
  MARKUP_FONT,
  MARKUP_HTML5_FOOTER,
  MARKUP_H1,
  MARKUP_H2,
  MARKUP_H3,
  MARKUP_H4,
  MARKUP_H5,
  MARKUP_H6,
  MARKUP_HTML5_HEADER,
  MARKUP_HR,
  MARKUP_I,
  MARKUP_IMG,
  MARKUP_INS,
  MARKUP_KBD,
  MARKUP_LI,
  MARKUP_HTML5_NAV,
  MARKUP_NOBR,
  MARKUP_NOWIKI,
  MARKUP_OL,
  MARKUP_P,
  MARKUP_PRE,
  MARKUP_S,
  MARKUP_SAMP,
  MARKUP_HTML5_SECTION,
  MARKUP_SMALL,
  MARKUP_SPAN,
  MARKUP_STRIKE,
  MARKUP_STRONG,
  MARKUP_SUB,
  MARKUP_SUMMARY,
  MARKUP_SUP,
  MARKUP_TABLE,
  MARKUP_TBODY,
  MARKUP_TD,
  MARKUP_TFOOT,
  MARKUP_TH,
  MARKUP_THEAD,
  MARKUP_TITLE,
  MARKUP_TR,
  MARKUP_TT,
  MARKUP_U,
  MARKUP_UL,
  MARKUP_VAR,
  MARKUP_VERBATIM
};

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

/* MUTYPE values for elements that require strictly nested end-tags */
#define MUTYPE_Nested      0x0656

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
                    AMSK_HREF|AMSK_NAME|AMSK_CLASS|AMSK_TARGET|AMSK_STYLE|
                    AMSK_TITLE},
 { "abbr",          MARKUP_ABBR,         MUTYPE_FONT,
                    AMSK_ID|AMSK_CLASS|AMSK_STYLE|AMSK_TITLE },
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
 { "del",           MARKUP_DEL,          MUTYPE_FONT,          AMSK_STYLE },
 { "details",       MARKUP_DETAILS,      MUTYPE_BLOCK,
                    AMSK_ID|AMSK_CLASS|AMSK_STYLE },
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
 { "ins",           MARKUP_INS,          MUTYPE_FONT,          AMSK_STYLE },
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
 { "summary",       MARKUP_SUMMARY,      MUTYPE_BLOCK,
                    AMSK_ALIGN|AMSK_CLASS|AMSK_STYLE  },
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
#define IN_LIST             0x0200000  /* Within wiki <ul> or <ol> */

/*
** Current state of the rendering engine
*/
typedef struct Renderer Renderer;
struct Renderer {
  Blob *pOut;                 /* Output appended to this blob */
  int state;                  /* Flag that govern rendering */
  int mRender;                /* Mask of RENDER_* values to return */
  unsigned renderFlags;       /* Flags from the client */
  int wikiList;               /* Current wiki list type */
  int inVerbatim;             /* True in <verbatim> mode */
  int preVerbState;           /* Value of state prior to verbatim */
  int wantAutoParagraph;      /* True if a <p> is desired */
  int inAutoParagraph;        /* True if within an automatic paragraph */
  int pikchrHtmlFlags;        /* Flags for pikchr_to_html() */
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
int html_tag_length(const char *z){
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
  const char *zReject;
  if( flags & ALLOW_WIKI ){
    zReject = "<&[\n";
  }else if( flags & ALLOW_LINKS ){
    zReject = "<&[";
  }else{
    zReject = "<&";
  }
  return strcspn(z, zReject);
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
** Check to see if the z[] string is the beginning of an enumeration value.
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
    n = html_tag_length(z);
    if( n>0 ){
      p->mRender |= RENDER_TAG;
      *pTokenType = TOKEN_MARKUP;
      return n;
    }else{
      p->mRender |= RENDER_BADTAG;
      *pTokenType = TOKEN_CHARACTER;
      return 1;
    }
  }
  if( z[0]=='&' ){
    p->mRender |= RENDER_ENTITY;
    if( (p->inVerbatim || !isElement(z)) ){
      *pTokenType = TOKEN_CHARACTER;
      return 1;
    }
  }
  if( (p->state & ALLOW_WIKI)!=0 ){
    if( z[0]=='\n' ){
      n = paragraphBreakLength(z);
      if( n>0 ){
        *pTokenType = TOKEN_PARAGRAPH;
        return n;
      }else{
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
    if( z[0]=='[' ){
      if( (n = linkLength(z))>0 ){
        *pTokenType = TOKEN_LINK;
        return n;
      }else if( p->state & WIKI_MARK ){
        blob_append_string(p->pOut, "<mark>");
        p->mRender |= RENDER_BADLINK|RENDER_MARK;
      }else{
        p->mRender |= RENDER_BADLINK;
      }
    }
  }else if( (p->state & ALLOW_LINKS)!=0 && z[0]=='[' ){
    if( (n = linkLength(z))>0 ){
      *pTokenType = TOKEN_LINK;
      return n;
    }else if( p->state & WIKI_MARK ){
      blob_append_string(p->pOut, "<mark>");
      p->mRender |= RENDER_BADLINK|RENDER_MARK;
    }else{
      p->mRender |= RENDER_BADLINK;
    }
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
  if( z[0]=='[' ){
    if( (n = linkLength(z))>0 ){
      *pTokenType = TOKEN_LINK;
      return n;
   }else if( p->state & WIKI_MARK ){
     blob_append_string(p->pOut, "<mark>");
     p->mRender |= RENDER_BADLINK|RENDER_MARK;
   }else{
      p->mRender |= RENDER_BADLINK;
    }
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
    if( j<(int)sizeof(zTag)-1 ) zTag[j++] = fossil_tolower(z[i]);
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
      if( j<(int)sizeof(zTag)-1 ) zTag[j++] = fossil_tolower(z[i]);
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
        while( !fossil_isspace(z[i]) && z[i]!='>' ){
          if( z[i]=='\'' || z[i]=='"' ) attrOk = 0;
          i++;
        }
      }
      if( attrOk ){
        p->aAttr[p->nAttr].zValue = zValue;
        p->aAttr[p->nAttr].cTerm = c = z[i];
        if( z[i]==0 ){
          i--;
        }else{
          z[i] = 0;
        }
      }
      i++;
    }
    if( attrOk ){
      seen |= aAttribute[iACode].iMask;
      p->nAttr++;
    }
    while( fossil_isspace(z[i]) ){ i++; }
    if( z[i]==0 || z[i]=='>' || (z[i]=='/' && z[i+1]=='>') ) break;
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
          blob_appendf(pOut, "=\"%R%s\"", zVal);
        }else{
          blob_appendf(pOut, "=\"%s\"", zVal);
        }
      }
    }
    if (p->iType & MUTYPE_SINGLE){
      blob_append_string(pOut, " /");
    }
    blob_append_char(pOut, '>');
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
  blob_append_string(p->pOut, "<p>");
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
  if( n>=(int)sizeof(zU2) ) n = sizeof(zU2)-1;
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
int is_ticket(
  const char *zTarget,    /* Ticket UUID */
  int *pClosed            /* True if the ticket is closed */
){
  static Stmt q;
  int n;
  int rc;
  char zLower[HNAME_MAX+1];
  char zUpper[HNAME_MAX+1];
  n = strlen(zTarget);
  memcpy(zLower, zTarget, n+1);
  canonical16(zLower, n+1);
  memcpy(zUpper, zLower, n+1);
  zUpper[n-1]++;
  if( !db_static_stmt_is_init(&q) ){
    char *zClosedExpr = db_get("ticket-closed-expr", "status='Closed'");
    db_static_prepare(&q,
      "SELECT %z FROM ticket "
      " WHERE tkt_uuid>=:lwr AND tkt_uuid<:upr",
      zClosedExpr /*safe-for-%s*/
    );
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
static const char *validWikiPageName(int mFlags, const char *zTarget){
  if( strncmp(zTarget, "wiki:", 5)==0
      && wiki_name_is_wellformed((const unsigned char*)zTarget) ){
    return zTarget+5;
  }
  if( strcmp(zTarget, "Sandbox")==0 ) return zTarget;
  if( wiki_name_is_wellformed((const unsigned char *)zTarget)
   && ((mFlags & WIKI_NOBADLINKS)==0 ||
        db_exists("SELECT 1 FROM tag WHERE tagname GLOB 'wiki-%q'"
                  " AND (SELECT value FROM tagxref WHERE tagid=tag.tagid"
                  " ORDER BY mtime DESC LIMIT 1) > 0", zTarget))
  ){
    return zTarget;
  }
  return 0;
}

static const char *wikiOverrideHash = 0;

/*
** Fossil-wiki hyperlinks to wiki pages should be overridden to the
** hash value supplied.  If the value is NULL, then override is cancelled
** and all overwrites operate normally.
*/
void wiki_hyperlink_override(const char *zUuid){
  wikiOverrideHash = zUuid;
}


/*
** If links to wiki page zTarget should be redirected to some historical
** version of that page, then return the hash of the historical version.
** If no override is required, return NULL.
*/
static const char *wiki_is_overridden(const char *zTarget){
  if( wikiOverrideHash==0 ) return 0;
  /* The override should only happen if the override version is not the
  ** latest version of the wiki page. */
  if( !db_exists(
    "SELECT 1 FROM tag, blob, tagxref AS xA, tagxref AS xB "
    " WHERE tag.tagname GLOB 'wiki-%q*'"
    "   AND blob.uuid GLOB '%q'"
    "   AND xA.tagid=tag.tagid AND xA.rid=blob.rid"
    "   AND xB.tagid=tag.tagid AND xB.mtime>xA.mtime",
    zTarget, wikiOverrideHash
  ) ){
    return 0;
  }
  return wikiOverrideHash;
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
**    [http://fossil-scm.org/]
**    [https://fossil-scm.org/]
**    [ftp://fossil-scm.org/]
**    [mailto:fossil-users@lists.fossil-scm.org]
**
**    [/path]        ->  Refers to the root of the Fossil hierarchy, not
**                       the root of the URI domain
**
**    [./relpath]
**    [../relpath]
**
**    [#fragment]
**
**    [0123456789abcdef]
**
**    [WikiPageName]
**    [wiki:WikiPageName]
**
**    [2010-02-27 07:13]
**
**    [InterMap:Link]  ->  Interwiki link
**
** The return value is a mask of RENDER_* values indicating what happened.
** Probably the return value is 0 on success and RENDER_BADTARGET or
** RENDER_BADLINK if there are problems.
*/
int wiki_resolve_hyperlink(
  Blob *pOut,             /* Write the HTML output here */
  int mFlags,             /* Rendering option flags */
  const char *zTarget,    /* Hyperlink target; text within [...] */
  char *zClose,           /* Write hyperlink closing text here */
  int nClose,             /* Bytes available in zClose[] */
  const char *zOrig,      /* Complete document text */
  const char *zTitle      /* Title of the link */
){
  const char *zTerm = "</a>";
  const char *z;
  char *zExtra = 0;
  const char *zExtraNS = 0;
  char *zRemote = 0;
  int rc = 0;

  if( zTitle ){
    zExtra = mprintf(" title='%h'", zTitle);
    zExtraNS = zExtra+1;
  }else if( mFlags & WIKI_TARGET_BLANK ){
    zExtra = mprintf(" target='_blank'");
    zExtraNS = zExtra+1;
  }
  assert( nClose>=20 );
  if( strncmp(zTarget, "http:", 5)==0
   || strncmp(zTarget, "https:", 6)==0
   || strncmp(zTarget, "ftp:", 4)==0
   || strncmp(zTarget, "mailto:", 7)==0
  ){
    blob_appendf(pOut, "<a href=\"%s\"%s>", zTarget, zExtra);
  }else if( zTarget[0]=='/' ){
    blob_appendf(pOut, "<a href=\"%R%h\"%s>", zTarget, zExtra);
  }else if( zTarget[0]=='.'
         && (zTarget[1]=='/' || (zTarget[1]=='.' && zTarget[2]=='/'))
         && (mFlags & WIKI_LINKSONLY)==0 ){
    blob_appendf(pOut, "<a href=\"%h\"%s>", zTarget, zExtra);
  }else if( zTarget[0]=='#' ){
    blob_appendf(pOut, "<a href=\"%h\"%s>", zTarget, zExtra);
  }else if( is_valid_hname(zTarget) ){
    int isClosed = 0;
    const char *zLB = (mFlags & WIKI_NOBRACKET)==0 ? "[" : "";
    if( strlen(zTarget)<=HNAME_MAX && is_ticket(zTarget, &isClosed) ){
      /* Special display processing for tickets.  Display the hyperlink
      ** as crossed out if the ticket is closed.
      */
      if( isClosed ){
        if( g.perm.Hyperlink ){
          blob_appendf(pOut,
             "%z<span class=\"wikiTagCancelled\">%s",
             xhref(zExtraNS,"%R/info/%s",zTarget), zLB
          );
          zTerm = "]</span></a>";
        }else{
          blob_appendf(pOut,"<span class=\"wikiTagCancelled\">%s", zLB);
          zTerm = "]</span>";
        }
      }else{
        if( g.perm.Hyperlink ){
          blob_appendf(pOut,"%z%s", xhref(zExtraNS,"%R/info/%s", zTarget),zLB);
          zTerm = "]</a>";
        }else{
          blob_appendf(pOut, "%s", zLB);
          zTerm = "]";
        }
      }
    }else if( !in_this_repo(zTarget) ){
      if( (mFlags & (WIKI_LINKSONLY|WIKI_NOBADLINKS))!=0 ){
        zTerm = "";
      }else if( (mFlags & WIKI_MARK)!=0 ){
        blob_appendf(pOut, "<mark>%s", zLB);
        zTerm = "]</mark>";
        rc |= RENDER_MARK;
      }else{
        blob_appendf(pOut, "<span class=\"brokenlink\">%s", zLB);
        zTerm = "]</span>";
      }
      rc |= RENDER_BADTARGET;
    }else if( g.perm.Hyperlink || (mFlags & WIKI_ADMIN)!=0 ){
      blob_appendf(pOut, "%z%s",xhref(zExtraNS, "%R/info/%s", zTarget), zLB);
      zTerm = "]</a>";
    }else{
      zTerm = "";
    }
    if( zTerm[0]==']' && (mFlags & WIKI_NOBRACKET)!=0 ) zTerm++;
  }else if( (zRemote = interwiki_url(zTarget))!=0 ){
    blob_appendf(pOut, "<a href=\"%z\"%s>", zRemote, zExtra);
    zTerm = "</a>";
  }else if( (z = validWikiPageName(mFlags, zTarget))!=0 ){
    /* The link is to a valid wiki page name */
    const char *zOverride = wiki_is_overridden(zTarget);
    if( zOverride ){
      blob_appendf(pOut, "<a href=\"%R/info/%S\"%s>", zOverride, zExtra);
    }else{
      blob_appendf(pOut, "<a href=\"%R/wiki?name=%T\"%s>", z, zExtra);
    }
  }else if( strlen(zTarget)>=10 && fossil_isdigit(zTarget[0]) && zTarget[4]=='-'
            && db_int(0, "SELECT datetime(%Q) NOT NULL", zTarget) ){
    /* Dates or date-and-times in ISO8601 resolve to a link to the
    ** timeline for that date */
    blob_appendf(pOut, "<a href=\"%R/timeline?c=%T\"%s>", zTarget, zExtra);
  }else if( mFlags & WIKI_MARKDOWNLINKS ){
    /* If none of the above, and if rendering links for markdown, then
    ** create a link to the literal text of the target */
    blob_appendf(pOut, "<a href=\"%h\"%s>", zTarget, zExtra);
  }else if( mFlags & WIKI_MARK ){
    blob_appendf(pOut, "<mark>[");
    zTerm = "]</mark>";
    rc |= RENDER_BADTARGET|RENDER_MARK;
  }else if( zOrig && zTarget>=&zOrig[2]
        && zTarget[-1]=='[' && !fossil_isspace(zTarget[-2]) ){
    /* If the hyperlink markup is not preceded by whitespace, then it
    ** is probably a C-language subscript or similar, not really a
    ** hyperlink.  Just ignore it. */
    zTerm = "";
  }else if( (mFlags & (WIKI_NOBADLINKS|WIKI_LINKSONLY))!=0 ){
    /* Also ignore the link if various flags are set */
    zTerm = "";
    rc |= RENDER_BADTARGET;
  }else{
    blob_appendf(pOut, "<span class=\"brokenlink\">[%h]", zTarget);
    zTerm = "</span>";
    rc |= RENDER_BADTARGET;
  }
  if( zExtra ) fossil_free(zExtra);
  assert( (int)strlen(zTerm)<nClose );
  sqlite3_snprintf(nClose, zClose, "%s", zTerm);
  return rc;
}

/*
** Check zTarget to see if it looks like a valid hyperlink target.
** Return true if it does seem valid and false if not.
*/
int wiki_valid_link_target(char *zTarget){
  char zClose[30];
  Blob notUsed;
  blob_init(&notUsed, 0, 0);
  wiki_resolve_hyperlink(&notUsed, WIKI_NOBADLINKS|WIKI_ADMIN,
       zTarget, zClose, sizeof(zClose)-1, 0, 0);
  blob_reset(&notUsed);
  return zClose[0]!=0;
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
** z[] points to the text that immediately follows markup of the form:
**
**      <verbatim type='pikchr ...'>
**
** zClass is the argument to "type".  This routine will process the
** Pikchr text through the next matching </verbatim> (or until end-of-file)
** and append the resulting SVG output onto p.  It then returns the
** number of bytes of text processed, including the closing </verbatim>.
*/
static int wiki_process_pikchr(Renderer *p, char *z, const char *zClass){
  ParsedMarkup m;         /* Parsed closing tag */
  int i = 0;              /* For looping over z[] in search of </verbatim> */
  int iRet = 0;           /* Value  to return */
  int atEnd = 0;          /* True if se have found the </verbatim> */
  int nMarkup = 0;        /* Length of a markup we are checking */

  /* Search for the closing </verbatim> tag */
  while( z[i]!=0 ){
    char *zEnd = strchr(z+i, '<');
    if( zEnd==0 ){
      i += (int)strlen(z+i);
      iRet = i;
      break;
    }
    nMarkup = html_tag_length(zEnd);
    if( nMarkup<11 || fossil_strnicmp(zEnd, "</verbatim", 10)!=0 ){
      i = (int)(zEnd - z) + 1;
      continue;
    }
    (void)parseMarkup(&m, z+i);
    atEnd = endVerbatim(p, &m);
    unparseMarkup(&m);
    if( atEnd ){
      iRet = i + nMarkup;
      break;
    }
    i++;
  }

  /* The Pikchr source text should be i character in length and iRet is
  ** i plus the number of bytes in the </verbatim>.  Generate the reply.
  */
  assert( strncmp(zClass,"pikchr",6)==0 );
  zClass += 6;
  while( fossil_isspace(zClass[0]) ) zClass++;
  blob_append(p->pOut, "<p>", 3);
  pikchr_to_html(p->pOut, z, i, zClass, (int)strlen(zClass));
  blob_append(p->pOut, "</p>\n", 5);
  return iRet;
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
        if( p->wikiList ){
          popStackToTag(p, p->wikiList);
          p->wikiList = 0;
        }
        endAutoParagraph(p);
        blob_append_string(p->pOut, "\n\n");
        p->wantAutoParagraph = 1;
        p->state |= AT_PARAGRAPH|AT_NEWLINE;
        break;
      }
      case TOKEN_NEWLINE: {
        if( p->renderFlags & WIKI_NEWLINE ){
          blob_append_string(p->pOut, "<br>\n");
        }else{
          blob_append_string(p->pOut, "\n");
        }
        p->state |= AT_NEWLINE;
        break;
      }
      case TOKEN_BUL_LI: {
        p->mRender |= RENDER_BLOCK;
        if( p->wikiList!=MARKUP_UL ){
          if( p->wikiList ){
            popStackToTag(p, p->wikiList);
          }
          endAutoParagraph(p);
          pushStack(p, MARKUP_UL);
          blob_append_string(p->pOut, "<ul>");
          p->wikiList = MARKUP_UL;
        }
        popStackToTag(p, MARKUP_LI);
        startAutoParagraph(p);
        pushStack(p, MARKUP_LI);
        blob_append_string(p->pOut, "<li>");
        break;
      }
      case TOKEN_NUM_LI: {
        p->mRender |= RENDER_BLOCK;
        if( p->wikiList!=MARKUP_OL ){
          if( p->wikiList ){
            popStackToTag(p, p->wikiList);
          }
          endAutoParagraph(p);
          pushStack(p, MARKUP_OL);
          blob_append_string(p->pOut, "<ol>");
          p->wikiList = MARKUP_OL;
        }
        popStackToTag(p, MARKUP_LI);
        startAutoParagraph(p);
        pushStack(p, MARKUP_LI);
        blob_append_string(p->pOut, "<li>");
        break;
      }
      case TOKEN_ENUM: {
        p->mRender |= RENDER_BLOCK;
        if( p->wikiList!=MARKUP_OL ){
          if( p->wikiList ){
            popStackToTag(p, p->wikiList);
          }
          endAutoParagraph(p);
          pushStack(p, MARKUP_OL);
          blob_append_string(p->pOut, "<ol>");
          p->wikiList = MARKUP_OL;
        }
        popStackToTag(p, MARKUP_LI);
        startAutoParagraph(p);
        pushStack(p, MARKUP_LI);
        blob_appendf(p->pOut, "<li value=\"%d\">", atoi(z));
        break;
      }
      case TOKEN_INDENT: {
        p->mRender |= RENDER_BLOCK;
        assert( p->wikiList==0 );
        pushStack(p, MARKUP_BLOCKQUOTE);
        blob_append_string(p->pOut, "<blockquote>");
        p->wantAutoParagraph = 0;
        p->wikiList = MARKUP_BLOCKQUOTE;
        break;
      }
      case TOKEN_CHARACTER: {
        startAutoParagraph(p);
        if( p->state & WIKI_MARK ){
          blob_append_string(p->pOut, "<mark>");
          p->mRender |= RENDER_MARK;
        }
        if( z[0]=='<' ){
          p->mRender |= RENDER_BADTAG;
          blob_append_string(p->pOut, "&lt;");
        }else if( z[0]=='&' ){
          p->mRender |= RENDER_BADENTITY;
          blob_append_string(p->pOut, "&amp;");
        }
        if( p->state & WIKI_MARK ){
          if( fossil_isalnum(z[1]) || (z[1]=='/' && fossil_isalnum(z[2])) ){
            int kk;
            for(kk=2; fossil_isalnum(z[kk]); kk++){}
            blob_append(p->pOut, &z[1], kk-1);
            n = kk;
          }
          blob_append_string(p->pOut, "</mark>");
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
        p->mRender |= RENDER_LINK;
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
          zDisplay = zTarget + interwiki_removable_prefix(zTarget);
        }else{
          while( fossil_isspace(*zDisplay) ) zDisplay++;
        }
        p->mRender |= wiki_resolve_hyperlink(p->pOut, p->state,
                               zTarget, zClose, sizeof(zClose), zOrig, 0);
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
            blob_append_string(p->pOut, "</pre>");
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
            blob_append_string(p->pOut, "</pre>");
          }else{
            unparseMarkup(&markup);
            blob_append_string(p->pOut, "&lt;");
            n = 1;
          }
        }else

        /* Render invalid markup literally.  The markup appears in the
        ** final output as plain text.
        */
        if( markup.iCode==MARKUP_INVALID ){
          p->mRender |= RENDER_BADTAG;
          unparseMarkup(&markup);
          startAutoParagraph(p);
          if( p->state & WIKI_MARK ){
            p->mRender |= RENDER_MARK;
            blob_append_string(p->pOut, "<mark>");
            htmlize_to_blob(p->pOut, z, n);
            blob_append_string(p->pOut, "</mark>");
          }else{
            blob_append_string(p->pOut, "&lt;");
            htmlize_to_blob(p->pOut, z+1, n-1);
          }
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
          int ii; //, vAttrDidAppend=0;
          const char *zClass = 0;
          p->zVerbatimId = 0;
          p->inVerbatim = 1;
          p->preVerbState = p->state;
          p->state &= ~ALLOW_WIKI;
          for(ii=0; ii<markup.nAttr; ii++){
            if( markup.aAttr[ii].iACode == ATTR_ID ){
              p->zVerbatimId = markup.aAttr[ii].zValue;
            }else if( markup.aAttr[ii].iACode==ATTR_TYPE ){
              zClass = markup.aAttr[ii].zValue;
            }else if( markup.aAttr[ii].iACode==ATTR_LINKS
                   && !is_false(markup.aAttr[ii].zValue) ){
              p->state |= ALLOW_LINKS;
            }
          }
          endAutoParagraph(p);
          if( zClass==0 ){
            blob_append_string(p->pOut, "<pre class='verbatim'>");
          }else if( strncmp(zClass,"pikchr",6)==0 &&
                    (fossil_isspace(zClass[6]) || zClass[6]==0) ){
            n += wiki_process_pikchr(p, z+n, zClass);
            p->inVerbatim = 0;
            p->state = p->preVerbState;
          }else{
            blob_appendf(p->pOut, "<pre name='code' class='%h'>",
               zClass);
          }
          p->wantAutoParagraph = 0;
        }else
        if( markup.iType==MUTYPE_LI ){
          if( backupToType(p, MUTYPE_LIST)==0 ){
            endAutoParagraph(p);
            pushStack(p, MARKUP_UL);
            blob_append_string(p->pOut, "<ul>");
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
              blob_append_string(p->pOut, "<tr>");
            }
            p->wantAutoParagraph = 0;
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
            p->mRender |= RENDER_BLOCKTAG;
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
**
** Return a mask of RENDER_ flags indicating what happened.
*/
int wiki_convert(Blob *pIn, Blob *pOut, int flags){
  Renderer renderer;

  memset(&renderer, 0, sizeof(renderer));
  renderer.renderFlags = flags;
  renderer.state = ALLOW_WIKI|AT_NEWLINE|AT_PARAGRAPH|flags;
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
  blob_append_char(renderer.pOut, '\n');
  free(renderer.aStack);
  return renderer.mRender;
}

/*
** COMMAND: test-wiki-render
**
** Usage: %fossil test-wiki-render FILE [OPTIONS]
**
** Translate the input FILE from Fossil-wiki into HTML and write
** the resulting HTML on standard output.
**
** Options:
**    --buttons        Set the WIKI_BUTTONS flag
**    --dark-pikchr    Render pikchrs in dark mode
**    --flow           Render as text using comment_format
**    --htmlonly       Set the WIKI_HTMLONLY flag
**    --inline         Set the WIKI_INLINE flag
**    --linksonly      Set the WIKI_LINKSONLY flag
**    -m TEXT          Use TEXT in place of the content of FILE
**    --mark           Add <mark>...</mark> around problems
**    --nobadlinks     Set the WIKI_NOBADLINKS flag
**    --text           Run the output through html_to_plaintext()
**    --type           Break down the return code from wiki_convert()
*/
void test_wiki_render(void){
  Blob in, out;
  int flags = 0;
  int bText;
  int bFlow = 0;
  int showType = 0;
  int mType;
  const char *zIn;
  if( find_option("buttons",0,0)!=0 ) flags |= WIKI_BUTTONS;
  if( find_option("htmlonly",0,0)!=0 ) flags |= WIKI_HTMLONLY;
  if( find_option("linksonly",0,0)!=0 ) flags |= WIKI_LINKSONLY;
  if( find_option("nobadlinks",0,0)!=0 ) flags |= WIKI_NOBADLINKS;
  if( find_option("inline",0,0)!=0 ) flags |= WIKI_INLINE;
  if( find_option("mark",0,0)!=0 ) flags |= WIKI_MARK;
  if( find_option("dark-pikchr",0,0)!=0 ){
    pikchr_to_html_add_flags( PIKCHR_PROCESS_DARK_MODE );
  }
  bText = find_option("text",0,0)!=0;
  bFlow = find_option("flow",0,0)!=0;
  showType = find_option("type",0,0)!=0;
  zIn = find_option("msg","m",1);
  db_find_and_open_repository(OPEN_OK_NOT_FOUND|OPEN_SUBSTITUTE,0);
  verify_all_options();
  if( (zIn==0 && g.argc!=3) || (zIn!=0 && g.argc!=2) ) usage("FILE");
  blob_zero(&out);
  if( zIn ){
    blob_init(&in, zIn, -1);
  }else{
    blob_read_from_file(&in, g.argv[2], ExtFILE);
  }
  mType = wiki_convert(&in, &out, flags);
  if( bText ){
    Blob txt;
    int htot = HTOT_TRIM;
    if( terminal_is_vt100() ) htot |= HTOT_VT100;
    if( bFlow ) htot |= HTOT_FLOW;
    blob_init(&txt, 0, 0);
    html_to_plaintext(blob_str(&out),&txt, htot);
    blob_reset(&out);
    out = txt;
  }
  if( bFlow ){
    fossil_print("   ");
    comment_print(blob_str(&out), 0, 3, terminal_get_width(80)-3,
                  get_comment_format());
  }else{
    blob_write_to_file(&out, "-");
  }
  if( showType ){
    fossil_print("%.*c\nResult Codes:", terminal_get_width(80)-1, '*');
    if( mType & RENDER_LINK )      fossil_print(" LINK");
    if( mType & RENDER_ENTITY )    fossil_print(" ENTITY");
    if( mType & RENDER_TAG )       fossil_print(" TAG");
    if( mType & RENDER_BLOCKTAG )  fossil_print(" BLOCKTAG");
    if( mType & RENDER_BLOCK )     fossil_print(" BLOCK");
    if( mType & RENDER_MARK )      fossil_print(" MARK");
    if( mType & RENDER_BADLINK )   fossil_print(" BADLINK");
    if( mType & RENDER_BADTARGET ) fossil_print(" BADTARGET");
    if( mType & RENDER_BADTAG )    fossil_print(" BADTAG");
    if( mType & RENDER_BADENTITY ) fossil_print(" BADENTITY");
    if( mType & RENDER_BADHTML )   fossil_print(" BADHTML");
    if( mType & RENDER_ERROR )     fossil_print(" ERROR");
    fossil_print("\n");
  }
}

/*
** COMMAND: test-markdown-render
**
** Usage: %fossil test-markdown-render FILE ...
**
** Render markdown in FILE as HTML on stdout.
** Options:
**
**    --dark-pikchr     Render pikchrs in dark mode
**    --lint-footnotes  Print stats for footnotes-related issues
**    --safe            Restrict the output to use only "safe" HTML
**    --text            Run the output through html_to_plaintext().
*/
void test_markdown_render(void){
  Blob in, out;
  int i;
  int bSafe = 0, bFnLint = 0, bText = 0;
  db_find_and_open_repository(OPEN_OK_NOT_FOUND|OPEN_SUBSTITUTE,0);
  bSafe = find_option("safe",0,0)!=0;
  bFnLint = find_option("lint-footnotes",0,0)!=0;
  if( find_option("dark-pikchr",0,0)!=0 ){
    pikchr_to_html_add_flags( PIKCHR_PROCESS_DARK_MODE );
  }
  bText = find_option("text",0,0)!=0;
  verify_all_options();
  for(i=2; i<g.argc; i++){
    blob_zero(&out);
    blob_read_from_file(&in, g.argv[i], ExtFILE);
    if( g.argc>3 ){
      fossil_print("<!------ %h ------->\n", g.argv[i]);
    }
    markdown_to_html(&in, 0, &out);
    safe_html_context( bSafe ? DOCSRC_UNTRUSTED : DOCSRC_TRUSTED );
    safe_html(&out);
    if( bText ){
      Blob txt;
      blob_init(&txt, 0, 0);
      html_to_plaintext(blob_str(&out), &txt, HTOT_VT100);
      blob_reset(&out);
      out = txt;
    }
    blob_write_to_file(&out, "-");
    blob_reset(&in);
    blob_reset(&out);
  }
  if( bFnLint && (g.ftntsIssues[0] || g.ftntsIssues[1]
      || g.ftntsIssues[2] || g.ftntsIssues[3] )){
    fossil_fatal("There were issues with footnotes:\n"
                  " %8d misreference%s\n"
                  " %8d unreferenced\n"
                  " %8d split\n"
                  " %8d overnested",
                  g.ftntsIssues[0], g.ftntsIssues[0]==1?"":"s",
                  g.ftntsIssues[1], g.ftntsIssues[2], g.ftntsIssues[3]);
  }
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
**
** The return value is a mask of RENDER_ flags.
*/
int wiki_extract_links(
  char *z,           /* The wiki text from which to extract links */
  Backlink *pBklnk,  /* Backlink extraction context */
  int flags          /* wiki parsing flags */
){
  Renderer renderer;
  int tokenType;
  ParsedMarkup markup;
  int n;
  int wikiHtmlOnly = 0;

  memset(&renderer, 0, sizeof(renderer));
  renderer.state = ALLOW_WIKI|AT_NEWLINE|AT_PARAGRAPH;
  if( wikiUsesHtml() ){
    renderer.state |= WIKI_HTMLONLY;
    wikiHtmlOnly = 1;
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
        int i;

        zTarget = &z[1];
        for(i=0; zTarget[i] && zTarget[i]!='|' && zTarget[i]!=']'; i++){}
        while(i>1 && zTarget[i-1]==' '){ i--; }
        backlink_create(pBklnk, zTarget, i);
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
  return renderer.mRender;
}

/*
** Return the length, in bytes, of the HTML token that z is pointing to.
*/
int html_token_length(const char *z){
  int n;
  char c;
  if( (c=z[0])=='<' ){
    n = html_tag_length(z);
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
** z points to someplace in the middle of HTML markup.  Return the length
** of the subtoken that starts on z.
*/
int html_subtoken_length(const char *z){
  int n;
  char c;
  c = z[0];
  if( fossil_isspace(c) ){
    for(n=1; z[n] && fossil_isspace(z[n]); n++){}
    return n;
  }
  if( c=='"' || c=='\'' ){
    for(n=1; z[n] && z[n]!=c && z[n]!='>'; n++){}
    if( z[n]==c ) n++;
    return n;
  }
  if( c=='>' ){
    return 0;
  }
  if( c=='=' ){
    return 1;
  }
  if( fossil_isalnum(c) || c=='/' ){
    for(n=1; (c=z[n])!=0 && (fossil_isalnum(c) || c=='-' || c=='_'); n++){}
    return n;
  }
  return 1;
}

/*
** z points to an HTML markup token:  <TAG ATTR=VALUE ...>
** This routine looks for the VALUE associated with zAttr and returns
** a pointer to the start of that value and sets *pLen to be the length
** in bytes for the value.  Or it returns NULL if no such attr exists.
*/
const char *html_attribute(const char *zMarkup, const char *zAttr, int *pLen){
  int i = 1;
  int n;
  int nAttr;
  int iMatchCnt = 0;
  assert( zMarkup[0]=='<' );
  assert( zMarkup[1]!=0 );
  n = html_subtoken_length(zMarkup+i);
  if( n==0 ) return 0;
  i += n;
  nAttr = (int)strlen(zAttr);
  while( 1 ){
    const char *zStart = zMarkup+i;
    n = html_subtoken_length(zStart);
    if( n==0 ) break;
    i += n;
    if( fossil_isspace(zStart[0]) ) continue;
    if( n==nAttr && fossil_strnicmp(zAttr,zStart,nAttr)==0 ){
      iMatchCnt = 1;
    }else if( n==1 && zStart[0]=='=' && iMatchCnt==1 ){
      iMatchCnt = 2;
    }else if( iMatchCnt==2 ){
      if( (zStart[0]=='"' || zStart[0]=='\'') && zStart[n-1]==zStart[0] ){
        zStart++;
        n -= 2;
      }
      *pLen = n;
      return zStart;
    }else{
      iMatchCnt = 0;
    }
  }
  return 0;
}

/*
** COMMAND: test-html-tokenize
**
** Tokenize an HTML file.  Return the offset and length and text of
** each token - one token per line.  Omit white-space tokens.
*/
void test_html_tokenize(void){
  Blob in;
  char *z;
  int i;
  int iOfst, n;

  for(i=2; i<g.argc; i++){
    blob_read_from_file(&in, g.argv[i], ExtFILE);
    z = blob_str(&in);
    for(iOfst=0; z[iOfst]; iOfst+=n){
      n = html_token_length(z+iOfst);
      if( fossil_isspace(z[iOfst]) ) continue;
      fossil_print("%d %d %.*s\n", iOfst, n, n, z+iOfst);
      if( z[iOfst]=='<' && n>1 ){
        int j,k;
        for(j=iOfst+1; (k = html_subtoken_length(z+j))>0; j+=k){
          if( fossil_isspace(z[j]) || z[j]=='=' ) continue;
          fossil_print("# %d %d %.*s\n", j, k, k, z+j);
        }
      }
    }
    blob_reset(&in);
  }
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
    n = html_token_length(zIn);
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
          if( nPre==0 ){ blob_append_char(pOut, '\n'); iCur = 0; }
          continue;
        }else{
          if( iCur && nPre==0 ){ blob_append_char(pOut, '\n'); iCur = 0; }
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
          blob_append_char(pOut, '\n');
          iCur = 0;
        }
        wantSpace = 0;
        omitSpace = 1;
      }
      if( wantSpace && nPre==0 ){
        if( iCur+n+1>=80 ){
          blob_append_char(pOut, '\n');
          iCur = 0;
        }else{
          blob_append_char(pOut, ' ');
          iCur++;
        }
      }
      blob_append(pOut, zIn, n);
      iCur += n;
      wantSpace = 0;
      if( eTag==MARKUP_BR || eTag==MARKUP_HR ){
        blob_append_char(pOut, '\n');
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
          blob_append_char(pOut, '\n');
          iCur = 0;
        }else{
          blob_append_char(pOut, ' ');
          iCur++;
        }
      }
      blob_append(pOut, zIn, n);
      iCur += n;
      wantSpace = omitSpace = 0;
    }
    zIn += n;
  }
  if( iCur ) blob_append_char(pOut, '\n');
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
    blob_read_from_file(&in, g.argv[i], ExtFILE);
    blob_zero(&out);
    htmlTidy(blob_str(&in), &out);
    blob_reset(&in);
    fossil_puts(blob_buffer(&out), 0, blob_size(&out));
    blob_reset(&out);
  }
}

#if INTERFACE
/*
** Allowed flag options for html_to_plaintext().
*/
#define HTOT_VT100   0x01 /* <mark> becomes ^[[91m */
#define HTOT_FLOW    0x02 /* Collapse internal whitespace to a single space */
#define HTOT_TRIM    0x04 /* Trim off leading and trailing whitespace */

#endif /* INTERFACE */

/*
** Add <MARK> or </MARK> to the output, or similar VT-100 escape
** codes.
*/
static void addMark(Blob *pOut, int mFlags, int isClose){
  static const char *az[4] = { "<MARK>", "</MARK>", "\033[91m", "\033[0m" };
  int i = 0;
  if( isClose ) i++;
  if( mFlags & HTOT_VT100 ) i += 2;
  blob_append(pOut, az[i], -1);
}

/*
** Remove all HTML markup from the input text.  The output written into
** pOut is pure text.
**
** Put the title on the first line, if there is any <title> markup.
** If there is no <title>, then create a blank first line.
*/
void html_to_plaintext(const char *zIn, Blob *pOut, int mFlags){
  int n;
  int i, j;
  int bFlow = 0;          /* Transform internal WS into a single space */
  int prevWS = 1;         /* Previous output was whitespace or start of msg */
  int nMark = 0;          /* True if inside of <mark>..</mark> */

  for(i=0; fossil_isspace(zIn[i]); i++){}
  if( i>0 && (mFlags & HTOT_TRIM)==0 ){
    blob_append(pOut, zIn, i);
  }
  zIn += i;
  if( mFlags & HTOT_FLOW ) bFlow = 1;
  while( zIn[0] ){
    n = html_token_length(zIn);
    if( zIn[0]=='<' && n>1 ){
      int isCloseTag;
      int eTag;
      int eType;
      char zTag[32];
      prevWS = 0;
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
          n = html_token_length(zIn);
          if( fossil_strnicmp(zIn, "</style",7)==0 ) break;
          zIn += n;
        }
        if( zIn[0]=='<' ) zIn += n;
        continue;
      }
      if( eTag==MARKUP_INVALID && strcmp(zTag,"mark")==0 ){
        if( isCloseTag && nMark ){
          addMark(pOut, mFlags, 1);
          nMark = 0;
        }else if( !isCloseTag && !nMark ){
          addMark(pOut, mFlags, 0);
          nMark = 1;
        }
        zIn += n;
        continue;            
      }
      if( eTag==MARKUP_TITLE ){
        if( isCloseTag && (mFlags & HTOT_FLOW)==0 ){
          bFlow = 0;
        }else{
          bFlow = 1;
        }
      }
      if( !isCloseTag && (eType & (MUTYPE_BLOCK|MUTYPE_TABLE))!=0 ){
        blob_append_char(pOut, '\n');
      }
    }else if( fossil_isspace(zIn[0]) ){
      if( bFlow==0 ){
        if( zIn[n]==0 && (mFlags & HTOT_TRIM) ) break;
        blob_append(pOut, zIn, n);
      }else if( !prevWS ){
        prevWS = 1;
        blob_append_char(pOut, ' ');
        zIn += n;
        n = 0;
      }
    }else if( zIn[0]=='&' ){
      u32 c = '?';
      prevWS = 0;
      if( zIn[1]=='#' ){
        c = atoi(&zIn[2]);
        if( c==0 ) c = '?';
      }else{
        static const struct { int n; u32 c; char *z; } aEntity[] = {
           { 5, '&', "&amp;"   },
           { 4, '<', "&lt;"    },
           { 4, '>', "&gt;"    },
           { 6, ' ', "&nbsp;"  },
           { 6, '"', "&quot;"  },
        };
        int jj;
        for(jj=0; jj<count(aEntity); jj++){
          if( aEntity[jj].n==n && strncmp(aEntity[jj].z,zIn,n)==0 ){
            c = aEntity[jj].c;
            break;
          }
        }
      }
      if( c<0x00080 ){
        blob_append_char(pOut, c & 0xff);
      }else if( c<0x00800 ){
        blob_append_char(pOut, 0xc0 + (u8)((c>>6)&0x1f));
        blob_append_char(pOut, 0x80 + (u8)(c&0x3f));
      }else if( c<0x10000 ){
        blob_append_char(pOut, 0xe0 + (u8)((c>>12)&0x0f));
        blob_append_char(pOut, 0x80 + (u8)((c>>6)&0x3f));
        blob_append_char(pOut, 0x80 + (u8)(c&0x3f));
      }else{
        blob_append_char(pOut, 0xf0 + (u8)((c>>18)&0x07));
        blob_append_char(pOut, 0x80 + (u8)((c>>12)&0x3f));
        blob_append_char(pOut, 0x80 + (u8)((c>>6)&0x3f));
        blob_append_char(pOut, 0x80 + (u8)(c&0x3f));
      }
    }else{
      prevWS = 0;
      blob_append(pOut, zIn, n);
    }
    zIn += n;
  }
  if( nMark ){
    addMark(pOut, mFlags, 1);
  }
}

/*
** COMMAND: test-html-to-text
**
** Usage: %fossil test-html-to-text [OPTIONS] FILE ...
**
** Read all files named on the command-line.  Convert the file
** content from HTML to text and write the results on standard
** output.
**
** This command is intended as a test and debug interface for
** the html_to_plaintext() routine.
**
** Options:
**
**     --vt100              Translate <mark> and </mark> into ANSI/VT100
**                          escapes to highlight the contained text.
*/
void test_html_to_text(void){
  Blob in, out;
  int i;
  int mFlags = 0;
  if( find_option("vt100",0,0)!=0 ) mFlags |= HTOT_VT100;

  for(i=2; i<g.argc; i++){
    blob_read_from_file(&in, g.argv[i], ExtFILE);
    blob_zero(&out);
    html_to_plaintext(blob_str(&in), &out, mFlags);
    blob_reset(&in);
    fossil_puts(blob_buffer(&out), 0, blob_size(&out));
    blob_reset(&out);
  }
}

/****************************************************************************
** safe-html:
**
** An interface for preventing HTML constructs (ex: <style>, <form>, etc)
** from being inserted into Wiki and Forum posts using Markdown.   See the
** comment on safe_html_append() for additional information on what is meant
** by "safe".
**
** The safe-html restrictions only apply to Markdown, as Fossil-Wiki only
** allows safe-html by design - unsafe-HTML is never and has never been
** allowed in Fossil-Wiki.
**
** This code is in the wikiformat.c file so that it can have access to the
** white-list of acceptable HTML in the aMarkup[] array.
*/

/*
** An instance of this object keeps track of the nesting of HTML
** elements for safe_html_append().
*/
typedef struct HtmlTagStack HtmlTagStack;
struct HtmlTagStack {
  int n;                /* Current tag stack depth */
  int nAlloc;           /* Space allocated for aStack[] */
  int *aStack;          /* The stack of tags */
  int aSpace[10];       /* Initial static space, to avoid malloc() */
};

/*
** Initialize bulk memory to a valid empty tagstack.
*/
static void html_tagstack_init(HtmlTagStack *p){
  p->n = 0;
  p->nAlloc = 0;
  p->aStack = p->aSpace;
}

/*
** Push a new element onto the tag statk
*/
static void html_tagstack_push(HtmlTagStack *p, int e){
  if( p->n>=ArraySize(p->aSpace) && p->n>=p->nAlloc ){
    if( p->nAlloc==0 ){
      int *aNew;
      p->nAlloc = 50;
      aNew = fossil_malloc( sizeof(p->aStack[0])*p->nAlloc );
      memcpy(aNew, p->aStack, sizeof(p->aStack[0])*p->n );
      p->aStack = aNew;
    }else{
      p->nAlloc *= 2;
      p->aStack = fossil_realloc(p->aStack, sizeof(p->aStack[0])*p->nAlloc );
    }
  }
  p->aStack[p->n++] = e;
}

/*
** Clear a tag stack, reclaiming any memory allocations.
*/
static void html_tagstack_clear(HtmlTagStack *p){
  if( p->nAlloc ){
    fossil_free(p->aStack);
    p->nAlloc = 0;
    p->aStack = p->aSpace;
  }
  p->n = 0;
}

/*
** The HTML end-tag eEnd wants to be added to pBlob.
**
** If an open-tag for eEnd exists anywhere on the stack, then
** pop it and all prior elements from the task, issuing appropriate
** end-tags as you go.
**
** If there is no open-tag for eEnd on the stack, then this
** routine is a no-op.
*/
static void html_tagstack_pop(HtmlTagStack *p, Blob *pBlob, int eEnd){
  int i, e;
  if( eEnd!=0 ){
    for(i=p->n-1; i>=0 && p->aStack[i]!=eEnd; i--){}
    if( i<0 ){
      blob_appendf(pBlob, "<span class='error'>&lt;/%s&gt;</span>",
                   aMarkup[eEnd].zName);
      return;
    }
  }else if( p->n==0 ){
    return;
  }
  do{
    e = p->aStack[--p->n];
    if( e==eEnd || (aMarkup[e].iType & MUTYPE_Nested)!=0 ){
      blob_appendf(pBlob, "</%s>", aMarkup[e].zName);
    }
  }while( e!=eEnd && p->n>0 );
}

/*
** Return a nonce to indicate that safe_html() can allow code through
** without censoring.
**
** When safe_html() is asked to sanitize some HTML, it will ignore
** any text in between two consecutive instances of the nonce.  The
** nonce itself is an HTML comment so it is harmless to keep the
** nonce in the middle of the HTML stream.  A different nonce is
** choosen each time Fossil is run, using a lot of randomness, so
** an attacker will be unable to guess the nonce in advance.
**
** The original use-case for this mechanism is to allow Pikchr-generated
** SVG in the middle of HTML generated from Markdown.  The Markdown
** output will normally be processed by safe_html() to prevent accidental
** or malicious introduction of harmful HTML (ex: <script>) in the
** output stream.  The safe_html() only lets through HTML elements
** that are on its allow-list and SVG is not on that list.  Hence, in order
** to allow the Pikchr-generated SVG through, it must be surrounded by
** the nonce.
*/
const char *safe_html_nonce(int bGenerate){
  static char *zNonce = 0;
  if( zNonce==0 && bGenerate ){
    zNonce = db_text(0, "SELECT '<!--'||hex(randomblob(32))||'-->';");
  }
  return zNonce;
}
#define SAFE_NONCE_SIZE (4+64+3)

/*
** Append a safe translation of HTML text to a Blob object.
**
** Restriction: The input to this routine must be writable.
*  Temporary changes may be made to the input, but the input is restored
** to its original state prior to returning.  If zHtml[nHtml] is not a
** zero character, then a zero might be written in that position
** temporarily, but that slot will also be restored before this routine
** returns.
*/
static void safe_html_append(Blob *pBlob, char *zHtml, int nHtml){
  char cLast;
  int i, j, n;
  HtmlTagStack s;
  ParsedMarkup markup;
  const char *zNonce;
  char *z;

  if( nHtml<=0 ) return;
  cLast = zHtml[nHtml];
  zHtml[nHtml] = 0;
  html_tagstack_init(&s);

  i = 0;
  while( i<nHtml ){
    if( zHtml[i]=='<' ){
      j = i;
    }else{
      z = strchr(zHtml+i, '<');
      if( z==0 ){
        blob_append(pBlob, zHtml+i, nHtml-i);
        break;
      }
      j = (int)(z - zHtml);
      blob_append(pBlob, zHtml+i, j-i);
    }
    if( zHtml[j+1]=='!'
     && j+2*SAFE_NONCE_SIZE<nHtml
     && (zNonce = safe_html_nonce(0))!=0
     && strncmp(zHtml+j,zNonce,SAFE_NONCE_SIZE)==0
     && (z = strstr(zHtml+j+SAFE_NONCE_SIZE,zNonce))!=0
    ){
      i = (int)(z - zHtml) + SAFE_NONCE_SIZE;
      blob_append(pBlob, zHtml+j, i-j);
      continue;
    }
    n = html_tag_length(zHtml+j);
    if( n==0 ){
      blob_append(pBlob, "&lt;", 4);
      i = j+1;
      continue;
    }else{
      i = j + n;
    }
    parseMarkup(&markup, zHtml+j);
    if( markup.iCode==MARKUP_INVALID ){
      unparseMarkup(&markup);
      blob_appendf(pBlob, "<span class='error'>&lt;%.*s&gt;</span>",
                   n-2, zHtml+j+1);
      continue;
    }
    if( (markup.iType & MUTYPE_Nested)==0 || markup.iCode==MARKUP_P ){
      renderMarkup(pBlob, &markup);
    }else{
      if( markup.endTag ){
        html_tagstack_pop(&s, pBlob, markup.iCode);
      }else{
        renderMarkup(pBlob, &markup);
        html_tagstack_push(&s, markup.iCode);
      }
    }
    unparseMarkup(&markup);
  }
  html_tagstack_pop(&s, pBlob, 0);
  html_tagstack_clear(&s);
  zHtml[nHtml] = cLast;
}

/*
** This local variable is true if the safe_html() function is enabled.
** In other words, this is true if the output of Markdown should be
** restricted to use only "safe" HTML.
*/
static int safeHtmlEnable = 1;


#if INTERFACE
/*
** Allowed values for the eTrust parameter to safe_html_context().
*/
#define DOCSRC_FILE       1     /* Document is a checked-in file */
#define DOCSRC_FORUM      2     /* Document is a forum post */
#define DOCSRC_TICKET     3     /* Document is a ticket comment */
#define DOCSRC_WIKI       4     /* Document is a wiki page */
#define DOCSRC_TRUSTED    5     /* safe_html() is always a no-op */
#define DOCSRC_UNTRUSTED  6     /* safe_html() is always enabled */
#endif /* INTERFACE */


/*
** Specify the context in which a markdown document with potentially
** unsafe HTML will be rendered.
*/
void safe_html_context(int eTrust){
  static const char *zSafeHtmlSetting = 0;
  char cPerm = 0;
  if( eTrust==DOCSRC_TRUSTED ){
    safeHtmlEnable = 0;
    return;
  }
  if( eTrust==DOCSRC_UNTRUSTED ){
    safeHtmlEnable = 1;
    return;
  }
  if( zSafeHtmlSetting==0 ){
    zSafeHtmlSetting = db_get("safe-html", "");
  }
  switch( eTrust ){
    case DOCSRC_FILE:   cPerm = 'b';  break;
    case DOCSRC_FORUM:  cPerm = 'f';  break;
    case DOCSRC_TICKET: cPerm = 't';  break;
    case DOCSRC_WIKI:   cPerm = 'w';  break;
  }
  safeHtmlEnable = (strchr(zSafeHtmlSetting,cPerm)==0);
}

/*
** SETTING: safe-html        width=8
** This setting controls whether or not unsafe HTML elements
** (such as SCRIPT or STYLE tags) are allowed in Markdown-formatted
** documents.  Unsafe HTML is disabled by default.  If this setting
** exists and is a string, then letters in that string can enable
** unsafe HTML in various contexts:
**
**    - b         Unsafe HTML allowed in embedded documentation
**    - f         Unsafe HTML allowed in forum posts
**    - t         Unsafe HTML allowed in tickets
**    - w         Unsafe HTML allowed on wiki pages
*/
/*
** The input blob contains HTML.  If safe-html is enabled, then
** convert the input into "safe HTML".  The following modifications
** are made:
**
**    1.  Remove any elements that are not on the AllowedMarkup list.
**        (ex: <script>, <form>, etc.)
**
**    2.  Remove any attributes that are not on the AllowedMarkup list.
**        (ex: onload=, etc.)
**
**    3.  Omit any surplus close-tags.  This prevents the script from
**        terminating an <div> or similar in the outer context.
**
**    4.  Insert additional close-tags as necessary so that any
**        tag in the input that needs a close-tag has one.  This
**        prevents tags in the embedded script from affecting the
**        display of content that follows this script in the enclosing
**        context.
**
** These modifications are intended to make the generated HTML safe
** to be embedded in a larger HTML document, such that the embedded
** HTML has no influence on the formatting and operation of the
** larger document.
**
** If safe-html is disabled, then this routine is a no-op.
*/
void safe_html(Blob *in){
  Blob out;      /* Holding area for the revised text during construction */
  char *z;       /* Original input text */
  int n;         /* Number of bytes in the original input text */
  int k;

  if( safeHtmlEnable==0 ) return;
  z = blob_str(in);
  n = blob_size(in);
  blob_init(&out, 0, 0);
  while( fossil_isspace(z[0]) ){ z++; n--; }
  for(k=n-1; k>5 && fossil_isspace(z[k]); k--){}

  if( fossil_strnicmp(z, "<div",4)==0 && !fossil_isalpha(z[4])
   && fossil_strnicmp(z+k-5, "</div>",6)==0
  ){
    /* The input contains an outer <div>...</div>.  Preserve the
    ** full scope of that <div>. */
    int m = html_tag_length(z);
    k -= 5;
    blob_append(&out, z, m);
    safe_html_append(&out, z+m, k-m);
    blob_append(&out, z+k, n-k);
  }else{
    safe_html_append(&out, z, n);
  }
  blob_reset(in);
  *in = out;
}

/*
** COMMAND: test-safe-html
**
** Usage: %fossil test-safe-html FILE ...
**
** Read files named on the command-line.  Send the text of each file
** through safe_html_append() and then write the result on
** standard output.
*/
void test_safe_html_cmd(void){
  int i;
  Blob x;
  for(i=2; i<g.argc; i++){
    char *z;
    int n;
    blob_read_from_file(&x, g.argv[i], ExtFILE);
    blob_terminate(&x);
    safe_html(&x);
    z = blob_str(&x);
    n = blob_size(&x);
    while( n>0 && (z[n-1]=='\n' || z[n-1]=='\r') ) n--;
    fossil_print("%.*s\n", n, z);
    blob_reset(&x);
  }
}
