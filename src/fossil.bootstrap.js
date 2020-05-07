"use strict";
(function(global){
  /* Bootstrapping bits for the global.fossil object. Must be
     loaded after style.c:style_emit_script_tag() has initialized
     that object.
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
    args.unshift(timestring(),'UTC:');
    if(tgt){
      tgt.classList.remove('error');
      tgt.innerText = args.join(' ');
    }
    else{
      args.unshift('Fossil status:');
      console.debug.apply(console,args);
    }
    return this;
  };
  /*
  ** Set default message.targetElement to #fossil-status-bar, if found.
  */
  F.message.targetElement =
    document.querySelector('#fossil-status-bar');
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
     paramters encoded in the form "key=val&key2=val2...", WITHOUT
     a leading '?'. If it's an object, all of its properties get
     appended to the URL in that form.
  */
  F.repoUrl = function(path,urlParams){
    if(!urlParams) return this.rootPath+path;
    const url=[this.rootPath,path];
    url.push('?');
    if('string'===typeof urlParams) url.push(urlParams);
    else if('object'===typeof urlParams){
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
     = fossil.config.hashDigits. Both of those values are derived from
     the 'hash-digits' repo-level config setting or the
     FOSSIL_HASH_DIGITS_URL/FOSSIL_HASH_DIGITS compile-time options.

     If its first arugment is a non-string, that value is returned
     as-is.
  */
  F.hashDigits = function(hash,forUrl){
    return ('string'==typeof hash ? hash.substr(
      0, F.config[forUrl ? 'hashDigitsUrl' : 'hashDigits']
    ) : hash);
  };

  /**
     Sets up pseudo-automatic content preview handling between a
     source element (typically a TEXTAREA) and a target rendering
     element (typically a DIV). The selector argument must be one of:

     - A single DOM element
     - A collection of DOM elements with a forEach method.
     - A CSS selector

     Each element in the collection must have the following data
     attributes:

     - data-f-preview-from: the DOM element id of the text source
       element. It must support .value to get the content.

     - data-f-preview-to: the DOM element id of the target "previewer"
       element.

     - data-f-preview-via: the name of a method (see below).

     - OPTIONAL data-f-preview-as-text: a numeric value. Explained below.

     Each element gets a click handler added to it which does the
     following:

     1) Reads the content from its data-f-preview-from element.

     2) Passes the content to
     methodNamespace[f-data-post-via](content,callback). f-data-post-via
     is responsible for submitting the preview HTTP request, including
     any parameters the request might require. When the response
     arrives, it must pass the content of the response to its 2nd
     argument, an auto-generated callback installed by this mechanism
     which...

     3) Assigns the response text to the data-f-preview-to element. If
     data-f-preview-as-text is '0' (the default) then the content
     is assigned to the target element's innerHTML property, else
     it is assigned to the element's textContent property.


     The methodNamespace (2nd argument) defaults to fossil.page, and
     data-f-preview-via must be a single method name, not a
     property-access-style string. e.g. "myPreview" is legal but
     "foo.myPreview" is not (unless, of course, the method is actually
     named "foo.myPreview" (which is legal but would be
     unconventional)).

     An example...

     First an input button:

     <button id='test-preview-connector'
       data-f-preview-from='fileedit-content-editor' // elem ID
       data-f-preview-via='myPreview' // method name
       data-f-preview-to='fileedit-tab-preview-wrapper' // elem ID
     >Preview update</button>

     And a sample data-f-preview-via method:

     fossil.page.myPreview = function(content,callback){
       const fd = new FormData();
       fd.append('foo', ...);
       fossil.fetch('preview_forumpost',{
         payload: fd,
         onload: callback,
         onerror: (e)=>{ // only if app-specific handling is needed
           fossil.fetch.onerror(e); // default impl
           ... any app-specific error reporting ...
         }
       });
     };

     Then connect the parts with:

     fossil.connectPagePreviewers('#test-preview-connector');

     Note that the data-f-preview-from, data-f-preview-via, and
     data-f-preview-to selector are not resolved until the button is
     actually clicked, so they need not exist in the DOM at the
     instant when the connection is set up, so long as they can be
     resolved when the preview-refreshing element is clicked.
  */
  F.connectPagePreviewers = function f(selector,methodNamespace){
    if('string'===typeof selector){
      selector = document.querySelectorAll(selector);
    }else if(!selector.forEach){
      selector = [selector];
    }
    if(!methodNamespace){
      methodNamespace = F.page;
    }
    selector.forEach(function(e){
      e.addEventListener(
        'click', function(r){
          const eTo = document.querySelector('#'+e.dataset.fPreviewTo),
                eFrom = document.querySelector('#'+e.dataset.fPreviewFrom),
                asText = +(e.dataset.fPreviewAsText || 0);
          eTo.textContent = "Fetching preview...";
          methodNamespace[e.dataset.fPreviewVia](
            eFrom.value,
            (r)=>eTo[asText ? 'textContent' : 'innerHTML'] = r||''
          );
        }, false
      );
    });
    return this;
  };

})(window);
