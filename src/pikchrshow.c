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
  const char *zContent = 0;
  int isDark, flipColors;

  login_check_credentials();
  if( !g.perm.RdWiki && !g.perm.Read && !g.perm.RdForum ){
    cgi_redirectf("%s/login?g=%s/pikchrshow", g.zTop, g.zTop);
  }
  zContent = P("content");
  if(P("ajax")!=0){
    /* Called from the JS-side preview updater. */
    cgi_set_content_type("text/html");
    if(zContent && *zContent){
      int w = 0, h = 0;
      char *zOut = pikchr(zContent, "pikchr", 0, &w, &h);
      if( w>0 && h>0 ){
        const char *zNonce = safe_html_nonce(1);
        CX("%s\n%s%s", zNonce, zOut, zNonce);
      }else{
        cgi_printf_header("x-pikchrshow-is-error: 1\r\n");
        CX("<pre>\n%s\n</pre>\n", zOut);
      }
      fossil_free(zOut);
    }else{
      CX("<pre>No content! Nothing to render</pre>");
    }
    return;
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
  CX("#pikchrshow-output {"
     "flex: 1 1 auto; border-width: 1px; border-style: solid;"
     "border-radius: 0.25em; padding: 0.5em;"
     "}");
  CX("#pikchrshow-output > pre, "
     "#pikchrshow-output > pre > div, "
     "#pikchrshow-output > pre > div > pre "
     "{margin: 0; padding: 0}");
  CX("#pikchrshow-controls {"
     "display: flex; flex-direction: row; align-items: center;"
     "}");
  CX("#pikchrshow-controls > * {"
     "display: inline; margin-left: 0.5em;"
     "}");
  CX("#pikchrshow-controls > .input-with-label > * {"
     "cursor: pointer;"
     "}");
  CX("#pikchrshow-output.dark-mode > svg {"
     /* Flip the colors to approximate a dark theme look */
     "filter: invert(1) hue-rotate(180deg);"
     "}");
  CX("</style>");
  CX("<div>Input pikchr code and tap Preview to render it:</div>");
  CX("<div id='sbs-wrapper'>");
  CX("<div id='pikchrshow-form'>");
  CX("<textarea id='content' name='content' rows='15'>%s</textarea>",
     zContent/*safe-for-%s*/);
  CX("<div id='pikchrshow-controls'>");
  CX("<button id='pikchr-submit-preview'>Preview</button>");
  style_labeled_checkbox("flipcolors-wrapper", "flipcolors",
                         "Simulate dark color theme?",
                         "1", flipColors, 0);
  CX("</div>"/*#pikchrshow-controls*/);
  CX("</div>"/*#pikchrshow-form*/);
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
  if(!builtin_bundle_all_fossil_js_apis()){
    builtin_emit_fossil_js_apis("dom", "fetch", 0);
  }
  builtin_emit_fossil_js_apis("page.pikchrshow", 0);
  builtin_fulfill_js_requests();
  style_footer();
}

