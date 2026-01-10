/*
** Copyright (c) 2013 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
******************************************************************************
**
** This file was adapted from the ext/misc/regexp.c file in SQLite3.  That
** file is in the public domain.
**
** See ../www/grep.md for details of the algorithm and RE dialect.
**
**
**  The following regular expression syntax is supported:
**
**     X*      zero or more occurrences of X
**     X+      one or more occurrences of X
**     X?      zero or one occurrences of X
**     X{p,q}  between p and q occurrences of X
**     (X)     match X
**     X|Y     X or Y
**     ^X      X occurring at the beginning of the string
**     X$      X occurring at the end of the string
**     .       Match any single character
**     \c      Character c where c is one of \{}()[]|*+?.
**     \c      C-language escapes for c in afnrtv.  ex: \t or \n
**     \uXXXX  Where XXXX is exactly 4 hex digits, unicode value XXXX
**     \xXX    Where XX is exactly 2 hex digits, unicode value XX
**     [abc]   Any single character from the set abc
**     [^abc]  Any single character not in the set abc
**     [a-z]   Any single character in the range a-z
**     [^a-z]  Any single character not in the range a-z
**     \b      Word boundary
**     \w      Word character.  [A-Za-z0-9_]
**     \W      Non-word character
**     \d      Digit
**     \D      Non-digit
**     \s      Whitespace character
**     \S      Non-whitespace character
**
** A nondeterministic finite automaton (NFA) is used for matching, so the
** performance is bounded by O(N*M) where N is the size of the regular
** expression and M is the size of the input string.  The matcher never
** exhibits exponential behavior.  Note that the X{p,q} operator expands
** to p copies of X following by q-p copies of X? and that the size of the
** regular expression in the O(N*M) performance bound is computed after
** this expansion.
**
** To help prevent DoS attacks, the maximum size of the NFA is restricted.
*/
#include "config.h"
#include "regexp.h"

/* The end-of-input character */
#define RE_EOF            0    /* End of input */
#define RE_START  0xfffffff    /* Start of input - larger than an UTF-8 */

/* The NFA is implemented as sequence of opcodes taken from the following
** set.  Each opcode has a single integer argument.
*/
#define RE_OP_MATCH       1    /* Match the one character in the argument */
#define RE_OP_ANY         2    /* Match any one character.  (Implements ".") */
#define RE_OP_ANYSTAR     3    /* Special optimized version of .* */
#define RE_OP_FORK        4    /* Continue to both next and opcode at iArg */
#define RE_OP_GOTO        5    /* Jump to opcode at iArg */
#define RE_OP_ACCEPT      6    /* Halt and indicate a successful match */
#define RE_OP_CC_INC      7    /* Beginning of a [...] character class */
#define RE_OP_CC_EXC      8    /* Beginning of a [^...] character class */
#define RE_OP_CC_VALUE    9    /* Single value in a character class */
#define RE_OP_CC_RANGE   10    /* Range of values in a character class */
#define RE_OP_WORD       11    /* Perl word character [A-Za-z0-9_] */
#define RE_OP_NOTWORD    12    /* Not a perl word character */
#define RE_OP_DIGIT      13    /* digit:  [0-9] */
#define RE_OP_NOTDIGIT   14    /* Not a digit */
#define RE_OP_SPACE      15    /* space:  [ \t\n\r\v\f] */
#define RE_OP_NOTSPACE   16    /* Not a digit */
#define RE_OP_BOUNDARY   17    /* Boundary between word and non-word */
#define RE_OP_ATSTART    18    /* Currently at the start of the string */

/* Each opcode is a "state" in the NFA */
typedef unsigned short ReStateNumber;

/* Because this is an NFA and not a DFA, multiple states can be active at
** once.  An instance of the following object records all active states in
** the NFA.  The implementation is optimized for the common case where the
** number of actives states is small.
*/
typedef struct ReStateSet {
  unsigned nState;            /* Number of current states */
  ReStateNumber *aState;      /* Current states */
} ReStateSet;

#if INTERFACE
/* An input string read one character at a time.
*/
struct ReInput {
  const unsigned char *z;  /* All text */
  int i;                   /* Next byte to read */
  int mx;                  /* EOF when i>=mx */
};

/* A compiled NFA (or an NFA that is in the process of being compiled) is
** an instance of the following object.
*/
struct ReCompiled {
  ReInput sIn;                /* Regular expression text */
  const char *zErr;           /* Error message to return */
  char *aOp;                  /* Operators for the virtual machine */
  int *aArg;                  /* Arguments to each operator */
  unsigned (*xNextChar)(ReInput*);  /* Next character function */
  unsigned char zInit[12];    /* Initial text to match */
  int nInit;                  /* Number of bytes in zInit */
  unsigned nState;            /* Number of entries in aOp[] and aArg[] */
  unsigned nAlloc;            /* Slots allocated for aOp[] and aArg[] */
  unsigned mxAlloc;           /* Complexity limit */
};
#endif

/* Add a state to the given state set if it is not already there */
static void re_add_state(ReStateSet *pSet, int newState){
  unsigned i;
  for(i=0; i<pSet->nState; i++) if( pSet->aState[i]==newState ) return;
  pSet->aState[pSet->nState++] = (ReStateNumber)newState;
}

/* Extract the next unicode character from *pzIn and return it.  Advance
** *pzIn to the first byte past the end of the character returned.  To
** be clear:  this routine converts utf8 to unicode.  This routine is
** optimized for the common case where the next character is a single byte.
*/
static unsigned re_next_char(ReInput *p){
  unsigned c;
  if( p->i>=p->mx ) return 0;
  c = p->z[p->i++];
  if( c>=0x80 ){
    if( (c&0xe0)==0xc0 && p->i<p->mx && (p->z[p->i]&0xc0)==0x80 ){
      c = (c&0x1f)<<6 | (p->z[p->i++]&0x3f);
      if( c<0x80 ) c = 0xfffd;
    }else if( (c&0xf0)==0xe0 && p->i+1<p->mx && (p->z[p->i]&0xc0)==0x80
           && (p->z[p->i+1]&0xc0)==0x80 ){
      c = (c&0x0f)<<12 | ((p->z[p->i]&0x3f)<<6) | (p->z[p->i+1]&0x3f);
      p->i += 2;
      if( c<=0x7ff || (c>=0xd800 && c<=0xdfff) ) c = 0xfffd;
    }else if( (c&0xf8)==0xf0 && p->i+2<p->mx && (p->z[p->i]&0xc0)==0x80
           && (p->z[p->i+1]&0xc0)==0x80 && (p->z[p->i+2]&0xc0)==0x80 ){
      c = (c&0x07)<<18 | ((p->z[p->i]&0x3f)<<12) | ((p->z[p->i+1]&0x3f)<<6)
                       | (p->z[p->i+2]&0x3f);
      p->i += 3;
      if( c<=0xffff || c>0x10ffff ) c = 0xfffd;
    }else{
      c = 0xfffd;
    }
  }
  return c;
}
static unsigned re_next_char_nocase(ReInput *p){
  unsigned c = re_next_char(p);
  return unicode_fold(c,2);
}

/* Return true if c is a perl "word" character:  [A-Za-z0-9_] */
static int re_word_char(int c){
  return unicode_isalnum(c) || c=='_';
}

/* Return true if c is a "digit" character:  [0-9] */
static int re_digit_char(int c){
  return (c>='0' && c<='9');
}

/* Return true if c is a perl "space" character:  [ \t\r\n\v\f] */
static int re_space_char(int c){
  return c==' ' || c=='\t' || c=='\n' || c=='\r' || c=='\v' || c=='\f';
}

/* Run a compiled regular expression on the zero-terminated input
** string zIn[].  Return true on a match and false if there is no match.
*/
int re_match(ReCompiled *pRe, const unsigned char *zIn, int nIn){
  ReStateSet aStateSet[2], *pThis, *pNext;
  ReStateNumber aSpace[100];
  ReStateNumber *pToFree;
  unsigned int i = 0;
  unsigned int iSwap = 0;
  int c = RE_START;
  int cPrev = 0;
  int rc = 0;
  ReInput in;

  in.z = zIn;
  in.i = 0;
  in.mx = nIn>=0 ? nIn : (int)strlen((char const*)zIn);

  /* Look for the initial prefix match, if there is one. */
  if( pRe->nInit ){
    unsigned char x = pRe->zInit[0];
    while( in.i+pRe->nInit<=in.mx
     && (zIn[in.i]!=x ||
         strncmp((const char*)zIn+in.i, (const char*)pRe->zInit, pRe->nInit)!=0)
    ){
      in.i++;
    }
    if( in.i+pRe->nInit>in.mx ) return 0;
    c = RE_START-1;
  }

  if( pRe->nState<=(sizeof(aSpace)/(sizeof(aSpace[0])*2)) ){
    pToFree = 0;
    aStateSet[0].aState = aSpace;
  }else{
    pToFree = fossil_malloc( sizeof(ReStateNumber)*2*pRe->nState );
    if( pToFree==0 ) return -1;
    aStateSet[0].aState = pToFree;
  }
  aStateSet[1].aState = &aStateSet[0].aState[pRe->nState];
  pNext = &aStateSet[1];
  pNext->nState = 0;
  re_add_state(pNext, 0);
  while( c!=RE_EOF && pNext->nState>0 ){
    cPrev = c;
    c = pRe->xNextChar(&in);
    pThis = pNext;
    pNext = &aStateSet[iSwap];
    iSwap = 1 - iSwap;
    pNext->nState = 0;
    for(i=0; i<pThis->nState; i++){
      int x = pThis->aState[i];
      switch( pRe->aOp[x] ){
        case RE_OP_MATCH: {
          if( pRe->aArg[x]==c ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_ATSTART: {
          if( cPrev==RE_START ) re_add_state(pThis, x+1);
          break;
        }
        case RE_OP_ANY: {
          if( c!=0 ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_WORD: {
          if( re_word_char(c) ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_NOTWORD: {
          if( !re_word_char(c) && c!=0 ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_DIGIT: {
          if( re_digit_char(c) ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_NOTDIGIT: {
          if( !re_digit_char(c) && c!=0 ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_SPACE: {
          if( re_space_char(c) ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_NOTSPACE: {
          if( !re_space_char(c) && c!=0 ) re_add_state(pNext, x+1);
          break;
        }
        case RE_OP_BOUNDARY: {
          if( re_word_char(c)!=re_word_char(cPrev) ) re_add_state(pThis, x+1);
          break;
        }
        case RE_OP_ANYSTAR: {
          re_add_state(pNext, x);
          re_add_state(pThis, x+1);
          break;
        }
        case RE_OP_FORK: {
          re_add_state(pThis, x+pRe->aArg[x]);
          re_add_state(pThis, x+1);
          break;
        }
        case RE_OP_GOTO: {
          re_add_state(pThis, x+pRe->aArg[x]);
          break;
        }
        case RE_OP_ACCEPT: {
          rc = 1;
          goto re_match_end;
        }
        case RE_OP_CC_EXC: {
          if( c==0 ) break;
          /* fall-through */ goto re_op_cc_inc;
        }
        case RE_OP_CC_INC: re_op_cc_inc: {
          int j = 1;
          int n = pRe->aArg[x];
          int hit = 0;
          for(j=1; j>0 && j<n; j++){
            if( pRe->aOp[x+j]==RE_OP_CC_VALUE ){
              if( pRe->aArg[x+j]==c ){
                hit = 1;
                j = -1;
              }
            }else{
              if( pRe->aArg[x+j]<=c && pRe->aArg[x+j+1]>=c ){
                hit = 1;
                j = -1;
              }else{
                j++;
              }
            }
          }
          if( pRe->aOp[x]==RE_OP_CC_EXC ) hit = !hit;
          if( hit ) re_add_state(pNext, x+n);
          break;
        }
      }
    }
  }
  for(i=0; i<pNext->nState; i++){
    int x = pNext->aState[i];
    while( pRe->aOp[x]==RE_OP_GOTO ) x += pRe->aArg[x];
    if( pRe->aOp[x]==RE_OP_ACCEPT ){ rc = 1; break; }
  }
re_match_end:
  fossil_free(pToFree);
  return rc;
}

/* Resize the opcode and argument arrays for an RE under construction.
*/
static int re_resize(ReCompiled *p, int N){
  char *aOp;
  int *aArg;
  if( N>p->mxAlloc ){ p->zErr = "REGEXP pattern too big"; return 1; }
  aOp = fossil_realloc(p->aOp, N*sizeof(p->aOp[0]));
  if( aOp==0 ){ p->zErr = "out of memory"; return 1; }
  p->aOp = aOp;
  aArg = fossil_realloc(p->aArg, N*sizeof(p->aArg[0]));
  if( aArg==0 ){ p->zErr = "out of memory"; return 1; }
  p->aArg = aArg;
  p->nAlloc = N;
  return 0;
}

/* Insert a new opcode and argument into an RE under construction.  The
** insertion point is just prior to existing opcode iBefore.
*/
static int re_insert(ReCompiled *p, int iBefore, int op, int arg){
  int i;
  if( p->nAlloc<=p->nState && re_resize(p, p->nAlloc*2) ) return 0;
  for(i=p->nState; i>iBefore; i--){
    p->aOp[i] = p->aOp[i-1];
    p->aArg[i] = p->aArg[i-1];
  }
  p->nState++;
  p->aOp[iBefore] = (char)op;
  p->aArg[iBefore] = arg;
  return iBefore;
}

/* Append a new opcode and argument to the end of the RE under construction.
*/
static int re_append(ReCompiled *p, int op, int arg){
  return re_insert(p, p->nState, op, arg);
}

/* Make a copy of N opcodes starting at iStart onto the end of the RE
** under construction.
*/
static void re_copy(ReCompiled *p, int iStart, int N){
  if( p->nState+N>=p->nAlloc && re_resize(p, p->nAlloc*2+N) ) return;
  memcpy(&p->aOp[p->nState], &p->aOp[iStart], N*sizeof(p->aOp[0]));
  memcpy(&p->aArg[p->nState], &p->aArg[iStart], N*sizeof(p->aArg[0]));
  p->nState += N;
}

/* Return true if c is a hexadecimal digit character:  [0-9a-fA-F]
** If c is a hex digit, also set *pV = (*pV)*16 + valueof(c).  If
** c is not a hex digit *pV is unchanged.
*/
static int re_hex(int c, int *pV){
  if( c>='0' && c<='9' ){
    c -= '0';
  }else if( c>='a' && c<='f' ){
    c -= 'a' - 10;
  }else if( c>='A' && c<='F' ){
    c -= 'A' - 10;
  }else{
    return 0;
  }
  *pV = (*pV)*16 + (c & 0xff);
  return 1;
}

/* A backslash character has been seen, read the next character and
** return its interpretation.
*/
static unsigned re_esc_char(ReCompiled *p){
  static const char zEsc[] = "afnrtv\\()*.+?[$^{|}]";
  static const char zTrans[] = "\a\f\n\r\t\v";
  int i, v = 0;
  char c;
  if( p->sIn.i>=p->sIn.mx ) return 0;
  c = p->sIn.z[p->sIn.i];
  if( c=='u' && p->sIn.i+4<p->sIn.mx ){
    const unsigned char *zIn = p->sIn.z + p->sIn.i;
    if( re_hex(zIn[1],&v)
     && re_hex(zIn[2],&v)
     && re_hex(zIn[3],&v)
     && re_hex(zIn[4],&v)
    ){
      p->sIn.i += 5;
      return v;
    }
  }
  if( c=='x' && p->sIn.i+2<p->sIn.mx ){
    const unsigned char *zIn = p->sIn.z + p->sIn.i;
    if( re_hex(zIn[1],&v)
     && re_hex(zIn[2],&v)
    ){
      p->sIn.i += 3;
      return v;
    }
  }
  for(i=0; zEsc[i] && zEsc[i]!=c; i++){}
  if( zEsc[i] ){
    if( i<6 ) c = zTrans[i];
    p->sIn.i++;
  }else{
    p->zErr = "unknown \\ escape";
  }
  return c;
}

/* Forward declaration */
static const char *re_subcompile_string(ReCompiled*);

/* Peek at the next byte of input */
static unsigned char rePeek(ReCompiled *p){
  return p->sIn.i<p->sIn.mx ? p->sIn.z[p->sIn.i] : 0;
}

/* Compile RE text into a sequence of opcodes.  Continue up to the
** first unmatched ")" character, then return.  If an error is found,
** return a pointer to the error message string.
*/
static const char *re_subcompile_re(ReCompiled *p){
  const char *zErr;
  int iStart, iEnd, iGoto;
  iStart = p->nState;
  zErr = re_subcompile_string(p);
  if( zErr ) return zErr;
  while( rePeek(p)=='|' ){
    iEnd = p->nState;
    re_insert(p, iStart, RE_OP_FORK, iEnd + 2 - iStart);
    iGoto = re_append(p, RE_OP_GOTO, 0);
    p->sIn.i++;
    zErr = re_subcompile_string(p);
    if( zErr ) return zErr;
    p->aArg[iGoto] = p->nState - iGoto;
  }
  return 0;
}

/* Compile an element of regular expression text (anything that can be
** an operand to the "|" operator).  Return NULL on success or a pointer
** to the error message if there is a problem.
*/
static const char *re_subcompile_string(ReCompiled *p){
  int iPrev = -1;
  int iStart;
  unsigned c;
  const char *zErr;
  while( (c = p->xNextChar(&p->sIn))!=0 ){
    iStart = p->nState;
    switch( c ){
      case '|':
      case ')': {
        p->sIn.i--;
        return 0;
      }
      case '(': {
        zErr = re_subcompile_re(p);
        if( zErr ) return zErr;
        if( rePeek(p)!=')' ) return "unmatched '('";
        p->sIn.i++;
        break;
      }
      case '.': {
        if( rePeek(p)=='*' ){
          re_append(p, RE_OP_ANYSTAR, 0);
          p->sIn.i++;
        }else{
          re_append(p, RE_OP_ANY, 0);
        }
        break;
      }
      case '*': {
        if( iPrev<0 ) return "'*' without operand";
        re_insert(p, iPrev, RE_OP_GOTO, p->nState - iPrev + 1);
        re_append(p, RE_OP_FORK, iPrev - p->nState + 1);
        break;
      }
      case '+': {
        if( iPrev<0 ) return "'+' without operand";
        re_append(p, RE_OP_FORK, iPrev - p->nState);
        break;
      }
      case '?': {
        if( iPrev<0 ) return "'?' without operand";
        re_insert(p, iPrev, RE_OP_FORK, p->nState - iPrev+1);
        break;
      }
      case '$': {
        re_append(p, RE_OP_MATCH, RE_EOF);
        break;
      }
      case '^': {
        re_append(p, RE_OP_ATSTART, 0);
        break;
      }
      case '{': {
        unsigned int m = 0, n = 0;
        unsigned int sz, j;
        if( iPrev<0 ) return "'{m,n}' without operand";
        while( (c=rePeek(p))>='0' && c<='9' ){
          m = m*10 + c - '0';
          if( m*2>p->mxAlloc ) return "REGEXP pattern too big";
          p->sIn.i++;
        }
        n = m;
        if( c==',' ){
          p->sIn.i++;
          n = 0;
          while( (c=rePeek(p))>='0' && c<='9' ){
            n = n*10 + c-'0';
            if( n*2>p->mxAlloc ) return "REGEXP pattern too big";
            p->sIn.i++;
          }
        }
        if( c!='}' ) return "unmatched '{'";
        if( n<m ) return "n less than m in '{m,n}'";
        p->sIn.i++;
        sz = p->nState - iPrev;
        if( m==0 ){
          if( n==0 ) return "both m and n are zero in '{m,n}'";
          re_insert(p, iPrev, RE_OP_FORK, sz+1);
          iPrev++;
          n--;
        }else{
          for(j=1; j<m; j++) re_copy(p, iPrev, sz);
        }
        for(j=m; j<n; j++){
          re_append(p, RE_OP_FORK, sz+1);
          re_copy(p, iPrev, sz);
        }
        if( n==0 && m>0 ){
          re_append(p, RE_OP_FORK, -(int)sz);
        }
        break;
      }
      case '[': {
        unsigned int iFirst = p->nState;
        if( rePeek(p)=='^' ){
          re_append(p, RE_OP_CC_EXC, 0);
          p->sIn.i++;
        }else{
          re_append(p, RE_OP_CC_INC, 0);
        }
        while( (c = p->xNextChar(&p->sIn))!=0 ){
          if( c=='[' && rePeek(p)==':' ){
            return "POSIX character classes not supported";
          }
          if( c=='\\' ) c = re_esc_char(p);
          if( rePeek(p)=='-' ){
            re_append(p, RE_OP_CC_RANGE, c);
            p->sIn.i++;
            c = p->xNextChar(&p->sIn);
            if( c=='\\' ) c = re_esc_char(p);
            re_append(p, RE_OP_CC_RANGE, c);
          }else{
            re_append(p, RE_OP_CC_VALUE, c);
          }
          if( rePeek(p)==']' ){ p->sIn.i++; break; }
        }
        if( c==0 ) return "unclosed '['";
        if( p->nState>iFirst ) p->aArg[iFirst] = p->nState - iFirst;
        break;
      }
      case '\\': {
        int specialOp = 0;
        switch( rePeek(p) ){
          case 'b': specialOp = RE_OP_BOUNDARY;   break;
          case 'd': specialOp = RE_OP_DIGIT;      break;
          case 'D': specialOp = RE_OP_NOTDIGIT;   break;
          case 's': specialOp = RE_OP_SPACE;      break;
          case 'S': specialOp = RE_OP_NOTSPACE;   break;
          case 'w': specialOp = RE_OP_WORD;       break;
          case 'W': specialOp = RE_OP_NOTWORD;    break;
        }
        if( specialOp ){
          p->sIn.i++;
          re_append(p, specialOp, 0);
        }else{
          c = re_esc_char(p);
          re_append(p, RE_OP_MATCH, c);
        }
        break;
      }
      default: {
        re_append(p, RE_OP_MATCH, c);
        break;
      }
    }
    iPrev = iStart;
  }
  return 0;
}

/* Free and reclaim all the memory used by a previously compiled
** regular expression.  Applications should invoke this routine once
** for every call to re_compile() to avoid memory leaks.
*/
void re_free(ReCompiled *pRe){
  if( pRe ){
    fossil_free(pRe->aOp);
    fossil_free(pRe->aArg);
    fossil_free(pRe);
  }
}

/*
** Compile a textual regular expression in zIn[] into a compiled regular
** expression suitable for us by re_match() and return a pointer to the
** compiled regular expression in *ppRe.  Return NULL on success or an
** error message if something goes wrong.
*/
static const char *re_compile(
  ReCompiled **ppRe,      /* OUT: write compiled NFA here */
  const char *zIn,        /* Input regular expression */
  int mxRe,               /* Complexity limit */
  int noCase              /* True for caseless comparisons */
){
  ReCompiled *pRe;
  const char *zErr;
  int i, j;

  *ppRe = 0;
  pRe = fossil_malloc( sizeof(*pRe) );
  if( pRe==0 ){
    return "out of memory";
  }
  memset(pRe, 0, sizeof(*pRe));
  pRe->xNextChar = noCase ? re_next_char_nocase : re_next_char;
  pRe->mxAlloc = mxRe;
  if( re_resize(pRe, 30) ){
    zErr = pRe->zErr;
    re_free(pRe);
    return zErr;
  }
  if( zIn[0]=='^' ){
    zIn++;
  }else{
    re_append(pRe, RE_OP_ANYSTAR, 0);
  }
  pRe->sIn.z = (unsigned char*)zIn;
  pRe->sIn.i = 0;
  pRe->sIn.mx = (int)strlen(zIn);
  zErr = re_subcompile_re(pRe);
  if( zErr ){
    re_free(pRe);
    return zErr;
  }
  if( pRe->sIn.i>=pRe->sIn.mx ){
    re_append(pRe, RE_OP_ACCEPT, 0);
    *ppRe = pRe;
  }else{
    re_free(pRe);
    return "unrecognized character";
  }

  /* The following is a performance optimization.  If the regex begins with
  ** ".*" (if the input regex lacks an initial "^") and afterwards there are
  ** one or more matching characters, enter those matching characters into
  ** zInit[].  The re_match() routine can then search ahead in the input
  ** string looking for the initial match without having to run the whole
  ** regex engine over the string.  Do not worry about trying to match
  ** unicode characters beyond plane 0 - those are very rare and this is
  ** just an optimization. */
  if( pRe->aOp[0]==RE_OP_ANYSTAR && !noCase ){
    for(j=0, i=1; j<(int)sizeof(pRe->zInit)-2 && pRe->aOp[i]==RE_OP_MATCH; i++){
      unsigned x = pRe->aArg[i];
      if( x<=0x7f ){
        pRe->zInit[j++] = (unsigned char)x;
      }else if( x<=0x7ff ){
        pRe->zInit[j++] = (unsigned char)(0xc0 | (x>>6));
        pRe->zInit[j++] = 0x80 | (x&0x3f);
      }else if( x<=0xffff ){
        pRe->zInit[j++] = (unsigned char)(0xe0 | (x>>12));
        pRe->zInit[j++] = 0x80 | ((x>>6)&0x3f);
        pRe->zInit[j++] = 0x80 | (x&0x3f);
      }else{
        break;
      }
    }
    if( j>0 && pRe->zInit[j-1]==0 ) j--;
    pRe->nInit = j;
  }
  return pRe->zErr;
}

/*
** Implementation of the regexp() SQL function.  This function implements
** the build-in REGEXP operator.  The first argument to the function is the
** pattern and the second argument is the string.  So, the SQL statements:
**
**       A REGEXP B
**
** is implemented as regexp(B,A).
*/
static void re_sql_func(
  sqlite3_context *context,
  int argc,
  sqlite3_value **argv
){
  ReCompiled *pRe;          /* Compiled regular expression */
  const char *zPattern;     /* The regular expression */
  const unsigned char *zStr;/* String being searched */
  const char *zErr;         /* Compile error message */
  int setAux = 0;           /* True to invoke sqlite3_set_auxdata() */

  (void)argc;  /* Unused */
  pRe = sqlite3_get_auxdata(context, 0);
  if( pRe==0 ){
    zPattern = (const char*)sqlite3_value_text(argv[0]);
    if( zPattern==0 ) return;
    zErr = fossil_re_compile(&pRe, zPattern, sqlite3_user_data(context)!=0);
    if( zErr ){
      re_free(pRe);
      /* The original SQLite function from which this code was copied raises
      ** an error if the REGEXP contained a syntax error.  This variant
      ** silently fails to match, as that works better for Fossil.
      ** sqlite3_result_error(context, zErr, -1); */
      sqlite3_result_int(context, 0);
      return;
    }
    if( pRe==0 ){
      sqlite3_result_error_nomem(context);
      return;
    }
    setAux = 1;
  }
  zStr = (const unsigned char*)sqlite3_value_text(argv[1]);
  if( zStr!=0 ){
    sqlite3_result_int(context, re_match(pRe, zStr, -1));
  }
  if( setAux ){
    sqlite3_set_auxdata(context, 0, pRe, (void(*)(void*))re_free);
  }
}

/*
** Invoke this routine to register the regexp() function with the
** SQLite database connection.
*/
int re_add_sql_func(sqlite3 *db){
  int rc;
  rc = sqlite3_create_function(db, "regexp", 2,
                           SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                               0, re_sql_func, 0, 0);
  if( rc==SQLITE_OK ){
    /* The regexpi(PATTERN,STRING) function is a case-insensitive version
    ** of regexp(PATTERN,STRING). */
    rc = sqlite3_create_function(db, "regexpi", 2,
                           SQLITE_UTF8|SQLITE_INNOCUOUS|SQLITE_DETERMINISTIC,
                                 (void*)db, re_sql_func, 0, 0);
  }
  return rc;
}

/*
** The input zIn is a string that we want to match exactly as part of
** a regular expression.  Return a new string (in space obtained from
** fossil_malloc() or the equivalent) that escapes all regexp syntax
** characters in zIn.
*/
char *re_quote(const char *zIn){
  Blob out;
  blob_init(&out, 0, 0);
  while( zIn[0] ){
    switch( zIn[0] ){
      case '.':
      case '?':
      case '*':
      case '+':
      case '\\':
      case '(':
      case ')':
      case '[':
      case ']':
      case '|':
      case '^':
      case '$':
      case '{':
      case '}': {
        blob_appendf(&out,"\\x%02x", (unsigned char)zIn[0]);
        break;
      }
      default: {
        blob_append_char(&out, zIn[0]);
        break;
      }
    }
    zIn++;
  }
  blob_materialize(&out);
  return out.aData;
}

/*
** SETTING:  regexp-limit                  width=8 default=1000
**
** Limit the size of the bytecode used to implement a regular expression
** to this many steps.  It is important to limit this to avoid possible
** DoS attacks.
*/

/*
** Compile an RE using re_maxlen().
*/
const char *fossil_re_compile(
  ReCompiled **ppRe,      /* OUT: write compiled NFA here */
  const char *zIn,        /* Input regular expression */
  int noCase              /* True for caseless comparisons */
){
  int mxLen = g.db ? db_get_int("regexp-limit",1000) : 1000;
  return re_compile(ppRe, zIn, mxLen, noCase);
}

/*
** Run a "grep" over a single file read from disk.
*/
static void grep_file(ReCompiled *pRe, const char *zFile, FILE *in){
  int ln = 0;
  int n;
  char zLine[2000];
  while( fgets(zLine, sizeof(zLine), in) ){
    ln++;
    n = (int)strlen(zLine);
    while( n && (zLine[n-1]=='\n' || zLine[n-1]=='\r') ) n--;
    if( re_match(pRe, (const unsigned char*)zLine, n) ){
      fossil_print("%s:%d:%.*s\n", zFile, ln, n, zLine);
    }
  }
}

/*
** Flags for grep_buffer()
*/
#define GREP_EXISTS    0x001    /* If any match, print only the name and stop */
#define GREP_QUIET     0x002    /* Return code only */

/*
** Run a "grep" over a text file
*/
static int grep_buffer(
  ReCompiled *pRe,
  const char *zName,
  const char *z,
  u32 flags
){
  int i, j, n, ln, cnt;
  for(i=j=ln=cnt=0; z[i]; i=j+1){
    for(j=i; z[j] && z[j]!='\n'; j++){}
    n = j - i;
    ln++;
    if( re_match(pRe, (const unsigned char*)(z+i), j-i) ){
      cnt++;
      if( flags & GREP_EXISTS ){
        if( (flags & GREP_QUIET)==0 && zName ) fossil_print("%s\n", zName);
        break;
      }
      if( (flags & GREP_QUIET)==0 ){
        if( cnt==1 && zName ){
          fossil_print("== %s\n", zName);
        }
        fossil_print("%d:%.*s\n", ln, n, z+i);
      }
    }
  }
  return cnt;
}

/*
** COMMAND: test-grep
**
** Usage: %fossil test-grep REGEXP [FILE...]
**
** Run a regular expression match over the named disk files, or against
** standard input if no disk files are named on the command-line.
**
** Options:
**   -i|--ignore-case    Ignore case
**   --robot-exception   Use the robot-exception setting as the REGEXP
*/
void re_test_grep(void){
  ReCompiled *pRe;
  const char *zErr;
  int iFileList = 3;
  int ignoreCase = find_option("ignore-case","i",0)!=0;
  int bRobot = find_option("robot-exception",0,0)!=0;
  if( bRobot ){
    const char *zRe;
    db_find_and_open_repository(0,0);
    verify_all_options();
    zRe = db_get("robot-exception","^$");
    zErr = fossil_re_compile(&pRe, zRe, ignoreCase);
    iFileList = 2;
  }else{
    verify_all_options();
    if( g.argc<3 ){
      usage("REGEXP [FILE...]");
    }
    zErr = fossil_re_compile(&pRe, g.argv[2], ignoreCase);
  }
  if( zErr ) fossil_fatal("%s", zErr);
  if( g.argc==iFileList ){
    grep_file(pRe, "-", stdin);
  }else{
    int i;
    for(i=iFileList; i<g.argc; i++){
      FILE *in = fossil_fopen(g.argv[i], "rb");
      if( in==0 ){
        fossil_warning("cannot open \"%s\"", g.argv[i]);
      }else{
        grep_file(pRe, g.argv[i], in);
        fclose(in);
      }
    }
  }
  re_free(pRe);
}

/*
** COMMAND: grep
**
** Usage: %fossil grep [OPTIONS] PATTERN FILENAME ...
**
** Attempt to match the given POSIX extended regular expression PATTERN over
** all historic versions of FILENAME.  The search begins with the most recent
** version of the file and moves backwards in time.  Multiple FILENAMEs can
** be specified, in which case all named files are searched in reverse
** chronological order.
**
** For details of the supported regular expression dialect, see
** https://fossil-scm.org/fossil/doc/trunk/www/grep.md
**
** Options:
**     -c|--count                 Suppress normal output; instead print a count
**                                of the number of matching files
**     -i|--ignore-case           Ignore case
**     -l|--files-with-matches    List only hash for each match
**     --once                     Stop searching after the first match
**     -s|--no-messages           Suppress error messages about nonexistent
**                                or unreadable files
**     -v|--invert-match          Invert the sense of matching.  Show only
**                                files that have no matches. Implies -l
**     --verbose                  Show each file as it is analyzed
*/
void re_grep_cmd(void){
  u32 flags = 0;
  int bVerbose = 0;
  ReCompiled *pRe;
  const char *zErr;
  int ignoreCase = 0;
  Blob fullName;
  int ii;
  int nMatch = 0;
  int bNoMsg;
  int cntFlag;
  int bOnce;
  int bInvert;
  int nSearch = 0;
  Stmt q;


  if( find_option("ignore-case","i",0)!=0 ) ignoreCase = 1;
  if( find_option("files-with-matches","l",0)!=0 ) flags |= GREP_EXISTS;
  if( find_option("verbose",0,0)!=0 ) bVerbose = 1;
  if( find_option("quiet","q",0) ) flags |= GREP_QUIET|GREP_EXISTS;
  bNoMsg = find_option("no-messages","s",0)!=0;
  bOnce = find_option("once",0,0)!=0;
  bInvert = find_option("invert-match","v",0)!=0;
  if( bInvert ){
    flags |= GREP_QUIET|GREP_EXISTS;
  }
  cntFlag = find_option("count","c",0)!=0;
  if( cntFlag ){
    flags |= GREP_QUIET|GREP_EXISTS;
  }
  db_find_and_open_repository(0, 0);
  verify_all_options();
  if( g.argc<4 ){
    usage("REGEXP FILENAME ...");
  }
  zErr = fossil_re_compile(&pRe, g.argv[2], ignoreCase);
  if( zErr ) fossil_fatal("%s", zErr);

  add_content_sql_commands(g.db);
  db_multi_exec("CREATE TEMP TABLE arglist(iname,fname,fnid);");
  for(ii=3; ii<g.argc; ii++){
    const char *zTarget = g.argv[ii];
    if( file_tree_name(zTarget, &fullName, 0, 1) ){
      int fnid = db_int(0, "SELECT fnid FROM filename WHERE name=%Q",
                        blob_str(&fullName));
      if( !fnid ){
        if( bNoMsg ) continue;
        if( file_size(zTarget, ExtFILE)<0 ){
          fossil_fatal("no such file: %s", zTarget);
        }
        fossil_fatal("not a managed file: %s", zTarget);
      }else{
        db_multi_exec(
          "INSERT INTO arglist(iname,fname,fnid) VALUES(%Q,%Q,%d)",
          zTarget, blob_str(&fullName), fnid);
      }
    }
    blob_reset(&fullName);
  }
  db_prepare(&q,
    " SELECT"
    "   A.uuid,"       /* file hash */
    "   A.rid,"        /* file rid */
    "   B.uuid,"       /* check-in hash */
    "   datetime(min(event.mtime)),"  /* check-in time */
    "   arglist.iname"                /* file name */
    " FROM arglist, mlink, blob A, blob B, event"
    " WHERE mlink.mid=event.objid"
    "   AND mlink.fid=A.rid"
    "   AND mlink.mid=B.rid"
    "   AND mlink.fnid=arglist.fnid"
    " GROUP BY A.uuid"
    " ORDER BY min(event.mtime) DESC;"
  );
  while( db_step(&q)==SQLITE_ROW ){
    const char *zFileHash = db_column_text(&q,0);
    int rid = db_column_int(&q,1);
    const char *zCkinHash = db_column_text(&q,2);
    const char *zDate = db_column_text(&q,3);
    const char *zFN = db_column_text(&q,4);
    char *zLabel;
    Blob cx;
    content_get(rid, &cx);
    zLabel = mprintf("%.16s %s %S checkin %S", zDate, zFN,zFileHash,zCkinHash);
    if( bVerbose ) fossil_print("Scanning: %s\n", zLabel);
    nSearch++;
    nMatch += grep_buffer(pRe, zLabel, blob_str(&cx), flags);
    blob_reset(&cx);
    if( bInvert && cntFlag==0 ){
      if( nMatch==0 ){
        fossil_print("== %s\n", zLabel);
        if( bOnce ) nMatch = 1;
      }else{
        nMatch = 0;
      }
    }
    fossil_free(zLabel);
    if( nMatch ){
      if( (flags & GREP_QUIET)!=0 ) break;
      if( bOnce ) break;
    }
  }
  db_finalize(&q);
  re_free(pRe);
  if( cntFlag ){
    if( bInvert ){
      fossil_print("%d\n", nSearch-nMatch);
    }else{
      fossil_print("%d\n", nMatch);
    }
  }
}

/*
** WEBPAGE: re_rules
**
** Show a summary of the regular expression matching rules for Fossil.
*/
void re_rules_page(void){
  style_set_current_feature("wiki");
  style_header("Regular Expression Syntax");
  @ <p>Syntax rules for regular expression matching in Fossil:</p>
  @ 
  @ <table border="0" cellpadding="0" cellspacing="0">
  @ <tr><th>&emsp;&emsp;&emsp;<th>Pattern
  @     <th>&emsp;&emsp;&emsp;<th align="left">Match
  @ <tr><td><td><i>X</i><b>*</b>
  @     <td><td>Zero or more occurrences of <i>X</i>
  @ <tr><td><td><i>X</i><b>+</b>
  @     <td><td>One or more occurrences of <i>X</i>
  @ <tr><td><td><i>X</i><b>?</b>
  @     <td><td>Zero or one occurrences of <i>X</i>
  @ <tr><td><td><i>X</i><b>{</b><i>P</i><b>,</b><i>Q</i><b>}</b>
  @     <td><td>Between P and Q occurrences of <i>X</i>
  @ <tr><td><td><b>(</b><i>X</i><b>)</b>
  @     <td><td><i>X</i>
  @ <tr><td><td><i>X</i><b>|</b><i>Y</i>
  @     <td><td><i>X</i> or <i>Y</i>
  @ <tr><td><td><b>^</b><i>X</i>
  @     <td><td><i>X</i> at the beginning of the string
  @ <tr><td><td><i>X</i><b>$</b>
  @     <td><td><i>X</i> at the end of the string
  @ <tr><td><td><b>.</b>
  @     <td><td>Any single character
  @ <tr><td><td><b>\</b><i>C</i>
  @     <td><td>Character <i>C</i> if <i>C</i> is one of: <b>\{}()[]|*+?</b>
  @ <tr><td><td><b>\</b><i>C</i>
  @     <td><td>C-language escapes if <i>C</i> is one of: <b>afnrtv</b>
  @ <tr><td><td><b>\u</b><i>HHHH</i>
  @     <td><td>Unicode character U+HHHH where <i>HHHH</i> is four hex digits
  @ <tr><td><td><b>\</b><i>HH</i>
  @     <td><td>Unicode character U+00HH where <i>HH</i> is two hex digits
  @ <tr><td><td><b>[</b><i>abc</i><b>]</b>
  @     <td><td>Any single character from <i>abc</i>
  @ <tr><td><td><b>[^</b><i>abc</i><b>]</b>
  @     <td><td>Any single character not in <i>abc</i>
  @ <tr><td><td><b>[</b><i>a-z</i><b>]</b>
  @     <td><td>Any single character between <i>a</i> and <i>z</i>, inclusive
  @ <tr><td><td><b>[^</b><i>a-z</i><b>]</b>
  @     <td><td>Any single character not between <i>a</i> and <i>z</i>
  @ <tr><td><td><b>\b</b>
  @     <td><td>Word boundary
  @ <tr><td><td><b>\w</b>
  @     <td><td>A word character: a-zA-Z0-9 or _
  @ <tr><td><td><b>\W</b>
  @     <td><td>A non-word character
  @ <tr><td><td><b>\d</b>
  @     <td><td>A digit.  0-9
  @ <tr><td><td><b>\D</b>
  @     <td><td>A non-digit character
  @ <tr><td><td><b>\s</b>
  @     <td><td>A whitespace character
  @ <tr><td><td><b>\S</b>
  @     <td><td>A non-whitespace character
  @ </table>
  @ 
  @ <p>In the "Pattern" column of the table above:</p>
  @ <ul>
  @ <li> "<i>X</i>" and "<i>Y</i>" mean any subpattern
  @ <li> "<i>P</i>" and "<i>Q</i>" mean integers
  @ <li> "<i>C</i>" means a single character
  @ <li> "<i>H</i>" means a hexadecimal digit
  @ <li> "<i>abc</i>" means any sequences of one or more characters
  @ <li> "<i>a-z</i>" means any single character, a single "<b>-</b>"
  @      character, and then one additional character.
  @ <li> All other symbols in the patterns are literal text
  @ </ul>
  @ 
  @ <p>The "<i>X</i><b>|</b><i>Y</i>" pattern has lower precedence
  @ than the others.  Use "<b>(</b>...<b>)</b>" for grouping, as
  @ necessary.
  style_finish_page();
}
