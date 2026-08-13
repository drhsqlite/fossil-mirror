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
** This file contains code used to implement a priority queue.
** A priority queue is a list of items order by a floating point
** value.  Each value can be associated with either a pointer or
** an integer.  Items are inserted into the queue in an arbitrary
** order, but are returned in order of the floating point value.
**
** This implementation uses a heap of QueueElement objects.  The
** root of the heap is PQueue.a[0].  Each node a[x] has two daughter
** nodes a[x*2+1] and a[x*2+2].  The mother node of a[y] is a[(y-1)/2]
** (assuming integer division rounded down).  The following is always true:
**
**    The value of any node is less than or equal two the values
**    of both daughter nodes.  (The Heap Property).
**
** A consequence of the heap property is that a[0] always contains
** the node with the smallest value.
**
** Compatibility note:  Some versions of OpenSSL export a symbols
** like "pqueue_insert".  This is, technically, a bug in OpenSSL.
** We work around it here by using "pqueuex_" instead of "pqueue_".
*/
#include "config.h"
#include "pqueue.h"
#include <assert.h>


#if INTERFACE
/*
** An integer can appear in the bag at most once.
** Integers must be positive.
*/
struct PQueue {
  int cnt;   /* Number of entries in the queue */
  int sz;    /* Number of slots in a[] */
  struct QueueElement {
    union {
      int id;          /* ID of the element */
      void *p;         /* Pointer to an object */
    } u;
    double value;    /* Value of element.  Kept in ascending order */
  } *a;
};
#endif

/*
** Initialize a PQueue structure
*/
void pqueuex_init(PQueue *p){
  memset(p, 0, sizeof(*p));
}

/*
** Destroy a PQueue.  Delete all of its content.
*/
void pqueuex_clear(PQueue *p){
  free(p->a);
  pqueuex_init(p);
}

/*
** Change the size of the queue so that it contains N slots
*/
static void pqueuex_resize(PQueue *p, int N){
  p->a = fossil_realloc(p->a, sizeof(p->a[0])*N);
  p->sz = N;
}

/*
** Allocate a new queue entry and return a pointer to it.
*/
static struct QueueElement *pqueuex_new_entry(PQueue *p){
  if( p->cnt+1>p->sz ){
    pqueuex_resize(p, p->cnt+7);
  }
  return &p->a[p->cnt++];
}

/*
** Element p->a[p->cnt-1] has just been inserted.  Shift entries
** around so as to preserve the heap property.
*/
static void pqueuex_rebalance(PQueue *p){
  int i, j;
  struct QueueElement *a = p->a;
  i = p->cnt-1;
  while( (j = (i-1)/2)>=0 && a[j].value>a[i].value ){
    struct QueueElement t = a[j];
    a[j] = a[i];
    a[i] = t;
    i = j;
  }
}

/*
** Insert element e into the queue.
*/
void pqueuex_insert(PQueue *p, int e, double v){
  struct QueueElement *pE = pqueuex_new_entry(p);
  pE->value = v;
  pE->u.id = e;
  pqueuex_rebalance(p);
}
void pqueuex_insert_ptr(PQueue *p, void *pPtr, double v){
  struct QueueElement *pE = pqueuex_new_entry(p);
  pE->value = v;
  pE->u.p = pPtr;
  pqueuex_rebalance(p);
}

/*
** Remove and discard p->a[0] element from the queue.  Rearrange
** nodes to preserve the heap property.
*/
static void pqueuex_pop(PQueue *p){
  int i, j;
  struct QueueElement *a = p->a;
  struct QueueElement tmp;
  i = 0;
  a[0] = a[p->cnt-1];
  p->cnt--;
  while( (j = i*2+1)<p->cnt ){
    if( j+1<p->cnt && a[j].value > a[j+1].value ) j++;
    if( a[i].value < a[j].value ) break;
    tmp = a[i];
    a[i] = a[j];
    a[j] = tmp;
    i = j;
  }
}

/*
** Extract the first element from the queue (the element with
** the smallest value) and return its ID.  Return 0 if the queue
** is empty.
*/
int pqueuex_extract(PQueue *p){
  int e;
  if( p->cnt==0 ){
    return 0;
  }
  e = p->a[0].u.id;
  pqueuex_pop(p);
  return e;
}
void *pqueuex_extract_ptr(PQueue *p){
  void *pPtr;
  if( p->cnt==0 ){
    return 0;
  }
  pPtr = p->a[0].u.p;
  pqueuex_pop(p);
  return pPtr;
}

/*
** Print the entire heap associated with the test-pqueue command.
*/
static void pqueuex_test_print(PQueue *p){
  int j;
  for(j=0; j<p->cnt; j++){
    fossil_print("(%d) %g/%s ",j,p->a[j].value,p->a[j].u.p);
  }
  fossil_print("\n");
}

/*
** COMMAND: test-pqueue
**
** This command is used for testing the PQueue object.  There are one
** or more arguments, each of the form:
**
**     (1)    NUMBER/TEXT
**     (2)    ^
**     (3)    -v
**
** Form (1) arguments add an entry to the queue with value NUMBER and
** content TEXT.  Form (2) pops off the queue entry with the smallest
** value.  Form (3) (the -v option) causes the heap to be displayed after
** each subsequent operation.
*/
void pqueuex_test_cmd(void){
  int i;
  PQueue x;
  const char *zId;
  int bDebug = 0;

  pqueuex_init(&x);
  for(i=2; i<g.argc; i++){
    const char *zArg = g.argv[i];
    if( strcmp(zArg,"-v")==0 ){
      bDebug = 1;
    }else if( strcmp(zArg, "^")==0 ){
      zId = pqueuex_extract_ptr(&x);
      if( zId==0 ){
        fossil_print("%2d: POP     NULL\n", i);
      }else{
        fossil_print("%2d: POP     \"%s\"\n", i, zId);
      }
      if( bDebug) pqueuex_test_print(&x);
    }else{
      double r = atof(zArg);
      zId = strchr(zArg,'/');
      if( zId==0 ) zId = zArg;
      if( zId[0]=='/' ) zId++;
      pqueuex_insert_ptr(&x, (void*)zId, r);
      fossil_print("%2d: INSERT  \"%s\"\n", i, zId);
      if( bDebug) pqueuex_test_print(&x);
    }
  }
  while( (zId = pqueuex_extract_ptr(&x))!=0 ){
    fossil_print("... POP     \"%s\"\n", zId);
    if( bDebug) pqueuex_test_print(&x);
  }
  pqueuex_clear(&x);
}
