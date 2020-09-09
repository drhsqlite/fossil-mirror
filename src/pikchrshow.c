/*
** Copyright (c) 2020 D. Richard Hipp
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
** This file contains fossil-specific code related to pikchr.
*/
#include "config.h"
#include <assert.h>
#include <ctype.h>
#include "pikchrshow.h"


/*
** WEBPAGE: pikchrshow
*/
void pikchrshow_cmd(void){
  const char *zContent = P("content");

  login_check_credentials();
  if( !g.perm.WrWiki && !g.perm.Write ){
    cgi_redirectf("%s/login?g=%s/pikchrshow", g.zTop, g.zTop);
  }
  if(!zContent){
    zContent = "arrow right 200% \"Markdown\" \"Source\"\n"
      "box rad 10px \"Markdown\" \"Formatter\" \"(markdown.c)\" fit\n"
      "arrow right 200% \"HTML+SVG\" \"Output\"\n"
      "arrow <-> down from last box.s\n"
      "box same \"Pikchr\" \"Formatter\" \"(pikchr.c)\" fit\n";
  }
  style_header("PikchrShow");
  CX("<style>"
     "#pikchrshow-output, #pikchrshow-form"
     "{display: flex; flex-direction: column;}"
     "#pikchrshow-form > * {margin: 0.25em 0;}"
     "#pikchrshow-output {margin-top: 1em;}"
     "</style>");
  CX("<form method='POST' id='pikchrshow-form' action=''>");
  CX("<div>Input pikchr code and tap SUBMIT to render it:</div>");
  CX("<textarea name='content' rows='20' cols='80'>");
  CX("%s", zContent/*safe-for-%s*/);
  CX("</textarea>");
  CX("<input type='submit'></input>");
  CX("</form>");
  CX("<div id='pikchrshow-output'>");
  if(*zContent){
    int w = 0, h = 0;
    char *zOut = pikchr(zContent, "pikchr", 0, &w, &h);
    if( w>0 && h>0 ){
      const char *zNonce = safe_html_nonce(1);
      CX("%s\n%s%s", zNonce, zOut, zNonce);
    }else{
      CX("<pre>\n%s\n</pre>\n", zOut);
    }
    fossil_free(zOut);
  }
  CX("</div>");
  style_footer();
}

