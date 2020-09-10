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
**
** A basic pikchr code editor and renderer, allowing users with write-
** or wiki-write permissions to experiment with pikchr code.
*/
void pikchrshow_cmd(void){
  const char *zContent = P("content");
  int isDark, flipColors;

  login_check_credentials();
  if( !g.perm.WrWiki && !g.perm.Write ){
    cgi_redirectf("%s/login?g=%s/pikchrshow", g.zTop, g.zTop);
  }
  isDark = skin_detail_boolean("white-foreground");
  flipColors = zContent ? P("flipcolors")!=0 : isDark;
  if(!zContent){
    zContent = "arrow right 200% \"Markdown\" \"Source\"\n"
      "box rad 10px \"Markdown\" \"Formatter\" \"(markdown.c)\" fit\n"
      "arrow right 200% \"HTML+SVG\" \"Output\"\n"
      "arrow <-> down from last box.s\n"
      "box same \"Pikchr\" \"Formatter\" \"(pikchr.c)\" fit\n";
  }
  style_header("PikchrShow");
  CX("<style>");
  CX("#sbs-wrapper {"
     "display: flex; flex-direction: row; flex-wrap: wrap;"
     "}");
  CX("#sbs-wrapper > * {margin: 0 0 1em 0}");
  CX("#pikchrshow-output, #pikchrshow-form"
     "{display: flex; flex-direction: column}");
  CX("#pikchrshow-form {flex: 2 1 auto}");
  CX("#pikchrshow-form > * {margin: 0.25em 0}");
  CX("#pikchrshow-output {flex: 1 1 auto}");
  CX("#pikchrshow-controls {"
     "display: flex; flex-direction: row; align-items: center;"
     "}");
  CX("#pikchrshow-controls > * {"
     "display: inline; margin-left: 0.5em;"
     "}");
  CX("#pikchrshow-controls > .input-with-label > * {"
     "cursor: pointer;"
     "}");
  CX("</style>");
  if( flipColors ){
    /* Flip the colors to approximate a dark theme look */
    CX("<style>#pikchrshow-output > svg {"
       "filter: invert(1) hue-rotate(180deg);"
       "}</style>");
  }
  CX("<div>Input pikchr code and tap SUBMIT to render it:</div>");
  CX("<div id='sbs-wrapper'>");
  CX("<form method='POST' id='pikchrshow-form' action=''>");
  CX("<textarea name='content' rows='15'>%s</textarea>",
     zContent/*safe-for-%s*/);
  CX("<div id='pikchrshow-controls'>");
  CX("<input type='submit' value='Submit'></input>");
  style_labeled_checkbox(0, "flipcolors", "Simulate dark color theme?",
                         "1", flipColors, 0);
  CX("</div>"/*#pikchrshow-controls*/);
  CX("</form>"/*#pikchrshow-form*/);
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
  CX("</div>"/*#pikchrshow-output*/);
  CX("</div>"/*sbs-wrapper*/);
  style_footer();
}

