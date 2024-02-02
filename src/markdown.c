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
** This file contains code to parse a blob containing markdown text,
** using an external renderer.
*/

#include "config.h"
#include "markdown.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>

#define MKD_LI_END 8  /* internal list flag */

/********************
 * TYPE DEFINITIONS *
 ********************/

#if INTERFACE

/* mkd_autolink -- type of autolink */
enum mkd_autolink {
  MKDA_NOT_AUTOLINK,    /* used internally when it is not an autolink*/
  MKDA_NORMAL,          /* normal http/http/ftp link */
  MKDA_EXPLICIT_EMAIL,  /* e-mail link with explicit mailto: */
  MKDA_IMPLICIT_EMAIL   /* e-mail link without mailto: */
};

/* mkd_renderer -- functions for rendering parsed data */
struct mkd_renderer {
  /* document level callbacks */
  void (*prolog)(struct Blob *ob, void *opaque);
  void (*epilog)(struct Blob *ob, void *opaque);
  void (*footnotes)(struct Blob *ob, const struct Blob *items, void *opaque);

  /* block level callbacks - NULL skips the block */
  void (*blockcode)(struct Blob *ob, struct Blob *text, void *opaque);
  void (*blockquote)(struct Blob *ob, struct Blob *text, void *opaque);
  void (*blockhtml)(struct Blob *ob, struct Blob *text, void *opaque);
  void (*header)(struct Blob *ob, struct Blob *text,
            int level, void *opaque);
  void (*hrule)(struct Blob *ob, void *opaque);
  void (*list)(struct Blob *ob, struct Blob *text, int flags, void *opaque);
  void (*listitem)(struct Blob *ob, struct Blob *text,
            int flags, void *opaque);
  void (*paragraph)(struct Blob *ob, struct Blob *text, void *opaque);
  void (*table)(struct Blob *ob, struct Blob *head_row, struct Blob *rows,
              void *opaque);
  void (*table_cell)(struct Blob *ob, struct Blob *text, int flags,
              void *opaque);
  void (*table_row)(struct Blob *ob, struct Blob *cells, int flags,
              void *opaque);
  void (*footnote_item)(struct Blob *ob, const struct Blob *text,
              int index, int nUsed, void *opaque);

  /* span level callbacks - NULL or return 0 prints the span verbatim */
  int (*autolink)(struct Blob *ob, struct Blob *link,
          enum mkd_autolink type, void *opaque);
  int (*codespan)(struct Blob *ob, struct Blob *text, int nSep, void *opaque);
  int (*double_emphasis)(struct Blob *ob, struct Blob *text,
            char c, void *opaque);
  int (*emphasis)(struct Blob *ob, struct Blob *text, char c,void*opaque);
  int (*image)(struct Blob *ob, struct Blob *link, struct Blob *title,
            struct Blob *alt, void *opaque);
  int (*linebreak)(struct Blob *ob, void *opaque);
  int (*link)(struct Blob *ob, struct Blob *link, struct Blob *title,
          struct Blob *content, void *opaque);
  int (*raw_html_tag)(struct Blob *ob, struct Blob *tag, void *opaque);
  int (*triple_emphasis)(struct Blob *ob, struct Blob *text,
            char c, void *opaque);
  int (*footnote_ref)(struct Blob *ob, const struct Blob *span,
              const struct Blob *upc, int index, int locus, void *opaque);

  /* low level callbacks - NULL copies input directly into the output */
  void (*entity)(struct Blob *ob, struct Blob *entity, void *opaque);
  void (*normal_text)(struct Blob *ob, struct Blob *text, void *opaque);

  /* renderer data */
  const char *emph_chars; /* chars that trigger emphasis rendering */
  void *opaque; /* opaque data send to every rendering callback */
};



/*********
 * FLAGS *
 *********/

/* list/listitem flags */
#define MKD_LIST_ORDERED  1
#define MKD_LI_BLOCK      2  /* <li> containing block data */

/* table cell flags */
#define MKD_CELL_ALIGN_DEFAULT  0
#define MKD_CELL_ALIGN_LEFT     1
#define MKD_CELL_ALIGN_RIGHT    2
#define MKD_CELL_ALIGN_CENTER   3  /* LEFT | RIGHT */
#define MKD_CELL_ALIGN_MASK     3
#define MKD_CELL_HEAD           4


#endif /* INTERFACE */

#define BLOB_COUNT(pBlob,el_type) (blob_size(pBlob)/sizeof(el_type))
#define COUNT_FOOTNOTES(pBlob) BLOB_COUNT(pBlob,struct footnote)
#define CAST_AS_FOOTNOTES(pBlob) ((struct footnote*)blob_buffer(pBlob))

/***************
 * LOCAL TYPES *
 ***************/

/*
** link_ref -- reference to a link.
*/
struct link_ref {
  struct Blob id;     /* must be the first field as in footnote struct */
  struct Blob link;
  struct Blob title;
};

/*
** A footnote's data.
** id, text, and upc fields must be in that particular order.
*/
struct footnote {
  struct Blob id;      /* must be the first field as in link_ref struct  */
  struct Blob text;    /* footnote's content that is rendered at the end */
  struct Blob upc;     /* user-provided classes  .ASCII-alnum.or-hypen:  */
  int bRndred;         /* indicates if `text` holds a rendered content   */

  int defno;  /* serial number of definition, set during the first pass  */
  int index;  /* set to the index within array after ordering by id      */
  int iMark;  /* user-visible numeric marker, assigned upon the first use*/
  int nUsed;  /* counts references to this note, increments upon each use*/
};
#define FOOTNOTE_INITIALIZER {empty_blob,empty_blob,empty_blob, 0,0,0,0,0}

/* char_trigger -- function pointer to render active chars */
/*   returns the number of chars taken care of */
/*   data is the pointer of the beginning of the span */
/*   offset is the number of valid chars before data */
struct render;
typedef size_t (*char_trigger)(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size);


/* render -- structure containing one particular render */
struct render {
  struct mkd_renderer make;
  struct Blob refs;
  char_trigger active_char[256];
  int iDepth;                    /* Depth of recursion */
  int nBlobCache;                /* Number of entries in aBlobCache */
  struct Blob *aBlobCache[20];   /* Cache of Blobs available for reuse */

  struct {
    Blob all;    /* Buffer that holds array of footnotes. Its underline
                    memory may be reallocated when a new footnote is added. */
    int nLbled;  /* number of labeled footnotes found during the first pass */
    int nMarks;  /* counts distinct indices found during the second pass    */
    struct footnote misref; /* nUsed counts misreferences, iMark must be -1 */
  } notes;
};

/* html_tag -- structure for quick HTML tag search (inspired from discount) */
struct html_tag {
  const char *text;
  int size;
};



/********************
 * GLOBAL VARIABLES *
 ********************/

/* block_tags -- recognised block tags, sorted by cmp_html_tag.
**
** When these HTML tags are separated from other text by newlines
** then they are rendered verbatim.  Their content is not interpreted
** in any way.
*/
static const struct html_tag block_tags[] = {
  { "html",         4 },
  { "pre",          3 },
  { "script",       6 },
};

/***************************
 * STATIC HELPER FUNCTIONS *
 ***************************/

/*
** build_ref_id -- collapse whitespace from input text to make it a ref id.
** Potential TODO: maybe also handle CR+LF line endings?
*/
static int build_ref_id(struct Blob *id, const char *data, size_t size){
  size_t beg, i;
  char *id_data;

  /* skip leading whitespace */
  while( size>0 && (data[0]==' ' || data[0]=='\t' || data[0]=='\n') ){
    data++;
    size--;
  }

  /* skip trailing whitespace */
  while( size>0 && (data[size-1]==' '
   || data[size-1]=='\t'
   || data[size-1]=='\n')
  ){
    size--;
  }
  if( size==0 ) return -1;

  /* making the ref id */
  i = 0;
  blob_reset(id);
  while( i<size ){
    /* copy non-whitespace into the output buffer */
    beg = i;
    while( i<size && !(data[i]==' ' || data[i]=='\t' || data[i]=='\n') ){
      i++;
    }
    blob_append(id, data+beg, i-beg);

    /* add a single space and skip all consecutive whitespace */
    if( i<size ) blob_append_char(id, ' ');
    while( i<size && (data[i]==' ' || data[i]=='\t' || data[i]=='\n') ){ i++; }
  }

  /* turn upper-case ASCII into their lower-case counterparts */
  id_data = blob_buffer(id);
  for(i=0; i<blob_size(id); i++){
    if( id_data[i]>='A' && id_data[i]<='Z' ) id_data[i] += 'a' - 'A';
  }
  return 0;
}


/* cmp_link_ref -- comparison function for link_ref sorted arrays */
static int cmp_link_ref(const void *key, const void *array_entry){
  struct link_ref *lr = (void *)array_entry;
  return blob_compare((void *)key, &lr->id);
}


/* cmp_link_ref_sort -- comparison function for link_ref qsort */
static int cmp_link_ref_sort(const void *a, const void *b){
  struct link_ref *lra = (void *)a;
  struct link_ref *lrb = (void *)b;
  return blob_compare(&lra->id, &lrb->id);
}

/*
** cmp_footnote_id -- comparison function for footnotes qsort.
** Empty IDs sort last (in undetermined order).
** Equal IDs are sorted in the order of definition in the source.
*/
static int cmp_footnote_id(const void *fna, const void *fnb){
  const struct footnote *a = fna, *b = fnb;
  const int szA = blob_size(&a->id), szB = blob_size(&b->id);
  if( szA ){
    if( szB ){
      int cmp = blob_compare(&a->id, &b->id);
      if( cmp ) return cmp;
    }else return -1;
  }else return szB ? 1 : 0;
  /* IDs are equal and non-empty */
  if( a->defno < b->defno ) return -1;
  if( a->defno > b->defno ) return  1;
  assert(!"reachable");
  return 0; /* should never reach here */
}

/*
** cmp_footnote_sort -- comparison function for footnotes qsort.
** Unreferenced footnotes (when nUsed == 0) sort last and
** are sorted in the order of definition in the source.
*/
static int cmp_footnote_sort(const void *fna, const void *fnb){
  const struct footnote *a = fna, *b = fnb;
  int i, j;
  assert( a->nUsed >= 0 );
  assert( b->nUsed >= 0 );
  assert( a->defno >= 0 );
  assert( b->defno >= 0 );
  if( a->nUsed ){
    assert( a->iMark >  0 );
    if( !b->nUsed ) return -1;
    assert( b->iMark >  0 );
    i = a->iMark;
    j = b->iMark;
  }else{
    if( b->nUsed )  return  1;
    i = a->defno;
    j = b->defno;
  }
  if( i < j ) return -1;
  if( i > j ) return  1;
  return 0;
}

/* cmp_html_tag -- comparison function for bsearch() (stolen from discount) */
static int cmp_html_tag(const void *a, const void *b){
  const struct html_tag *hta = a;
  const struct html_tag *htb = b;
  int sz = hta->size;
  int c;
  if( htb->size<sz ) sz = htb->size;
  c = fossil_strnicmp(hta->text, htb->text, sz);
  if( c==0 ) c = hta->size - htb->size;
  return c;
}


/* find_block_tag -- returns the current block tag */
static const struct html_tag *find_block_tag(const char *data, size_t size){
  size_t i = 0;
  struct html_tag key;

  /* looking for the word end */
  while( i<size
   && ((data[i]>='0' && data[i]<='9')
       || (data[i]>='A' && data[i]<='Z')
       || (data[i]>='a' && data[i]<='z'))
  ){
    i++;
  }
  if( i>=size ) return 0;

  /* binary search of the tag */
  key.text = data;
  key.size = i;
  return bsearch(&key,
                 block_tags,
                 count(block_tags),
                 sizeof block_tags[0],
                 cmp_html_tag);
}

/* return true if recursion has gone too deep */
static int too_deep(struct render *rndr){
  return rndr->iDepth>200;
}

/* get a new working buffer from the cache or create one.  return NULL
** if failIfDeep is true and the depth of recursion has gone too deep. */
static struct Blob *new_work_buffer(struct render *rndr){
  struct Blob *ret;
  rndr->iDepth++;
  if( rndr->nBlobCache ){
    ret = rndr->aBlobCache[--rndr->nBlobCache];
  }else{
    ret = fossil_malloc(sizeof(*ret));
  }
  *ret = empty_blob;
  return ret;
}


/* release the given working buffer back to the cache */
static void release_work_buffer(struct render *rndr, struct Blob *buf){
  if( !buf ) return;
  rndr->iDepth--;
  blob_reset(buf);
  if( rndr->nBlobCache <
        (int)(sizeof(rndr->aBlobCache)/sizeof(rndr->aBlobCache[0])) ){
    rndr->aBlobCache[rndr->nBlobCache++] = buf;
  }else{
    fossil_free(buf);
  }
}



/****************************
 * INLINE PARSING FUNCTIONS *
 ****************************/

/* is_mail_autolink -- looks for the address part of a mail autolink and '>' */
/* this is less strict than the original markdown e-mail address matching */
static size_t is_mail_autolink(char *data, size_t size){
  size_t i = 0, nb = 0;
  /* address is assumed to be: [-@._a-zA-Z0-9]+ with exactly one '@' */
  while( i<size && (data[i]=='-'
   || data[i]=='.'
   || data[i]=='_'
   || data[i]=='@'
   || (data[i]>='a' && data[i]<='z')
   || (data[i]>='A' && data[i]<='Z')
   || (data[i]>='0' && data[i]<='9'))
  ){
    if( data[i]=='@' ) nb++;
    i++;
  }
  if( i>=size || data[i]!='>' || nb!=1 ) return 0;
  return i+1;
}


/* tag_length -- returns the length of the given tag, or 0 if it's not valid */
static size_t tag_length(char *data, size_t size, enum mkd_autolink *autolink){
  size_t i, j;

  /* a valid tag can't be shorter than 3 chars */
  if( size<3 ) return 0;

  /* begins with a '<' optionally followed by '/', followed by letter */
  if( data[0]!='<' ) return 0;
  i = (data[1]=='/') ? 2 : 1;
  if( (data[i]<'a' || data[i]>'z') &&  (data[i]<'A' || data[i]>'Z') ){
    if( data[1]=='!' && size>=7 && data[2]=='-' && data[3]=='-' ){
      for(i=6; i<size && (data[i]!='>'||data[i-1]!='-'|| data[i-2]!='-');i++){}
      if( i<size ) return i;
    }
    return 0;
  }

  /* scheme test */
  *autolink = MKDA_NOT_AUTOLINK;
  if( size>6
   && fossil_strnicmp(data+1, "http", 4)==0
   && (data[5]==':'
       || ((data[5]=='s' || data[5]=='S') && data[6]==':'))
  ){
    i = (data[5]==':') ? 6 : 7;
    *autolink = MKDA_NORMAL;
  }else if( size>5 && fossil_strnicmp(data+1, "ftp:", 4)==0 ){
    i = 5;
    *autolink = MKDA_NORMAL;
  }else if( size>7 && fossil_strnicmp(data+1, "mailto:", 7)==0 ){
    i = 8;
    /* not changing *autolink to go to the address test */
   }

  /* completing autolink test: no whitespace or ' or " */
  if( i>=size || i=='>' ){
    *autolink = MKDA_NOT_AUTOLINK;
  }else if( *autolink ){
    j = i;
    while( i<size
     && data[i]!='>'
     && data[i]!='\''
     && data[i]!='"'
     && data[i]!=' '
     && data[i]!='\t'
     && data[i]!='\n'
    ){
      i++;
    }
    if( i>=size ) return 0;
    if( i>j && data[i]=='>' ) return i+1;
    /* one of the forbidden chars has been found */
    *autolink = MKDA_NOT_AUTOLINK;
  }else if( (j = is_mail_autolink(data+i, size-i))!=0 ){
    *autolink = (i==8) ? MKDA_EXPLICIT_EMAIL : MKDA_IMPLICIT_EMAIL;
    return i+j;
  }

  /* looking for something looking like a tag end */
  while( i<size && data[i]!='>' ){ i++; }
  if( i>=size ) return 0;
  return i+1;
}


/* parse_inline -- parses inline markdown elements */
static void parse_inline(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size
){
  size_t i = 0, end = 0;
  char_trigger action = 0;
  struct Blob work = BLOB_INITIALIZER;

  if( too_deep(rndr) ){
    blob_append(ob, data, size);
    return;
  }
  while( i<size ){
    /* copying inactive chars into the output */
    while( end<size
     && (action = rndr->active_char[(unsigned char)data[end]])==0
    ){
      end++;
    }
    if( end>i ){
      if( rndr->make.normal_text ){
        blob_init(&work, data+i, end-i);
        rndr->make.normal_text(ob, &work, rndr->make.opaque);
      }else{
        blob_append(ob, data+i, end-i);
      }
    }
    if( end>=size ) break;
    i = end;

    /* calling the trigger */
    end = action(ob, rndr, data+i, i, size-i);
    if( !end ){
      /* no action from the callback */
      end = i+1;
    }else{
      i += end;
      end = i;
    }
  }
}

/*
** data[*pI] should be a "`" character that introduces a code-span.
** The code-span boundry mark can be any number of one or more "`"
** characters.  We do not know the size of the boundry marker, only
** that there is at least one "`" at data[*pI].
**
** This routine increases *pI to move it past the code-span, including
** the closing boundary mark.  Or, if the code-span is unterminated,
** this routine moves *pI past the opening boundary mark only.
*/
static void skip_codespan(const char *data, size_t size, size_t *pI){
  size_t i = *pI;
  size_t span_nb;   /* Number of "`" characters in the boundary mark */
  size_t bt;

  assert( i<size );
  assert( data[i]=='`' );
  data += i;
  size -= i;

  /* counting the number of opening backticks */
  i = 0;
  span_nb = 0;
  while( i<size && data[i]=='`' ){
    i++;
    span_nb++;
  }
  if( i>=size ){
    *pI += span_nb;
    return;
  }

  /* finding the matching closing sequence */
  bt = 0;
  while( i<size && bt<span_nb ){
    if( data[i]=='`' ) bt += 1; else bt = 0;
    i++;
  }
  *pI += (bt == span_nb) ? i : span_nb;
}


/* find_emph_char -- looks for the next emph char, skipping other constructs */
static size_t find_emph_char(char *data, size_t size, char c){
  size_t i = data[0]!='`';

  while( i<size ){
    while( i<size && data[i]!=c && data[i]!='`' && data[i]!='[' ){ i++; }
    if( i>=size ) return 0;

    /* not counting escaped chars */
    if( i && data[i-1]=='\\' ){
      i++;
      continue;
    }

    if( data[i]==c ) return i;

    if( data[i]=='`' ){            /* skip a code span */
      skip_codespan(data, size, &i);
    }else if( data[i]=='[' ){      /* skip a link */
      size_t tmp_i = 0;
      char cc;
      i++;
      while( i<size && data[i]!=']' ){
        if( !tmp_i && data[i]==c ) tmp_i = i;
        i++;
      }
      i++;
      while( i<size && (data[i]==' ' || data[i]=='\t' || data[i]=='\n') ){
        i++;
      }
      if( i>=size ) return tmp_i;
      if( data[i]!='[' && data[i]!='(' ){ /* not a link*/
        if( tmp_i ) return tmp_i; else continue;
      }
      cc = data[i];
      i++;
      while( i<size && data[i]!=cc ){
        if( !tmp_i && data[i]==c ) tmp_i = i;
        i++;
      }
      if( i>=size ) return tmp_i;
      i++;
    }
  }
  return 0;
}

/* CommonMark defines separate "right-flanking" and "left-flanking"
** deliminators for emphasis.  Whether a deliminator is left- or
** right-flanking, or both, or neither depends on the characters
** immediately before and after.
**
**   before   after   example    left-flanking   right-flanking
**   ------   -----   -------    -------------   --------------
**    space   space      *            no              no
**    space   punct      *)          yes              no
**    space   alnum      *x          yes              no
**    punct   space     (*            no             yes
**    punct   punct     (*)          yes             yes
**    punct   alnum     (*x          yes              no
**    alnum   space     a*            no             yes
**    alnum   punct     a*(           no             yes
**    alnum   alnum     a*x          yes             yes
**
** The following routines determine whether a delimitor is left
** or right flanking.
*/
static int left_flanking(char before, char after){
  if( fossil_isspace(after) ) return 0;
  if( fossil_isalnum(after) ) return 1;
  if( fossil_isalnum(before) ) return 0;
  return 1;
}
static int right_flanking(char before, char after){
  if( fossil_isspace(before) ) return 0;
  if( fossil_isalnum(before) ) return 1;
  if( fossil_isalnum(after) ) return 0;
  return 1;
}


/*
** parse_emph1 -- parsing single emphasis.
** closed by a symbol not preceded by whitespace and not followed by symbol.
*/
static size_t parse_emph1(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size,
  char c
){
  size_t i = 0, len;
  struct Blob *work = 0;
  int r;
  char after;

  if( !rndr->make.emphasis ) return 0;

  /* skipping one symbol if coming from emph3 */
  if( size>1 && data[0]==c && data[1]==c ) i = 1;

  while( i<size ){
    len = find_emph_char(data+i, size-i, c);
    if( !len ) return 0;
    i += len;
    if( i>=size ) return 0;

    if( i+1<size && data[i+1]==c ){
      i++;
      continue;
    }
    after = i+1<size ? data[i+1] : ' ';
    if( data[i]==c
     && right_flanking(data[i-1],after)
     && (c!='_' || !fossil_isalnum(after))
     && !too_deep(rndr)
    ){
      work = new_work_buffer(rndr);
      parse_inline(work, rndr, data, i);
      r = rndr->make.emphasis(ob, work, c, rndr->make.opaque);
      release_work_buffer(rndr, work);
      return r ? i+1 : 0;
    }
  }
  return 0;
}

/*
** parse_emph2 -- parsing single emphasis.
*/
static size_t parse_emph2(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size,
  char c
){
  size_t i = 0, len;
  struct Blob *work = 0;
  int r;
  char after;

  if( !rndr->make.double_emphasis ) return 0;

  while( i<size ){
    len = find_emph_char(data+i, size-i, c);
    if( !len ) return 0;
    i += len;
    after = i+2<size ? data[i+2] : ' ';
    if( i+1<size
     && data[i]==c
     && data[i+1]==c
     && right_flanking(data[i-1],after)
     && (c!='_' || !fossil_isalnum(after))
     && !too_deep(rndr)
    ){
      work = new_work_buffer(rndr);
      parse_inline(work, rndr, data, i);
      r = rndr->make.double_emphasis(ob, work, c, rndr->make.opaque);
      release_work_buffer(rndr, work);
      return r ? i+2 : 0;
    }
    i++;
  }
  return 0;
}

/*
** parse_emph3 -- parsing single emphasis.
** finds the first closing tag, and delegates to the other emph.
*/
static size_t parse_emph3(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size,
  char c
){
  size_t i = 0, len;
  int r;

  while( i<size ){
    len = find_emph_char(data+i, size-i, c);
    if( !len ) return 0;
    i += len;

    /* skip whitespace preceded symbols */
    if( data[i]!=c || data[i-1]==' ' || data[i-1]=='\t' || data[i-1]=='\n' ){
      continue;
    }

    if( i+2<size
     && data[i+1]==c
     && data[i+2] == c
     && rndr->make.triple_emphasis
     && !too_deep(rndr)
    ){
      /* triple symbol found */
      struct Blob *work = new_work_buffer(rndr);
      parse_inline(work, rndr, data, i);
      r = rndr->make.triple_emphasis(ob, work, c, rndr->make.opaque);
      release_work_buffer(rndr, work);
      return r ? i+3 : 0;
    }else if( i+1<size && data[i+1]==c ){
      /* double symbol found, handing over to emph1 */
      len = parse_emph1(ob, rndr, data-2, size+2, c);
      return len ? len-2 : 0;
    }else{
      /* single symbol found, handing over to emph2 */
      len = parse_emph2(ob, rndr, data-1, size+1, c);
      return len ? len-1 : 0;
    }
  }
  return 0;
}

/*
** char_emphasis -- single and double emphasis parsing.
*/
static size_t char_emphasis(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size
){
  char c = data[0];
  char before = offset>0 ? data[-1] : ' ';
  size_t ret;

  if( size>2 && data[1]!=c ){
    if( !left_flanking(before, data[1])
     || (c=='_' && fossil_isalnum(before))
     || (ret = parse_emph1(ob, rndr, data+1, size-1, c))==0
    ){
      return 0;
    }
    return ret+1;
  }

  if( size>3 && data[1]==c && data[2]!=c ){
    if( !left_flanking(before, data[2])
     || (c=='_' && fossil_isalnum(before))
     || (ret = parse_emph2(ob, rndr, data+2, size-2, c))==0
    ){
      return 0;
    }
    return ret+2;
  }

  if( size>4 && data[1]==c && data[2]==c && data[3]!=c ){
    if( !left_flanking(before, data[3])
     || (c=='_' && fossil_isalnum(before))
     || (ret = parse_emph3(ob, rndr, data+3, size-3, c))==0
    ){
      return 0;
    }
    return ret+3;
  }
  return 0;
}

/*
** char_linebreak -- '\n' preceded by two spaces (assuming linebreak != 0).
*/
static size_t char_linebreak(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size
){
  if( offset<2 || data[-1]!=' ' || data[-2]!=' ' ) return 0;
  /* removing the last space from ob and rendering */
  if( blob_size(ob)>0 && blob_buffer(ob)[blob_size(ob)-1]==' ' ) ob->nUsed--;
  return rndr->make.linebreak(ob, rndr->make.opaque) ? 1 : 0;
}

/*
** char_codespan -- '`' parsing a code span (assuming codespan != 0).
*/
static size_t char_codespan(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size
){
  size_t end, nb = 0, i, f_begin, f_end;
  char delim = data[0];

  /* counting the number of backticks in the delimiter */
  while( nb<size && data[nb]==delim ){ nb++; }

  /* finding the next delimiter */
  i = 0;
  for(end=nb; end<size && i<nb; end++){
    if( data[end]==delim ) i++; else i = 0;
  }
  if( i<nb && end>=size ) return 0; /* no matching delimiter */

  /* trimming outside whitespaces */
  f_begin = nb;
  while( f_begin<end && (data[f_begin]==' ' || data[f_begin]=='\t') ){
    f_begin++;
  }
  f_end = end-nb;
  while( f_end>nb && (data[f_end-1]==' ' || data[f_end-1]=='\t') ){ f_end--; }

  /* real code span */
  if( f_begin<f_end ){
    struct Blob work = BLOB_INITIALIZER;
    blob_init(&work, data+f_begin, f_end-f_begin);
    if( !rndr->make.codespan(ob, &work, nb, rndr->make.opaque) ) end = 0;
  }else{
    if( !rndr->make.codespan(ob, 0, nb, rndr->make.opaque) ) end = 0;
  }
  return end;
}


/*
** char_escape -- '\\' backslash escape.
*/
static size_t char_escape(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size
){
  struct Blob work = BLOB_INITIALIZER;
  if( size>1 ){
    if( rndr->make.normal_text ){
      blob_init(&work, data+1,1);
      rndr->make.normal_text(ob, &work, rndr->make.opaque);
    }else{
      blob_append(ob, data+1, 1);
    }
  }
  return 2;
}

/*
** char_entity -- '&' escaped when it doesn't belong to an entity.
** valid entities are assumed to be anything matching &#?[A-Za-z0-9]+;
*/
static size_t char_entity(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size
){
  size_t end = 1;
  struct Blob work = BLOB_INITIALIZER;
  if( end<size && data[end]=='#' ) end++;
  while( end<size
   && ((data[end]>='0' && data[end]<='9')
       || (data[end]>='a' && data[end]<='z')
       || (data[end]>='A' && data[end]<='Z'))
  ){
    end++;
  }
  if( end<size && data[end]==';' ){
    /* real entity */
    end++;
  }else{
    /* lone '&' */
    return 0;
  }
  if( rndr->make.entity ){
    blob_init(&work, data, end);
    rndr->make.entity(ob, &work, rndr->make.opaque);
  }else{
    blob_append(ob, data, end);
  }
  return end;
}

/*
** char_langle_tag -- '<' when tags or autolinks are allowed.
*/
static size_t char_langle_tag(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size
){
  enum mkd_autolink altype = MKDA_NOT_AUTOLINK;
  size_t end = tag_length(data, size, &altype);
  struct Blob work = BLOB_INITIALIZER;
  int ret = 0;
  if( end ){
    if( rndr->make.autolink && altype!=MKDA_NOT_AUTOLINK ){
      blob_init(&work, data+1, end-2);
      ret = rndr->make.autolink(ob, &work, altype, rndr->make.opaque);
    }else if( rndr->make.raw_html_tag ){
      blob_init(&work, data, end);
      ret = rndr->make.raw_html_tag(ob, &work, rndr->make.opaque);
    }
  }

  if( !ret ){
    return 0;
  }else{
    return end;
  }
}

/*
** get_link_inline -- extract inline-style link and title from
** parenthesed data
*/
static int get_link_inline(
  struct Blob *link,
  struct Blob *title,
  char *data,
  size_t size
){
  size_t i = 0, mark;
  size_t link_b, link_e;
  size_t title_b = 0, title_e = 0;

  /* skipping initial whitespace */
  while( i<size && (data[i]==' ' || data[i]=='\t' || data[i]=='\n') ){ i++; }
  link_b = i;

  /* looking for link end: ' " */
  while( i<size && data[i]!='\'' && data[i]!='"' ){ i++; }
  link_e = i;

  /* looking for title end if present */
  if( data[i]=='\'' || data[i]=='"' ){
    i++;
    title_b = i;

    /* skipping whitespaces after title */
    title_e = size-1;
    while( title_e>title_b
     && (data[title_e]==' '
         || data[title_e]=='\t'
         || data[title_e]=='\n')
    ){
      title_e--;
    }

    /* checking for closing quote presence */
    if (data[title_e] != '\'' &&  data[title_e] != '"') {
      title_b = title_e = 0;
      link_e = i;
    }
  }

  /* remove whitespace at the end of the link */
  while( link_e>link_b
   && (data[link_e-1]==' '
       || data[link_e-1]=='\t'
       || data[link_e-1]=='\n')
  ){
    link_e--;
  }

  /* remove optional angle brackets around the link */
  if( data[link_b]=='<' ) link_b += 1;
  if( link_e && data[link_e-1]=='>' ) link_e -= 1;

  /* escape backslashed character from link */
  blob_reset(link);
  i = link_b;
  while( i<link_e ){
    mark = i;
    while( i<link_e && data[i]!='\\' ){ i++; }
    blob_append(link, data+mark, i-mark);
    while( i<link_e && data[i]=='\\' ){ i++; }
  }

  /* handing back title */
  blob_reset(title);
  if( title_e>title_b ) blob_append(title, data+title_b, title_e-title_b);

  /* this function always succeed */
  return 0;
}


/*
** get_link_ref -- extract referenced link and title from id.
*/
static int get_link_ref(
  struct render *rndr,
  struct Blob *link,
  struct Blob *title,
  char *data,
  size_t size
){
  struct link_ref *lr;
  const size_t sz = blob_size(&rndr->refs);

  /* find the link from its id (stored temporarily in link) */
  blob_reset(link);
  if( !sz || build_ref_id(link, data, size)<0 ) return -1;
  lr = bsearch(link,
               blob_buffer(&rndr->refs),
               sz/sizeof(struct link_ref),
               sizeof (struct link_ref),
               cmp_link_ref);
  if( !lr ) return -1;

  /* fill the output buffers */
  blob_reset(link);
  blob_reset(title);
  blob_appendb(link, &lr->link);
  blob_appendb(title, &lr->title);
  return 0;
}

/*
** get_footnote() -- find a footnote by label, invoked during the 2nd pass.
** If found then return a shallow copy of the corresponding footnote;
** otherwise return a shallow copy of rndr->notes.misref.
** In both cases corresponding `nUsed` field is incremented before return.
*/
static struct footnote get_footnote(
  struct render *rndr,
  const char *data,
  size_t size
){
  struct footnote *fn = 0;
  struct Blob *id;
  if( !rndr->notes.nLbled ) goto fallback;
  id = new_work_buffer(rndr);
  if( build_ref_id(id, data, size)<0 ) goto cleanup;
  fn = bsearch(id, blob_buffer(&rndr->notes.all),
               rndr->notes.nLbled,
               sizeof (struct footnote),
               cmp_link_ref);
  if( !fn ) goto cleanup;

  if( fn->nUsed == 0 ){  /* the first reference to the footnote */
    assert( fn->iMark == 0 );
    fn->iMark = ++(rndr->notes.nMarks);
  }
  assert( fn->iMark > 0 );
cleanup:
  release_work_buffer( rndr, id );
fallback:
  if( !fn ) fn = &rndr->notes.misref;
  fn->nUsed++;
  assert( fn->nUsed > 0 );
  return *fn;
}

/*
** Counts characters in the blank prefix within at most nHalfLines.
** A sequence of spaces and tabs counts as odd halfline,
** a newline counts as even halfline.
** If nHalfLines < 0 then proceed without constraints.
*/
static inline size_t sizeof_blank_prefix(
  const char *data, size_t size, int nHalfLines
){
  const char *p = data;
  const char * const end = data+size;
  if( nHalfLines < 0 ){
    while( p!=end && fossil_isspace(*p) ){
      p++;
    }
  }else while( nHalfLines > 0 ){
    while( p!=end && (*p==' ' || *p=='\t' ) ){ p++; }
    if( p==end || --nHalfLines == 0 ) break;
    if( *p=='\n' || *p=='\r' ){
      p++;
      if( p==end ) break;
      if( *p=='\n' && p[-1]=='\r' ){
        p++;
      }
    }
    nHalfLines--;
  }
  return p-data;
}

/*
** Check if the data starts with a classlist token of the special form.
** If so then return the length of that token, otherwise return 0.
**
** The token must start with a dot and must end with a colon;
** in between of these it must be a dot-separated list of words;
** each word may contain only alphanumeric characters and hyphens.
**
** If `bBlank` is non-zero then a blank character must follow
** the token's ending colon: otherwise function returns 0
** despite the well-formed token.
*/
static size_t is_footnote_classlist(const char * const data, size_t size,
                                    int bBlank){
  const char *p;
  const char * const end = data+size;
  if( data==end || *data != '.' ) return 0;
  for(p=data+1; p!=end; p++){
    if( fossil_isalnum(*p) || *p=='-' ) continue;
    if( p[-1]=='.' ) break;
    if( *p==':' ){
      p++;
      if( bBlank ){
        if( p==end || !fossil_isspace(*p) ) break;
      }
      return p-data;
    }
    if( *p!='.' ) break;
  }
  return 0;
}

/*
** Adds unlabeled footnote to the rndr->notes.all.
** On success puts a shallow copy of the constructed footnote into pFN
** and returns 1, otherwise pFN is unchanged and 0 is returned.
*/
static inline int add_inline_footnote(
  struct render *rndr,
  const char *text,
  size_t size,
  struct footnote* pFN
){
  struct footnote fn = FOOTNOTE_INITIALIZER, *last;
  const char *zUPC = 0;
  size_t nUPC = 0, n = sizeof_blank_prefix(text, size, 3);
  if( n >= size ) return 0;
  text += n;
  size -= n;
  nUPC = is_footnote_classlist(text, size, 1);
  if( nUPC ){
    assert( nUPC<size );
    zUPC = text;
    text += nUPC;
    size -= nUPC;
  }
  if( sizeof_blank_prefix(text,size,-1)==size ){
    if( !nUPC ) return 0; /* empty inline footnote */
    text = zUPC;
    size = nUPC;          /* bare classlist is treated */
    nUPC = 0;             /* as plain text */
  }
  fn.iMark = ++(rndr->notes.nMarks);
  fn.nUsed  = 1;
  fn.index  = COUNT_FOOTNOTES(&rndr->notes.all);
  assert( fn.iMark > 0 );
  blob_append(&fn.text, text, size);
  if(nUPC) blob_append(&fn.upc, zUPC, nUPC);
  blob_append(&rndr->notes.all, (char *)&fn, sizeof fn);
  last = (struct footnote*)( blob_buffer(&rndr->notes.all)
                            +( blob_size(&rndr->notes.all)-sizeof fn ));
  assert( pFN );
  memcpy( pFN, last, sizeof fn );
  return 1;
}

/*
** Return the byte offset of the matching closing bracket or 0 if not
** found.  begin[0] must be either '[' or '('.
**
** TODO: It seems that things like "\\(" are not handled correctly.
**       That is historical behavior for a corner-case,
**       so it's left as it is until somebody complains.
*/
static inline size_t matching_bracket_offset(
  const char* begin,
  const char* end
){
  const char *i;
  int level;
  const char bra = *begin;
  const char ket = bra=='[' ? ']' : ')';
  assert( bra=='[' || bra=='(' );
  for(i=begin+1,level=1; i!=end; i++){
    if( *i=='\n' )        /* do nothing */;
    else if( i[-1]=='\\' ) continue;
    else if( *i==bra )     level++;
    else if( *i==ket ){
      if( --level<=0 ) return i-begin;
    }
  }
  return 0;
}

/*
** char_footnote -- '(': parsing a standalone inline footnote.
*/
static size_t char_footnote(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size
){
  size_t end;
  struct footnote fn;

  if( size<4 || data[1]!='^' ) return 0;
  end = matching_bracket_offset(data, data+size);
  if( !end ) return 0;
  if( !add_inline_footnote(rndr, data+2, end-2, &fn) ) return 0;
  if( rndr->make.footnote_ref ){
    rndr->make.footnote_ref(ob,0,&fn.upc,fn.iMark,1,rndr->make.opaque);
  }
  return end+1;
}

/*
** char_link -- '[': parsing a link or an image.
*/
static size_t char_link(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t offset,
  size_t size     /* parse_inline() ensures that size > 0 */
){
  const int bFsfn = (size>3 && data[1]=='^'); /*free-standing footnote ref*/
  const int bImg = !bFsfn && (offset && data[-1] == '!');
  size_t i, txt_e;
  struct Blob *content;
  struct Blob *link;
  struct Blob *title;
  struct footnote fn;
  int ret;

  /* checking whether the correct renderer exists */
  if( !bFsfn ){
    if( (bImg && !rndr->make.image) || (!bImg && !rndr->make.link) ){
      return 0;
    }
  }

  /* looking for the matching closing bracket */
  txt_e = matching_bracket_offset(data, data+size);
  if( !txt_e ) return 0;
  i = txt_e + 1;
  ret = 0; /* error if we don't get to the callback */

  /* free-standing footnote reference */
  if( bFsfn ){
    fn = get_footnote(rndr, data+2, txt_e-2);
    content = link = title = 0;
  }else{
    fn.nUsed = 0;

    /* skip "inter-bracket-whitespace" - any amount of whitespace or newline */
    /* (this is much more lax than original markdown syntax) */
    while( i<size && (data[i]==' ' || data[i]=='\t' || data[i]=='\n') ){ i++; }

    /* allocate temporary buffers to store content, link and title */
    title = new_work_buffer(rndr);
    content = new_work_buffer(rndr);
    link = new_work_buffer(rndr);

    if( i<size && data[i]=='(' ){

      if( i+2<size && data[i+1]=='^' ){  /* span-bounded inline footnote */

        const size_t k = matching_bracket_offset(data+i, data+size);
        if( !k ) goto char_link_cleanup;
        add_inline_footnote(rndr, data+(i+2), k-2, &fn);
        i += k+1;
      }else{                             /* inline style link  */
        size_t span_end = i;
        while( span_end<size
               && !(data[span_end]==')'
                    && (span_end==i || data[span_end-1]!='\\')) ){
          span_end++;
        }
        if( span_end>=size
            || get_link_inline(link, title, data+i+1, span_end-(i+1))<0 ){
          goto char_link_cleanup;
        }
        i = span_end+1;
      }
    /* reference style link or span-bounded footnote reference */
    }else if( i<size && data[i]=='[' ){
      char *id_data;
      size_t id_size, id_end = i;
      int bFootnote;

      while( id_end<size && data[id_end]!=']' ){ id_end++; }
      if( id_end>=size ) goto char_link_cleanup;
      bFootnote = data[i+1]=='^';
      if( i+1==id_end || (bFootnote && i+2==id_end) ){
        /* implicit id - use the contents */
        id_data = data+1;
        id_size = txt_e-1;
      }else{
        /* explicit id - between brackets */
        id_data = data+i+1;
        id_size = id_end-(i+1);
        if( bFootnote ){
          id_data++;
          id_size--;
        }
      }
      if( bFootnote ){
        fn = get_footnote(rndr, id_data, id_size);
      }else if( get_link_ref(rndr, link, title, id_data, id_size)<0 ){
        goto char_link_cleanup;
      }
      i = id_end+1;
    /* shortcut reference style link */
    }else{
      if( get_link_ref(rndr, link, title, data+1, txt_e-1)<0 ){
        goto char_link_cleanup;
      }
      /* rewinding an "inter-bracket-whitespace" */
      i = txt_e+1;
    }
  }
  /* building content: img alt is escaped, link content is parsed */
  if( txt_e>1 && content ){
    if( bImg ) blob_append(content, data+1, txt_e-1);
    else parse_inline(content, rndr, data+1, txt_e-1);
  }

  /* calling the relevant rendering function */
  if( bImg ){
    if( blob_size(ob)>0 && blob_buffer(ob)[blob_size(ob)-1]=='!' ){
      ob->nUsed--;
    }
    ret = rndr->make.image(ob, link, title, content, rndr->make.opaque);
  }else if( fn.nUsed ){
    if( rndr->make.footnote_ref ){
      ret = rndr->make.footnote_ref(ob, content, &fn.upc, fn.iMark,
                                    fn.nUsed, rndr->make.opaque);
    }
  }else{
    ret = rndr->make.link(ob, link, title, content, rndr->make.opaque);
  }

  /* cleanup */
char_link_cleanup:
  release_work_buffer(rndr, title);
  release_work_buffer(rndr, link);
  release_work_buffer(rndr, content);
  return ret ? i : 0;
}


/*********************************
 * BLOCK-LEVEL PARSING FUNCTIONS *
 *********************************/

/* is_empty -- returns the line length when it is empty, 0 otherwise */
static size_t is_empty(const char *data, size_t size){
  size_t i;
  for(i=0; i<size && data[i]!='\n'; i++){
    if( data[i]!=' ' && data[i]!='\t' ) return 0;
  }
  return i+1;
}


/* is_hrule -- returns whether a line is a horizontal rule */
static int is_hrule(char *data, size_t size){
  size_t i = 0, n = 0;
  char c;

  /* skipping initial spaces */
  if( size<3 ) return 0;
  if( data[0]==' ' ){
    i++;
    if( data[1]==' ' ){
      i++;
       if( data[2]==' ' ){
         i++;
       }
     }
   }

  /* looking at the hrule char */
  if( i+2>=size || (data[i]!='*' && data[i]!='-' && data[i]!='_') ) return 0;
  c = data[i];

  /* the whole line must be the char or whitespace */
  while (i < size && data[i] != '\n') {
    if( data[i]==c ){
      n += 1;
    }else if( data[i]!=' ' && data[i]!='\t' ){
      return 0;
    }
    i++;
  }

  return n>=3;
}


/* is_headerline -- returns whether the line is a setext-style hdr underline */
static int is_headerline(char *data, size_t size){
  size_t i = 0;

  /* test of level 1 header */
  if( data[i]=='=' ){
    for(i=1; i<size && data[i]=='='; i++);
    while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }
    return (i>=size || data[i]=='\n') ? 1 : 0;
  }

  /* test of level 2 header */
  if( data[i]=='-' ){
    for(i=1; i<size && data[i]=='-'; i++);
    while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }
    return (i>=size || data[i]=='\n') ? 2 : 0;
  }

  return 0;
}


/* is_table_sep -- returns whether there is a table separator at pos */
static int is_table_sep(char *data, size_t pos){
  return data[pos]=='|' && (pos==0 || data[pos-1]!='\\');
}


/* is_tableline -- returns the number of column tables in the given line */
static int is_tableline(char *data, size_t size){
  size_t i = 0;
  int n_sep = 0, outer_sep = 0;

  /* skip initial blanks */
  while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }

  /* check for initial '|' */
  if( i<size && data[i]=='|') outer_sep++;

  /* count the number of pipes in the line */
  for(n_sep=0; i<size && data[i]!='\n'; i++){
    if( is_table_sep(data, i) ) n_sep++;
    if( data[i]=='`' ){
      skip_codespan(data, size, &i);
      i--;
    }
  }

  /* march back to check for optional last '|' before blanks and EOL */
  while( i && (data[i-1]==' ' || data[i-1]=='\t' || data[i-1]=='\n') ){ i--; }
  if( i && is_table_sep(data, i-1) ) outer_sep += 1;

  /* return the number of column or 0 if it's not a table line */
  return (n_sep>0) ? (n_sep-outer_sep+1) : 0;
}


/* prefix_quote -- returns blockquote prefix length */
static size_t prefix_quote(char *data, size_t size){
  size_t i = 0;
  if( i<size && data[i]==' ' ) i++;
  if( i<size && data[i]==' ' ) i++;
  if( i<size && data[i]==' ' ) i++;
  if( i<size && data[i]=='>' ){
    if( i+1<size && (data[i+1]==' ' || data[i+1]=='\t') ){
      return i + 2;
    }else{
      return i + 1;
    }
  }else{
    return 0;
  }
}


/* prefix_code -- returns prefix length for block code */
static size_t prefix_code(char *data, size_t size){
  if( size>0 && data[0]=='\t' ) return 1;
  if( size>3 && data[0]==' ' && data[1]==' ' && data[2]==' ' && data[3]==' ' ){
    return 4;
  }
  return 0;
}

/* Return the number of characters in the delimiter of a fenced code
** block. */
static size_t prefix_fencedcode(char *data, size_t size){
  char c = data[0];
  int nb;
  if( c!='`' && c!='~' ) return 0;
  for(nb=1; nb<(int)size-3 && data[nb]==c; nb++){}
  if( nb<3 ) return 0;
  if( nb>=(int)size-nb ) return 0;
  return nb;
}

/* prefix_oli -- returns ordered list item prefix */
static size_t prefix_oli(char *data, size_t size){
  size_t i = 0;
  if( i<size && data[i]==' ') i++;
  if( i<size && data[i]==' ') i++;
  if( i<size && data[i]==' ') i++;

  if( i>=size || data[i]<'0' || data[i]>'9' ) return 0;
  while( i<size && data[i]>='0' && data[i]<='9' ){ i++; }

  if( i+1>=size
   || (data[i]!='.' && data[i]!=')')
   || (data[i+1]!=' ' && data[i+1]!='\t')
  ){
   return 0;
  }
  i = i+2;
  while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }
  return i;
}


/* prefix_uli -- returns ordered list item prefix */
static size_t prefix_uli(char *data, size_t size){
  size_t i = 0;
  if( i<size && data[i]==' ') i++;
  if( i<size && data[i]==' ') i++;
  if( i<size && data[i]==' ') i++;
  if( i+1>=size
   || (data[i]!='*' && data[i]!='+' && data[i]!='-')
   || (data[i+1]!=' ' && data[i+1]!='\t')
  ){
    return 0;
  }
  i = i+2;
  while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }
  return i;
}


/* parse_block predeclaration */
static void parse_block(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size);


/* parse_blockquote -- handles parsing of a blockquote fragment */
static size_t parse_blockquote(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size
){
  size_t beg, end = 0, pre, work_size = 0, nb, endFence = 0;
  char *work_data = 0;
  struct Blob *out = new_work_buffer(rndr);

  /* Check to see if this is a quote of a fenced code block, because
  ** if it is, then blank lines do not terminated the quoted text.  Ex:
  **
  **    > ~~~~
  **    First line
  **
  **    Line after blank
  **    ~~~~
  **
  **  If this is a quoted fenced block, then set endFence to be the
  **  offset of the end of the fenced block.
  */
  pre = prefix_quote(data,size);
  pre += is_empty(data+pre,size-pre);
  nb = prefix_fencedcode(data+pre,size-pre);
  if( nb ){
    size_t i = 0;
    char delim = data[pre];
    for(end=pre+nb; end<size && i<nb; end++){
      if( data[end]==delim ) i++; else i = 0;
    }
    if( i>=nb ) endFence = end;
  }

  beg = 0;
  while( beg<size ){
    for(end=beg+1; end<size && data[end-1]!='\n'; end++);
    pre = prefix_quote(data+beg, end-beg);
    if( pre ){
      beg += pre; /* skipping prefix */
    }else if( is_empty(data+beg, end-beg)
     && (end>=size
         || (end>endFence
             && prefix_quote(data+end, size-end)==0
             && !is_empty(data+end, size-end)))
    ){
      /* empty line followed by non-quote line */
      break;
    }
    if( beg<end ){ /* copy into the in-place working buffer */
      if( !work_data ){
        work_data = data+beg;
      }else if( (data+beg)!=(work_data+work_size) ){
        memmove(work_data+work_size, data+beg, end-beg);
      }
      work_size += end-beg;
    }
    beg = end;
  }

  if( rndr->make.blockquote ){
    if( !too_deep(rndr) ){
      parse_block(out, rndr, work_data, work_size);
    }else{
      blob_append(out, work_data, work_size);
    }
    rndr->make.blockquote(ob, out, rndr->make.opaque);
  }
  release_work_buffer(rndr, out);
  return end;
}


/* parse_paragraph -- handles parsing of a regular paragraph */
static size_t parse_paragraph(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size
){
  size_t i = 0, end = 0;
  int level = 0;
  char *work_data = data;
  size_t work_size = 0;

  while( i<size ){
    char *zEnd = memchr(data+i, '\n', size-i-1);
    end = zEnd==0 ? size : (size_t)(zEnd - (data-1));
    /* The above is the same as:
    **    for(end=i+1; end<size && data[end-1]!='\n'; end++);
    ** "end" is left with a value such that data[end] is one byte
    ** past the first '\n' or one byte past the end of the string */
    if( is_empty(data+i, size-i)
     || (level = is_headerline(data+i, size-i))!= 0
    ){
      break;
    }
    if( (i && data[i]=='#')
     || is_hrule(data+i, size-i)
     || prefix_uli(data+i, size-i)
     || prefix_oli(data+i, size-i)
    ){
      end = i;
      break;
    }
    i = end;
  }

  work_size = i;
  while( work_size && data[work_size-1]=='\n' ){ work_size--; }

  if( !level ){
    if( rndr->make.paragraph ){
      struct Blob *tmp = new_work_buffer(rndr);
      parse_inline(tmp, rndr, work_data, work_size);
      rndr->make.paragraph(ob, tmp, rndr->make.opaque);
      release_work_buffer(rndr, tmp);
    }
  }else{
    if( work_size ){
      size_t beg;
      i = work_size;
      work_size -= 1;
      while( work_size && data[work_size]!='\n' ){ work_size--; }
      beg = work_size+1;
      while( work_size && data[work_size-1]=='\n'){ work_size--; }
      if( work_size ){
        struct Blob *tmp = new_work_buffer(rndr);
        parse_inline(tmp, rndr, work_data, work_size);
        if( rndr->make.paragraph ){
          rndr->make.paragraph(ob, tmp, rndr->make.opaque);
        }
        release_work_buffer(rndr, tmp);
        work_data += beg;
        work_size = i - beg;
      }else{
        work_size = i;
      }
    }

    if( rndr->make.header ){
      struct Blob *span = new_work_buffer(rndr);
      parse_inline(span, rndr, work_data, work_size);
      rndr->make.header(ob, span, level, rndr->make.opaque);
      release_work_buffer(rndr, span);
    }
  }
  return end;
}


/* parse_blockcode -- handles parsing of a block-level code fragment */
static size_t parse_blockcode(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size
){
  size_t beg, end, pre;
  struct Blob *work = new_work_buffer(rndr);

  beg = 0;
  while( beg<size ){
    char *zEnd = memchr(data+beg, '\n', size-beg-1);
    end = zEnd==0 ? size : (size_t)(zEnd - (data-1));
    /* The above is the same as:
    **   for(end=beg+1; end<size && data[end-1]!='\n'; end++);
    ** "end" is left with a value such that data[end] is one byte
    ** past the first \n or past then end of the string. */
    pre = prefix_code(data+beg, end-beg);
    if( pre ){
      beg += pre; /* skipping prefix */
    }else if( !is_empty(data+beg, end-beg) ){
      /* non-empty non-prefixed line breaks the pre */
      break;
    }
    if( beg<end ){
      /* verbatim copy to the working buffer, escaping entities */
      if( is_empty(data + beg, end - beg) ){
        blob_append_char(work, '\n');
      }else{
        blob_append(work, data+beg, end-beg);
      }
    }
    beg = end;
  }

  end = blob_size(work);
  while( end>0 && blob_buffer(work)[end-1]=='\n' ){ end--; }
  work->nUsed = end;
  blob_append_char(work, '\n');

  if( work!=ob ){
    if( rndr->make.blockcode ){
      rndr->make.blockcode(ob, work, rndr->make.opaque);
    }
    release_work_buffer(rndr, work);
  }
  return beg;
}


/* parse_listitem -- parsing of a single list item */
/*  assuming initial prefix is already removed */
static size_t parse_listitem(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size,
  int *flags
){
  struct Blob *work = 0, *inter = 0;
  size_t beg = 0, end, pre, sublist = 0, orgpre = 0, i;
  int in_empty = 0, has_inside_empty = 0;

  /* keeping track of the first indentation prefix */
  if( size>1 && data[0]==' ' ){
    orgpre = 1;
    if( size>2 && data[1]==' ' ){
      orgpre = 2;
      if( size>3 && data[2]==' ' ){
        orgpre = 3;
      }
    }
   }
  beg = prefix_uli(data, size);
  if( !beg ) beg = prefix_oli(data, size);
  if( !beg ) return 0;
  /* skipping to the beginning of the following line */
  end = beg;
  while( end<size && data[end-1]!='\n' ){ end++; }

  /* getting working buffers */
  work = new_work_buffer(rndr);
  inter = new_work_buffer(rndr);

  /* putting the first line into the working buffer */
  blob_append(work, data+beg, end-beg);
  beg = end;

  /* process the following lines */
  while( beg<size ){
    end++;
    while( end<size && data[end-1]!='\n' ){ end++; }

    /* process an empty line */
    if( is_empty(data+beg, end-beg) ){
      in_empty = 1;
      beg = end;
      continue;
    }

    /* computing the indentation */
    i = 0;
    if( end-beg>1 && data[beg]==' ' ){
      i = 1;
      if( end-beg>2 && data[beg+1]==' ' ){
        i = 2;
        if( end-beg>3 && data[beg+2]==' ' ){
          i = 3;
          if( end-beg>3 && data[beg+3]==' ' ){
            i = 4;
          }
        }
      }
    }
    pre = i;
    if( data[beg]=='\t' ){ i = 1; pre = 8; }

    /* checking for a new item */
    if( (prefix_uli(data+beg+i, end-beg-i) && !is_hrule(data+beg+i, end-beg-i))
     || prefix_oli(data+beg+i, end-beg-i)
    ){
      if( in_empty ) has_inside_empty = 1;
      if( pre == orgpre ){ /* the following item must have */
        break;             /* the same indentation */
      }
      if( !sublist ) sublist = blob_size(work);

    /* joining only indented stuff after empty lines */
    }else if( in_empty && i<4 && data[beg]!='\t' ){
        *flags |= MKD_LI_END;
        break;
    }else if( in_empty ){
      blob_append_char(work, '\n');
      has_inside_empty = 1;
    }
    in_empty = 0;

    /* adding the line without prefix into the working buffer */
    blob_append(work, data+beg+i, end-beg-i);
    beg = end;
  }

  /* non-recursive fallback when working buffer stack is full */
  if( !inter ){
    if( rndr->make.listitem ){
      rndr->make.listitem(ob, work, *flags, rndr->make.opaque);
    }
    release_work_buffer(rndr, work);
    return beg;
  }

  /* render of li contents */
  if( has_inside_empty ) *flags |= MKD_LI_BLOCK;
  if( *flags & MKD_LI_BLOCK ){
    /* intermediate render of block li */
    if( sublist && sublist<blob_size(work) ){
      parse_block(inter, rndr, blob_buffer(work), sublist);
      parse_block(inter,
                  rndr,
                  blob_buffer(work)+sublist,
                  blob_size(work)-sublist);
    }else{
      parse_block(inter, rndr, blob_buffer(work), blob_size(work));
    }
  }else{
    /* intermediate render of inline li */
    if( sublist && sublist<blob_size(work) ){
      parse_inline(inter, rndr, blob_buffer(work), sublist);
      parse_block(inter,
                  rndr,
                  blob_buffer(work)+sublist,
                  blob_size(work)-sublist);
    }else{
      parse_inline(inter, rndr, blob_buffer(work), blob_size(work));
    }
  }

  /* render of li itself */
  if( rndr->make.listitem ){
    rndr->make.listitem(ob, inter, *flags, rndr->make.opaque);
  }
  release_work_buffer(rndr, inter);
  release_work_buffer(rndr, work);
  return beg;
}


/* parse_list -- parsing ordered or unordered list block */
static size_t parse_list(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size,
  int flags
){
  struct Blob *work = new_work_buffer(rndr);
  size_t i = 0, j;

  while( i<size ){
    j = parse_listitem(work, rndr, data+i, size-i, &flags);
    i += j;
    if( !j || (flags & MKD_LI_END) ) break;
  }

  if( rndr->make.list ) rndr->make.list(ob, work, flags, rndr->make.opaque);
  release_work_buffer(rndr, work);
  return i;
}


/* parse_atxheader -- parsing of atx-style headers */
static size_t parse_atxheader(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size
){
  int level = 0;
  size_t i, end, skip, span_beg, span_size;

  if( !size || data[0]!='#' ) return 0;

  while( level<(int)size && level<6 && data[level]=='#' ){ level++; }
  for(i=level; i<size && (data[i]==' ' || data[i]=='\t'); i++);
  if ( (int)i == level ) return parse_paragraph(ob, rndr, data, size);
  span_beg = i;

  for(end=i; end<size && data[end]!='\n'; end++);
  skip = end;
  if( end<=i ) return parse_paragraph(ob, rndr, data, size);
  while( end && data[end-1]=='#' ){ end--; }
  while( end && (data[end-1]==' ' || data[end-1]=='\t') ){ end--; }
  if( end<=i ) return parse_paragraph(ob, rndr, data, size);

  span_size = end-span_beg;
  if( rndr->make.header ){
    struct Blob *span = new_work_buffer(rndr);
    parse_inline(span, rndr, data+span_beg, span_size);
    rndr->make.header(ob, span, level, rndr->make.opaque);
    release_work_buffer(rndr, span);
  }
  return skip;
}


/* htmlblock_end -- checking end of HTML block : </tag>[ \t]*\n[ \t*]\n */
/*  returns the length on match, 0 otherwise */
static size_t htmlblock_end(
  const struct html_tag *tag,
  const char *data,
  size_t size
){
  size_t i, w;

  /* assuming data[0]=='<' && data[1]=='/' already tested */

  /* checking tag is a match */
  if( (tag->size+3)>(int)size
    || fossil_strnicmp(data+2, tag->text, tag->size)
    || data[tag->size+2]!='>'
  ){
    return 0;
  }

  /* checking white lines */
  i = tag->size + 3;
  w = 0;
  if( i<size && (w = is_empty(data+i, size-i))==0 ){
    return 0; /* non-blank after tag */
  }
  i += w;
  w = 0;

  if( i<size && (w = is_empty(data + i, size - i))==0 ){
    return 0; /* non-blank line after tag line */
  }
  return i+w;
}


/* parse_htmlblock -- parsing of inline HTML block */
static size_t parse_htmlblock(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size
){
  size_t i, j = 0;
  const struct html_tag *curtag;
  int found;
  size_t work_size = 0;
  struct Blob work = BLOB_INITIALIZER;

  /* identification of the opening tag */
  if( size<2 || data[0]!='<' ) return 0;
  curtag = find_block_tag(data+1, size-1);

  /* handling of special cases */
  if( !curtag ){

    /* HTML comment, laxist form */
    if( size>5 && data[1]=='!' && data[2]=='-' && data[3]=='-' ){
      i = 5;
      while( i<size && !(data[i-2]=='-' && data[i-1]=='-' && data[i]=='>') ){
        i++;
      }
      i++;
      if( i<size ){
        j = is_empty(data+i, size-i);
        if( j ){
          work_size = i+j;
          if( !rndr->make.blockhtml ) return work_size;
          blob_init(&work, data, work_size);
          rndr->make.blockhtml(ob, &work, rndr->make.opaque);
          return work_size;
        }
      }
    }

    /* HR, which is the only self-closing block tag considered */
    if( size>4
      && (data[1]=='h' || data[1]=='H')
      && (data[2]=='r' || data[2]=='R')
    ){
      i = 3;
      while( i<size && data[i]!='>' ){ i++; }
      if( i+1<size ){
        i += 1;
        j = is_empty(data+i, size-i);
        if( j ){
          work_size = i+j;
          if( !rndr->make.blockhtml ) return work_size;
          blob_init(&work, data, work_size);
          rndr->make.blockhtml(ob, &work, rndr->make.opaque);
          return work_size;
        }
      }
    }

    /* no special case recognised */
    return 0;
  }

  /* looking for an matching closing tag */
  /* followed by a blank line */
  i = 1;
  found = 0;
  while( i<size ){
    i++;
    while( i<size && !(data[i-1]=='<' && data[i]=='/') ){ i++; }
    if( (i+2+curtag->size)>size ) break;
    j = htmlblock_end(curtag, data+i-1, size-i+1);
    if (j) {
      i += j-1;
      found = 1;
      break;
    }
  }
  if( !found ) return 0;

  /* the end of the block has been found */
  if( strcmp(curtag->text,"html")==0 ){
    /* Omit <html> tags */
    enum mkd_autolink dummy;
    int k = tag_length(data, size, &dummy);
    int sz = i - (j+k);
    if( sz>0 ) blob_init(&work, data+k, sz);
  }else{
    blob_init(&work, data, i);
  }
  if( rndr->make.blockhtml ){
    rndr->make.blockhtml(ob, &work, rndr->make.opaque);
  }
  return i;
}


/* parse_table_cell -- parse a cell inside a table */
static void parse_table_cell(
  struct Blob *ob,     /* output blob */
  struct render *rndr, /* renderer description */
  char *data,          /* input text */
  size_t size,         /* input text size */
  int flags            /* table flags */
){
  struct Blob *span = new_work_buffer(rndr);
  parse_inline(span, rndr, data, size);
  rndr->make.table_cell(ob, span, flags, rndr->make.opaque);
  release_work_buffer(rndr, span);
}


/* parse_table_row -- parse an input line into a table row */
static size_t parse_table_row(
  struct Blob *ob,        /* output blob for rendering */
  struct render *rndr,    /* renderer description */
  char *data,             /* input text */
  size_t size,            /* input text size */
  int *aligns,            /* array of default alignment for columns */
  size_t align_size,      /* number of columns with default alignment */
  int flags               /* table flags */
){
  size_t i = 0, col = 0;
  size_t beg, end, total = 0;
  struct Blob *cells = new_work_buffer(rndr);
  int align;

  /* skip leading blanks and separator */
  while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }
  if( i<size && data[i]=='|' ) i++;

  /* go over all the cells */
  while( i<size && total==0 ){
    /* check optional left/center align marker */
    align = 0;
    if( data[i]==':' ){
      align |= MKD_CELL_ALIGN_LEFT;
      i++;
    }

    /* skip blanks */
    while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }
    beg = i;

    /* forward to the next separator or EOL */
    while( i<size && !is_table_sep(data, i) && data[i]!='\n' ){
      if( data[i]=='`' ){
        skip_codespan(data, size, &i);
      }else{
        i++;
      }
    }
    end = i;
    if( i<size ){
      i++;
      if( data[i-1]=='\n' ) total = i;
    }

    /* check optional right/center align marker */
    if( i>beg && data[end-1]==':' ){
      align |= MKD_CELL_ALIGN_RIGHT;
      end--;
    }

    /* remove trailing blanks */
    while( end>beg && (data[end-1]==' ' || data[end-1]=='\t') ){ end--; }

    /* skip the last cell if it was only blanks */
    /* (because it is only the optional end separator) */
    if( total && end<=beg ) continue;

    /* fallback on default alignment if not explicit */
    if( align==0 && aligns && col<align_size ) align = aligns[col];

    /* render cells */
    if( cells && end>=beg ){
      parse_table_cell(cells, rndr, data+beg, end-beg, align|flags);
    }

    col++;
  }

  /* render the whole row and clean up */
  rndr->make.table_row(ob, cells, flags, rndr->make.opaque);
  release_work_buffer(rndr, cells);
  return total ? total : size;
}


/* parse_table -- parsing of a whole table */
static size_t parse_table(
  struct Blob *ob,
  struct render *rndr,
  char *data,
  size_t size
){
  size_t i = 0, head_end, col;
  size_t align_size = 0;
  int *aligns = 0;
  struct Blob *head = 0;
  struct Blob *rows = new_work_buffer(rndr);

  /* skip the first (presumably header) line */
  while( i<size && data[i]!='\n' ){ i++; }
  head_end = i;

  /* fallback on end of input */
  if( i>=size ){
    parse_table_row(rows, rndr, data, size, 0, 0, 0);
    rndr->make.table(ob, 0, rows, rndr->make.opaque);
    release_work_buffer(rndr, rows);
    return i;
  }

  /* attempt to parse a table rule, i.e. blanks, dash, colons and sep */
  i++;
  col = 0;
  while( i<size
    && (data[i]==' '
      || data[i]=='\t'
      || data[i]=='-'
      || data[i] == ':'
      || data[i] =='|')
  ){
    if( data[i] == '|' ) align_size++;
    if( data[i] == ':' ) col = 1;
    i += 1;
  }

  if( i<size && data[i]=='\n' ){
    align_size++;

    /* render the header row */
    head = new_work_buffer(rndr);
    parse_table_row(head, rndr, data, head_end, 0, 0, MKD_CELL_HEAD);

    /* parse alignments if provided */
    if( col && (aligns=fossil_malloc(align_size * sizeof *aligns))!=0 ){
      for(i=0; i<align_size; i++) aligns[i] = 0;
      col = 0;
      i = head_end+1;

      /* skip initial white space and optional separator */
      while( i<size && (data[i]==' ' || data[i]=='\t') ){ i++; }
      if( data[i]=='|' ) i++;

      /* compute default alignment for each column */
      while (i < size && data[i] != '\n') {
        if (data[i] == ':')
          aligns[col] |= MKD_CELL_ALIGN_LEFT;
        while (i < size
        && data[i] != '|' && data[i] != '\n')
          i += 1;
        if (data[i - 1] == ':')
          aligns[col] |= MKD_CELL_ALIGN_RIGHT;
        if (i < size && data[i] == '|')
          i += 1;
        col += 1; }
    }

    /* point i to the beginning of next line/row */
    i++;

  }else{
    /* there is no valid ruler, continuing without header */
    i = 0;
  }

  /* render the table body lines */
  while( i<size && is_tableline(data + i, size - i) ){
    i += parse_table_row(rows, rndr, data+i, size-i, aligns, align_size, 0);
  }

  /* render the full table */
  rndr->make.table(ob, head, rows, rndr->make.opaque);

  /* cleanup */
  release_work_buffer(rndr, head);
  release_work_buffer(rndr, rows);
  fossil_free(aligns);
  return i;
}


/* parse_block -- parsing of one block, returning next char to parse */
static void parse_block(
  struct Blob *ob,        /* output blob */
  struct render *rndr,    /* renderer internal state */
  char *data,             /* input text */
  size_t size             /* input text size */
){
  size_t beg, end, i;
  char *txt_data;
  int has_table;
  if( !size ) return;
  has_table = (rndr->make.table
    && rndr->make.table_row
    && rndr->make.table_cell
    && memchr(data, '|', size)!=0);

  beg = 0;
  while( beg<size ){
    txt_data = data+beg;
    end = size-beg;
    if( data[beg]=='#' ){
      beg += parse_atxheader(ob, rndr, txt_data, end);
    }else if( data[beg]=='<'
      && rndr->make.blockhtml
      && (i = parse_htmlblock(ob, rndr, txt_data, end))!=0
    ){
      beg += i;
    }else if( (i=is_empty(txt_data, end))!=0 ){
      beg += i;
    }else if( is_hrule(txt_data, end) ){
      if( rndr->make.hrule ) rndr->make.hrule(ob, rndr->make.opaque);
      while( beg<size && data[beg]!='\n' ){ beg++; }
      beg++;
    }else if( prefix_quote(txt_data, end) ){
      beg += parse_blockquote(ob, rndr, txt_data, end);
    }else if( prefix_code(txt_data, end) ){
      beg += parse_blockcode(ob, rndr, txt_data, end);
    }else if( prefix_uli(txt_data, end) ){
      beg += parse_list(ob, rndr, txt_data, end, 0);
    }else if( prefix_oli(txt_data, end) ){
      beg += parse_list(ob, rndr, txt_data, end, MKD_LIST_ORDERED);
    }else if( has_table && is_tableline(txt_data, end) ){
      beg += parse_table(ob, rndr, txt_data, end);
    }else if( prefix_fencedcode(txt_data, end)
             && (i = char_codespan(ob, rndr, txt_data, 0, end))!=0
    ){
      beg += i;
    }else{
      beg += parse_paragraph(ob, rndr, txt_data, end);
    }
  }
}



/*********************
 * REFERENCE PARSING *
 *********************/

/* is_ref -- returns whether a line is a reference or not */
static int is_ref(
  const char *data,   /* input text */
  size_t beg,         /* offset of the beginning of the line */
  size_t end,         /* offset of the end of the text */
  size_t *last,       /* last character of the link */
  struct Blob *refs   /* array of link references */
){
  size_t i = 0;
  size_t id_offset, id_end;
  size_t link_offset, link_end;
  size_t title_offset, title_end;
  size_t line_end;
  struct link_ref lr = {
    BLOB_INITIALIZER,
    BLOB_INITIALIZER,
    BLOB_INITIALIZER
  };

  /* up to 3 optional leading spaces */
  if( beg+3>=end ) return 0;
  if( data[beg]==' ' ){
    i = 1;
    if( data[beg+1]==' ' ){
      i = 2;
      if( data[beg+2]==' ' ){
        i = 3;
        if( data[beg+3]==' ' ) return 0;
      }
    }
  }
  i += beg;

  /* id part: anything but a newline between brackets */
  if( data[i]!='[' ) return 0;
  i++;
  if( i>=end || data[i]=='^' ) return 0;  /* see is_footnote() */

  id_offset = i;
  while( i<end && data[i]!='\n' && data[i]!='\r' && data[i]!=']' ){ i++; }
  if( i>=end || data[i]!=']' ) return 0;
  id_end = i;

  /* spacer: colon (space | tab)* newline? (space | tab)* */
  i++;
  if( i>=end || data[i]!=':' ) return 0;
  i++;
  while( i<end && (data[i]==' ' || data[i]=='\t') ){ i++; }
  if( i<end && (data[i]=='\n' || data[i]=='\r') ){
    i++;
    if( i<end && data[i]=='\r' && data[i-1] == '\n' ) i++;
  }
  while( i<end && (data[i]==' ' || data[i]=='\t') ){ i++; }
  if( i>=end ) return 0;

  /* link: whitespace-free sequence, optionally between angle brackets */
  if( data[i]=='<' ) i++;
  link_offset = i;
  while( i<end
   && data[i]!=' '
   && data[i]!='\t'
   && data[i]!='\n'
   && data[i]!='\r'
  ){
    i += 1;
  }
  /* TODO: maybe require both data[i-1]=='>' && data[link_offset-1]=='<' ? */
  if( data[i-1]=='>' ) link_end = i-1; else link_end = i;

  /* optional spacer: (space | tab)* (newline | '\'' | '"' | '(' ) */
  while( i<end && (data[i]==' ' || data[i]=='\t') ){ i++; }
  if( i<end
   && data[i]!='\n'
   && data[i]!='\r'
   && data[i]!='\''
   && data[i]!='"'
   && data[i]!='('
  ){
    return 0;
  }
  line_end = 0;
  /* computing end-of-line */
  if( i>=end || data[i]=='\r' || data[i]=='\n' ) line_end = i;
  if( i+1<end && data[i]=='\n' && data[i+1]=='\r' ) line_end = i+1;

  /* optional (space|tab)* spacer after a newline */
  if( line_end ){
    i = line_end+1;
    while( i<end && (data[i]==' ' || data[i]=='\t') ){ i++; }
  }

  /* optional title: any non-newline sequence enclosed in '"()
          alone on its line */
  title_offset = title_end = 0;
  if( i+1<end && (data[i]=='\'' || data[i]=='"' || data[i]=='(') ){
    i += 1;
    title_offset = i;
    /* looking for EOL */
    while( i<end && data[i]!='\n' && data[i]!='\r' ){ i++; }
    if( i+1<end && data[i]=='\n' && data[i+1]=='\r' ){
      title_end = i + 1;
    }else{
      title_end = i;
    }
    /* stepping back */
    i--;
    while( i>title_offset && (data[i]==' ' || data[i]=='\t') ){ i--; }
    if( i>title_offset && (data[i]=='\'' || data[i]=='"' || data[i]==')') ){
      line_end = title_end;
      title_end = i;
    }
  }
  if( !line_end ) return 0; /* garbage after the link */

  /* a valid ref has been found, filling-in return structures */
  if( last ) *last = line_end;
  if( !refs ) return 1;
  if( build_ref_id(&lr.id, data+id_offset, id_end-id_offset)<0 ) return 0;
  blob_append(&lr.link, data+link_offset, link_end-link_offset);
  if( title_end>title_offset ){
    blob_append(&lr.title, data+title_offset, title_end-title_offset);
  }
  blob_append(refs, (char *)&lr, sizeof lr);
  return 1;
}

/*********************
 * FOOTNOTE PARSING  *
 *********************/

/* is_footnote -- check if data holds a definition of a labeled footnote.
 * If so then append the corresponding element to `footnotes` array */
static int is_footnote(
  const char *data,   /* input text */
  size_t beg,         /* offset of the beginning of the line */
  size_t end,         /* offset of the end of the text */
  size_t *last,       /* last character of the link */
  struct Blob * footnotes
){
  size_t i, id_offset, id_end, upc_offset, upc_size;
  struct footnote fn = FOOTNOTE_INITIALIZER;

  /* failfast if data is too short */
  if( beg+5>=end ) return 0;
  i = beg;

  /* footnote definition must start at the begining of a line */
  if( data[i]!='[' ) return 0;
  i++;
  if( data[i]!='^' ) return 0;
  id_offset = ++i;

  /* id part: anything but a newline between brackets */
  while( i<end && data[i]!=']' && data[i]!='\n' && data[i]!='\r' ){ i++; }
  if( i>=end || data[i]!=']' ) return 0;
  id_end = i++;

  /* spacer: colon (space | tab)* */
  if( i>=end || data[i]!=':' ) return 0;
  i++;
  while( i<end && (data[i]==' ' || data[i]=='\t') ){ i++; }

  /* passthrough truncated footnote definition */
  if( i>=end ) return 0;

  if( build_ref_id(&fn.id, data+id_offset, id_end-id_offset)<0 ) return 0;

  /* footnote's text may start on the same line after [^id]: */
  upc_offset = upc_size = 0;
  if( data[i]!='\n' && data[i]!='\r' ){
    size_t j;
    upc_size = is_footnote_classlist(data+i, end-i, 1);
    upc_offset = i; /* prevent further checks for a classlist */
    i += upc_size;
    j = i;
    while( i<end && data[i]!='\n' && data[i]!='\r' ){ i++; };
    if( i!=j )blob_append(&fn.text, data+j, i-j);
    if( i<end ){
      blob_append_char(&fn.text, data[i]);
      i++;
      if( i<end && data[i]=='\n' && data[i-1]=='\r' ){
        blob_append_char(&fn.text, data[i]);
        i++;
      }
    }
  }else{
    i++;
    if( i<end && data[i]=='\n' && data[i-1]=='\r' ) i++;
  }
  if( i<end ){

    /* compute the indentation from the 2nd line  */
    size_t indent = i;
    const char *spaces = data+i;
    while( indent<end && data[indent]==' ' ){ indent++; }
    if( indent>=end ) goto footnote_finish;
    indent -= i;
    if( indent<2 ) goto footnote_finish;

    /* process the 2nd and subsequent lines */
    while( i+indent<end && memcmp(data+i,spaces,indent)==0 ){
      size_t j;
      i += indent;
      if( !upc_offset ){
        /* a classlist must be provided no later than at the 2nd line */
        upc_offset = i + sizeof_blank_prefix(data+i, end-i, 1);
        upc_size = is_footnote_classlist(data+upc_offset,
                                         end-upc_offset, 1);
        if( upc_size ){
          i = upc_offset + upc_size;
        }
      }
      j = i;
      while( i<end && data[i]!='\n' && data[i]!='\r' ){ i++; }
      if( i!=j ) blob_append(&fn.text, data+j, i-j);
      if( i>=end ) break;
      blob_append_char(&fn.text, data[i]);
      i++;
      if( i<end && data[i]=='\n' && data[i-1]=='\r' ){
        blob_append_char(&fn.text, data[i]);
        i++;
      }
    }
  }
footnote_finish:
  if( !blob_size(&fn.text) ){
    blob_reset(&fn.id);
    return 0;
  }
  if( !blob_trim(&fn.text) ){  /* if the content is all-blank */
    if( upc_size ){            /* interpret UPC as plain text */
      blob_append(&fn.text, data+upc_offset, upc_size);
      upc_size = 0;
    }else{
      blob_reset(&fn.id);      /* or clean up and fail */
      blob_reset(&fn.text);
      return 0;
    }
  }
  /* a valid note has been found */
  if( last ) *last = i;
  if( footnotes ){
    fn.defno = COUNT_FOOTNOTES( footnotes );
    if( upc_size ){
      assert( upc_offset && upc_offset+upc_size<end );
      blob_append(&fn.upc, data+upc_offset, upc_size);
    }
    blob_append(footnotes, (char *)&fn, sizeof fn);
  }
  return 1;
}

/**********************
 * EXPORTED FUNCTIONS *
 **********************/

/* markdown -- parses the input buffer and renders it into the output buffer */
void markdown(
  struct Blob *ob,                   /* output blob for rendered text */
  const struct Blob *ib,             /* input blob in markdown */
  const struct mkd_renderer *rndrer  /* renderer descriptor (callbacks) */
){
  struct link_ref *lr;
  struct footnote *fn;
  int i;
  size_t beg, end = 0;
  struct render rndr;
  size_t size;
  Blob text = BLOB_INITIALIZER;        /* input after the first pass  */
  Blob * const allNotes = &rndr.notes.all;

  /* filling the render structure */
  if( !rndrer ) return;
  rndr.make = *rndrer;
  rndr.nBlobCache = 0;
  rndr.iDepth = 0;
  rndr.refs  = empty_blob;
  rndr.notes.all = empty_blob;
  rndr.notes.nMarks = 0;
  rndr.notes.misref.id    = empty_blob;
  rndr.notes.misref.text  = empty_blob;
  rndr.notes.misref.upc   = empty_blob;
  rndr.notes.misref.bRndred = 0;
  rndr.notes.misref.nUsed =  0;
  rndr.notes.misref.iMark = -1;

  for(i=0; i<256; i++) rndr.active_char[i] = 0;
  if( (rndr.make.emphasis
    || rndr.make.double_emphasis
    || rndr.make.triple_emphasis)
   && rndr.make.emph_chars
  ){
    for(i=0; rndr.make.emph_chars[i]; i++){
      rndr.active_char[(unsigned char)rndr.make.emph_chars[i]] = char_emphasis;
    }
  }
  if( rndr.make.codespan ) rndr.active_char['`'] = char_codespan;
  if( rndr.make.linebreak ) rndr.active_char['\n'] = char_linebreak;
  if( rndr.make.image || rndr.make.link ) rndr.active_char['['] = char_link;
  if( rndr.make.footnote_ref ) rndr.active_char['('] = char_footnote;
  rndr.active_char['<'] = char_langle_tag;
  rndr.active_char['\\'] = char_escape;
  rndr.active_char['&'] = char_entity;

  /* first pass: iterate over lines looking for references,
   * copying everything else into "text" */
  beg = 0;
  for(size = blob_size(ib); beg<size ;){
    const char* const data = blob_buffer(ib);
    if( is_ref(data, beg, size, &end, &rndr.refs) ){
      beg = end;
    }else if(is_footnote(data, beg, size, &end, &rndr.notes.all)){
      beg = end;
    }else{ /* skipping to the next line */
      end = beg;
      while( end<size && data[end]!='\n' && data[end]!='\r' ){
        end += 1;
      }
      /* adding the line body if present */
      if( end>beg ) blob_append(&text, data + beg, end - beg);
      while( end<size && (data[end]=='\n' || data[end]=='\r') ){
        /* add one \n per newline */
        if( data[end]=='\n' || (end+1<size && data[end+1]!='\n') ){
          blob_append_char(&text, '\n');
        }
        end += 1;
      }
      beg = end;
    }
  }

  /* sorting the reference array */
  if( blob_size(&rndr.refs) ){
    qsort(blob_buffer(&rndr.refs),
          blob_size(&rndr.refs)/sizeof(struct link_ref),
          sizeof(struct link_ref),
          cmp_link_ref_sort);
  }
  rndr.notes.nLbled = COUNT_FOOTNOTES( allNotes );

  /* sort footnotes by ID and join duplicates */
  if( rndr.notes.nLbled > 1 ){
    int nDups = 0;
    fn = CAST_AS_FOOTNOTES( allNotes );
    qsort(fn, rndr.notes.nLbled, sizeof(struct footnote), cmp_footnote_id);

    /* concatenate footnotes with equal labels */
    for(i=0; i<rndr.notes.nLbled ;){
      struct footnote *x = fn + i;
      int j = i+1;
      size_t k = blob_size(&x->text) + 64 + blob_size(&x->upc);
      while(j<rndr.notes.nLbled && !blob_compare(&x->id, &fn[j].id)){
        k += blob_size(&fn[j].text) + 10 + blob_size(&fn[j].upc);
        j++;
        nDups++;
      }
      if( i+1<j ){
        Blob list = empty_blob;
        blob_reserve(&list, k);
        /* must match _joined_footnote_indicator in html_footnote_item() */
        blob_append_literal(&list, "<ul class='fn-joined'>\n");
        for(k=i; (int)k<j; k++){
          struct footnote *y = fn + k;
          blob_append_literal(&list, "<li>");
          if( blob_size(&y->upc) ){
            blob_appendb(&list, &y->upc);
            blob_reset(&y->upc);
          }
          blob_appendb(&list, &y->text);
          blob_append_literal(&list, "</li>\n");

          /* free memory buffer */
          blob_reset(&y->text);
          if( (int)k!=i ) blob_reset(&y->id);
        }
        blob_append_literal(&list, "</ul>\n");
        x->text = list;
        g.ftntsIssues[2]++;
      }
      i = j;
    }
    if( nDups ){  /* clean rndr.notes.all from invalidated footnotes */
      const int n = rndr.notes.nLbled - nDups;
      struct Blob filtered = empty_blob;
      blob_reserve(&filtered, n*sizeof(struct footnote));
      for(i=0; i<rndr.notes.nLbled; i++){
        if( blob_size(&fn[i].id) ){
          blob_append(&filtered, (char*)(fn+i), sizeof(struct footnote));
        }
      }
      blob_reset( allNotes );
      rndr.notes.all = filtered;
      rndr.notes.nLbled = n;
      assert( (int)(COUNT_FOOTNOTES(allNotes)) == rndr.notes.nLbled );
    }
  }
  fn = CAST_AS_FOOTNOTES( allNotes );
  for(i=0; i<rndr.notes.nLbled; i++){
    fn[i].index = i;
  }
  assert( rndr.notes.nMarks==0 );

  /* second pass: actual rendering */
  if( rndr.make.prolog ) rndr.make.prolog(ob, rndr.make.opaque);
  parse_block(ob, &rndr, blob_buffer(&text), blob_size(&text));

  if( blob_size(allNotes) || rndr.notes.misref.nUsed ){

    /* Footnotes must be parsed for the correct discovery of (back)links */
    Blob *notes = new_work_buffer( &rndr );
    if( blob_size(allNotes) ){
      Blob *tmp   = new_work_buffer( &rndr );
      int nMarks = -1, maxDepth = 5;

      /* inline notes may get appended to rndr.notes.all while rendering */
      while(1){
        struct footnote *aNotes;
        const int N = COUNT_FOOTNOTES( allNotes );

        /* make a shallow copy of `allNotes` */
        blob_truncate(notes,0);
        blob_appendb(notes, allNotes);
        aNotes = CAST_AS_FOOTNOTES(notes);
        qsort(aNotes, N, sizeof(struct footnote), cmp_footnote_sort);

        if( --maxDepth < 0 || nMarks == rndr.notes.nMarks ) break;
        nMarks = rndr.notes.nMarks;

        for(i=0; i<N; i++){
          const int j = aNotes[i].index;
          struct footnote *x = CAST_AS_FOOTNOTES(allNotes) + j;
          assert( 0<=j && j<N );
          if( x->bRndred || !x->nUsed ) continue;
          assert( x->iMark > 0 );
          assert( blob_size(&x->text) );
          blob_truncate(tmp,0);

          /* `allNotes` may be altered and extended through this call */
          parse_inline(tmp, &rndr, blob_buffer(&x->text), blob_size(&x->text));

          x = CAST_AS_FOOTNOTES(allNotes) + j;
          blob_truncate(&x->text,0);
          blob_appendb(&x->text, tmp);
          x->bRndred = 1;
        }
      }
      release_work_buffer(&rndr,tmp);
    }

    /* footnotes rendering */
    if( rndr.make.footnote_item && rndr.make.footnotes ){
      Blob *all_items = new_work_buffer(&rndr);
      int j = -1;

      /* Assert that the in-memory layout of id, text and upc within
      ** footnote struct matches the expectations of html_footnote_item()
      ** If it doesn't then a compiler has done something very weird.
      */
      assert( &(rndr.notes.misref.id)  == &(rndr.notes.misref.text) - 1 );
      assert( &(rndr.notes.misref.upc) == &(rndr.notes.misref.text) + 1 );

      for(i=0; i<(int)(COUNT_FOOTNOTES(notes)); i++){
        const struct footnote* x = CAST_AS_FOOTNOTES(notes) + i;
        const int xUsed = x->bRndred ? x->nUsed : 0;
        if( !x->iMark ) break;
        assert( x->nUsed );
        rndr.make.footnote_item(all_items, &x->text, x->iMark,
                                     xUsed, rndr.make.opaque);
        if( !xUsed ) g.ftntsIssues[3]++;  /* an overnested footnote */
        j = i;
      }
      if( rndr.notes.misref.nUsed ){
        rndr.make.footnote_item(all_items, 0, -1,
                    rndr.notes.misref.nUsed, rndr.make.opaque);
        g.ftntsIssues[0] += rndr.notes.misref.nUsed;
      }
      while( ++j < (int)(COUNT_FOOTNOTES(notes)) ){
        const struct footnote* x = CAST_AS_FOOTNOTES(notes) + j;
        assert( !x->iMark );
        assert( !x->nUsed );
        assert( !x->bRndred );
        rndr.make.footnote_item(all_items,&x->text,0,0,rndr.make.opaque);
        g.ftntsIssues[1]++;
      }
      rndr.make.footnotes(ob, all_items, rndr.make.opaque);
      release_work_buffer(&rndr, all_items);
    }
    release_work_buffer(&rndr, notes);
  }
  if( rndr.make.epilog ) rndr.make.epilog(ob, rndr.make.opaque);

  /* clean-up */
  assert( rndr.iDepth==0 );
  blob_reset(&text);
  lr = (struct link_ref *)blob_buffer(&rndr.refs);
  end = blob_size(&rndr.refs)/sizeof(struct link_ref);
  for(i=0; i<(int)end; i++){
    blob_reset(&lr[i].id);
    blob_reset(&lr[i].link);
    blob_reset(&lr[i].title);
  }
  blob_reset(&rndr.refs);
  fn = CAST_AS_FOOTNOTES( allNotes );
  end = COUNT_FOOTNOTES( allNotes );
  for(i=0; i<(int)end; i++){
    if(blob_size(&fn[i].id)) blob_reset(&fn[i].id);
    if(blob_size(&fn[i].upc)) blob_reset(&fn[i].upc);
    blob_reset(&fn[i].text);
  }
  blob_reset(&rndr.notes.all);
  for(i=0; i<rndr.nBlobCache; i++){
    fossil_free(rndr.aBlobCache[i]);
  }
}
