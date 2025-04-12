"use strict";
/**
   Requires that window.fossil has already been set up.
*/
(function(namespace){
const fossil = namespace;
  /**
   fetch() is an HTTP request/response mini-framework
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

   - onload: callback(responseData) (default = output response to the
   console). In the context of the callback, the options object is
   "this", noting that this call may have amended the options object
   with state other than what the caller provided.

   - onerror: callback(Error object) (default = output error message
   to console.error() and fossil.error()). Triggered if the request
   generates any response other than HTTP 200, or if the beforesend()
   or onload() handler throws an exception. In the context of the
   callback, the options object is "this". This function is intended
   to be used solely for error reporting, not error recovery. Special
   cases for the Error object:

       1. Timeouts unfortunately show up as a series of 2 events: an
       HTTP 0 followed immediately by an XHR.ontimeout(). The former
       cannot(?) be unambiguously identified as the trigger for the
       pending timeout, so we have no option but to pass it on as-is
       instead of flagging it as a timeout response. The latter will
       trigger the client-provided ontimeout() if it's available (see
       below), else it calls the onerror() callback. An error object
       passed to ontimeout() by fetch() will have (.name='timeout',
       .status=XHR.status).

       2. Else if the response contains a JSON-format exception on the
       server, it will have (.name='json-error',
       status=XHR.status). Any JSON-format result object which has a
       property named "error" is considered to be a server-generated
       error.

       3. Else if it gets a non 2xx HTTP code then it will have
       (.name='http',.status=XHR.status).

       4. If onerror() throws, the exception is suppressed but may
       generate a console error message.

   - ontimeout: callback(Error object). If set, timeout errors are
   reported here, else they are reported through onerror().
   Unfortunately, XHR fires two events for a timeout: an
   onreadystatechange() and an ontimeout(), in that order.  From the
   former, however, we cannot unambiguously identify the error as
   having been caused by a timeout, so clients which set ontimeout()
   will get _two_ callback calls: one with with an HTTP error response
   followed immediately by an ontimeout() response. Error objects
   passed to this will have (.name='timeout', .status=xhr.HttpStatus).
   In the context of the callback, the options object is "this", Like
   onerror(), any exceptions thrown by the ontimeout() handler are
   suppressed, but may generate a console error message. The onerror()
   handler is _not_ called in this case.

   - method: 'POST' | 'GET' (default = 'GET'). CASE SENSITIVE!

   - payload: anything acceptable by XHR2.send(ARG) (DOMString,
   Document, FormData, Blob, File, ArrayBuffer), or a plain object or
   array, either of which gets JSON.stringify()'d. If payload is set
   then the method is automatically set to 'POST'. By default XHR2
   will set the content type based on the payload type. If an
   object/array is converted to JSON, the contentType option is
   automatically set to 'application/json', and if JSON.stringify() of
   that value fails then the exception is propagated to this
   function's caller. (beforesend(), aftersend(), and onerror() are
   NOT triggered in that case.)

   - contentType: Optional request content type when POSTing. Ignored
   if the method is not 'POST'.

   - responseType: optional string. One of ("text", "arraybuffer",
   "blob", or "document") (as specified by XHR2). Default = "text".
   As an extension, it supports "json", which tells it that the
   response is expected to be text and that it should be JSON.parse()d
   before passing it on to the onload() callback. If parsing of such
   an object fails, the onload callback is not called, and the
   onerror() callback is passed the exception from the parsing error.
   If the parsed JSON object has an "error" property, it is assumed to
   be an error string, which is used to populate a new Error object,
   which will gets (.name="json") set on it.

   - urlParams: string|object. If a string, it is assumed to be a
   URI-encoded list of params in the form "key1=val1&key2=val2...",
   with NO leading '?'.  If it is an object, all of its properties get
   converted to that form. Either way, the parameters get appended to
   the URL before submitting the request.

   - responseHeaders: If true, the onload() callback is passed an
   additional argument: a map of all of the response headers. If it's
   a string value, the 2nd argument passed to onload() is instead the
   value of that single header. If it's an array, it's treated as a
   list of headers to return, and the 2nd argument is a map of those
   header values. When a map is passed on, all of its keys are
   lower-cased. When a given header is requested and that header is
   set multiple times, their values are (per the XHR docs)
   concatenated together with "," between them.

   - beforesend/aftersend: optional callbacks which are called
   without arguments immediately before the request is submitted
   and immediately after it is received, regardless of success or
   error. In the context of the callback, the options object is
   the "this". These can be used to, e.g., keep track of in-flight
   requests and update the UI accordingly, e.g. disabling/enabling
   DOM elements. Any exceptions thrown in an beforesend are passed
   to the onerror() handler and cause the fetch() to prematurely
   abort. Exceptions thrown in aftersend are currently silently
   ignored (feature or bug?).

   - timeout: integer in milliseconds specifying the XHR timeout
   duration. Default = fossil.fetch.timeout.

   When an options object does not provide
   onload/onerror/beforesend/aftersend handlers of its own, this
   function falls back to defaults which are member properties of this
   function with the same name, e.g. fossil.fetch.onload(). The
   default onload/onerror implementations route the data through the
   dev console and (for onerror()) through fossil.error(). The default
   beforesend/aftersend are no-ops. Individual pages may overwrite
   those members to provide default implementations suitable for the
   page's use, e.g. keeping track of how many in-flight ajax requests
   are pending.

   Note that this routine may add properties to the 2nd argument, so
   that instance should not be kept around for later use.

   Returns this object, noting that the XHR request is asynchronous,
   and still in transit (or has yet to be sent) when that happens.
*/
fossil.fetch = function f(uri,opt){
  const F = fossil;
  if(!f.onload){
    f.onload = (r)=>console.debug('fossil.fetch() XHR response:',r);
  }
  if(!f.onerror){
    f.onerror = function(e/*exception*/){
      console.error("fossil.fetch() XHR error:",e);
      if(e instanceof Error) F.error('Exception:',e);
      else F.error("Unknown error in handling of XHR request.");
    };
  }
  if(!f.parseResponseHeaders){
    f.parseResponseHeaders = function(h){
      const rc = {};
      if(!h) return rc;
      const ar = h.trim().split(/[\r\n]+/);
      ar.forEach(function(line) {
        const parts = line.split(': ');
        const header = parts.shift();
        const value = parts.join(': ');
        rc[header.toLowerCase()] = value;
      });
      return rc;
    };
  }
  if('/'===uri[0]) uri = uri.substr(1);
  if(!opt) opt = {}/* should arguably be Object.create(null) */;
  else if('function'===typeof opt) opt={onload:opt};
  if(!opt.onload) opt.onload = f.onload;
  if(!opt.onerror) opt.onerror = f.onerror;
  if(!opt.beforesend) opt.beforesend = f.beforesend;
  if(!opt.aftersend) opt.aftersend = f.aftersend;
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
  const url=[f.urlTransform(uri,opt.urlParams)],
        x=new XMLHttpRequest();
  if('json'===opt.responseType){
    /* 'json' is an extension to the supported XHR.responseType
       list. We use it as a flag to tell us to JSON.parse()
       the response. */
    jsonResponse = true;
    x.responseType = 'text';
  }else{
    x.responseType = opt.responseType||'text';
  }
  x.ontimeout = function(ev){
    try{opt.aftersend()}catch(e){/*ignore*/}
    const err = new Error("XHR timeout of "+x.timeout+"ms expired.");
    err.status = x.status;
    err.name = 'timeout';
    //console.warn("fetch.ontimeout",ev);
    try{
      (opt.ontimeout || opt.onerror)(err);
    }catch(e){
      /*ignore*/
      console.error("fossil.fetch()'s ontimeout() handler threw",e);
    }
  };
  /* Ensure that if onerror() throws, it's ignored. */
  const origOnError = opt.onerror;
  opt.onerror = (arg)=>{
    try{ origOnError.call(this, arg) }
    catch(e){
      /*ignored*/
      console.error("fossil.fetch()'s onerror() threw",e);
    }
  };
  x.onreadystatechange = function(ev){
    //console.warn("onreadystatechange", x.readyState, ev.target.responseText);
    if(XMLHttpRequest.DONE !== x.readyState) return;
    try{opt.aftersend()}catch(e){/*ignore*/}
    if(false && 0===x.status){
      /* For reasons unknown, we _sometimes_ trigger x.status==0 in FF
         when the /chat page starts up, but not in Chrome nor in other
         apps. Insofar as has been determined, this happens before a
         request is actually sent and it appears to have no
         side-effects on the app other than to generate an error
         (i.e. no requests/responses are missing). This is a silly
         workaround which may or may not bite us later. If so, it can
         be removed at the cost of an unsightly console error message
         in FF.

         2025-04-10: that behavior is now also in Chrome and enabling
         this workaround causes our timeout errors to never arrive.
      */
      return;
    }
    if(200!==x.status){
      //console.warn("Error response",ev.target);
      let err;
      try{
        const j = JSON.parse(x.response);
        if(j.error){
          err = new Error(j.error);
          err.name = 'json.error';
        }
      }catch(ex){/*ignore*/}
      if( !err ){
        /* We can't tell from here whether this was a timeout-capable
           request which timed out on our end or was one which is a
           genuine error. We also don't know whether the server timed
           out the connection before we did. */
        err = new Error("HTTP response status "+x.status+".")
        err.name = 'http';
      }
      err.status = x.status;
      opt.onerror(err);
      return;
    }
    const orh = opt.responseHeaders;
    let head;
    if(true===orh){
      head = f.parseResponseHeaders(x.getAllResponseHeaders());
    }else if('string'===typeof orh){
      head = x.getResponseHeader(orh);
    }else if(orh instanceof Array){
      head = {};
      orh.forEach((s)=>{
        if('string' === typeof s) head[s.toLowerCase()] = x.getResponseHeader(s);
      });
    }
    try{
      const args = [(jsonResponse && x.response)
                    ? JSON.parse(x.response) : x.response];
      if(head) args.push(head);
      opt.onload.apply(opt, args);
    }catch(err){
      opt.onerror(err);
    }
  }/*onreadystatechange()*/;
  try{opt.beforesend()}
  catch(err){
    opt.onerror(err);
    return;
  }
  x.open(opt.method||'GET', url.join(''), true);
  if('POST'===opt.method && 'string'===typeof opt.contentType){
    x.setRequestHeader('Content-Type',opt.contentType);
  }
  x.timeout = +opt.timeout || f.timeout;
  if(undefined!==payload) x.send(payload);
  else x.send();
  return this;
};

/**
   urlTransform() must refer to a function which accepts a relative path
   to the same site as fetch() is served from and an optional set of
   URL parameters to pass with it (in the form a of a string
   ("a=b&c=d...") or an object of key/value pairs (which it converts
   to such a string), and returns the resulting URL or URI as a string.
*/
fossil.fetch.urlTransform = (u,p)=>fossil.repoUrl(u,p);
fossil.fetch.beforesend = function(){};
fossil.fetch.aftersend = function(){};
fossil.fetch.timeout = 15000/* Default timeout, in ms. */;
})(window.fossil);
