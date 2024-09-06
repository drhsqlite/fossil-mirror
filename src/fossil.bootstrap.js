"use strict";
(function () {
  /* CustomEvent polyfill, courtesy of Mozilla:
     https://developer.mozilla.org/en-US/docs/Web/API/CustomEvent/CustomEvent
  */
  if(typeof window.CustomEvent === "function") return false;
  window.CustomEvent = function(event, params) {
    if(!params) params = {bubbles: false, cancelable: false, detail: null};
    const evt = document.createEvent('CustomEvent');
    evt.initCustomEvent( event, !!params.bubbles, !!params.cancelable, params.detail );
    return evt;
  };
})();
(function(global){
  /* Bootstrapping bits for the global.fossil object. Must be loaded
     after style.c:builtin_emit_script_fossil_bootstrap() has
     initialized that object.
  */

  const F = global.fossil;

  /**
     Returns the current time in something approximating
     ISO-8601 format.
  */
  const timestring = function f(){
    if(!f.rx1){
      f.rx1 = /\.\d+Z$/;
    }
    const d = new Date();
    return d.toISOString().replace(f.rx1,'').split('T').join(' ');
  };

  /** Returns the local time string of Date object d, defaulting
      to the current time. */
  const localTimeString = function ff(d){
    if(!ff.pad){
      ff.pad = (x)=>(''+x).length>1 ? x : '0'+x;
    }
    d || (d = new Date());
    return [
      d.getFullYear(),'-',ff.pad(d.getMonth()+1/*sigh*/),
      '-',ff.pad(d.getDate()),
      ' ',ff.pad(d.getHours()),':',ff.pad(d.getMinutes()),
      ':',ff.pad(d.getSeconds())
    ].join('');
  };

  /*
  ** By default fossil.message() sends its arguments console.debug(). If
  ** fossil.message.targetElement is set, it is assumed to be a DOM
  ** element, its innerText gets assigned to the concatenation of all
  ** arguments (with a space between each), and the CSS 'error' class is
  ** removed from the object. Pass it a falsy value to clear the target
  ** element.
  **
  ** Returns this object.
  */
  F.message = function f(msg){
    const args = Array.prototype.slice.call(arguments,0);
    const tgt = f.targetElement;
    if(args.length) args.unshift(
      localTimeString()+':'
      //timestring(),'UTC:'
    );
    if(tgt){
      tgt.classList.remove('error');
      tgt.innerText = args.join(' ');
    }
    else{
      if(args.length){
        args.unshift('Fossil status:');
        console.debug.apply(console,args);
      }
    }
    return this;
  };
  /*
  ** Set default message.targetElement to #fossil-status-bar, if found.
  */
  F.message.targetElement =
    document.querySelector('#fossil-status-bar');
  if(F.message.targetElement){
    F.message.targetElement.addEventListener(
      'dblclick', ()=>F.message(), false
    );
  }
  /*
  ** By default fossil.error() sends its first argument to
  ** console.error(). If fossil.message.targetElement (yes,
  ** fossil.message) is set, it adds the 'error' CSS class to
  ** that element and sets its content as defined for message().
  **
  ** Returns this object.
  */
  F.error = function f(msg){
    const args = Array.prototype.slice.call(arguments,0);
    const tgt = F.message.targetElement;
    args.unshift(timestring(),'UTC:');
    if(tgt){
      tgt.classList.add('error');
      tgt.innerText = args.join(' ');
    }
    else{
      args.unshift('Fossil error:');
      console.error.apply(console,args);
    }
    return this;
  };

  /**
     For each property in the given object, its key/value are encoded
     for use as URL parameters and the combined string is
     returned. e.g. {a:1,b:2} encodes to "a=1&b=2".

     If the 2nd argument is an array, each encoded element is appended
     to that array and tgtArray is returned. The above object would be
     appended as ['a','=','1','&','b','=','2']. This form is used for
     building up parameter lists before join('')ing the array to create
     the result string.

     If passed a truthy 3rd argument, it does not really encode each
     component - it simply concatenates them together.
  */
  F.encodeUrlArgs = function(obj,tgtArray,fakeEncode){
    if(!obj) return '';
    const a = (tgtArray instanceof Array) ? tgtArray : [],
          enc = fakeEncode ? (x)=>x : encodeURIComponent;
    let k, i = 0;
    for( k in obj ){
      if(i++) a.push('&');
      a.push(enc(k),'=',enc(obj[k]));
    }
    return a===tgtArray ? a : a.join('');
  };
  /**
     repoUrl( repoRelativePath [,urlParams] )

     Creates a URL by prepending this.rootPath to the given path
     (which must be relative from the top of the site, without a
     leading slash). If urlParams is a string, it must be
     parameters encoded in the form "key=val&key2=val2..." WITHOUT
     a leading '?'. If it's an object, all of its properties get
     appended to the URL in that form.
  */
  F.repoUrl = function(path,urlParams){
    if(!urlParams) return this.rootPath+path;
    const url=[this.rootPath,path];
    url.push('?');
    if('string'===typeof urlParams) url.push(urlParams);
    else if(urlParams && 'object'===typeof urlParams){
      this.encodeUrlArgs(urlParams, url);
    }
    return url.join('');
  };

  /**
     Returns true if v appears to be a plain object.
  */
  F.isObject = function(v){
    return v &&
      (v instanceof Object) &&
      ('[object Object]' === Object.prototype.toString.apply(v) );
  };

  /**
     For each object argument, this function combines their properties,
     using a last-one-wins policy, and returns a new object with the
     combined properties. If passed a single object, it effectively
     shallowly clones that object.
  */
  F.mergeLastWins = function(){
    var k, o, i;
    const n = arguments.length, rc={};
    for(i = 0; i < n; ++i){
      if(!F.isObject(o = arguments[i])) continue;
      for( k in o ){
        if(o.hasOwnProperty(k)) rc[k] = o[k];
      }
    }
    return rc;
  };

  /**
     Expects to be passed as hash code as its first argument. It
     returns a "shortened" form of hash, with a length which depends
     on the 2nd argument: truthy = fossil.config.hashDigitsUrl, falsy
     = fossil.config.hashDigits, number == that many digits. The
     fossil.config values are derived from the 'hash-digits'
     repo-level config setting or the
     FOSSIL_HASH_DIGITS_URL/FOSSIL_HASH_DIGITS compile-time options.

     If its first arugment is a non-string, that value is returned
     as-is.
  */
  F.hashDigits = function(hash,forUrl){
    const n = ('number'===typeof forUrl)
          ? forUrl : F.config[forUrl ? 'hashDigitsUrl' : 'hashDigits'];
    return ('string'==typeof hash ? hash.substr(
      0, n
    ) : hash);
  };

  /**
     Convenience wrapper which adds an onload event listener to the
     window object. Returns this.
  */
  F.onPageLoad = function(callback){
    window.addEventListener('load', callback, false);
    return this;
  };

  /**
     Convenience wrapper which adds a DOMContentLoadedevent listener
     to the window object. Returns this.
  */
  F.onDOMContentLoaded = function(callback){
    window.addEventListener('DOMContentLoaded', callback, false);
    return this;
  };

  /**
     Assuming name is a repo-style filename, this function returns
     a shortened form of that name:

     .../LastDirectoryPart/FilenamePart

     If the name has 0-1 directory parts, it is returned as-is.

     Design note: in practice it is generally not helpful to elide the
     *last* directory part because embedded docs (in particular) often
     include x/y/index.md and x/z/index.md, both of which would be
     shortened to something like x/.../index.md.
  */
  F.shortenFilename = function(name){
    const a = name.split('/');
    if(a.length<=2) return name;
    while(a.length>2) a.shift();
    return '.../'+a.join('/');
  };

  /**
     Adds a listener for fossil-level custom events. Events are
     delivered to their callbacks as CustomEvent objects with a
     'detail' property holding the event's app-level data.

     The exact events fired differ by page, and not all pages trigger
     events.

     Pedantic sidebar: the custom event's 'target' property is an
     unspecified DOM element. Clients must not rely on its value being
     anything specific or useful.

     Returns this object.
  */
  F.page.addEventListener = function f(eventName, callback){
    if(!f.proxy){
      f.proxy = document.createElement('span');
    }
    f.proxy.addEventListener(eventName, callback, false);
    return this;
  };

  /**
     Internal. Dispatches a new CustomEvent to all listeners
     registered for the given eventName via
     fossil.page.addEventListener(), passing on a new CustomEvent with
     a 'detail' property equal to the 2nd argument. Returns this
     object.
  */
  F.page.dispatchEvent = function(eventName, eventDetail){
    if(this.addEventListener.proxy){
      try{
        this.addEventListener.proxy.dispatchEvent(
          new CustomEvent(eventName,{detail: eventDetail})
        );
      }catch(e){
        console.error(eventName,"event listener threw:",e);
      }
    }
    return this;
  };

  /**
     Sets the innerText of the page's TITLE tag to
     the given text and returns this object.
   */
  F.page.setPageTitle = function(title){
    const t = document.querySelector('title');
    if(t) t.innerText = title;
    return this;
  };

  /**
     Returns a function, that, as long as it continues to be invoked,
     will not be triggered. The function will be called after it stops
     being called for N milliseconds. If `immediate` is passed, call
     the callback immediately and hinder future invocations until at
     least the given time has passed.

     If passed only 1 argument, or passed a falsy 2nd argument,
     the default wait time set in this function's $defaultDelay
     property is used.

     Inspiration: underscore.js, by way of https://davidwalsh.name/javascript-debounce-function
  */
  F.debounce = function f(func, waitMs, immediate) {
    var timeoutId;
    if(!waitMs) waitMs = f.$defaultDelay;
    return function() {
      const context = this, args = Array.prototype.slice.call(arguments);
      const later = function() {
        timeoutId = undefined;
        if(!immediate) func.apply(context, args);
      };
      const callNow = immediate && !timeoutId;
      clearTimeout(timeoutId);
      timeoutId = setTimeout(later, waitMs);
      if(callNow) func.apply(context, args);
    };
  };
  F.debounce.$defaultDelay = 500 /*arbitrary*/;

})(window);
