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

#ifdef INTERFACE
/* These are described in pikchr_process()'s docs. */
#define PIKCHR_PROCESS_TH1        0x01
#define PIKCHR_PROCESS_TH1_NOSVG  0x02
#define PIKCHR_PROCESS_DIV        0x04
#define PIKCHR_PROCESS_NONCE      0x08
#define PIKCHR_PROCESS_ERR_PRE    0x10
#endif

/*
** Processes a pikchr script, optionally with embedded TH1. zIn is the
** input script. pikFlags may be a bitmask of any of the
** PIKCHR_PROCESS_xxx flags (see below). thFlags may be a bitmask of
** any of the TH_INIT_xxx and/or TH_R2B_xxx flags. Output is sent to
** pOut, appending to it without modifying any prior contents.
**
** Returns 0 on success, 1 if TH1 processing failed, or 2 if pikchr
** processing failed. In either case, the error message (if any) from
** TH1 or pikchr will be appended to pOut.
**
** pikFlags flag descriptions:
**
** - PIKCHR_PROCESS_TH1 means to run zIn through TH1, using the TH1
** init flags specified in the 3rd argument. If thFlags is non-0 then
** this flag is assumed even if it is not specified.
**
** - PIKCHR_PROCESS_TH1_NOSVG means that processing stops after the
** TH1 step, thus the output will be (presumably) a
** TH1-generated/processed pikchr script, and not an SVG. If this flag
** is set, PIKCHR_PROCESS_TH1 is assumed even if it is not specified.
** The remaining flags listed below are ignored if this flag is
** specified.
**
** - PIKCHR_PROCESS_DIV: if set, the SVG result is wrapped in a DIV
** element which specifies a max-width style value based on the SVG's
** calculated size.
**
** - PIKCHR_PROCESS_NONCE: if set, the resulting SVG/DEV are wrapped
** in "safe nonce" comments, which are a fossil-internal mechanism
** which prevents the wiki/markdown processors from processing this
** output.
**
** - PIKCHR_PROCESS_ERR_PRE: if set and pikchr() fails, the resulting
** error report is wrapped in PRE element.
*/
int pikchr_process(const char * zIn, int pikFlags, int thFlags,
                   Blob * pOut){
  Blob bIn = empty_blob;
  int isErr = 0;

  if(!(PIKCHR_PROCESS_TH1 & pikFlags)
     && (PIKCHR_PROCESS_TH1_NOSVG & pikFlags || thFlags!=0)){
    pikFlags |= PIKCHR_PROCESS_TH1;
  }  
  if(PIKCHR_PROCESS_TH1 & pikFlags){
    Blob out = empty_blob;
    isErr = Th_RenderToBlob(zIn, &out, thFlags)
      ? 1 : 0;
    if(isErr){
      blob_append(pOut, blob_str(&out), blob_size(&out));
      blob_reset(&out);
    }else{
      bIn = out;
    }
  }else{
    blob_init(&bIn, zIn, -1);
  }
  if(!isErr){
    if(PIKCHR_PROCESS_TH1_NOSVG & pikFlags){
      blob_append(pOut, blob_str(&bIn), blob_size(&bIn));
    }else{
      int w = 0, h = 0;
      const char * zContent = blob_str(&bIn);
      char *zOut;

      zOut = pikchr(zContent, "pikchr", 0, &w, &h);
      if( w>0 && h>0 ){
        const char *zNonce = (PIKCHR_PROCESS_NONCE & pikFlags)
          ? safe_html_nonce(1) : 0;
        if(zNonce){
          blob_append(pOut, zNonce, -1);
        }
        if(PIKCHR_PROCESS_DIV & pikFlags){
          blob_appendf(pOut,"<div style='max-width:%dpx;'>\n", w);
        }
        blob_append(pOut, zOut, -1);
        if(PIKCHR_PROCESS_DIV & pikFlags){
          blob_append(pOut,"</div>\n", 7);
        }
        if(zNonce){
          blob_append(pOut, zNonce, -1);
        }
      }else{
        isErr = 2;
        if(PIKCHR_PROCESS_ERR_PRE & pikFlags){
          blob_append(pOut, "<pre>\n", 6);
        }
        blob_append(pOut, zOut, -1);
        if(PIKCHR_PROCESS_ERR_PRE & pikFlags){
          blob_append(pOut, "\n</pre>\n", 8);
        }
      }
      fossil_free(zOut);
    }
  }
  blob_reset(&bIn);
  return isErr;
}

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
      Blob out = empty_blob;
      const int isErr =
        pikchr_process(zContent,
                       PIKCHR_PROCESS_DIV | PIKCHR_PROCESS_ERR_PRE,
                       0, &out);
      if(isErr){
        cgi_printf_header("x-pikchrshow-is-error: %d\r\n", isErr);
      }
      CX("%b", &out);
      blob_reset(&out);
    }else{
      CX("<pre>No content! Nothing to render</pre>");
    }
    return;
  }/*ajax response*/
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
  CX("<style>"); {
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
  } CX("</style>");
  CX("<div>Input pikchr code and tap Preview to render it:</div>");
  CX("<div id='sbs-wrapper'>"); {
    CX("<div id='pikchrshow-form'>"); {
      CX("<textarea id='content' name='content' rows='15'>"
         "%s</textarea>",zContent/*safe-for-%s*/);
      CX("<div id='pikchrshow-controls'>"); {
        CX("<button id='pikchr-submit-preview'>Preview</button>");
        CX("<div class='input-with-label'>"); {
          CX("<button id='pikchr-stash'>Stash</button>");
          CX("<button id='pikchr-unstash'>Unstash</button>");
          CX("<button id='pikchr-clear-stash'>Clear stash</button>");
          CX("<span>Stores/restores a single pikchr script to/from "
             "browser-local storage from/to the editor.</span>"
             /* gets turned into a help-buttonlet */);
        } CX("</div>"/*stash controls*/);
        style_labeled_checkbox("flipcolors-wrapper", "flipcolors",
                               "Dark mode?",
                               "1", isDark, 0);
      } CX("</div>"/*#pikchrshow-controls*/);
    }
    CX("</div>"/*#pikchrshow-form*/);
    CX("<fieldset id='pikchrshow-output-wrapper'>"); {
      CX("<legend></legend>"
         /* Reminder: Firefox does not properly flexbox a LEGEND
            element, always flowing it in column mode. */);
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
      } CX("</div>"/*#pikchrshow-output*/);
    } CX("</fieldset>"/*#pikchrshow-output-wrapper*/);
  } CX("</div>"/*sbs-wrapper*/);
  if(!builtin_bundle_all_fossil_js_apis()){
    builtin_emit_fossil_js_apis("dom", "fetch", "copybutton",
                                "popupwidget", "storage",
                                "pikchr", 0);
  }
  builtin_emit_fossil_js_apis("page.pikchrshow", 0);
  builtin_fulfill_js_requests();
  style_footer();
}

/*
** COMMAND: pikchr*
**
** Usage: %fossil pikchr [options] ?INFILE? ?OUTFILE?
**
** Accepts a pikchr script as input and outputs the rendered script as
** an SVG graphic. The INFILE and OUTFILE options default to stdin
** resp. stdout, and the names "-" can be used as aliases for those
** streams.
**
** Options:
**
**    -div       On success, adds a DIV wrapper around the
**               resulting SVG output which limits its max-width to
**               its computed maximum ideal size, in order to mimic
**               how fossil's web-based components work.
**
**    -th        Process the input using TH1 before passing it to pikchr.
**
**    -th-novar  Disable $var and $<var> TH1 processing. Use this if the
**               pikchr script uses '$' for its own purposes and that
**               causes issues. This only affects parsing of '$' outside
**               of TH1 script blocks. Code in such blocks is unaffected.
**
**    -th-nosvg  When using -th, output the post-TH1'd script
**               instead of the pikchr-rendered output.
**
**    -th-trace  Trace TH1 execution (for debugging purposes).
**
** TH1-related Notes and Caveats:
**
** If the -th flag is used, this command must open a fossil database
** for certain functionality to work (via a checkout or the -R REPO
** flag). If opening a db fails, execution will continue but any TH1
** commands which require a db will trigger a fatal error.
**
** In Fossil skins, TH1 variables in the form $varName are expanded
** as-is and those in the form $<varName> are htmlized in the
** resulting output. This processor disables the htmlizing step, so $x
** and $<x> are equivalent unless the TH1-processed pikchr script
** invokes the TH1 command [enable_htmlify 1] to enable it. Normally
** that option will interfere with pikchr output, however, e.g. by
** HTML-encoding double-quotes.
**
** Many of the fossil-installed TH1 functions simply do not make any
** sense for pikchr scripts.
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
  int pikFlags = 0;
  u32 fThFlags = TH_INIT_NO_ENCODE
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
    db_find_and_open_repository(OPEN_ANY_SCHEMA | OPEN_OK_NOT_FOUND, 0)
      /* ^^^ needed for certain TH1 functions to work */;;
    pikFlags |= PIKCHR_PROCESS_TH1;
    if(fNosvg) pikFlags |= PIKCHR_PROCESS_TH1_NOSVG;
  }
  if(fWithDiv){
    pikFlags |= PIKCHR_PROCESS_DIV;
  }
  isErr = pikchr_process(blob_str(&bIn), pikFlags,
                         fTh1 ? fThFlags : 0, &bOut);
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
