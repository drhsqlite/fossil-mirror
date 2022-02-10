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

typedef union { uint64_t u; char c[8]; unsigned char b[8]; } bitfield64_t;

/*
** An instance of the following structure is passed through the
** "opaque" pointer.
*/
typedef struct MarkdownToHtml MarkdownToHtml;
struct MarkdownToHtml {
  Blob *output_title;     /* Store the title here */
  bitfield64_t unique;    /* Enables construction of unique #id elements */
};


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

/* Converts an integer to a null-terminated base26 representation
 * Return empty string if that integer is negative.   */
static bitfield64_t to_base26(int i, int uppercase){
  bitfield64_t x;
  int j;
  x.u = 0;
  if( i >= 0 ){
    for(j=7; j >= 0; j--){
      x.b[j] = (unsigned char)(uppercase?'A':'a') + i%26;
      if( (i /= 26) == 0 ) break;
    }
    assert( j > 0 );    /* because 2^32 < 26^7 */
    for(i=0; i<8-j; i++)  x.b[i] = x.b[i+j];
    for(   ; i<8  ; i++)  x.b[i] = 0;
  }
  assert( x.c[7] == 0 );
  return x;
}

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

static void html_blockhtml(struct Blob *ob, struct Blob *text, void *opaque){
  char *data = blob_buffer(text);
  size_t size = blob_size(text);
  Blob *title = ((MarkdownToHtml*)opaque)->output_title;
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
  struct Blob *title = ((MarkdownToHtml*)opaque)->output_title;
  /* The first header at the beginning of a text is considered as
   * a title and not output. */
  if( blob_size(ob)<=PROLOG_SIZE && title!=0 && blob_size(title)==0 ){
    BLOB_APPEND_BLOB(title, text);
    return;
  }
  INTER_BLOCK(ob);
  blob_appendf(ob, "<h%d>", level);
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

static int html_footnote_ref(
  struct Blob *ob, const struct Blob *span, int iMark, int locus, void *opaque
){
  const struct MarkdownToHtml* ctx = (struct MarkdownToHtml*)opaque;
  const bitfield64_t l = to_base26(locus-1,0);
  char pos[32];
  memset(pos,0,32);
  assert( locus > 0 );
  /* expect BUGs if the following yields compiler warnings */
  if( iMark > 0 ){      /* a regular reference to a footnote */

    sprintf(pos, "%s-%i-%s", ctx->unique.c, iMark, l.c);
    if(span && blob_size(span)) {
      BLOB_APPEND_LITERAL(ob,"<span class='notescope' id='noteref");
      blob_appendf(ob,"%s'>",pos);
      BLOB_APPEND_BLOB(ob, span);
      blob_trim(ob);
      BLOB_APPEND_LITERAL(ob,"<sup class='noteref'><a href='#footnote");
      blob_appendf(ob,"%s'>%i</a></sup></span>", pos, iMark);
    }else{
      blob_trim(ob);
      BLOB_APPEND_LITERAL(ob,"<sup class='noteref'><a href='#footnote");
      blob_appendf(ob,"%s' id='noteref%s'>%i</a></sup>",
                      pos,           pos,  iMark);
    }
  }else{              /* misreference */
    assert( iMark == -1 );

    sprintf(pos, "%s-%s", ctx->unique.c, l.c);
    if(span && blob_size(span)) {
      blob_appendf(ob, "<span class='notescope' id='misref%s'>", pos);
      BLOB_APPEND_BLOB(ob, span);
      blob_trim(ob);
      BLOB_APPEND_LITERAL(ob,
        "<sup class='noteref misref'><a href='#misreference");
      blob_appendf(ob, "%s'>misref</a></sup></span>", pos);
    }else{
      blob_trim(ob);
      BLOB_APPEND_LITERAL(ob,
        "<sup class='noteref misref'><a href='#misreference");
      blob_appendf(ob, "%s' id='misref%s'>", pos, pos);
      BLOB_APPEND_LITERAL(ob, "misref</a></sup>");
    }
  }
  return 1;
}

/* Render a single item of the footnotes list.
 * Each backref gets a unique id to enable dynamic styling. */
static void html_footnote_item(
  struct Blob *ob, const struct Blob *text, int iMark, int nUsed, void *opaque
){
  const char * const unique = ((struct MarkdownToHtml*)opaque)->unique.c;
  assert( nUsed >= 0 );
  /* expect BUGs if the following yields compiler warnings */

  if( iMark < 0 ){                     /* misreferences */
    assert( iMark == -1 );
    if( !nUsed ) return;
    BLOB_APPEND_LITERAL(ob,"<li class='fn-misreference'>"
                              "<sup class='fn-backrefs'>");
    if( nUsed == 1 ){
      blob_appendf(ob,"<a id='misreference%s-a' "
                      "href='#misref%s-a'>^</a>", unique, unique);
    }else{
      int i;
      blob_append_char(ob, '^');
      for(i=0; i<nUsed && i<26; i++){
        const int c = i + (unsigned)'a';
        blob_appendf(ob," <a id='misreference%s-%c' "
              "href='#misref%s-%c'>%c</a>", unique,c, unique,c, c);
      }
      if( i < nUsed ) BLOB_APPEND_LITERAL(ob," &hellip;");
    }
    BLOB_APPEND_LITERAL(ob,"</sup>\n<span>Misreference</span>");

  }else if( nUsed ){                   /* a regular footnote */
    char pos[24];
    const char *join = "";
    #define _joined_footnote_indicator "<ul class='fn-joined'>"
    #define _jfi_sz (sizeof(_joined_footnote_indicator)-1)
    assert( text );
    assert( blob_size(text) );
    if( blob_size(text)>=_jfi_sz &&
       !memcmp(blob_buffer(text),_joined_footnote_indicator,_jfi_sz)){
      join = "fn-joined ";
    }
    memset(pos,0,24);
    sprintf(pos, "%s-%i", unique, iMark);
    blob_appendf(ob, "<li id='footnote%s' class='%s", pos, join);

    if( nUsed == 1 ){
      BLOB_APPEND_LITERAL(ob, "fn-monoref'><sup class='fn-backrefs'>");
      blob_appendf(ob,"<a id='footnote%s-a' "
                       "href='#noteref%s-a'>^</a>", pos, pos);
    }else{
      int i;
      BLOB_APPEND_LITERAL(ob, "fn-polyref'><sup class='fn-backrefs'>^");
      for(i=0; i<nUsed && i<26; i++){
        const int c = i + (unsigned)'a';
        blob_appendf(ob," <a id='footnote%s-%c'"
                         " href='#noteref%s-%c'>%c</a>", pos,c, pos,c, c);
      }
      /* It's unlikely that so many backrefs will be usefull */
      /* but maybe for some machine generated documents... */
      for(; i<nUsed && i<676; i++){
        const bitfield64_t l = to_base26(i,0);
        blob_appendf(ob," <a id='footnote%s-%s'"
                         " href='#noteref%s-%s'>%s</a>",
                         pos,l.c, pos,l.c, l.c);
      }
      if( i < nUsed ) BLOB_APPEND_LITERAL(ob," &hellip;");
    }
    BLOB_APPEND_LITERAL(ob,"</sup>\n");
    if( join[0] ){
      BLOB_APPEND_LITERAL(ob,"<sup class='fn-joined'></sup><ul>");
      blob_append(ob,blob_buffer(text)+_jfi_sz,blob_size(text)-_jfi_sz);
    }else{
      BLOB_APPEND_BLOB(ob, text);
    }
    #undef _joined_footnote_indicator
    #undef _jfi_sz
  }else{
    /* a footnote was defined but wasn't used */
    /* make.footnote_item() invocations should pass args accordingly */
    const struct Blob * id = text-1;
    assert( text );
    assert( blob_size(text) );
    assert( blob_size(id) );
    BLOB_APPEND_LITERAL(ob,"<li class='fn-unreferenced'>\n[^&nbsp;<code>");
    html_escape(ob, blob_buffer(id), blob_size(id));
    BLOB_APPEND_LITERAL(ob, "</code>&nbsp;]<i></i>\n"
        "<pre><code class='language-markdown'>");
    html_escape(ob, blob_buffer(text), blob_size(text));
    BLOB_APPEND_LITERAL(ob,"</code></pre>");
  }
  BLOB_APPEND_LITERAL(ob, "\n</li>\n");
}
static void html_footnotes(
  struct Blob *ob, const struct Blob *items, void *opaque
){
  if( items && blob_size(items) ){
    BLOB_APPEND_LITERAL(ob,
      "\n<hr class='footnotes-separator'/>\n<ol class='footnotes'>\n");
    BLOB_APPEND_BLOB(ob, items);
    BLOB_APPEND_LITERAL(ob, "</ol>\n");
  }
}

/* HTML span tags */

static int html_raw_html_tag(struct Blob *ob, struct Blob *text, void *opaque){
  blob_append(ob, blob_buffer(text), blob_size(text));
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
  int pikFlags = PIKCHR_PROCESS_NONCE
    | PIKCHR_PROCESS_DIV
    | PIKCHR_PROCESS_SRC
    | PIKCHR_PROCESS_ERR_PRE;
  Blob bSrc = empty_blob;
  const char *zPikVar;
  double rPikVar;

  while( nArg>0 ){
    int i;
    for(i=0; i<nArg && !fossil_isspace(zArg[i]); i++){}
    if( i==6 && strncmp(zArg, "center", 6)==0 ){
      pikFlags |= PIKCHR_PROCESS_DIV_CENTER;
    }else if( i==6 && strncmp(zArg, "indent", 6)==0 ){
      pikFlags |= PIKCHR_PROCESS_DIV_INDENT;
    }else if( i==10 && strncmp(zArg, "float-left", 10)==0 ){
      pikFlags |= PIKCHR_PROCESS_DIV_FLOAT_LEFT;
    }else if( i==11 && strncmp(zArg, "float-right", 11)==0 ){
      pikFlags |= PIKCHR_PROCESS_DIV_FLOAT_RIGHT;
    }else if( i==6 && strncmp(zArg, "toggle", 6)==0 ){
      pikFlags |= PIKCHR_PROCESS_DIV_TOGGLE;
    }else if( i==6 && strncmp(zArg, "source", 6)==0 ){
      pikFlags |= PIKCHR_PROCESS_DIV_SOURCE;
    }else if( i==13 && strncmp(zArg, "source-inline", 13)==0 ){
      pikFlags |= PIKCHR_PROCESS_DIV_SOURCE_INLINE;
    }
    while( i<nArg && fossil_isspace(zArg[i]) ){ i++; }
    zArg += i;
    nArg -= i;
  }
  if( skin_detail_boolean("white-foreground") ){
    pikFlags |= 0x02;  /* PIKCHR_DARK_MODE */
  }
  zPikVar = skin_detail("pikchr-foreground");
  if( zPikVar && zPikVar[0] ){
    blob_appendf(&bSrc, "fgcolor = %s\n", zPikVar);
  }
  zPikVar = skin_detail("pikchr-background");
  if( zPikVar && zPikVar[0] ){
    blob_appendf(&bSrc, "bgcolor = %s\n", zPikVar);
  }
  zPikVar = skin_detail("pikchr-scale");
  if( zPikVar
   && (rPikVar = atof(zPikVar))>=0.1
   && rPikVar<10.0
  ){
    blob_appendf(&bSrc, "scale = %.13g\n", rPikVar);
  }
  zPikVar = skin_detail("pikchr-fontscale");
  if( zPikVar
   && (rPikVar = atof(zPikVar))>=0.1
   && rPikVar<10.0
  ){
    blob_appendf(&bSrc, "fontscale = %.13g\n", rPikVar);
  }
  blob_append(&bSrc, zSrc, nSrc)
    /*have to dup input to ensure a NUL-terminated source string */;
  pikchr_process(blob_str(&bSrc), pikFlags, 0, ob);
  blob_reset(&bSrc);
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
    html_footnotes,

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
    html_footnote_item,

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
    html_footnote_ref,

    /* low level elements */
    0,    /* entity */
    html_normal_text,

    /* misc. parameters */
    "*_", /* emph_chars */
    0     /* opaque */
  };
  static int invocation = -1; /* no marker for the first document */
  MarkdownToHtml context;
  memset(&context, 0, sizeof(context));
  context.output_title = output_title;
  context.unique = to_base26(invocation++,1);
  html_renderer.opaque = &context;
  if( output_title ) blob_reset(output_title);
  blob_reset(output_body);
  markdown(output_body, input_markdown, &html_renderer);
}
