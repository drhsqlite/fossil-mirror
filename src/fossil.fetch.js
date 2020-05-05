"use strict";
/**
  Documented in style.c:style_emit_script_fetch(). Requires that
  window.fossil have already been set up.
*/
window.fossil.fetch = function f(uri,opt){
  if(!f.onerror){
    f.onerror = function(e){
      console.error("Ajax error:",e);
      fossil.error("Ajax error!");
      if(e.originalTarget && e.originalTarget.responseType==='text'){
        const txt = e.originalTarget.responseText;
        try{
          /* The convention from the /filepage_xyz routes is to
          ** return error responses in JSON form if possible.
          */
          const j = JSON.parse(txt);
          console.error("Error JSON:",j);
          if(j.error){ fossil.error(j.error) };
        }catch(e){/*ignore: not JSON*/}
      }
    };
  }
  if('/'===uri[0]) uri = uri.substr(1);
  if(!opt){
    opt = {};
  }else if('function'===typeof opt){
    opt={onload:opt};
  }
  if(!opt.onload) opt.onload = (r)=>console.debug('ajax response:',r);
  if(!opt.onerror) opt.onerror = f.onerror;
  let payload = opt.payload, jsonResponse = false;
  if(payload){
    opt.method = 'POST';
    if(!(payload instanceof FormData)
       && !(payload instanceof Document)
       && !(payload instanceof Blob)
       && !(payload instanceof File)
       && !(payload instanceof ArrayBuffer)){
      if('object'===typeof payload || payload instanceof Array){
        payload = JSON.stringify(payload);
        opt.contentType = 'application/json';
      }
    }
  }
  const url=[window.fossil.rootPath+uri], x=new XMLHttpRequest();
  if(opt.urlParams){
    url.push('?');
    if('string'===typeof opt.urlParams){
      url.push(opt.urlParams);
    }else{/*assume object*/
      let k, i = 0;
      for( k in opt.urlParams ){
        if(i++) url.push('&');
        url.push(k,'=',encodeURIComponent(opt.urlParams[k]));
      }
    }
  }
  if('POST'===opt.method && 'string'===typeof opt.contentType){
    x.setRequestHeader('Content-Type',opt.contentType);
  }
  x.open(opt.method||'GET', url.join(''), true);
  if('json'===opt.responseType){
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
