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


})(window);
