/*
** Copyright (c) 2007 D. Richard Hipp
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
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code used to implement a priority queue.
** A priority queue is a list of items order by a floating point
** value.  We can insert integers with each integer tied to its
** value then extract the integer with the smallest value.
**
** The way this queue is used, we never expect it to contain more
** than 2 or 3 elements, so a simple array is sufficient as the
** implementation.  This could give worst case O(N) insert times,
** but because of the nature of the problem we expect O(1) performance.
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
    int id;          /* ID of the element */
    double value;    /* Value of element.  Kept in ascending order */
  } *a;
};
#endif

/*
** Initialize a PQueue structure
*/
void pqueue_init(PQueue *p){
  memset(p, 0, sizeof(*p));
}

/*
** Destroy a PQueue.  Delete all of its content.
*/
void pqueue_clear(PQueue *p){
  free(p->a);
  pqueue_init(p);
}

/*
** Change the size of the queue so that it contains N slots
*/
static void pqueue_resize(PQueue *p, int N){
  p->a = realloc(p->a, sizeof(p->a[0])*N);
  p->sz = N;
}

/*
** Insert element e into the queue.
*/
void pqueue_insert(PQueue *p, int e, double v){
  int i, j;
  if( p->cnt+1>p->sz ){
    pqueue_resize(p, p->cnt+5);
  }
  for(i=0; i<p->cnt; i++){
    if( p->a[i].value>v ){
      for(j=p->cnt; j>i; j--){
        p->a[j] = p->a[j-1];
      }
      break;
    }
  }
  p->a[i].id = e;
  p->a[i].value = v;
  p->cnt++;
}

/*
** Extract the first element from the queue (the element with
** the smallest value) and return its ID.  Return 0 if the queue
** is empty.
*/
int pqueue_extract(PQueue *p){
  int e, i;
  if( p->cnt==0 ){
    return 0;
  }
  e = p->a[0].id;
  for(i=0; i<p->cnt-1; i++){
    p->a[i] = p->a[i+1];
  }
  p->cnt--;
  return e;
}
