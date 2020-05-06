"use strict";
/**
   Requires that window.fossil has already been set up.

   window.fossil.fetch() is an HTTP request/response mini-framework
   similar (but not identical) to the not-quite-ubiquitous
   window.fetch().

   JS usages:

   fossil.fetch( URI [, onLoadCallback] );

   fossil.fetch( URI [, optionsObject = {}] );

   Noting that URI must be relative to the top of the repository and
   should not start with a slash (if it does, it is stripped). It gets
   the equivalent of "%R/" prepended to it.

   The optionsObject may be an onload callback or an object with any
   of these properties:

   - onload: callback(responseData) (default = output response to
   the console).

   - onerror: callback(XHR onload event | exception)
   (default = event or exception to the console).

   - method: 'POST' | 'GET' (default = 'GET'). CASE SENSITIVE!

   - payload: anything acceptable by XHR2.send(ARG) (DOMString,
   Document, FormData, Blob, File, ArrayBuffer), or a plain object or
   array, either of which gets JSON.stringify()'d. If payload is set
   then the method is automatically set to 'POST'. If an object/array
   is converted to JSON, the contentType option is automatically set
   to 'application/json'. By default XHR2 will set the content type
   based on the payload type.

   - contentType: Optional request content type when POSTing. Ignored
   if the method is not 'POST'.

   - responseType: optional string. One of ("text", "arraybuffer",
   "blob", or "document") (as specified by XHR2). Default = "text".
   As an extension, it supports "json", which tells it that the
   response is expected to be text and that it should be JSON.parse()d
   before passing it on to the onload() callback.

   - urlParams: string|object. If a string, it is assumed to be a
   URI-encoded list of params in the form "key1=val1&key2=val2...",
   with NO leading '?'.  If it is an object, all of its properties get
   converted to that form. Either way, the parameters get appended to
   the URL before submitting the request.

   When an options object does not provide onload() or onerror()
   handlers of its own, this function falls back to
   fossil.fetch.onload() and fossil.fetch.onerror() as defaults. The
   default implementations route the data through the dev console and
   (for onerror()) through fossil.error(). Individual pages may
   overwrite those members to provide default implementations suitable
   for the page's use.

   Returns this object, noting that the XHR request is asynchronous,
   and still in transit (or has yet to be sent) when that happens.
*/
window.fossil.fetch = function f(uri,opt){
  const F = fossil;
  if(!f.onerror){
    f.onerror = function(e/*event or exception*/){
      console.error("Ajax error:",e);
      if(e instanceof Error){
        F.error('Exception:',e);
      }
      else if(e.originalTarget && e.originalTarget.responseType==='text'){
        const txt = e.originalTarget.responseText;
        try{
          /* The convention from the /filepage_xyz routes is to
             return error responses in JSON form if possible:
             {error: "..."}
          */
          const j = JSON.parse(txt);
          console.error("Error JSON:",j);
          if(j.error){ F.error(j.error) };
        }catch(e){/* Try harder */
          F.error(txt)
        }
      }
    };
    f.onload = (r)=>console.debug('ajax response:',r);
  }
  if('/'===uri[0]) uri = uri.substr(1);
  if(!opt) opt = {};
  else if('function'===typeof opt) opt={onload:opt};
  if(!opt.onload) opt.onload = f.onload;
  if(!opt.onerror) opt.onerror = f.onerror;
  let payload = opt.payload, jsonResponse = false;
  if(undefined!==payload){
    opt.method = 'POST';
    if(!(payload instanceof FormData)
       && !(payload instanceof Document)
       && !(payload instanceof Blob)
       && !(payload instanceof File)
       && !(payload instanceof ArrayBuffer)
       && ('object'===typeof payload
           || payload instanceof Array)){
      payload = JSON.stringify(payload);
      opt.contentType = 'application/json';
    }
  }
  const url=[F.repoUrl(uri,opt.urlParams)],
        x=new XMLHttpRequest();
  if('POST'===opt.method && 'string'===typeof opt.contentType){
    x.setRequestHeader('Content-Type',opt.contentType);
  }
  x.open(opt.method||'GET', url.join(''), true);
  if('json'===opt.responseType){
    /* 'json' is an extension to the supported XHR.responseType
       list. We use it as a flag to tell us to JSON.parse()
       the response. */
    jsonResponse = true;
    x.responseType = 'text';
  }else{
    x.responseType = opt.responseType||'text';
  }
  if(opt.onload){
    x.onload = function(e){
      if(200!==this.status){
        if(opt.onerror) opt.onerror(e);
        return;
      }
      try{
        opt.onload((jsonResponse && this.response)
                   ? JSON.parse(this.response) : this.response);
      }catch(e){
        if(opt.onerror) opt.onerror(e);
      }
    }
  }
  if(undefined!==payload) x.send(payload);
  else x.send();
  return this;
};
