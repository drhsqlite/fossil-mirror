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
** An instance of the following structure is passed through the
** "opaque" pointer.
*/
typedef struct MarkdownToHtml MarkdownToHtml;
struct MarkdownToHtml {
  Blob *output_title;     /* Store the title here */
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

/* Invoked for @name and #tag tagged words, marked up in the
** output text in a way that JS and CSS can do something
** interesting with them.  This isn't standard Markdown, so
** it's implementation-specific what occurs here.  More, each
** Fossil feature using Markdown is free to apply markup and
** behavior to these in feature-specific ways.
*/
static int html_tagspan(
  struct Blob *ob,        /* Write the output here */
  struct Blob *text,      /* The word after the tag character */
  enum mkd_tagspan type,  /* Which type of tagspan we're creating */
  void *opaque
){
  if( text==0 ){
    /* no-op */
  }else{
    char c;
    BLOB_APPEND_LITERAL(ob, "<span data-");
    switch (type) {
        case MKDT_ATREF: c='@'; BLOB_APPEND_LITERAL(ob, "atref"); break;
        case MKDT_HASH:  c='#'; BLOB_APPEND_LITERAL(ob, "hash");  break;
    }
    BLOB_APPEND_LITERAL(ob, "=\"");
    html_quote(ob, blob_buffer(text), blob_size(text));
    BLOB_APPEND_LITERAL(ob, "\"");
    blob_appendf(ob, ">%c%b</span>", c, text);
  }
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
    html_tagspan,
    html_triple_emphasis,

    /* low level elements */
    0,    /* entity */
    html_normal_text,

    /* misc. parameters */
    "*_", /* emph_chars */
    0     /* opaque */
  };
  MarkdownToHtml context;
  memset(&context, 0, sizeof(context));
  context.output_title = output_title;
  html_renderer.opaque = &context;
  if( output_title ) blob_reset(output_title);
  blob_reset(output_body);
  markdown(output_body, input_markdown, &html_renderer);
}
