/*
** Copyright (c) 2010 Michael T. Richter, assigned to the Fossil SCM project.
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
**   ttmrichter@gmail.com
**
*******************************************************************************
**
** This utility is a preprocessor that takes raw CSS and HTML files, along with
** plain text descriptions, and generates the skins.c file that is used to embed
** the distributed default skins into Fossil.
**
** The intent of the utility is to make adding new skins to a Fossil build
** easier without involving error-prone C programming.
**
** This utility must be run BEFORE the translate program is executed on the C
** source files.  (This is in retrospect obvious since it generates one of the
** said C files.)
*/
#include <stdio.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv){
  if( argc==3 ){
    /*
    ** 1. Validate the arguments: <skins> and <src> directory.
    ** 2. Open the <src>/skins.c file.
    ** 3. Write the initial boilerplate.
    ** 4. Walk the directory structure of <skins>.
    ** 5. For each one:
    **    a) Store the title and author information. (info.txt)
    **    b) Write out the description as a comment. (info.txt)
    **    c) Write out the CSS information.          (style.css)
    **    d) Write out the header information.       (header.html)
    **    e) Write out the footer information.       (footer.html)
    ** 6. Write out the built-in skins table.
    ** 7. Write the trailing boilerplate.
    */
  }else{
    /* error -- need a pair of directories */
  }
  return 0;
}
