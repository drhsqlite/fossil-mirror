/*
** Copyright (c) 2021 D. Richard Hipp
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
*******************************************************************************
**
** This file contains code used to implement the "diff" command
*/
#include "config.h"
#include "patch.h"
#include <assert.h>


/*
** COMMAND: patch
**
** Usage: %fossil patch SUBCOMMAND ?ARGS ..?
**
** This command is used to creates, view, and apply Fossil binary patches.
** A Fossil binary patch is a single (binary) file that captures all of the
** uncommitted changes of a check-out.  Use Fossil binary patches to transfer
** proposed or incomplete changes between machines for testing or analysis.
**
** > fossil patch create FILENAME
**
**       Create a new binary patch in FILENAME that captures all uncommitted
**       changes in the current check-out.
**
** > fossil patch apply FILENAME
**
**       Apply the changes in FILENAME to the current check-out.
**
** > fossil patch diff [DIFF-FLAGS] FILENAME
**
**       View the changes specified by the binary patch FILENAME in a
**       human-readable format.  The usual diff flags apply.
**
** > fossil patch push REMOTE-CHECKOUT
**
**       Create a patch for the current check-out, transfer that patch to
**       a remote machine (using ssh) and apply the patch there.
**
** > fossil patch pull REMOTE-CHECKOUT
**
**       Create a patch on a remote check-out, transfer that patch to the
**       local machine (using ssh) and apply the patch in the local checkout.
*/
void patch_cmd(void){
  const char *zCmd;
  size_t n;
  if( g.argc<3 ){
    patch_usage:
    usage("apply|create|pull|push|view");
  }
  zCmd = g.argv[2];
  n = strlen(zCmd);
  if( strncmp(zCmd, "apply", n)==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("apply FILENAME");
    }
    fossil_print("TBD...\n");
  }else
  if( strncmp(zCmd, "create", n)==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("create FILENAME");
    }
  }else
  if( strncmp(zCmd, "pull", n)==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("pull REMOTE-CHECKOUT");
    }
    fossil_print("TBD...\n");
  }else
  if( strncmp(zCmd, "push", n)==0 ){
    db_must_be_within_tree();
    verify_all_options();
    if( g.argc!=4 ){
      usage("push REMOTE-CHECKOUT");
    }
    fossil_print("TBD...\n");
  }else
  if( strncmp(zCmd, "view", n)==0 ){
    /* u64 diffFlags = diff_options(); */
    verify_all_options();
    if( g.argc!=4 ){
      usage("view FILENAME");
    }
    fossil_print("TBD...\n");
  }else
  {
    goto patch_usage;
  } 
}
