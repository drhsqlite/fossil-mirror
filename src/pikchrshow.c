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
** A pikchr code editor and previewer, allowing users to experiment
** with pikchr code or prototype it for use in copy/pasting into forum
** posts, wiki pages, or embedded docs.
**
** It optionally accepts a p=pikchr-script-code URL parameter or POST
** value to pre-populate the editor with that code.
*/
void pikchrshow_page(void){
  const char *zContent = 0;
  int isDark;              /* true if the current skin is "dark" */

  login_check_credentials();
  if( !g.perm.RdWiki && !g.perm.Read && !g.perm.RdForum ){
    cgi_redirectf("%s/login?g=%s/pikchrshow", g.zTop, g.zTop);
  }
  zContent = PD("content",P("p"));
  if(P("ajax")!=0){
    /* Called from the JS-side preview updater. */
    cgi_set_content_type("text/html");
    if(zContent && *zContent){
      int w = 0, h = 0;
      char *zOut = pikchr(zContent, "pikchr", 0, &w, &h);
      if( w>0 && h>0 ){
        const char *zNonce = safe_html_nonce(1);
        CX("%s<div style='max-width:%dpx;'>\n%s</div>%s",
           zNonce, w, zOut, zNonce);
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
  style_emit_noscript_for_js_page();
  isDark = skin_detail_boolean("white-foreground");
  if(!zContent){
    zContent = "arrow right 200% \"Markdown\" \"Source\"\n"
      "box rad 10px \"Markdown\" \"Formatter\" \"(markdown.c)\" fit\n"
      "arrow right 200% \"HTML+SVG\" \"Output\"\n"
      "arrow <-> down from last box.s\n"
      "box same \"Pikchr\" \"Formatter\" \"(pikchr.c)\" fit\n";
  }
  style_header("PikchrShow");
  CX("<style>");
  CX("div.content { padding-top: 0.5em }\n");
  CX("#sbs-wrapper {"
     "display: flex; flex-direction: column;"
     "}\n");
  CX("#sbs-wrapper > * {"
     "margin: 0 0.25em 0.5em 0; flex: 1 10 auto;"
     "align-self: stretch;"
     "}\n");
  CX("#sbs-wrapper textarea {"
     "max-width: initial; flex: 1 1 auto;"
     "}\n");
  CX("#pikchrshow-output, #pikchrshow-form"
     "{display: flex; flex-direction: column; align-items: stretch;}");
  CX("#pikchrshow-form > * {margin: 0.25em 0}\n");
  CX("#pikchrshow-output {flex: 5 1 auto; padding: 0}\n");
  CX("#pikchrshow-output > pre, "
     "#pikchrshow-output > pre > div, "
     "#pikchrshow-output > pre > div > pre "
     "{margin: 0; padding: 0}\n");
  CX("#pikchrshow-output.error > pre "
     /* Server-side error report */
     "{padding: 0.5em}\n");
  CX("#pikchrshow-controls {" /* where the buttons live */
     "display: flex; flex-direction: row; "
     "align-items: center; flex-wrap: wrap;"
     "}\n");
  CX("#pikchrshow-controls > * {"
     "display: inline; margin: 0 0.25em 0.5em 0;"
     "}\n");
  CX("#pikchrshow-output-wrapper label {"
     "cursor: pointer;"
     "}\n");
  CX("body.pikchrshow .input-with-label > * {"
     "margin: 0 0.2em;"
     "}\n");
  CX("body.pikchrshow .input-with-label > label {"
     "cursor: pointer;"
     "}\n");
  CX("#pikchrshow-output.dark-mode svg {"
     /* Flip the colors to approximate a dark theme look */
     "filter: invert(1) hue-rotate(180deg);"
     "}\n");
  CX("#pikchrshow-output-wrapper {"
     "padding: 0.25em 0.5em; border-radius: 0.25em;"
     "border-width: 1px;"/*some skins disable fieldset borders*/
     "}\n");
  CX("#pikchrshow-output-wrapper > legend > *:not(.copy-button){"
     "margin-right: 0.5em; vertical-align: middle;"
     "}\n");
  CX("body.pikchrshow .v-align-middle{"
     "vertical-align: middle"
     "}\n");
  CX(".dragover {border: 3px dotted rgba(0,255,0,0.6)}\n");
  CX("</style>");
  CX("<div>Input pikchr code and tap Preview to render it:</div>");
  CX("<div id='sbs-wrapper'>");
  CX("<div id='pikchrshow-form'>");
  CX("<textarea id='content' name='content' rows='15'>%s</textarea>",
     zContent/*safe-for-%s*/);
  CX("<div id='pikchrshow-controls'>");
  CX("<button id='pikchr-submit-preview'>Preview</button>");
  style_labeled_checkbox("flipcolors-wrapper", "flipcolors",
                         "Dark mode?",
                         "1", isDark, 0);
  CX("</div>"/*#pikchrshow-controls*/);
  CX("</div>"/*#pikchrshow-form*/);
  CX("<fieldset id='pikchrshow-output-wrapper'>");
  CX("<legend></legend>"
     /* Reminder: Firefox does not properly flexbox a LEGEND element,
        always flowing it in column mode. */);
  CX("<div id='pikchrshow-output'>");
  if(*zContent){
    int w = 0, h = 0;
    char *zOut = pikchr(zContent, "pikchr", 0, &w, &h);
    if( w>0 && h>0 ){
      const char *zNonce = safe_html_nonce(1);
      CX("%s<div style='max-width:%dpx;'>\n%s</div>%s",
         zNonce, w, zOut, zNonce);
    }else{
      CX("<pre>\n%s\n</pre>\n", zOut);
    }
    fossil_free(zOut);
  }
  CX("</div>"/*#pikchrshow-output*/);
  CX("</fieldset>"/*#pikchrshow-output-wrapper*/);
  CX("</div>"/*sbs-wrapper*/);
  if(!builtin_bundle_all_fossil_js_apis()){
    builtin_emit_fossil_js_apis("dom", "fetch", "copybutton",
                                "popupwidget", 0);
  }
  builtin_emit_fossil_js_apis("page.pikchrshow", 0);
  builtin_fulfill_js_requests();
  style_footer();
}

static void pikchr_th_init(u32 fThInit){
  Th_FossilInit(fThInit & TH_INIT_MASK);
}

/*
** COMMAND: pikchr
**
** Usage: %fossil pikchr [options] ?INFILE? ?OUTFILE?
**
** Options:
**
**    -div      On success, adds a DIV wrapper around the
**              resulting SVG output which limits its max-width.
**
**    -th       Process the input using TH1 before passing it to pikchr.
**
**    -th-novar Disable $var and $<var> TH1 processing. Only applies
**              with the -th flag.
**
**    -th-nosvg When using -th, output the post-TH1'd script
**              instead of the pikchr-rendered output.
**
**    -th-trace Trace TH1 execution (for debugging purposes)
**
** TH1 Caveats: the built-in TH1 commands make some assumptions about
** HTML escaping and output which do not apply via this
** command. e.g. some commands will output directly to stdout, rather
** than the output buffer this command requires. Improvements in that
** regard are under consideration/construction.
*/
void pikchr_cmd(void){
  Blob bIn = empty_blob;
  Blob bOut = empty_blob;
  const char * zInfile = "-";
  const char * zOutfile = "-";
  const int fWithDiv = find_option("div",0,0)!=0;
  const int fTh1 = find_option("th",0,0)!=0;
  const int fNosvg = find_option("th-nosvg",0,0)!=0;
  int isErr = 0;
  u32 fThFlags = TH_INIT_DEFAULT/* | TH_INIT_NO_ESC*/
    | (find_option("th-novar",0,0)!=0 ? TH_R2B_NO_VARS : 0);

  Th_InitTraceLog()/*processes -th-trace flag*/;
  verify_all_options();
  if(g.argc>4){
    usage("?INFILE? ?OUTFILE?");
  }
  if(g.argc>2){
    zInfile = g.argv[2];
  }
  if(g.argc>3){
    zOutfile = g.argv[3];
  }
  blob_read_from_file(&bIn, zInfile, ExtFILE);
  if(fTh1){
    Blob out = empty_blob;
    db_find_and_open_repository(OPEN_ANY_SCHEMA | OPEN_OK_NOT_FOUND, 0)
      /* ^^^ needed for certain TH1 functions to work */;
    pikchr_th_init(fThFlags);
    isErr = Th_RenderToBlob(blob_str(&bIn), &out, fThFlags) ? 1 : 0;
    if(isErr){
      blob_reset(&bOut);
      bOut = out;
    }else{
      blob_reset(&bIn);
      bIn = out;
    }
  }
  if(!isErr){
    if(fTh1 && fNosvg){
      assert(0==blob_size(&bOut));
      bOut = bIn;
      bIn = empty_blob;
    }else{
      int w = 0, h = 0;
      const char * zContent = blob_str(&bIn);
      char *zOut;

      zOut = pikchr(zContent, "pikchr", 0, &w, &h);
      if( w>0 && h>0 ){
        if(fWithDiv){
          blob_appendf(&bOut,"<div style='max-width:%dpx;'>\n", w);
        }
        blob_append(&bOut, zOut, -1);
        if(fWithDiv){
          blob_append(&bOut,"</div>\n", 7);
        }
      }else{
        isErr = 2;
        blob_append(&bOut, zOut, -1);
      }
      fossil_free(zOut);
    }
  }
  if(isErr){
    /*fossil_print("ERROR: raw input:\n%b\n", &bIn);*/
    fossil_fatal("%s ERROR: %b", 1==isErr ? "TH1" : "pikchr",
                 &bOut);
  }else{
    blob_write_to_file(&bOut, zOutfile);
  }
  Th_PrintTraceLog();
  blob_reset(&bIn);
  blob_reset(&bOut);
}
