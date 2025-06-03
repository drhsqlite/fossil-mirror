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
** This file contains shared Ajax-related code for /fileedit, the wiki/forum
** editors, and friends.
*/
#include "config.h"
#include "ajax.h"
#include <assert.h>
#include <stdarg.h>

#if INTERFACE
/* enum ajax_render_preview_flags: */
#define AJAX_PREVIEW_LINE_NUMBERS 1
/* enum ajax_render_modes: */
#define AJAX_RENDER_GUESS 0   /* Guess rendering mode based on mimetype. */
/* GUESS must be 0. All others have unspecified values. */
#define AJAX_RENDER_PLAIN_TEXT 1  /* Render as plain text. */
#define AJAX_RENDER_HTML_IFRAME 2 /* Render as HTML inside an IFRAME. */
#define AJAX_RENDER_HTML_INLINE 3 /* Render as HTML without an IFRAME. */
#define AJAX_RENDER_WIKI 4        /* Render as wiki/markdown. */
#endif

/*
** Emits JS code which initializes the
** fossil.page.previewModes object to a map of AJAX_RENDER_xxx values
** and symbolic names for use by client-side scripts.
**
** If addScriptTag is true then the output is wrapped in a SCRIPT tag
** with the current nonce, else no SCRIPT tag is emitted.
**
** Requires that builtin_emit_script_fossil_bootstrap() has already been
** called in order to initialize the window.fossil.page object.
*/
void ajax_emit_js_preview_modes(int addScriptTag){
  if(addScriptTag){
    style_script_begin(__FILE__,__LINE__);
  }
  CX("fossil.page.previewModes={"
     "guess: %d, %d: 'guess', wiki: %d, %d: 'wiki',"
     "htmlIframe: %d, %d: 'htmlIframe', "
     "htmlInline: %d, %d: 'htmlInline', "
     "text: %d, %d: 'text'"
     "};\n",
     AJAX_RENDER_GUESS, AJAX_RENDER_GUESS,
     AJAX_RENDER_WIKI, AJAX_RENDER_WIKI,
     AJAX_RENDER_HTML_IFRAME, AJAX_RENDER_HTML_IFRAME,
     AJAX_RENDER_HTML_INLINE, AJAX_RENDER_HTML_INLINE,
     AJAX_RENDER_PLAIN_TEXT, AJAX_RENDER_PLAIN_TEXT);
  if(addScriptTag){
    style_script_end();
  }
}

/*
** Returns a value from the ajax_render_modes enum, based on the
** given mimetype string (which may be NULL), defaulting to
** AJAX_RENDER_PLAIN_TEXT.
 */
int ajax_render_mode_for_mimetype(const char * zMimetype){
  int rc = AJAX_RENDER_PLAIN_TEXT;
  if( zMimetype ){
    if( fossil_strcmp(zMimetype, "text/html")==0 ){
      rc = AJAX_RENDER_HTML_IFRAME;
    }else if( fossil_strcmp(zMimetype, "text/x-fossil-wiki")==0
              || fossil_strcmp(zMimetype, "text/x-markdown")==0 ){
      rc = AJAX_RENDER_WIKI;
    }
  }
  return rc;
}

/*
** Renders text/wiki content preview for various /ajax routes.
**
** pContent is text/wiki content to preview. zName is the name of the
** content, for purposes of determining the mimetype based on the
** extension (if NULL, mimetype text/plain is assumed). flags may be a
** bitmask of values from the ajax_render_preview_flags
** enum. *renderMode must specify the render mode to use. If
** *renderMode==AJAX_RENDER_GUESS then *renderMode gets set to the
** mode which is guessed at for the rendering (based on the mimetype).
**
** nIframeHeightEm is only used for the AJAX_RENDER_HTML_IFRAME
** renderMode, and specifies the height, in EM's, of the resulting
** iframe. If passed 0, it defaults to "some sane value."
*/
void ajax_render_preview(Blob * pContent, const char *zName,
                         int flags, int * renderMode,
                         int nIframeHeightEm){
  const char * zMime;

  zMime = zName ? mimetype_from_name(zName) : "text/plain";
  if(AJAX_RENDER_GUESS==*renderMode){
    *renderMode = ajax_render_mode_for_mimetype(zMime);
  }
  switch(*renderMode){
    case AJAX_RENDER_HTML_IFRAME:{
      char * z64 = encode64(blob_str(pContent), blob_size(pContent));
      CX("<iframe width='100%%' frameborder='0' "
         "marginwidth='0' style='height:%dem' "
         "marginheight='0' sandbox='allow-same-origin' "
         "src='data:text/html;base64,%z'"
         "></iframe>",
         nIframeHeightEm ? nIframeHeightEm : 40,
         z64);
      break;
    }
    case AJAX_RENDER_HTML_INLINE:{
      CX("%b",pContent);
      break;
    }
    case AJAX_RENDER_WIKI:
      safe_html_context(DOCSRC_FILE);
      wiki_render_by_mimetype(pContent, zMime);
      break;
    default:{
      const char *zContent = blob_str(pContent);
      if(AJAX_PREVIEW_LINE_NUMBERS & flags){
        output_text_with_line_numbers(zContent, blob_size(pContent),
                                      zName, "on", 0);
      }else{
        const char *zExt = strrchr(zName,'.');
        if(zExt && zExt[1]){
          CX("<pre><code class='language-%s'>%h</code></pre>",
             zExt+1, zContent);
        }else{
          CX("<pre>%h</pre>", zContent);
        }
      }
      break;
    }
  }
}

/*
** Renders diffs for ajax routes. pOrig is the "original" (v1) content
** and pContent is the locally-edited (v2) content. diffFlags is any
** set of flags suitable for passing to text_diff().
**
** zOrigHash, if not NULL, must be the SCM-side hash of pOrig's
** contents. If set, additional information may be built into
** the diff output to enable dynamic loading of additional
** diff context.
*/
void ajax_render_diff(Blob * pOrig, const char * zOrigHash,
                      Blob *pContent, u64 diffFlags){
  Blob out = empty_blob;
  DiffConfig DCfg;

  diff_config_init(&DCfg, diffFlags);
  DCfg.zLeftHash = zOrigHash;
  text_diff(pOrig, pContent, &out, &DCfg);
  if(blob_size(&out)==0){
    /* nothing to do */
  }else{
    CX("%b",&out);
  }
  blob_reset(&out);
}

/*
** Uses P(zKey) to fetch a CGI environment variable. If that var is
** NULL or starts with '0' or 'f' then this function returns false,
** else it returns true.
*/
int ajax_p_bool(char const *zKey){
  const char * zVal = P(zKey);
  return (!zVal || '0'==*zVal || 'f'==*zVal) ? 0 : 1;
}

/*
** Helper for /ajax routes. Clears the CGI content buffer, sets an
** HTTP error status code, and queues up a JSON response in the form
** of an object:
**
** {error: formatted message}
**
** If httpCode<=0 then it defaults to 500.
**
** After calling this, the caller should immediately return.
*/
void ajax_route_error(int httpCode, const char * zFmt, ...){
  Blob msg = empty_blob;
  Blob content = empty_blob;
  va_list vargs;
  va_start(vargs,zFmt);
  blob_vappendf(&msg, zFmt, vargs);
  va_end(vargs);
  blob_appendf(&content,"{\"error\":%!j}", blob_str(&msg));
  blob_reset(&msg);
  cgi_set_content(&content);
  cgi_set_status(httpCode>0 ? httpCode : 500, "Error");
  cgi_set_content_type("application/json");
}

/*
** Performs bootstrapping common to the /ajax/xyz AJAX routes, such as
** logging in the user.
**
** Returns false (0) if bootstrapping fails, in which case it has
** reported the error and the route should immediately return. Returns
** true on success.
**
** If requireWrite is true then write permissions are required.
** If requirePost is true then the request is assumed to be using
** POST'ed data and CSRF validation is performed.
**
*/
int ajax_route_bootstrap(int requireWrite, int requirePost){
  login_check_credentials();
  if( requireWrite!=0 && g.perm.Write==0 ){
    ajax_route_error(403,"Write permissions required.");
    return 0;
  }else if(0==cgi_csrf_safe(requirePost)){
    ajax_route_error(403,
                     "CSRF violation (make sure sending of HTTP "
                     "Referer headers is enabled for XHR "
                     "connections).");
    return 0;
  }
  return 1;
}

/*
** Helper for collecting filename/check-in request parameters.
**
** If zFn is not NULL, it is assigned the value of the first one of
** the "filename" or "fn" CGI parameters which is set.
**
** If zCi is not NULL, it is assigned the value of the first one of
** the "checkin" or "ci" CGI parameters which is set.
**
** If a parameter is not NULL, it will be assigned NULL if the
** corresponding parameter is not set.
**
** Returns the number of non-NULL values it assigns to arguments. Thus
** if passed (&x, NULL), it returns 1 if it assigns non-NULL to *x and
** 0 if it assigns NULL to *x.
*/
int ajax_get_fnci_args( const char **zFn, const char **zCi ){
  int rc = 0;
  if(zCi!=0){
    *zCi = PD("checkin",P("ci"));
    if( *zCi ) ++rc;
  }
  if(zFn!=0){
    *zFn = PD("filename",P("fn"));
    if (*zFn) ++rc;
  }
  return rc;
}

/*
** AJAX route /ajax/preview-text
**
** Required query parameters:
**
** filename=name of content, for use in determining the
** mimetype/render mode.
**
** content=text
**
** Optional query parameters:
**
** render_mode=integer (AJAX_RENDER_xxx) (default=AJAX_RENDER_GUESS)
**
** ln=0 or 1 to disable/enable line number mode in
** AJAX_RENDER_PLAIN_TEXT mode.
**
** iframe_height=integer (default=40) Height, in EMs of HTML preview
** iframe.
**
** User must have Write access to use this page.
**
** Responds with the HTML content of the preview. On error it produces
** a JSON response as documented for ajax_route_error().
**
** Extra response headers:
**
** x-ajax-render-mode: string representing the rendering mode
** which was really used (which will differ from the requested mode
** only if mode 0 (guess) was requested). The names are documented
** below in code and match those in the emitted JS object
** fossil.page.previewModes.
*/
void ajax_route_preview_text(void){
  const char * zFilename = 0;
  const char * zContent = P("content");
  int renderMode = atoi(PD("render_mode","0"));
  int ln = atoi(PD("ln","0"));
  int iframeHeight = atoi(PD("iframe_height","40"));
  Blob content = empty_blob;
  const char * zRenderMode = 0;

  ajax_get_fnci_args( &zFilename, 0 );

  if(!ajax_route_bootstrap(0,1)){
    return;
  }
  if(zFilename==0){
    /* The filename is only used for mimetype determination,
    ** so we can default it... */
    zFilename = "foo.txt";
  }
  cgi_set_content_type("text/html");
  blob_init(&content, zContent, -1);
  ajax_render_preview(&content, zFilename,
                      ln ? AJAX_PREVIEW_LINE_NUMBERS : 0,
                      &renderMode, iframeHeight);
  /*
  ** Now tell the caller if we did indeed use AJAX_RENDER_WIKI, so that
  ** they can re-set the <base href> to an appropriate value (which
  ** requires knowing the content's current check-in version, which we
  ** don't have here).
  */
  switch(renderMode){
    /* The strings used here MUST correspond to those used in the JS-side
    ** fossil.page.previewModes map.
    */
    case AJAX_RENDER_WIKI: zRenderMode = "wiki"; break;
    case AJAX_RENDER_HTML_INLINE: zRenderMode = "htmlInline"; break;
    case AJAX_RENDER_HTML_IFRAME: zRenderMode = "htmlIframe"; break;
    case AJAX_RENDER_PLAIN_TEXT: zRenderMode = "text"; break;
    case AJAX_RENDER_GUESS:
      assert(!"cannot happen");
  }
  if(zRenderMode!=0){
    cgi_printf_header("x-ajax-render-mode: %s\r\n", zRenderMode);
  }
}

#if INTERFACE
/*
** Internal mapping of ajax sub-route names to various metadata.
*/
struct AjaxRoute {
  const char *zName;   /* Name part of the route after "ajax/" */
  void (*xCallback)(); /* Impl function for the route. */
  int bWriteMode;      /* True if requires write mode */
  int bPost;           /* True if requires POST (i.e. CSRF
                       ** verification) */
};
typedef struct AjaxRoute AjaxRoute;
#endif /*INTERFACE*/

/*
** Comparison function for bsearch() for searching an AjaxRoute
** list for a matching name.
*/
int cmp_ajax_route_name(const void *a, const void *b){
  const AjaxRoute * rA = (const AjaxRoute*)a;
  const AjaxRoute * rB = (const AjaxRoute*)b;
  return fossil_strcmp(rA->zName, rB->zName);
}

/*
** WEBPAGE: ajax hidden
**
** The main dispatcher for shared ajax-served routes. Requires the
** 'name' parameter be the main route's name (as defined in a list in
** this function), noting that fossil automatically assigns all path
** parts after "ajax" to "name", e.g. /ajax/foo/bar assigns
** name=foo/bar.
**
** This "page" is only intended to be used by higher-level pages which
** have certain Ajax-driven features in common. It is not intended to
** be used by clients and NONE of its HTTP interfaces are considered
** documented/stable/supported - they may change on any given build of
** fossil.
**
** The exact response type depends on the route which gets called. In
** the case of an initialization error it emits a JSON-format response
** as documented for ajax_route_error(). Individual routes may emit
** errors in different formats, e.g. HTML.
*/
void ajax_route_dispatcher(void){
  const char * zName = P("name");
  AjaxRoute routeName = {0,0,0,0};
  const AjaxRoute * pRoute = 0;
  const AjaxRoute routes[] = {
  /* Keep these sorted by zName (for bsearch()) */
  {"preview-text", ajax_route_preview_text, 0, 1
   /* Note that this does not require write permissions in the repo.
   ** It should arguably require write permissions but doing means
   ** that /chat does not work without checkin permissions:
   **
   ** https://fossil-scm.org/forum/forumpost/ed4a762b3a557898
   **
   ** This particular route is used by /fileedit and /chat, whereas
   ** /wikiedit uses a simpler wiki-specific route.
   */ }
  };

  if(zName==0 || zName[0]==0){
    ajax_route_error(400,"Missing required [route] 'name' parameter.");
    return;
  }
  routeName.zName = zName;
  pRoute = (const AjaxRoute *)bsearch(&routeName, routes,
                                      count(routes), sizeof routes[0],
                                      cmp_ajax_route_name);
  if(pRoute==0){
    ajax_route_error(404,"Ajax route not found.");
    return;
  }else if(0==ajax_route_bootstrap(pRoute->bWriteMode, pRoute->bPost)){
    return;
  }
  pRoute->xCallback();
}
