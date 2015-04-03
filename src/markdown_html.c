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


/* INTER_BLOCK -- skip a line between block level elements */
#define INTER_BLOCK(ob) \
  do { if( blob_size(ob)>0 ) blob_append(ob, "\n", 1); } while (0)

/* BLOB_APPEND_LITTERAL -- append a string litteral to a blob */
#define BLOB_APPEND_LITTERAL(blob, litteral) \
  blob_append((blob), "" litteral, (sizeof litteral)-1)
  /*
   * The empty string in the second argument leads to a syntax error
   * when the macro is not used with a string litteral. Unfortunately
   * the error is not overly explicit.
   */

/* BLOB_APPEND_BLOB -- append blob contents to another */
#define BLOB_APPEND_BLOB(dest, src) \
  blob_append((dest), blob_buffer(src), blob_size(src))


/* HTML escape */

static void html_escape(struct Blob *ob, const char *data, size_t size){
  size_t beg = 0, i = 0;
  while( i<size ){
    beg = i;
    while( i<size
     && data[i]!='<'
     && data[i]!='>'
     && data[i]!='"'
     && data[i]!='&'
    ){
      i++;
    }
    blob_append(ob, data+beg, i-beg);
    while( i<size ){
      if( data[i]=='<' ){
        BLOB_APPEND_LITTERAL(ob, "&lt;");
      }else if( data[i]=='>' ){
        BLOB_APPEND_LITTERAL(ob, "&gt;");
      }else if( data[i]=='&' ){
        BLOB_APPEND_LITTERAL(ob, "&amp;");
      }else if( data[i]=='"' ){
        BLOB_APPEND_LITTERAL(ob, "&quot;");
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
  BLOB_APPEND_LITTERAL(ob, "<div class=\"markdown\">\n");
  assert( blob_size(ob)==PROLOG_SIZE );
}

static void html_epilog(struct Blob *ob, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITTERAL(ob, "</div>\n");
}

static void html_raw_block(struct Blob *ob, struct Blob *text, void *opaque){
  char *data = blob_buffer(text);
  size_t first = 0, size = blob_size(text);
  INTER_BLOCK(ob);
  while( first<size && data[first]=='\n' ) first++;
  while( size>first && data[size-1]=='\n' ) size--;
  blob_append(ob, data+first, size-first);
  BLOB_APPEND_LITTERAL(ob, "\n");
}

static void html_blockcode(struct Blob *ob, struct Blob *text, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITTERAL(ob, "<pre><code>");
  html_escape(ob, blob_buffer(text), blob_size(text));
  BLOB_APPEND_LITTERAL(ob, "</code></pre>\n");
}

static void html_blockquote(struct Blob *ob, struct Blob *text, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITTERAL(ob, "<blockquote>\n");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITTERAL(ob, "</blockquote>\n");
}

static void html_header(
  struct Blob *ob,
  struct Blob *text,
  int level,
  void *opaque
){
  struct Blob *title = opaque;
  /* The first header at the beginning of a text is considered as
   * a title and not output. */
  if( blob_size(ob)<=PROLOG_SIZE && blob_size(title)==0 ){
    BLOB_APPEND_BLOB(title, text);
  }
  INTER_BLOCK(ob);
  blob_appendf(ob, "<h%d>", level);
  BLOB_APPEND_BLOB(ob, text);
  blob_appendf(ob, "</h%d>", level);
}

static void html_hrule(struct Blob *ob, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITTERAL(ob, "<hr />\n");
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
  BLOB_APPEND_LITTERAL(ob, "<li>");
  blob_append(ob, text_data, text_size);
  BLOB_APPEND_LITTERAL(ob, "</li>\n");
}

static void html_paragraph(struct Blob *ob, struct Blob *text, void *opaque){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITTERAL(ob, "<p>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITTERAL(ob, "</p>\n");
}


static void html_table(
  struct Blob *ob,
  struct Blob *head_row,
  struct Blob *rows,
  void *opaque
){
  INTER_BLOCK(ob);
  BLOB_APPEND_LITTERAL(ob, "<table>\n");
  if( head_row && blob_size(head_row)>0 ){
    BLOB_APPEND_LITTERAL(ob, "<thead>\n");
    BLOB_APPEND_BLOB(ob, head_row);
    BLOB_APPEND_LITTERAL(ob, "</thead>\n<tbody>\n");
  }
  if( rows ){
    BLOB_APPEND_BLOB(ob, rows);
  }
  if( head_row && blob_size(head_row)>0 ){
    BLOB_APPEND_LITTERAL(ob, "</tbody>\n");
  }
  BLOB_APPEND_LITTERAL(ob, "</table>\n");
}

static void html_table_cell(
  struct Blob *ob,
  struct Blob *text,
  int flags,
  void *opaque
){
  if( flags & MKD_CELL_HEAD ){
    BLOB_APPEND_LITTERAL(ob, "    <th");
  }else{
    BLOB_APPEND_LITTERAL(ob, "    <td");
  }
  switch( flags & MKD_CELL_ALIGN_MASK ){
    case MKD_CELL_ALIGN_LEFT: {
      BLOB_APPEND_LITTERAL(ob, " align=\"left\"");
      break;
    }
    case MKD_CELL_ALIGN_RIGHT: {
      BLOB_APPEND_LITTERAL(ob, " align=\"right\"");
      break;
    }
    case MKD_CELL_ALIGN_CENTER: {
      BLOB_APPEND_LITTERAL(ob, " align=\"center\"");
      break;
    }
  }
  BLOB_APPEND_LITTERAL(ob, ">");
  BLOB_APPEND_BLOB(ob, text);
  if( flags & MKD_CELL_HEAD ){
    BLOB_APPEND_LITTERAL(ob, "</th>\n");
  }else{
    BLOB_APPEND_LITTERAL(ob, "</td>\n");
  }
}

static void html_table_row(
  struct Blob *ob,
  struct Blob *cells,
  int flags,
  void *opaque
){
  BLOB_APPEND_LITTERAL(ob, "  <tr>\n");
  BLOB_APPEND_BLOB(ob, cells);
  BLOB_APPEND_LITTERAL(ob, "  </tr>\n");
}



/* HTML span tags */

static int html_raw_span(struct Blob *ob, struct Blob *text, void *opaque){
  BLOB_APPEND_BLOB(ob, text);
  return 1;
}

static int html_autolink(
  struct Blob *ob,
  struct Blob *link,
  enum mkd_autolink type,
  void *opaque
){
  if( !link || blob_size(link)<=0 ) return 0;
  BLOB_APPEND_LITTERAL(ob, "<a href=\"");
  if( type==MKDA_IMPLICIT_EMAIL ) BLOB_APPEND_LITTERAL(ob, "mailto:");
  html_escape(ob, blob_buffer(link), blob_size(link));
  BLOB_APPEND_LITTERAL(ob, "\">");
  if( type==MKDA_EXPLICIT_EMAIL && blob_size(link)>7 ){
    /* remove "mailto:" from displayed text */
    html_escape(ob, blob_buffer(link)+7, blob_size(link)-7);
  }else{
    html_escape(ob, blob_buffer(link), blob_size(link));
  }
  BLOB_APPEND_LITTERAL(ob, "</a>");
  return 1;
}

static int html_code_span(struct Blob *ob, struct Blob *text, void *opaque){
  BLOB_APPEND_LITTERAL(ob, "<code>");
  html_escape(ob, blob_buffer(text), blob_size(text));
  BLOB_APPEND_LITTERAL(ob, "</code>");
  return 1;
}

static int html_double_emphasis(
  struct Blob *ob,
  struct Blob *text,
  char c,
  void *opaque
){
  BLOB_APPEND_LITTERAL(ob, "<strong>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITTERAL(ob, "</strong>");
  return 1;
}

static int html_emphasis(
  struct Blob *ob,
  struct Blob *text,
  char c,
  void *opaque
){
  BLOB_APPEND_LITTERAL(ob, "<em>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITTERAL(ob, "</em>");
  return 1;
}

static int html_image(
  struct Blob *ob,
  struct Blob *link,
  struct Blob *title,
  struct Blob *alt,
  void *opaque
){
  BLOB_APPEND_LITTERAL(ob, "<img src=\"");
  html_escape(ob, blob_buffer(link), blob_size(link));
  BLOB_APPEND_LITTERAL(ob, "\" alt=\"");
  html_escape(ob, blob_buffer(alt), blob_size(alt));
  if( title && blob_size(title)>0 ){
    BLOB_APPEND_LITTERAL(ob, "\" title=\"");
    html_escape(ob, blob_buffer(title), blob_size(title));
  }
  BLOB_APPEND_LITTERAL(ob, "\" />");
  return 1;
}

static int html_line_break(struct Blob *ob, void *opaque){
  BLOB_APPEND_LITTERAL(ob, "<br />\n");
  return 1;
}

static int html_link(
  struct Blob *ob,
  struct Blob *link,
  struct Blob *title,
  struct Blob *content,
  void *opaque
){
  BLOB_APPEND_LITTERAL(ob, "<a href=\"");
  html_escape(ob, blob_buffer(link), blob_size(link));
  if( title && blob_size(title)>0 ){
    BLOB_APPEND_LITTERAL(ob, "\" title=\"");
    html_escape(ob, blob_buffer(title), blob_size(title));
  }
  BLOB_APPEND_LITTERAL(ob, "\">");
  BLOB_APPEND_BLOB(ob, content);
  BLOB_APPEND_LITTERAL(ob, "</a>");
  return 1;
}

static int html_triple_emphasis(
  struct Blob *ob,
  struct Blob *text,
  char c,
  void *opaque
){
  BLOB_APPEND_LITTERAL(ob, "<strong><em>");
  BLOB_APPEND_BLOB(ob, text);
  BLOB_APPEND_LITTERAL(ob, "</em></strong>");
  return 1;
}


static void html_normal_text(struct Blob *ob, struct Blob *text, void *opaque){
  html_escape(ob, blob_buffer(text), blob_size(text));
}


void markdown_to_html(
  struct Blob *input_markdown,
  struct Blob *output_title,
  struct Blob *output_body
){
  struct mkd_renderer html_renderer = {
    /* prolog and epilog */
    html_prolog,
    html_epilog,

    /* block level elements */
    html_blockcode,
    html_blockquote,
    html_raw_block,
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
    html_code_span,
    html_double_emphasis,
    html_emphasis,
    html_image,
    html_line_break,
    html_link,
    html_raw_span,
    html_triple_emphasis,

    /* low level elements */
    0,  /* entities are copied verbatim */
    html_normal_text,

    /* misc. parameters */
    64, /* maximum stack */
    "*_", /* emphasis characters */
    0 /* opaque data */
  };
  html_renderer.opaque = output_title;
  blob_reset(output_title);
  blob_reset(output_body);
  markdown(output_body, input_markdown, &html_renderer);
}
