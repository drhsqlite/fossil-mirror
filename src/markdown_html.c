/*
** Copyright (c) 2012 D. Richard Hipp
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
** This file contains callbacks for the markdown parser that generate
** XHTML output.
*/

#include "config.h"
#include "markdown_html.h"

#if INTERFACE

void markdown_to_html(
  struct Blob *input_markdown,
  struct Blob *output_title,
  struct Blob *output_body);

#endif /* INTERFACE */

/*
** Each heading is recorded as an instance of the following
** structure, in its own separate memory allocation.
*/
typedef struct MarkdownHeading MarkdownHeading;
struct MarkdownHeading {
  MarkdownHeading *pPrev, *pNext;  /* List of them all */
  char *zTitle;                    /* Text as displayed */
  char *zTag;                      /* Pandoc-style tag */
  int iLevel;                      /* Level number for this entry */
  int nth;                         /* This is the nth with the same tag */
};

/*
** An instance of the following structure is passed through the
** "opaque" pointer.
*/
typedef struct MarkdownToHtml MarkdownToHtml;
struct MarkdownToHtml {
  Blob *output_title;                /* Store the title here */
  MarkdownHeading *pFirst, *pLast;   /* List of all headings */
  int iToc;         /* Where to insert table-of-contents */
  int mxToc;        /* Maximum table-of-content level */
  int mnLevel;      /* Minimum level seen over all headings */
  int iHdngNums;    /* True to automatically number headings */
  int aNum[6];      /* Most recent number at each level */
};

/*
** Add a new heading to the heading list.  This involves generating
** a Pandoc-compatible identifier based on the heading text.
*/
static void html_new_heading(MarkdownToHtml *pCtx, Blob *text, int iLevel){
  MarkdownHeading *pNew, *pSearch;
  int nText = blob_size(text);
  size_t n = sizeof(*pNew) + nText*2 + 10;
  const char *zText = blob_buffer(text);
  char *zTag;
  int i, j;
  int seenChar = 0;

  pNew = fossil_malloc( n );
  memset(pNew, 0, n);
  if( pCtx->pLast ){
    pCtx->pLast->pNext = pNew;
    if( pCtx->mnLevel>iLevel ) pCtx->mnLevel = iLevel;
  }else{
    pCtx->mnLevel = iLevel;
  }
  pNew->pPrev = pCtx->pLast;
  pCtx->pLast = pNew;
  if( pCtx->pFirst==0 ) pCtx->pFirst = pNew;
  pNew->zTitle = (char*)&pNew[1];
  memcpy(pNew->zTitle, zText, nText);
  pNew->zTitle[nText] = 0;
  pNew->zTag = pNew->zTitle + nText + 1;
  pNew->iLevel = iLevel;
  pNew->nth = 0;

  /* Generate an identifier.  The identifer name is approximately the
  ** same as a Pandoc identifier.
  **
  **  *  Skip all text up to the first letter.
  **  *  Remove all text past the last letter.
  **  *  Remove HTML markup and entities.
  **  *  Replace all whitespace sequences with a single "-"
  **  *  Remove all characters other than alphanumeric, "_", "-", and ".".
  **  *  Convert all alphabetics to lower case.
  **  *  If nothing remains, use "section" as the identifier.
  */
  while( nText>0 && !fossil_isalpha(zText[nText-1]) ){ nText--; }
  memcpy(pNew->zTag, zText, nText);
  pNew->zTag[nText] = 0;
  zTag = pNew->zTag;
  for(i=j=0; zTag[i]; i++){
    if( fossil_isupper(zTag[i]) ){
      if( !seenChar ){ j = 0; seenChar = 1; }
      zTag[j++] = fossil_tolower(zTag[i]);
      continue;
    }
    if( fossil_islower(zTag[i]) ){
      if( !seenChar ){ j = 0; seenChar = 1; }
      zTag[j++] = zTag[i];
      continue;
    }
    if( zTag[i]=='<' ){
      i += html_tag_length(zTag+i) - 1;
      continue;
    }
    if( zTag[i]=='&' ){
      while( zTag[i] && zTag[i]!=';' ){ i++; }
      if( zTag[i]==0 ) break;
      continue;
    }
    if( fossil_isspace(zTag[i]) ){
      zTag[j++] = '-';
      while( fossil_isspace(zTag[i+1]) ){ i++; }
      continue;
    }
    if( !fossil_isalnum(zTag[i]) && zTag[i]!='.' && zTag[i]!='_' ){
      zTag[j++] = '-';
    }else{
      zTag[j++] = zTag[i];
    }
  }
  if( j==0 || !seenChar ){
    memcpy(zTag, "section", 7);
    j = 7;
  }
  while( j>0 && !fossil_isalpha(zTag[j-1]) ){ j--; }
  zTag[j] = 0;

  /* Search for duplicate identifiers and disambiguate */
  pNew->nth = 0;
  for(pSearch=pNew->pPrev; pSearch; pSearch=pSearch->pPrev){
    if( strcmp(pSearch->zTag,zTag)==0 ){
      pNew->nth = pSearch->nth+1;
    }
  }
}   


/* INTER_BLOCK -- skip a line between block level elements */
#define INTER_BLOCK(ob) \
  do { if( blob_size(ob)>0 ) blob_append_char(ob, '\n'); } while (0)

/* BLOB_APPEND_LITERAL -- append a string literal to a blob */
#define BLOB_APPEND_LITERAL(blob, literal) \
  blob_append((blob), "" literal, (sizeof literal)-1)
  /*
   * The empty string in the second argument leads to a syntax error
   * when the macro is not used with a string literal. Unfortunately
   * the error is not overly explicit.
   */

/* BLOB_APPEND_BLOB -- append blob contents to another */
#define BLOB_APPEND_BLOB(dest, src) \
  blob_append((dest), blob_buffer(src), blob_size(src))


/* HTML escapes
**
** html_escape() converts < to &lt;, > to &gt;, and & to &amp;.
** html_quote() goes further and converts " into &quot; and ' in &#39;.
*/
static void html_quote(struct Blob *ob, const char *data, size_t size){
  size_t beg = 0, i = 0;
  while( i<size ){
    beg = i;
    while( i<size
     && data[i]!='<'
     && data[i]!='>'
     && data[i]!='"'
     && data[i]!='&'
     && data[i]!='\''
    ){
      i++;
    }
    blob_append(ob, data+beg, i-beg);
    while( i<size ){
      if( data[i]=='<' ){
        BLOB_APPEND_LITERAL(ob, "&lt;");
      }else if( data[i]=='>' ){
        BLOB_APPEND_LITERAL(ob, "&gt;");
      }else if( data[i]=='&' ){
        BLOB_APPEND_LITERAL(ob, "&amp;");
      }else if( data[i]=='"' ){
        BLOB_APPEND_LITERAL(ob, "&quot;");
      }else if( data[i]=='\'' ){
        BLOB_APPEND_LITERAL(ob, "&#39;");
      }else{
        break;
      }
      i++;
    }
  }
}
static void html_escape(struct Blob *ob, const char *data, size_t size){
  size_t beg = 0, i = 0;
  while( i<size ){
    beg = i;
    while( i<size
     && data[i]!='<'
     && data[i]!='>'
     && data[i]!='&'
    ){
      i++;
    }
    blob_append(ob, data+beg, i-beg);
    while( i<size ){
      if( data[i]=='<' ){
        BLOB_APPEND_LITERAL(ob, "&lt;");
      }else if( data[i]=='>' ){
        BLOB_APPEND_LITERAL(ob, "&gt;");
      }else if( data[i]=='&' ){
        BLOB_APPEND_LITERAL(ob, "&amp;");
      }else{
        break;
      }
      i++;
    }
  }
}


/* HTML block tags */

/* Size of the prolog: "<div class='markdown'>\n" */
#define PROLOG_SIZE 23

static void html_prolog(struct Blob *ob, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITERAL(ob, "<div class=\"markdown\">\n");
  assert( blob_size(ob)==PROLOG_SIZE );
}

static void html_epilog(struct Blob *ob, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITERAL(ob, "</div>\n");
}

/*
** If text is an HTML control comment, then deal with it and return true.
** Otherwise just return false without making any changes.
**
** We are looking for comments of the following form:
**
**     <!--markdown: toc=N -->
**     <!--markdown: paragraph-numbers=on -->
**     <!--markdown: paragraph-numbers=N -->
**
** In the paragraph-numbers=N form with N>1, N-th level headings are
** numbered like top-levels.  N+1-th level headings are like 2nd levels.
** and so forth.
**
** In the toc=N form, a table of contents is generated for all headings
** less than or equal to leve N.
*/
static int html_control_comment(Blob *ob, Blob *text, void *opaque){
  Blob token, arg;
  MarkdownToHtml *pCtx;
  if( blob_size(text)<20 ) return 0;
  if( strncmp(blob_buffer(text),"<!--markdown:",13)!=0 ) return 0;
  pCtx = (MarkdownToHtml*)opaque;
  blob_seek(text, 13, BLOB_SEEK_SET);
  blob_init(&token, 0, 0);
  blob_init(&arg, 0, 0);
  while( blob_argument_token(text, &token, 0) ){
    if( blob_eq_str(&token, "toc", 3) && blob_argument_token(text, &arg, 1) ){
      pCtx->iToc = blob_size(ob);
      pCtx->mxToc = atoi(blob_str(&arg));
      blob_reset(&arg);
    }else
    if( blob_eq_str(&token,"paragraph-numbers",-1)
     && blob_argument_token(text,&arg,1)
    ){
      char *zArg = blob_str(&arg);   
      pCtx->iHdngNums = fossil_isdigit(zArg[0]) ? atoi(zArg) : is_truth(zArg);
      blob_reset(&arg);
    }else
    if( !blob_eq_str(&token,"-->",3) ){
      blob_appendf(ob, "<!--markdown: unknown-tag=\"%h\" -->",
                   blob_str(&token));
    }
    blob_reset(&token); 
  } 
  return 1;
}

static void html_blockhtml(struct Blob *ob, struct Blob *text, void *opaque){
  char *data = blob_buffer(text);
  size_t size = blob_size(text);
  Blob *title = ((MarkdownToHtml*)opaque)->output_title;
  if( html_control_comment(ob,text,opaque) ) return;
  while( size>0 && fossil_isspace(data[0]) ){ data++; size--; }
  while( size>0 && fossil_isspace(data[size-1]) ){ size--; }
  /* If the first raw block is an <h1> element, then use it as the title. */
  if( blob_size(ob)<=PROLOG_SIZE
   && size>9
   && title!=0
   && sqlite3_strnicmp("<h1",data,3)==0
   && sqlite3_strnicmp("</h1>", &data[size-5],5)==0
  ){
    int nTag = html_tag_length(data);
    blob_append(title, data+nTag, size - nTag - 5);
    return;
  }
  INTER_BLOCK(ob);
  blob_append(ob, data, size);
  BLOB_APPEND_LITERAL(ob, "\n");
}

static void html_blockcode(struct Blob *ob, struct Blob *text, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITERAL(ob, "<pre><code>");
  html_escape(ob, blob_buffer(text), blob_size(text));
  BLOB_APPEND_LITERAL(ob, "</code></pre>\n");
}

static void html_blockquote(struct Blob *ob, struct Blob *text, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITERAL(ob, "<blockquote>\n");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITERAL(ob, "</blockquote>\n");
}

static void html_header(
  struct Blob *ob,
  struct Blob *text,
  int level,
  void *opaque
){
  MarkdownToHtml *pCtx = (MarkdownToHtml*)opaque;
  MarkdownHeading *pHdng;
  struct Blob *title = pCtx->output_title;
  /* The first header at the beginning of a text is considered as
   * a title and not output. */
  if( blob_size(ob)<=PROLOG_SIZE && title!=0 && blob_size(title)==0 ){
    BLOB_APPEND_BLOB(title, text);
    return;
  }
  INTER_BLOCK(ob);
  html_new_heading(pCtx, text, level);
  pHdng = pCtx->pLast;
  if( pHdng->nth ){
    blob_appendf(ob, "<h%d id='%h-%d'>", level, pHdng->zTag, pHdng->nth);
  }else{
    blob_appendf(ob, "<h%d id='%h'>", level, pHdng->zTag);
  }
  if( pCtx->iHdngNums && level>=pCtx->iHdngNums ){
    int i;
    for(i=pCtx->iHdngNums-1; i<level-1; i++){
      blob_appendf(ob,"%d.",pCtx->aNum[i]);
    }
    blob_appendf(ob,"%d", ++pCtx->aNum[i]);
    if( i==pCtx->iHdngNums-1 ) blob_append(ob, ".0", 2);
    blob_append(ob, " ", 1);
    for(i++; i<6; i++) pCtx->aNum[i] = 0;
  }
  BLOB_APPEND_BLOB(ob, text);
  blob_appendf(ob, "</h%d>", level);
}

static void html_hrule(struct Blob *ob, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITERAL(ob, "<hr />\n");
}


static void html_list(
  struct Blob *ob,
  struct Blob *text,
  int flags,
  void *opaque
){
  char ol[] = "ol";
  char ul[] = "ul";
  char *tag = (flags & MKD_LIST_ORDERED) ? ol : ul;
  INTER_BLOCK(ob);
  blob_appendf(ob, "<%s>\n", tag);
  BLOB_APPEND_BLOB(ob, text);
  blob_appendf(ob, "</%s>\n", tag);
}

static void html_list_item(
  struct Blob *ob,
  struct Blob *text,
  int flags,
  void *opaque
){
  char *text_data = blob_buffer(text);
  size_t text_size = blob_size(text);
  while( text_size>0 && text_data[text_size-1]=='\n' ) text_size--;
  BLOB_APPEND_LITERAL(ob, "<li>");
  blob_append(ob, text_data, text_size);
  BLOB_APPEND_LITERAL(ob, "</li>\n");
}

static void html_paragraph(struct Blob *ob, struct Blob *text, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITERAL(ob, "<p>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITERAL(ob, "</p>\n");
}


static void html_table(
  struct Blob *ob,
  struct Blob *head_row,
  struct Blob *rows,
  void *opaque
){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITERAL(ob, "<table>\n");
  if( head_row && blob_size(head_row)>0 ){
    BLOB_APPEND_LITERAL(ob, "<thead>\n");
    BLOB_APPEND_BLOB(ob, head_row);
    BLOB_APPEND_LITERAL(ob, "</thead>\n<tbody>\n");
  }
  if( rows ){
    BLOB_APPEND_BLOB(ob, rows);
  }
  if( head_row && blob_size(head_row)>0 ){
    BLOB_APPEND_LITERAL(ob, "</tbody>\n");
  }
  BLOB_APPEND_LITERAL(ob, "</table>\n");
}

static void html_table_cell(
  struct Blob *ob,
  struct Blob *text,
  int flags,
  void *opaque
){
  if( flags & MKD_CELL_HEAD ){
    BLOB_APPEND_LITERAL(ob, "    <th");
  }else{
    BLOB_APPEND_LITERAL(ob, "    <td");
  }
  switch( flags & MKD_CELL_ALIGN_MASK ){
    case MKD_CELL_ALIGN_LEFT: {
      BLOB_APPEND_LITERAL(ob, " align=\"left\"");
      break;
    }
    case MKD_CELL_ALIGN_RIGHT: {
      BLOB_APPEND_LITERAL(ob, " align=\"right\"");
      break;
    }
    case MKD_CELL_ALIGN_CENTER: {
      BLOB_APPEND_LITERAL(ob, " align=\"center\"");
      break;
    }
  }
  BLOB_APPEND_LITERAL(ob, ">");
  BLOB_APPEND_BLOB(ob, text);
  if( flags & MKD_CELL_HEAD ){
    BLOB_APPEND_LITERAL(ob, "</th>\n");
  }else{
    BLOB_APPEND_LITERAL(ob, "</td>\n");
  }
}

static void html_table_row(
  struct Blob *ob,
  struct Blob *cells,
  int flags,
  void *opaque
){
  BLOB_APPEND_LITERAL(ob, "  <tr>\n");
  BLOB_APPEND_BLOB(ob, cells);
  BLOB_APPEND_LITERAL(ob, "  </tr>\n");
}

/* HTML span tags */
static int html_raw_html_tag(struct Blob *ob, struct Blob *text, void *opaque){
  if( html_control_comment(ob,text,opaque) ){
    /* No-op */
  }else{
    /* Everything else is passed through without change */
    blob_append(ob, blob_buffer(text), blob_size(text));
  }
  return 1;
}

static int html_autolink(
  struct Blob *ob,
  struct Blob *link,
  enum mkd_autolink type,
  void *opaque
){
  if( !link || blob_size(link)<=0 ) return 0;
  BLOB_APPEND_LITERAL(ob, "<a href=\"");
  if( type==MKDA_IMPLICIT_EMAIL ) BLOB_APPEND_LITERAL(ob, "mailto:");
  html_quote(ob, blob_buffer(link), blob_size(link));
  BLOB_APPEND_LITERAL(ob, "\">");
  if( type==MKDA_EXPLICIT_EMAIL && blob_size(link)>7 ){
    /* remove "mailto:" from displayed text */
    html_escape(ob, blob_buffer(link)+7, blob_size(link)-7);
  }else{
    html_escape(ob, blob_buffer(link), blob_size(link));
  }
  BLOB_APPEND_LITERAL(ob, "</a>");
  return 1;
}

/*
** The nSrc bytes at zSrc[] are Pikchr input text (allegedly).  Process that
** text and insert the result in place of the original.
*/
void pikchr_to_html(
  Blob *ob,                     /* Write the generated SVG here */
  const char *zSrc, int nSrc,   /* The Pikchr source text */
  const char *zArg, int nArg    /* Addition arguments */
){
  int w = 0, h = 0;
  char *zIn = fossil_strndup(zSrc, nSrc);
  char *zOut = pikchr(zIn, "pikchr", PIKCHR_INCLUDE_SOURCE, &w, &h);
  fossil_free(zIn);
  if( w>0 && h>0 ){
    const char *zNonce = safe_html_nonce(1);
    Blob css;
    blob_init(&css,0,0);
    blob_appendf(&css,"max-width:%dpx;",w);
    blob_append(ob, zNonce, -1);
    blob_append_char(ob, '\n');
    while( nArg>0 ){
      int i;
      for(i=0; i<nArg && !fossil_isspace(zArg[i]); i++){}
      if( i==6 && strncmp(zArg, "center", 6)==0 ){
        blob_appendf(&css, "display:block;margin:auto;");
        break;
      }else if( i==6 && strncmp(zArg, "indent", 6)==0 ){
        blob_appendf(&css, "margin-left:4em;");
        break;
      }else if( i==10 && strncmp(zArg, "float-left", 10)==0 ){
        blob_appendf(&css, "float:left;padding=4em;");
        break;
      }else if( i==11 && strncmp(zArg, "float-right", 11)==0 ){
        blob_appendf(&css, "float:right;padding=4em;");
        break;
      }
      while( i<nArg && fossil_isspace(zArg[i]) ){ i++; }
      zArg += i;
      nArg -= i;
    }
    blob_appendf(ob, "<div style='%s'>\n", blob_str(&css));
    blob_append(ob, zOut, -1);
    blob_appendf(ob, "</div>\n");
    blob_reset(&css);
    blob_appendf(ob, "%s\n", zNonce);
  }else{
    blob_appendf(ob, "<pre>\n%s\n</pre>\n", zOut);
  }
  free(zOut);
}


/* Invoked for `...` blocks where there are nSep grave accents in a
** row that serve as the delimiter.  According to CommonMark:
**
**   *  https://spec.commonmark.org/0.29/#fenced-code-blocks
**   *  https://spec.commonmark.org/0.29/#code-spans
**
** If nSep is 1 or 2, then this is a code-span which is inline.
** If nSep is 3 or more, then this is a fenced code block
*/
static int html_codespan(
  struct Blob *ob,    /* Write the output here */
  struct Blob *text,  /* The stuff in between the code span marks */
  int nSep,           /* Number of grave accents marks as delimiters */
  void *opaque
){
  if( text==0 ){
    /* no-op */
  }else if( nSep<=2 ){
    /* One or two graves: an in-line code span */
    BLOB_APPEND_LITERAL(ob, "<code>");
    html_escape(ob, blob_buffer(text), blob_size(text));
    BLOB_APPEND_LITERAL(ob, "</code>");
  }else{
    /* Three or more graves: a fenced code block */
    int n = blob_size(text);
    const char *z = blob_buffer(text);
    int i;
    for(i=0; i<n && z[i]!='\n'; i++){}
    if( i>=n ){
      blob_appendf(ob, "<pre><code>%#h</code></pre>", n, z);
    }else{
      int k, j;
      i++;
      for(k=0; k<i && fossil_isspace(z[k]); k++){}
      if( k==i ){
        blob_appendf(ob, "<pre><code>%#h</code></pre>", n-i, z+i);
      }else{
        for(j=k+1; j<i && !fossil_isspace(z[j]); j++){}
        if( j-k==6 && strncmp(z+k,"pikchr",6)==0 ){
          while( j<i && fossil_isspace(z[j]) ){ j++; }
          pikchr_to_html(ob, z+i, n-i, z+j, i-j);
        }else{
          blob_appendf(ob, "<pre><code class='language-%#h'>%#h</code></pre>",
                            j-k, z+k, n-i, z+i);
        }
      }
    }
  }
  return 1;
}

static int html_double_emphasis(
  struct Blob *ob,
  struct Blob *text,
  char c,
  void *opaque
){
  BLOB_APPEND_LITERAL(ob, "<strong>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITERAL(ob, "</strong>");
  return 1;
}

static int html_emphasis(
  struct Blob *ob,
  struct Blob *text,
  char c,
  void *opaque
){
  BLOB_APPEND_LITERAL(ob, "<em>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITERAL(ob, "</em>");
  return 1;
}

static int html_image(
  struct Blob *ob,
  struct Blob *link,
  struct Blob *title,
  struct Blob *alt,
  void *opaque
){
  BLOB_APPEND_LITERAL(ob, "<img src=\"");
  html_quote(ob, blob_buffer(link), blob_size(link));
  BLOB_APPEND_LITERAL(ob, "\" alt=\"");
  html_quote(ob, blob_buffer(alt), blob_size(alt));
  if( title && blob_size(title)>0 ){
    BLOB_APPEND_LITERAL(ob, "\" title=\"");
    html_quote(ob, blob_buffer(title), blob_size(title));
  }
  BLOB_APPEND_LITERAL(ob, "\" />");
  return 1;
}

static int html_linebreak(struct Blob *ob, void *opaque){
  BLOB_APPEND_LITERAL(ob, "<br />\n");
  return 1;
}

static int html_link(
  struct Blob *ob,
  struct Blob *link,
  struct Blob *title,
  struct Blob *content,
  void *opaque
){
  char *zLink = blob_buffer(link);
  char *zTitle = title!=0 && blob_size(title)>0 ? blob_str(title) : 0;
  char zClose[20];

  if( zLink==0 || zLink[0]==0 ){
    zClose[0] = 0;
  }else{  
    static const int flags = 
       WIKI_NOBADLINKS |
       WIKI_MARKDOWNLINKS
    ;
    wiki_resolve_hyperlink(ob, flags, zLink, zClose, sizeof(zClose), 0, zTitle);
  }
  if( blob_size(content)==0 ){
    if( link ) BLOB_APPEND_BLOB(ob, link);
  }else{
    BLOB_APPEND_BLOB(ob, content);
  }
  blob_append(ob, zClose, -1);
  return 1;
}

static int html_triple_emphasis(
  struct Blob *ob,
  struct Blob *text,
  char c,
  void *opaque
){
  BLOB_APPEND_LITERAL(ob, "<strong><em>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITERAL(ob, "</em></strong>");
  return 1;
}


static void html_normal_text(struct Blob *ob, struct Blob *text, void *opaque){
  html_escape(ob, blob_buffer(text), blob_size(text));
}

/*
** Insert a table of contents into the body of the document.
**
** The pCtx provides the information needed to do this:
**
**    pCtx->iToc              Offset into pOut of where to insert the TOC
**    pCtx->mxToc             Maximum depth of the TOC
**    pCtx->pFirst            List of paragraphs to form the TOC
*/
static void html_insert_toc(MarkdownToHtml *pCtx, Blob *pOut){
  Blob new;
  MarkdownHeading *pX;
  int iLevel = pCtx->mnLevel-1;
  int iBase = iLevel;
  blob_init(&new, 0, 0);
  blob_append(&new, blob_buffer(pOut), pCtx->iToc);
  blob_append(&new, "<div class='markdown-toc'>\n", -1);
  for(pX=pCtx->pFirst; pX; pX=pX->pNext){
    if( pX->iLevel>pCtx->mxToc ) continue;
    while( iLevel<pX->iLevel ){
      iLevel++;
      blob_appendf(&new, "<ul class='markdown-toc%d markdown-toc'>\n",
                         iLevel - iBase);
    }
    while( iLevel>pX->iLevel ){
      iLevel--;
      blob_appendf(&new, "</ul>\n");
    }
    blob_appendf(&new,"<li><a href='#%h'>", pX->zTag);
    html_to_plaintext(pX->zTitle, &new);
    blob_appendf(&new,"</a></li>\n");
  }
  while( iLevel>iBase ){
    iLevel--;
    blob_appendf(&new, "</ul>\n");
  }
  blob_appendf(&new, "</div>\n");
  blob_append(&new, blob_buffer(pOut)+pCtx->iToc,
                    blob_size(pOut)-pCtx->iToc);
  blob_reset(pOut);
  *pOut = new;
}

/*
** Convert markdown into HTML.
**
** The document title is placed in output_title if not NULL.  Or if
** output_title is NULL, the document title appears in the body.
*/
void markdown_to_html(
  struct Blob *input_markdown,   /* Markdown content to be rendered */
  struct Blob *output_title,     /* Put title here.  May be NULL */
  struct Blob *output_body       /* Put document body here. */
){
  struct mkd_renderer html_renderer = {
    /* prolog and epilog */
    html_prolog,
    html_epilog,

    /* block level elements */
    html_blockcode,
    html_blockquote,
    html_blockhtml,
    html_header,
    html_hrule,
    html_list,
    html_list_item,
    html_paragraph,
    html_table,
    html_table_cell,
    html_table_row,

    /* span level elements */
    html_autolink,
    html_codespan,
    html_double_emphasis,
    html_emphasis,
    html_image,
    html_linebreak,
    html_link,
    html_raw_html_tag,
    html_triple_emphasis,

    /* low level elements */
    0,    /* entity */
    html_normal_text,

    /* misc. parameters */
    "*_", /* emph_chars */
    0     /* opaque */
  };
  MarkdownToHtml context;
  MarkdownHeading *pHdng, *pNextHdng;

  memset(&context, 0, sizeof(context));
  context.output_title = output_title;
  html_renderer.opaque = &context;
  if( output_title ) blob_reset(output_title);
  blob_reset(output_body);
  markdown(output_body, input_markdown, &html_renderer);
  if( context.mxToc>0 ) html_insert_toc(&context, output_body);
  for(pHdng=context.pFirst; pHdng; pHdng=pNextHdng){
    pNextHdng = pHdng->pNext;
    fossil_free(pHdng);
  }
}
