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
** This file contains code used to deal with moderator actions for
** Wiki and Tickets.
*/
#include "config.h"
#include "moderate.h"
#include <assert.h>


/*
** Create a table to represent pending moderation requests, if the
** table does not already exist.
*/
void moderation_table_create(void){
  db_multi_exec(
     "CREATE TABLE IF NOT EXISTS modreq("
     "  mreqid INTEGER PRIMARY KEY,"  /* Unique ID for the request */
     "  objid INT UNIQUE,"            /* Record pending approval */
     "  ctime DATETIME,"              /* Julian day number */
     "  user TEXT,"                   /* Name of user submitter */
     "  ipaddr TEXT,"                 /* IP address of submitter */
     "  mtype TEXT,"                  /* 't', 'w', 'at', or 'aw' */
     "  afile INT,"                   /* File being attached, or NULL */
     "  aid ANY"                      /* TicketId or Wiki Name */
     ");"
  );
}
