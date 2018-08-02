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
** This file contains code used to implement a "bag" of integers.
** A bag is an unordered collection without duplicates.  In this
** implementation, all elements must be positive integers.
*/
#include "config.h"
#include "bag.h"
#include <assert.h>


#if INTERFACE
/*
** An integer can appear in the bag at most once.
** Integers must be positive.
**
** On a hash collision, search continues to the next slot in the array,
** looping back to the beginning of the array when we reach the end.
** The search stops when a match is found or upon encountering a 0 entry.
**
** When an entry is deleted, its value is changed to -1.
**
** Bag.cnt is the number of live entries in the table.  Bag.used is
** the number of live entries plus the number of deleted entries.  So
** Bag.used>=Bag.cnt.  We want to keep Bag.used-Bag.cnt as small as
** possible.
**
** The length of a search increases as the hash table fills up.  So
** the table is enlarged whenever Bag.used reaches half of Bag.sz.  That
** way, the expected collision length never exceeds 2.
*/
struct Bag {
  int cnt;   /* Number of integers in the bag */
  int sz;    /* Number of slots in a[] */
  int used;  /* Number of used slots in a[] */
  int *a;    /* Hash table of integers that are in the bag */
};
#endif

/*
** Initialize a Bag structure
*/
void bag_init(Bag *p){
  memset(p, 0, sizeof(*p));
}

/*
** Destroy a Bag.  Delete all of its content.
*/
void bag_clear(Bag *p){
  free(p->a);
  bag_init(p);
}

/*
** The hash function
*/
#define bag_hash(i)  (i*101)

/*
** Change the size of the hash table on a bag so that
** it contains N slots
**
** Completely reconstruct the hash table from scratch.  Deleted
** entries (indicated by a -1) are removed.  When finished, it
** should be the case that p->cnt==p->used.
*/
static void bag_resize(Bag *p, int newSize){
  int i;
  Bag old;
  int nDel = 0;   /* Number of deleted entries */
  int nLive = 0;  /* Number of live entries */

  old = *p;
  assert( newSize>old.cnt );
  p->a = fossil_malloc( sizeof(p->a[0])*newSize );
  p->sz = newSize;
  memset(p->a, 0, sizeof(p->a[0])*newSize );
  for(i=0; i<old.sz; i++){
    int e = old.a[i];
    if( e>0 ){
      unsigned h = bag_hash(e)%newSize;
      while( p->a[h] ){
        h++;
        if( h==newSize ) h = 0;
      }
      p->a[h] = e;
      nLive++;
    }else if( e<0 ){
      nDel++;
    }
  }
  assert( p->cnt == nLive );
  assert( p->used == nLive+nDel );
  p->used = p->cnt;
  bag_clear(&old);
}

/*
** Insert element e into the bag if it is not there already.
** Return TRUE if the insert actually occurred.  Return FALSE
** if the element was already in the bag.
*/
int bag_insert(Bag *p, int e){
  unsigned h;
  int rc = 0;
  assert( e>0 );
  if( p->used+1 >= p->sz/2 ){
    int n = p->sz*2;
    bag_resize(p,  n + 20 );
  }
  h = bag_hash(e)%p->sz;
  while( p->a[h]>0 && p->a[h]!=e ){
    h++;
    if( h>=p->sz ) h = 0;
  }
  if( p->a[h]<=0 ){
    if( p->a[h]==0 ) p->used++;
    p->a[h] = e;
    p->cnt++;
    rc = 1;
  }
  return rc;
}

/*
** Return true if e in the bag.  Return false if it is no.
*/
int bag_find(Bag *p, int e){
  unsigned h;
  assert( e>0 );
  if( p->sz==0 ){
    return 0;
  }
  h = bag_hash(e)%p->sz;
  while( p->a[h] && p->a[h]!=e ){
    h++;
    if( h>=p->sz ) h = 0;
  }
  return p->a[h]==e;
}

/*
** Remove element e from the bag if it exists in the bag.
** If e is not in the bag, this is a no-op.
*/
void bag_remove(Bag *p, int e){
  unsigned h;
  assert( e>0 );
  if( p->sz==0 ) return;
  h = bag_hash(e)%p->sz;
  while( p->a[h] && p->a[h]!=e ){
    h++;
    if( h>=p->sz ) h = 0;
  }
  if( p->a[h] ){
    int nx = h+1;
    if( nx>=p->sz ) nx = 0;
    if( p->a[nx]==0 ){
      p->a[h] = 0;
      p->used--;
    }else{
      p->a[h] = -1;
    }
    p->cnt--;
    if( p->cnt==0 ){
      memset(p->a, 0, p->sz*sizeof(p->a[0]));
      p->used = 0;
    }else if( p->sz>40 && p->cnt<p->sz/8 ){
      bag_resize(p, p->sz/2);
    }
  }
}

/*
** Return the first element in the bag.  Return 0 if the bag
** is empty.
*/
int bag_first(Bag *p){
  int i;
  for(i=0; i<p->sz && p->a[i]<=0; i++){}
  if( i<p->sz ){
    return p->a[i];
  }else{
    return 0;
  }
}

/*
** Return the next element in the bag after e.  Return 0 if
** is the last element in the bag.  Any insert or removal from
** the bag might reorder the bag.
*/
int bag_next(Bag *p, int e){
  unsigned h;
  assert( p->sz>0 );
  assert( e>0 );
  h = bag_hash(e)%p->sz;
  while( p->a[h] && p->a[h]!=e ){
    h++;
    if( h>=p->sz ) h = 0;
  }
  assert( p->a[h] );
  h++;
  while( h<p->sz && p->a[h]<=0 ){
    h++;
  }
  return h<p->sz ? p->a[h] : 0;
}

/*
** Return the number of elements in the bag.
*/
int bag_count(Bag *p){
  return p->cnt;
}
