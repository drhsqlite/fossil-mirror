/*
** Copyright (c) 2009 Robert Ledger
**
** {{{ License
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
**   robert@pytrash.co.uk
**   http://pytrash.co.uk
**}}}
*******************************************************************************
**
** This file contains code to render creole 1.0 formated text as html.
*/
#include <assert.h>
#include "config.h"
#include "creoleparser.h"

#if INTERFACE
#define HAVE_CREOLE_MACRO 1
#endif

//{{{ LOCAL INTERFACE
#if LOCAL_INTERFACE

#define POOL_CHUNK_SIZE 100

//{{{ KIND
#define KIND_ROOT            0x0000001
#define KIND_HORIZONTAL_RULE 0x0000002
#define KIND_HEADING         0x0000004
#define KIND_ORDERED_LIST    0x0000008

#define KIND_UNORDERED_LIST  0x0000010
#define KIND_PARAGRAPH       0x0000020
#define KIND_TABLE           0x0000040
#define KIND_NO_WIKI_BLOCK   0x0000080

#define KIND_PARA_BREAK      0x0000100
#define KIND_END_WIKI_MARKER 0x0000200

#define KIND_BOLD            0x0000400
#define KIND_ITALIC          0x0000800
#define KIND_SUPERSCRIPT     0x0001000
#define KIND_SUBSCRIPT       0x0002000
#define KIND_MONOSPACED      0x0004000
#define KIND_BREAK           0x0008000

#define KIND_TABLE_ROW       0x0010000
#define KIND_MACRO           0X0020000
//}}}
//{{{ MACRO
#define MACRO_NONE           0X0000000
#define MACRO_FOSSIL         0x0000001
#define MACRO_WIKI_CONTENTS  0X0000002
//}}}
//{{{ FLAG
// keep first four bits free (why?:)
#define FLAG_CENTER      0x0000100
#define FLAG_MACRO_BLOCK 0X0000200
//}}}
struct Node {//{{{

  char *start;
  char *end;

  int kind;
  int level;
  int flags;

  Node *parent;
  Node *next;
  Node *children;

};
//}}}
struct NodePool {//{{{
  NodePool *next;
  Node a[POOL_CHUNK_SIZE];
}
//}}}
struct Parser {//{{{

  Blob *pOut;                 /* Output appended to this blob */
  Renderer *r;

  NodePool *pool;
  int nFree;

  Node *this;
  Node *previous;
  Node *list;

  char *cursor;

  int lineWasBlank;
  int charCount;

  Node *item;
  Node *istack;
  char *icursor;
  char *iend;

  int inLink;
  int inTable;
  int iesc;

  Blob *iblob;

};
//}}}

#endif

const int KIND_LIST = (KIND_UNORDERED_LIST | KIND_ORDERED_LIST);
const int KIND_LIST_OR_PARAGRAPH = (KIND_PARAGRAPH | KIND_UNORDERED_LIST | KIND_ORDERED_LIST);
//}}}

//{{{ POOL MANAGEMENT
static Node *pool_new(Parser *p){

  if ( p->pool == NULL || p->nFree == 0){

    NodePool *temp = p->pool;

    p->pool = malloc(sizeof(NodePool));
    if( p->pool == NULL ) fossil_panic("out of memory");

    p->pool->next = temp;
    p->nFree = POOL_CHUNK_SIZE;
  }
  p->nFree -= 1;
  Node *node = &(p->pool->a[p->nFree]);
  memset(node, 0, sizeof(*node));

  return node;
}


static void pool_free(Parser *p){

  NodePool *temp;

  while (p->pool != NULL){
    temp = p->pool;
    p->pool = temp->next;
    free(temp);
  }

}
//}}}

//{{{ Utility Methods

static char *cr_skipBlanks(Parser *p, char* z){//{{{
  char *s = z;
  while (z[0] == ' ' || z[0] == '\t') z++;
  p->charCount = z - s;
  return z;
}
//}}}
static int cr_countBlanks(Parser *p, char* z){//{{{
  cr_skipBlanks(p, z);
  return p->charCount;
}
//}}}
static char *cr_skipChars(Parser *p, char *z, char c){//{{{
  char *s = z;
  while (z[0] == c) z++;
  p->charCount = z - s;
  return z;
}
//}}}
static int cr_countChars(Parser *p, char *z, char c){//{{{
  cr_skipChars(p, z, c);
  return p->charCount;
}
//}}}
static char *cr_nextLine(Parser *p, char *z){//{{{

  p->lineWasBlank = 1;

  while (1){

    switch (z[0]){

      case '\r':
        if (z[1] == '\n') {
          z[0] = ' ';
          return z + 2;
        }
        z[0] = '\n';
        return z + 1;

      case '\n':
        return z + 1;

      case '\t':
        z[0] = ' ';
        z++;
        break;

      case ' ':
        z++;
        break;

      case '\0':
        return z;

      default:
        p->lineWasBlank = 0;
        z++;
    }
  }
}
//}}}
//}}}


//{{{ INLINE PARSER

static int cr_isEsc(Parser *p){//{{{
  if (p->iesc){
    blob_append(p->iblob, p->icursor, 1);
    p->iesc = 0;
    p->icursor += 1;
    return 1;
  }
  return 0;
}
//}}}
static int cr_iOpen(Parser *p, int kind){//{{{

  switch (kind){

    case KIND_BOLD:
      blob_append(p->iblob, "<strong>", 8);
      return 1;

    case KIND_ITALIC:
      blob_append(p->iblob, "<em>", 4);
      return 1;

    case KIND_SUPERSCRIPT:
      blob_append(p->iblob, "<sup>", 5);
      return 1;

    case KIND_SUBSCRIPT:
      blob_append(p->iblob, "<sub>", 5);
      return 1;

    case KIND_MONOSPACED:
      blob_append(p->iblob, "<tt>", 4);
      return 1;
  }
  return 0;
}
//}}}
static int cr_iClose(Parser *p, int kind){//{{{

  switch (kind){

    case KIND_BOLD:
      blob_append(p->iblob, "</strong>", 9);
      return 1;

    case KIND_ITALIC:
      blob_append(p->iblob, "</em>", 5);
      return 1;

    case KIND_SUPERSCRIPT:
      blob_append(p->iblob, "</sup>", 6);
      return 1;

    case KIND_SUBSCRIPT:
      blob_append(p->iblob, "</sub>", 6);
      return 1;

    case KIND_MONOSPACED:
      blob_append(p->iblob, "</tt>", 5);
      return 1;
  }
  return 0;
}
//}}}


static void cr_iMarkup(Parser *p, int kind){//{{{

  if (p->iesc) {
    blob_append(p->iblob, p->icursor, 1);
    p->icursor +=1;
    p->iesc =0;
    return;
  }

  if (p->icursor[1] != p->icursor[0]) {
    blob_append(p->iblob, p->icursor, 1);
    p->icursor +=1;
    return;
  }

  p->icursor += 2;

  if (kind & KIND_BREAK) {
      blob_append(p->iblob, "<br />", 6);
      return;
  }

  if (kind & KIND_ITALIC && p->icursor[-3] == ':'){
        blob_append(p->iblob, "//", 2);
        return;
  }

  Node *n = p->istack;

  int found = 0;
  while (n) {
    if (n->kind & kind) {
      found = 1;
      break;
    }
    n = n->next;
  }

  if (!found) {
    n = pool_new(p);
    n->kind = kind;
    n->next = p->istack;
    p->istack = n;

    assert(cr_iOpen(p, kind));
    return;
  };

  n= p->istack;
  while (n){
    p->istack = n->next;

    assert(cr_iClose(p, n->kind));

    if (kind == n->kind) return;
    n = p->istack;
  }
}
//}}}
static int cr_iNoWiki(Parser *p){//{{{

  if ((p->iend - p->icursor)<6) return 0;

  if (p->icursor[1]!='{' || p->icursor[2]!='{')
    return 0;

  char *s = p->icursor + 3;

  int count = p->iend - p->icursor - 3;
  while (count--){
    if (s[0]=='}' && s[1]=='}' && s[2]=='}' && s[3]!='}'){
      blob_appendf(p->iblob, "<tt class='creole-inline-nowiki'>%s</tt>", htmlize(p->icursor + 3, s - p->icursor-3));
      p->icursor = s + 3;
      return 1;
    }
    s++;
  }
  return 0;
}

//}}}
static int cr_iImage(Parser *p){//{{{

  if (p->inLink) return 0;
  if ((p->iend - p->icursor)<3) return 0;

  if (p->icursor[1]!='{') return 0;

  char *s = p->icursor + 2;
  char *bar = NULL;

  int count = p->iend - p->icursor - 4;
  while (count--){
    if (s[0]=='}' && s[1]=='}'){
      if (!bar) bar = p->icursor + 2;
      blob_appendf(p->iblob, "<span class='creole-noimage'>%s</span>", htmlize(bar, s - bar ));
      p->icursor = s + 2;
      return 1;
    }
    if (!bar && s[0]=='|') bar=s+1;
    s++;
  }
  return 0;
}
//}}}
static int cr_iMacro(Parser *p){//{{{

  if (p->inLink) return 0;
  if ((p->iend - p->icursor)<3) return 0;

  if (p->icursor[1]!='<') return 0;

  char *s = p->icursor + 2;

  int count = p->iend - p->icursor - 3;
  while (count--){
   if (s[0]=='>' && s[1]=='>'){
      blob_appendf(p->iblob, "<span class='creole-nomacro'>%s</span>", htmlize(p->icursor, s - p->icursor + 2));
      p->icursor = s + 2;
      return 1;
    }
    s++;
  }
  return 0;

}
//}}}

static void cr_renderLink(Parser *p, char *s, char *bar, char *e){//{{{

  int tsize = bar-s;
  int dsize = e - bar-1;

  if (tsize < 1) return;
  if (dsize < 1) dsize = 0;

  char zTarget[tsize + 1];
  memcpy(zTarget, s, tsize);
  zTarget[tsize] = '\0';

  char zClose[20];

  Blob *pOut = p->r->pOut;

  p->r->pOut = p->iblob;
  wf_openHyperlink(p->r, zTarget, zClose, sizeof(zClose));
  p->r->pOut = pOut;

  if (dsize)
    cr_parseInline(p, bar+1, e) ;
  else
    blob_append(p->iblob, htmlize(s, tsize), -1);
  blob_append(p->iblob, zClose, -1);
}
//}}}

static int cr_iLink(Parser *p){//{{{

  if (p->inLink) return 0;
  if ((p->iend - p->icursor)<3) return 0;

  if (p->icursor[1]!='[') return 0;

  char *s = p->icursor + 2;
  char *bar = NULL;

  int count = p->iend - p->icursor -3;
  while (count--){
    if (s[0]==']' && s[1]==']'){
      if (!bar) bar = s;
      p->inLink = 1;
      cr_renderLink(p, p->icursor+2, bar, s);
      p->inLink = 0;
      p->icursor = s + 2;
      return 1;
    }
    if (!bar && s[0]=='|') bar=s;
    s++;
  }
  return 0;
}
//}}}

LOCAL char *cr_parseInline(Parser *p, char *s, char *e){//{{{

  int save_iesc = p->iesc;
  char *save_iend = p->iend;
  Node *save_istack = p->istack;

  p->iesc = 0;
  p->iend = e;
  p->istack = NULL;

  p->icursor = s;

  char *eof = NULL;
  while (!eof &&  p->icursor < p->iend ){

    switch (*p->icursor) {//{{{

      case '~':
        if (p->iesc) {
          blob_append(p->iblob, "~", 1);
          p->iesc = 0;
        }
        p->iesc = !p->iesc;
        p->icursor+=1;
        break;

      case '*':
        cr_iMarkup(p, KIND_BOLD);
        break;

      case '/':
        cr_iMarkup(p, KIND_ITALIC);
        break;

      case '^':
        cr_iMarkup(p, KIND_SUPERSCRIPT);
        break;

      case ',':
        cr_iMarkup(p, KIND_SUBSCRIPT);
        break;

      case '#':
        cr_iMarkup(p, KIND_MONOSPACED);
        break;

      case '\\':
        cr_iMarkup(p, KIND_BREAK);
        break;

      case '{':
        if (cr_isEsc(p)) break;
        if (cr_iNoWiki(p)) break;
        if (cr_iImage(p)) break;
        blob_append(p->iblob, p->icursor, 1);
        p->icursor += 1;
        break;

      case '[':
        if (cr_isEsc(p)) break;
        if (cr_iLink(p)) break;
        blob_append(p->iblob, p->icursor, 1);
        p->icursor += 1;
        break;


      case '<':
        if (cr_isEsc(p)) break;
        if (cr_iMacro(p)) break;

        blob_append(p->iblob, "&lt;", 4);
        p->icursor += 1;
        break;

      case '>':
        if (p->iesc) {
          blob_append(p->iblob, "~", 1);
          p->iesc = 0;
        }
        blob_append(p->iblob, "&gt;", 4);
        p->icursor += 1;
        break;

      case '&':
        if (p->iesc) {
          blob_append(p->iblob, "~", 1);
          p->iesc = 0;
        }
        blob_append(p->iblob, "&amp;", 5);
        p->icursor += 1;
        break;

      case '|':
        if (p->inTable){
          if (p->iesc) {
            blob_append(p->iblob, p->icursor, 1);
            p->iesc = 0;
            p->icursor += 1;
            break;
          }
          eof = p->icursor + 1;
          break;
        }
        // fall through to default

      default:
        if (p->iesc) {
          blob_append(p->iblob, "~", 1);
          p->iesc = 0;
        }
        blob_append(p->iblob, p->icursor, 1);
        p->icursor +=1;
    }//}}}

  }

  while (p->istack){
    cr_iClose(p, p->istack->kind);
    p->istack = p->istack->next;
  }

  p->iesc = save_iesc;
  p->iend = save_iend;
  p->istack = save_istack;

  return eof;

}
//}}}
//}}}

//{{{ BLOCK PARSER

static void cr_renderListItem(Parser *p, Node *n){//{{{


  blob_append(p->iblob, "<li>", 4);
  cr_parseInline(p, n->start, n->end);

  if (n->children){

    int ord = (n->children->kind & KIND_ORDERED_LIST);

    if (ord)   blob_append(p->iblob, "<ol>", 4);
    else       blob_append(p->iblob, "<ul>", 4);

    n = n->children;
    while (n){
      cr_renderListItem(p, n);
      n = n->next;
    }

    if (ord)   blob_append(p->iblob, "</ol>", 5);
    else       blob_append(p->iblob, "</ul>", 5);
  }
  blob_append(p->iblob, "</li>", 5);
}
//}}}
static void cr_renderList(Parser *p){//{{{

  Node *n = p->list;

  while (n->parent !=n)  n = n->parent;

  int ord = (n->kind & KIND_ORDERED_LIST);

  if (ord)   blob_append(p->iblob, "\n\n<ol>", -1);
  else       blob_append(p->iblob, "\n\n<ul>", -1);

  while (n) {
    cr_renderListItem(p, n);
    n = n->next;
  }

  if (ord)   blob_append(p->iblob, "</ol>", 5);
  else       blob_append(p->iblob, "</ul>", 5);
}

//}}}

static void cr_renderTableRow(Parser *p, Node *row){//{{{

  char *s = row->start;
  int th;

  blob_append(p->iblob, "\n<tr>", -1);

  while (s && s < row->end){

    if ((th = *s == '=')) {
      s++;
      blob_append(p->iblob, "<th>", -1);
    }
    else {
      blob_append(p->iblob, "<td>", -1);
    }

    s = cr_parseInline(p, s, row->end);

    if (th)
      blob_append(p->iblob, "</th>\n", -1);
    else
      blob_append(p->iblob, "</td>\n", -1);

    if (!s) break;
  }
  blob_append(p->iblob, "</tr>", 5);
}
//}}}
static void cr_renderTable(Parser *p, Node *n){//{{{

  Node *row = n->children;

  blob_append(p->iblob, "<table class='creoletable'>", -1);
  p->inTable = 1;
  while (row){

    cr_renderTableRow(p, row);
    row = row->next;

  }
  blob_append(p->iblob, "</table>", -1);
  p->inTable = 0;

}
//}}}

static void cr_renderMacro(Parser *p, Node *n){//{{{

  switch (n->level){

    case MACRO_WIKI_CONTENTS:
      do_macro_wiki_contents(p, n);
      break;

  }

}
//}}}

static void cr_render(Parser *p, Node *node){//{{{

  if (node->kind & KIND_PARAGRAPH){
    blob_append(p->iblob,   "\n<p>", -1);
    cr_parseInline(p, node->start, node->end );
    blob_append(p->iblob, "</p>\n", -1  );
  }

  if (node->kind & KIND_HEADING){
    blob_appendf(p->iblob,
        "\n<h%d %s>",
        node->level,
        (node->flags & FLAG_CENTER) ? " style='text-align:center;'" : ""
    );
    cr_parseInline(p, node->start, node->end);
    blob_appendf(p->iblob, "</h%d>\n", node->level  );
    return;
  }

  if (node->kind & KIND_MACRO){
    cr_renderMacro(p, node);
    return;
  }

  if (node->kind & KIND_HORIZONTAL_RULE){
    blob_append(p->iblob, "<hr />", -1);
    return;
  }

  if (node->kind & KIND_LIST){
    cr_renderList(p);
    p->list = NULL;
    return;
  }

  if (node->kind & KIND_TABLE){
    cr_renderTable(p, node);
    return;
  }

  if (node->kind & KIND_NO_WIKI_BLOCK){
    blob_appendf(p->iblob,
      "\n<pre class='creole-block-nowiki'>%s</pre>\n",
        htmlize( node->start, node->end - node->start)
    );
  }
}
//}}}

static char *cr_findEndOfBlock(Parser *p, char *s, char c){//{{{

  char *end;
  while (s[0]){

    end = s;
    if (s[0] == c && s[0] == c && s[0] == c) {
      s = cr_nextLine(p, s + 3);
      if (p->lineWasBlank) {
          p->cursor = s;
          return end;
      }
    }
    else {
      s = cr_nextLine(p, s);
    }
  }
  return 0;
}
//}}}
static int cr_addListItem(Parser *p, Node *n){//{{{

  n->parent = n;
  n->next = n->children = NULL;

  if (!p->list) {
    if (n->level != 1) return 0;
    p->list = n;
    return 1;
  }

  Node *list = p->list;

  while (n->level < list->level){
    list = list->parent;
  }

  if (n->level == list->level){

    if (n->kind != list->kind){
      if (n->level>1) return 0;
      cr_renderList(p);
      p->list = n;
      return 1;
    }
    n->parent = list->parent;
    p->list = list->next = n;
    return 1;
  }

  if ( (n->level - list->level) > 1 ) return 0;
  n->parent = p->list;
  p->list->children = n;
  p->list = n;
  return 1;

}
//}}}

static int isEndWikiMarker(Parser *p){//{{{

  char *s = p->cursor;
  if (memcmp(s, "<<fossil>>", 10)) return 0;
  p->this->start = s;
  p->this->kind = KIND_END_WIKI_MARKER;
  p->cursor += 10;
  return 1;
}
//}}}
static int isNoWikiBlock(Parser *p){//{{{

  char *s = p->cursor;

  if (s[0] != '{') return 0; s++;
  if (s[0] != '{') return 0; s++;
  if (s[0] != '{') return 0; s++;

  s = cr_nextLine(p, s);
  if (!p->lineWasBlank) return 0;

  p->this->start = s;

  s = cr_findEndOfBlock(p, s, '}');

  if (!s) return 0;

  // p->cursor was set by findEndOfBlock
  p->this->kind = KIND_NO_WIKI_BLOCK;
  p->this->end = s;
  return 1;
}

//}}}
static int isParaBreak(Parser *p){//{{{

  char *s = cr_nextLine(p, p->cursor);
  if (!p->lineWasBlank) return 0;

  p->cursor = s;
  p->this->kind = KIND_PARA_BREAK;
  return 1;
}
//}}}
static int isMacro(Parser *p){//{{{

  char *s = p->cursor;
  int macroId;
  int matchLength;

  if (s[0]!='<') return 0; s++;
  if (s[0]!='<') return 0; s++;
  if (s[0]=='<') return 0;

  matchLength = cr_has_macro(s, &macroId);
  if (!matchLength) return 0;

  s += matchLength;
  p->this->start = s;

  if (s[-1]!='>'){
    while (s[0] && s[1] && s[0]!='\n' && !(s[0]=='>' && s[1]=='>')) s++;
    if (!(s[0] == '>' && s[1] == '>')) return 0;
    s +=2;
  }
  p->cursor = s;
  p->this->kind = KIND_MACRO;
  p->this->level = macroId;
  p->this->flags &= FLAG_MACRO_BLOCK;
  p->this->end = s-2;
  return 1;
}
//}}}
static int isHeading(Parser *p){//{{{

  char *s = cr_skipBlanks(p, p->cursor);

  int flags = 0;
  int level = cr_countChars(p, s, '=');
  if (!level) return 0;

  s += level;

  if (s[0] == '<' && s[1] == '>') {
    flags |= FLAG_CENTER;
    s += 2;
  }
  s = cr_skipBlanks(p, s);

  p->this->start = s;

  s = cr_nextLine(p, s);
  char *z = s;

  if (s[-1] == '\n') s--;
  while(s[-1] == ' ' || s[-1]=='\t') s--;
  while(s[-1] == '=' ) s--;
  if (p->this->start < s){
    p->cursor = z;
    p->this->kind = KIND_HEADING;
    p->this->end = s;
    p->this->level = level;
    p->this->flags |= flags;
    return 1;
  }
  return 0;
}
//}}}
static int isHorizontalRule(Parser *p){//{{{

  char *s = cr_skipBlanks(p, p->cursor);

  int level = cr_countChars(p, s, '-');

  if  (level < 4) return 0;
  s = cr_nextLine(p, s + level);
  if (!p->lineWasBlank) return 0;

  p->cursor = s;
  p->this->kind = KIND_HORIZONTAL_RULE;

  return 1;
}
//}}}
static int isListItem(Parser *p){//{{{

  char *s = cr_skipBlanks(p, p->cursor);

  int level = cr_countChars(p, s, '#');
  if (!level) level = cr_countChars(p, s, '*');

  if ( !level) return 0;

  p->this->kind = (s[0] == '#') ? KIND_ORDERED_LIST : KIND_UNORDERED_LIST;
  p->this->level = level;

  s = cr_skipBlanks(p, s + level);
  p->this->start = s;

  s = cr_nextLine(p, s);
  if (p->lineWasBlank) return 0;

  if (cr_addListItem(p, p->this)){
    p->cursor = p->this->end = s;
    return 1;
  }
  p->this->kind = 0;
  return 0;
}
//}}}
static int isTable(Parser *p){//{{{

  p->this->start = p->cursor;
  char *s = cr_skipBlanks(p, p->cursor);
  if (s[0] != '|') return 0;
  s +=1;
  p->this->kind = KIND_TABLE;


  //p->cursor =   p->this->end = cr_nextLine(p, s);
  Node *row;
  Node *tail = NULL;

  while (1) {

    row = pool_new(p);
    row->kind = KIND_TABLE_ROW;

    if (tail)   tail = tail->next = row;
    else p->this->children = tail = row;

    row->start = s;
    p->cursor = s =   row->end = p->this->end = cr_nextLine(p, s);

    if (row->end[-1] == '\n') row->end -= 1;
    while(row->end[-1] == ' ' ) row->end -= 1;
    if (row->end[-1] == '|') row->end -= 1;

    if (!*s) break;

    // blanks *not* normalized
    s = cr_skipBlanks(p, p->cursor);
    if (s[0] != '|') break;
    s++;

  }
  return 1;

};
//}}}
static int isParagraph(Parser *p){//{{{

  char *s = p->cursor;
  p->this->start = s;

  s = cr_nextLine(p, s);
  p->cursor = p->this->end = s;
  p->this->kind = KIND_PARAGRAPH;
  return 1;

}
//}}}

static void cr_parse(Parser *p, char* z){//{{{

  p->previous = pool_new(p);
  p->previous->kind = KIND_PARA_BREAK;

  p->this = pool_new(p);
  p->this->kind = KIND_PARA_BREAK;

  p->inLink = 0;
  p->inTable = 0;

  p->cursor = z;
  p->list = NULL;
  p->istack = NULL;

  while (p->cursor[0]) {

    while (1){

      // must be first
      if (isNoWikiBlock(p)) break;
      if (isParaBreak(p))   break;

      // order not important
      if (isMacro(p)) break;
      if (isHeading(p)) break;
      if (isHorizontalRule(p)) break;
      if (isListItem(p)) break;
      if (isTable(p)) break;

      // here for efficiency?
      if (isEndWikiMarker(p)) break;

      // must be last
      if (isParagraph(p)); break;

      // doh!
      assert(0);
    }

    int kind = p->this->kind;
    int prev = p->previous->kind;

    if (kind & KIND_END_WIKI_MARKER)  return;

    if (kind == KIND_PARAGRAPH && prev & KIND_LIST_OR_PARAGRAPH) {
        p->previous->end = p->this->end;
        p->this = pool_new(p);
        continue;
    }

    if ( !(kind & KIND_LIST && prev & KIND_LIST) )
      cr_render(p, p->previous);

    p->previous = p->this;
    p->this = pool_new(p);

  }
}
//}}}

//}}}

//{{{ MACROS
LOCAL void do_macro_wiki_contents(Parser *p, Node *n){//{{{

  Stmt q;

  blob_append(p->iblob, "<ul>", 4);

  db_prepare(&q,
    "SELECT substr(tagname, 6, 1000) FROM tag WHERE tagname GLOB 'wiki-*'"
    " ORDER BY lower(tagname)"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zName = db_column_text(&q, 0);
    blob_appendf(p->iblob, "<li><a href=\"%s/wiki?name=%T\">%h</a></li>", g.zBaseURL, zName, zName);
  }
  db_finalize(&q);
  blob_append(p->iblob, "</ul>", 5);
}//}}}


static int cr_match(char *z1, char *z2, int *len){
  *len = strlen(z2);
  return !memcmp(z1, z2 ,*len);
}

int cr_has_macro(char *z, int *tokenType){

  int len;

  if (cr_match(z, "wiki-contents>>", &len)) {
    *tokenType = MACRO_WIKI_CONTENTS;
    return len;
  }

  tokenType = MACRO_NONE;
  return 0;

}
//}}}

char *wiki_render_creole(Renderer *r, char *z){

  Parser parser;
  Parser *p = &parser;

  p->r = r;
  p->iblob = r->pOut;

  p->nFree = 0;
  p->pool = NULL;

  cr_parse(p, z);

  cr_render(p, p->previous);

  pool_free(p);

  return p->cursor;

}

