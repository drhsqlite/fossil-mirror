"use strict";
/**
  Documented in style.c:style_emit_script_fetch(). Requires that
  window.fossil has already been set up (which happens via the routine
  which emits this code C-side).
*/
window.fossil.fetch = function f(uri,opt){
  if(!f.onerror){
    f.onerror = function(e/*event or exception*/){
      console.error("Ajax error:",e);
      if(e instanceof Error){
        fossil.error('Exception:',e);
      }
      else if(e.originalTarget && e.originalTarget.responseType==='text'){
        const txt = e.originalTarget.responseText;
        try{
          /* The convention from the /filepage_xyz routes is to
          ** return error responses in JSON form if possible:
          ** {error: "..."}
          */
          const j = JSON.parse(txt);
          console.error("Error JSON:",j);
          if(j.error){ fossil.error(j.error) };
        }catch(e){/* Try harder */
          fossil.error(txt)
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
  if(payload){
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
  const url=[window.fossil.repoUrl(uri,opt.urlParams)],
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
  if(payload) x.send(payload);
  else x.send();
  return this;
};
