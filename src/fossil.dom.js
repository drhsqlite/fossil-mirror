"use strict";
(function(F/*fossil object*/){
  /**
     A collection of HTML DOM utilities to simplify, a bit, using the
     DOM API. It is focused on manipulation of the DOM, but one of its
     core mantras is "No innerHTML." Using innerHTML in this code, in
     particular assigning to it, is absolutely verboten.
  */
  const argsToArray = (a)=>Array.prototype.slice.call(a,0);
  const isArray = (v)=>v instanceof Array;

  const dom = {
    create: function(elemType){
      return document.createElement(elemType);
    },
    createElemFactory: function(eType){
      return function(){
        return document.createElement(eType);
      };
    },
    remove: function(e){
      if(e?.forEach){
        e.forEach(
          (x)=>x?.parentNode?.removeChild(x)
        );
      }else{
        e?.parentNode?.removeChild(e);
      }
      return e;
    },
    /**
       Removes all child DOM elements from the given element
       and returns that element.

       If e has a forEach method (is an array or DOM element
       collection), this function instead clears each element in the
       collection. May be passed any number of arguments, each of
       which must be a DOM element or a container of DOM elements with
       a forEach() method. Returns its first argument.
    */
    clearElement: function f(e){
      if(!f.each){
        f.each = function(e){
          if(e.forEach){
            e.forEach((x)=>f(x));
            return e;
          }
          while(e.firstChild) e.removeChild(e.firstChild);
        };
      }
      argsToArray(arguments).forEach(f.each);
      return arguments[0];
    },
  }/* dom object */;

  /**
     Returns the result of splitting the given str on
     a run of spaces of (\s*,\s*).
  */
  dom.splitClassList = function f(str){
    if(!f.rx){
      f.rx = /(\s+|\s*,\s*)/;
    }
    return str ? str.split(f.rx) : [str];
  };

  dom.div = dom.createElemFactory('div');
  dom.p = dom.createElemFactory('p');
  dom.code = dom.createElemFactory('code');
  dom.pre = dom.createElemFactory('pre');
  dom.header = dom.createElemFactory('header');
  dom.footer = dom.createElemFactory('footer');
  dom.section = dom.createElemFactory('section');
  dom.span = dom.createElemFactory('span');
  dom.strong = dom.createElemFactory('strong');
  dom.em = dom.createElemFactory('em');
  dom.ins = dom.createElemFactory('ins');
  dom.del = dom.createElemFactory('del');
  /**
     Returns a LABEL element. If passed an argument,
     it must be an id or an HTMLElement with an id,
     and that id is set as the 'for' attribute of the
     label. If passed 2 arguments, the 2nd is text or
     a DOM element to append to the label.
  */
  dom.label = function(forElem, text){
    const rc = document.createElement('label');
    if(forElem){
      if(forElem instanceof HTMLElement){
        forElem = this.attr(forElem, 'id');
      }
      if(forElem){
        dom.attr(rc, 'for', forElem);
      }
    }
    if(text) this.append(rc, text);
    return rc;
  };
  /**
     Returns an IMG element with an optional src
     attribute value.
  */
  dom.img = function(src){
    const e = this.create('img');
    if(src) e.setAttribute('src',src);
    return e;
  };
  /**
     Creates and returns a new anchor element with the given
     optional href and label. If label===true then href is used
     as the label.
  */
  dom.a = function(href,label){
    const e = this.create('a');
    if(href) e.setAttribute('href',href);
    if(label) e.appendChild(dom.text(true===label ? href : label));
    return e;
  };
  dom.hr = dom.createElemFactory('hr');
  dom.br = dom.createElemFactory('br');
  /** Returns a new TEXT node which contains the text of all of the
      arguments appended together. */
  dom.text = function(/*...*/){
    return document.createTextNode(argsToArray(arguments).join(''));
  };
  /** Returns a new Button element with the given optional
      label and on-click event listener function. */
  dom.button = function(label,callback){
    const b = this.create('button');
    if(label) b.appendChild(this.text(label));
    if('function' === typeof callback){
      b.addEventListener('click', callback, false);
    }
    return b;
  };
  /**
     Returns a TEXTAREA element.

     Usages:

     ([boolean readonly = false])
     (non-boolean rows[,cols[,readonly=false]])

     Each of the rows/cols/readonly attributes is only set if it is
     truthy.
  */
  dom.textarea = function(){
    const rc = this.create('textarea');
    let rows, cols, readonly;
    if(1===arguments.length){
      if('boolean'===typeof arguments[0]){
        readonly = !!arguments[0];
      }else{
        rows = arguments[0];
      }
    }else if(arguments.length){
      rows = arguments[0];
      cols = arguments[1];
      readonly = arguments[2];
    }
    if(rows) rc.setAttribute('rows',rows);
    if(cols) rc.setAttribute('cols', cols);
    if(readonly) rc.setAttribute('readonly', true);
    return rc;
  };

  /**
     Returns a new SELECT element.
  */
  dom.select = dom.createElemFactory('select');

  /**
     Returns an OPTION element with the given value and label text
     (which defaults to the value).

     Usage:

     (value[, label])
     (selectElement [,value [,label = value]])

     Any forms taking a SELECT element append the new element to the
     given SELECT element.

     If any label is falsy and the value is not then the value is used
     as the label. A non-falsy label value may have any type suitable
     for passing as the 2nd argument to dom.append().

     If the value has the undefined value then it is NOT assigned as
     the option element's value and no label is set unless it has a
     non-undefined value.
  */
  dom.option = function(value,label){
    const a = arguments;
    var sel;
    if(1==a.length){
      if(a[0] instanceof HTMLElement){
        sel = a[0];
      }else{
        value = a[0];
      }
    }else if(2==a.length){
      if(a[0] instanceof HTMLElement){
        sel = a[0];
        value = a[1];
      }else{
        value = a[0];
        label = a[1];
      }
    }
    else if(3===a.length){
      sel = a[0];
      value = a[1];
      label = a[2];
    }
    const o = this.create('option');
    if(undefined !== value){
      o.value = value;
      this.append(o, this.text(label || value));
    }else if(undefined !== label){
      this.append(o, label);
    }
    if(sel) this.append(sel, o);
    return o;
  };
  dom.h = function(level){
    return this.create('h'+level);
  };
  dom.ul = dom.createElemFactory('ul');
  /**
     Creates and returns a new LI element, appending it to the
     given parent argument if it is provided.
  */
  dom.li = function(parent){
    const li = this.create('li');
    if(parent) parent.appendChild(li);
    return li;
  };

  /**
     Returns a function which creates a new DOM element of the
     given type and accepts an optional parent DOM element
     argument. If the function's argument is truthy, the new
     child element is appended to the given parent element.
     Returns the new child element.
  */
  dom.createElemFactoryWithOptionalParent = function(childType){
    return function(parent){
      const e = this.create(childType);
      if(parent) parent.appendChild(e);
      return e;
    };
  };
  
  dom.table = dom.createElemFactory('table');
  dom.thead = dom.createElemFactoryWithOptionalParent('thead');
  dom.tbody = dom.createElemFactoryWithOptionalParent('tbody');
  dom.tfoot = dom.createElemFactoryWithOptionalParent('tfoot');
  dom.tr = dom.createElemFactoryWithOptionalParent('tr');
  dom.td = dom.createElemFactoryWithOptionalParent('td');
  dom.th = dom.createElemFactoryWithOptionalParent('th');

  /**
     Creates and returns a FIELDSET element, optionaly with a LEGEND
     element added to it. If legendText is an HTMLElement then is is
     assumed to be a LEGEND and is appended as-is, else it is assumed
     (if truthy) to be a value suitable for passing to
     dom.append(aLegendElement,...).
  */
  dom.fieldset = function(legendText){
    const fs = this.create('fieldset');
    if(legendText){
      this.append(
        fs,
        (legendText instanceof HTMLElement)
          ? legendText
          : this.append(this.legend(legendText))
      );
    }
    return fs;
  };
  /**
     Returns a new LEGEND legend element. The given argument, if
     not falsy, is append()ed to the element (so it may be a string
     or DOM element.
  */
  dom.legend = function(legendText){
    const rc = this.create('legend');
    if(legendText) this.append(rc, legendText);
    return rc;
  };

  /**
     Appends each argument after the first to the first argument
     (a DOM node) and returns the first argument.

     - If an argument is a string or number, it is transformed
     into a text node.

     - If an argument is an array or has a forEach member, this
     function appends each element in that list to the target
     by calling its forEach() method to pass it (recursively)
     to this function.

     - Else the argument assumed to be of a type legal
     to pass to parent.appendChild().
  */
  dom.append = function f(parent/*,...*/){
    const a = argsToArray(arguments);
    a.shift();
    for(let i in a) {
      var e = a[i];
      if(isArray(e) || e.forEach){
        e.forEach((x)=>f.call(this, parent,x));
        continue;
      }
      if('string'===typeof e
         || 'number'===typeof e
         || 'boolean'===typeof e
         || e instanceof Error) e = this.text(e);
      parent.appendChild(e);
    }
    return parent;
  };

  dom.input = function(type){
    return this.attr(this.create('input'), 'type', type);
  };
  /**
     Returns a new CHECKBOX input element.

     Usages:

     ([boolean checked = false])
     (non-boolean value [,boolean checked])
  */
  dom.checkbox = function(value, checked){
    const rc = this.input('checkbox');
    if(1===arguments.length && 'boolean'===typeof value){
      checked = !!value;
      value = undefined;
    }
    if(undefined !== value) rc.value = value;
    if(!!checked) rc.checked = true;
    return rc;
  };
  /**
     Returns a new RADIO input element.

     ([boolean checked = false])
     (string name [,boolean checked])
     (string name, non-boolean value [,boolean checked])
  */
  dom.radio = function(){
    const rc = this.input('radio');
    let name, value, checked;
    if(1===arguments.length && 'boolean'===typeof name){
      checked = arguments[0];
      name = value = undefined;
    }else if(2===arguments.length){
      name = arguments[0];
      if('boolean'===typeof arguments[1]){
        checked = arguments[1];
      }else{
        value = arguments[1];
        checked = undefined;
      }
    }else if(arguments.length){
      name = arguments[0];
      value = arguments[1];
      checked = arguments[2];
    }
    if(name) this.attr(rc, 'name', name);
    if(undefined!==value) rc.value = value;
    if(!!checked) rc.checked = true;
    return rc;
  };

  /**
     Internal impl for addClass(), removeClass().
  */
  const domAddRemoveClass = function f(action,e){
    if(!f.rxSPlus){
      f.rxSPlus = /\s+/;
      f.applyAction = function(e,a,v){
        if(!e || !v
           /*silently skip empty strings/flasy
             values, for user convenience*/) return;
        else if(e.forEach){
          e.forEach((E)=>E.classList[a](v));
        }else{
          e.classList[a](v);
        }
      };
    }
    var i = 2, n = arguments.length;
    for( ; i < n; ++i ){
      let c = arguments[i];
      if(!c) continue;
      else if(isArray(c) ||
              ('string'===typeof c
               && c.indexOf(' ')>=0
               && (c = c.split(f.rxSPlus)))
              || c.forEach
             ){
        c.forEach((k)=>k ? f.applyAction(e, action, k) : false);
        // ^^^ we could arguably call f(action,e,k) to recursively
        // apply constructs like ['foo bar'] or [['foo'],['bar baz']].
      }else if(c){
        f.applyAction(e, action, c);
      }
    }
    return e;
  };

  /**
     Adds one or more CSS classes to one or more DOM elements.

     The first argument is a target DOM element or a list type of such elements
     which has a forEach() method.  Each argument
     after the first may be a string or array of strings. Each
     string may contain spaces, in which case it is treated as a
     list of CSS classes.

     Returns e.
  */
  dom.addClass = function(e,c){
    const a = argsToArray(arguments);
    a.unshift('add');
    return domAddRemoveClass.apply(this, a);
  };
  /**
     The 'remove' counterpart of the addClass() method, taking
     the same arguments and returning the same thing.
  */
  dom.removeClass = function(e,c){
    const a = argsToArray(arguments);
    a.unshift('remove');
    return domAddRemoveClass.apply(this, a);
  };

  /**
     Toggles CSS class c on e (a single element for forEach-capable
     collection of elements). Returns its first argument.
  */
  dom.toggleClass = function f(e,c){
    if(e.forEach){
      e.forEach((x)=>x.classList.toggle(c));
    }else{
      e.classList.toggle(c);
    }
    return e;
  };

  /**
     Returns true if DOM element e contains CSS class c, else
     false.
  */
  dom.hasClass = function(e,c){
    return (e && e.classList) ? e.classList.contains(c) : false;
  };

  /**
     Each argument after the first may be a single DOM element or a
     container of them with a forEach() method. All such elements are
     appended, in the given order, to the dest element using
     dom.append(dest,theElement). Thus the 2nd and susequent arguments
     may be any type supported as the 2nd argument to that function.

     Returns dest.
  */
  dom.moveTo = function(dest,e){
    const n = arguments.length;
    var i = 1;
    const self = this;
    for( ; i < n; ++i ){
      e = arguments[i];
      this.append(dest, e);
    }
    return dest;
  };
  /**
     Each argument after the first may be a single DOM element
     or a container of them with a forEach() method. For each
     DOM element argument, all children of that DOM element
     are moved to dest (via appendChild()). For each list argument,
     each entry in the list is assumed to be a DOM element and is
     appended to dest.

     dest may be an Array, in which case each child is pushed
     into the array and removed from its current parent element.

     All children are appended in the given order.

     Returns dest.
  */
  dom.moveChildrenTo = function f(dest,e){
    if(!f.mv){
      f.mv = function(d,v){
        if(d instanceof Array){
          d.push(v);
          if(v.parentNode) v.parentNode.removeChild(v);
        }
        else d.appendChild(v);
      };
    }
    const n = arguments.length;
    var i = 1;
    for( ; i < n; ++i ){
      e = arguments[i];
      if(!e){
        console.warn("Achtung: dom.moveChildrenTo() passed a falsy value at argument",i,"of",
                     arguments,arguments[i]);
        continue;
      }
      if(e.forEach){
        e.forEach((x)=>f.mv(dest, x));
      }else{
        while(e.firstChild){
          f.mv(dest, e.firstChild);
        }
      }
    }
    return dest;
  };

  /**
     Adds each argument (DOM Elements) after the first to the
     DOM immediately before the first argument (in the order
     provided), then removes the first argument from the DOM.
     Returns void.

     If any argument beyond the first has a forEach method, that
     method is used to recursively insert the collection's
     contents before removing the first argument from the DOM.
  */
  dom.replaceNode = function f(old,nu){
    var i = 1, n = arguments.length;
    ++f.counter;
    try {
      for( ; i < n; ++i ){
        const e = arguments[i];
        if(e.forEach){
          e.forEach((x)=>f.call(this,old,e));
          continue;
        }
        old.parentNode.insertBefore(e, old);
      }
    }
    finally{
      --f.counter;
    }
    if(!f.counter){
      old.parentNode.removeChild(old);
    }
  };
  dom.replaceNode.counter = 0;
  /**
     Two args == getter: (e,key), returns value

     Three or more == setter: (e,key,val[...,keyN,valN]), returns
     e. If val===null or val===undefined then the attribute is
     removed. If (e) has a forEach method then this routine is applied
     to each element of that collection via that method. Each pair of
     keys/values is applied to all elements designated by the first
     argument.
  */
  dom.attr = function f(e){
    if(2===arguments.length) return e.getAttribute(arguments[1]);
    const a = argsToArray(arguments);
    if(e.forEach){ /* Apply to all elements in the collection */
      e.forEach(function(x){
        a[0] = x;
        f.apply(f,a);
      });
      return e;
    }
    a.shift(/*element(s)*/);
    while(a.length){
      const key = a.shift(), val = a.shift();
      if(null===val || undefined===val){
        e.removeAttribute(key);
      }else{
        e.setAttribute(key,val);
      }
    }
    return e;
  };

  const enableDisable = function f(enable){
    var i = 1, n = arguments.length;
    for( ; i < n; ++i ){
      let e = arguments[i];
      if(e.forEach){
        e.forEach((x)=>f(enable,x));
      }else{
        e.disabled = !enable;
      }
    }
    return arguments[1];
  };

  /**
     Enables (by removing the "disabled" attribute) each element
     (HTML DOM element or a collection with a forEach method)
     and returns the first argument.
  */
  dom.enable = function(e){
    const args = argsToArray(arguments);
    args.unshift(true);
    return enableDisable.apply(this,args);
  };
  /**
     Disables (by setting the "disabled" attribute) each element
     (HTML DOM element or a collection with a forEach method)
     and returns the first argument.
  */
  dom.disable = function(e){
    const args = argsToArray(arguments);
    args.unshift(false);
    return enableDisable.apply(this,args);
  };

  /**
     A proxy for document.querySelector() which throws if
     selection x is not found. It may optionally be passed an
     "origin" object as its 2nd argument, which restricts the
     search to that branch of the tree.
  */
  dom.selectOne = function(x,origin){
    var src = origin || document,
        e = src.querySelector(x);
    if(!e){
      e = new Error("Cannot find DOM element: "+x);
      console.error(e, src);
      throw e;
    }
    return e;
  };

  /**
     "Blinks" the given element a single time for the given number of
     milliseconds, defaulting (if the 2nd argument is falsy or not a
     number) to flashOnce.defaultTimeMs. If a 3rd argument is passed
     in, it must be a function, and it gets called at the end of the
     asynchronous flashing processes.

     This will only activate once per element during that timeframe -
     further calls will become no-ops until the blink is
     completed. This routine adds a dataset member to the element for
     the duration of the blink, to allow it to block multiple blinks.

     If passed 2 arguments and the 2nd is a function, it behaves as if
     it were called as (arg1, undefined, arg2).

     Returns e, noting that the flash itself is asynchronous and may
     still be running, or not yet started, when this function returns.
  */
  dom.flashOnce = function f(e,howLongMs,afterFlashCallback){
    if(e.dataset.isBlinking){
      return;
    }
    if(2===arguments.length && 'function' ===typeof howLongMs){
      afterFlashCallback = howLongMs;
      howLongMs = f.defaultTimeMs;
    }
    if(!howLongMs || 'number'!==typeof howLongMs){
      howLongMs = f.defaultTimeMs;
    }
    e.dataset.isBlinking = true;
    const transition = e.style.transition;
    e.style.transition = "opacity "+howLongMs+"ms ease-in-out";
    const opacity = e.style.opacity;
    e.style.opacity = 0;
    setTimeout(function(){
      e.style.transition = transition;
      e.style.opacity = opacity;
      delete e.dataset.isBlinking;
      if(afterFlashCallback) afterFlashCallback();
    }, howLongMs);
    return e;
  };
  dom.flashOnce.defaultTimeMs = 400;
  /**
     A DOM event handler which simply passes event.target
     to dom.flashOnce().
  */
  dom.flashOnce.eventHandler = (event)=>dom.flashOnce(event.target)

  /**
     This variant of flashOnce() flashes the element e n times
     for a duration of howLongMs milliseconds then calls the
     afterFlashCallback() callback. It may also be called with 2
     or 3 arguments, in which case:

     2 arguments: default flash time and no callback.

     3 arguments: 3rd may be a flash delay time or a callback
     function.

     Returns this object but the flashing is asynchronous.

     Depending on system load and related factors, a multi-flash
     animation might stutter and look suboptimal.
  */
  dom.flashNTimes = function(e,n,howLongMs,afterFlashCallback){
    const args = argsToArray(arguments);
    args.splice(1,1);
    if(arguments.length===3 && 'function'===typeof howLongMs){
      afterFlashCallback = howLongMs;
      howLongMs = args[1] = this.flashOnce.defaultTimeMs;
    }else if(arguments.length<3){
      args[1] = this.flashOnce.defaultTimeMs;
    }
    n = +n;
    const self = this;
    const cb = args[2] = function f(){
      if(--n){
        setTimeout(()=>self.flashOnce(e, howLongMs, f),
                   howLongMs+(howLongMs*0.1)/*we need a slight gap here*/);
      }else if(afterFlashCallback){
        afterFlashCallback();
      }
    };
    this.flashOnce.apply(this, args);
    return this;
  };

  /**
     Adds the given CSS class or array of CSS classes to the given
     element or forEach-capable list of elements for howLongMs, then
     removes it. If afterCallack is a function, it is called after the
     CSS class is removed from all elements. If called with 3
     arguments and the 3rd is a function, the 3rd is treated as a
     callback and the default time (addClassBriefly.defaultTimeMs) is
     used. If called with only 2 arguments, a time of
     addClassBriefly.defaultTimeMs is used.

     Passing a value of 0 for howLongMs causes the default value
     to be applied.

     Returns this object but the CSS removal is asynchronous.
  */
  dom.addClassBriefly = function f(e, className, howLongMs, afterCallback){
    if(arguments.length<4 && 'function'===typeof howLongMs){
      afterCallback = howLongMs;
      howLongMs = f.defaultTimeMs;
    }else if(arguments.length<3 || !+howLongMs){
      howLongMs = f.defaultTimeMs;
    }
    this.addClass(e, className);
    setTimeout(function(){
      dom.removeClass(e, className);
      if(afterCallback) afterCallback();
    }, howLongMs);
    return this;
  };
  dom.addClassBriefly.defaultTimeMs = 1000;

  /**
     Attempts to copy the given text to the system clipboard. Returns
     true if it succeeds, else false.
  */
  dom.copyTextToClipboard = function(text){
    if( window.clipboardData && window.clipboardData.setData ){
      window.clipboardData.setData('Text',text);
      return true;
    }else{
      const x = document.createElement("textarea");
      x.style.position = 'fixed';
      x.value = text;
      document.body.appendChild(x);
      x.select();
      var rc;
      try{
        document.execCommand('copy');
        rc = true;
      }catch(err){
        rc = false;
      }finally{
        document.body.removeChild(x);
      }
      return rc;
    }
  };

  /**
     Copies all properties from the 2nd argument (a plain object) into
     the style member of the first argument (DOM element or a
     forEach-capable list of elements). If the 2nd argument is falsy
     or empty, this is a no-op.

     Returns its first argument.
  */
  dom.copyStyle = function f(e, style){
    if(e.forEach){
      e.forEach((x)=>f(x, style));
      return e;
    }
    if(style){
      let k;
      for(k in style){
        if(style.hasOwnProperty(k)) e.style[k] = style[k];
      }
    }
    return e;
  };

  /**
     Given a DOM element, this routine measures its "effective
     height", which is the bounding top/bottom range of this element
     and all of its children, recursively. For some DOM structure
     cases, a parent may have a reported height of 0 even though
     children have non-0 sizes.

     Returns 0 if !e or if the element really has no height.
  */
  dom.effectiveHeight = function f(e){
    if(!e) return 0;
    if(!f.measure){
      f.measure = function callee(e, depth){
        if(!e) return;
        const m = e.getBoundingClientRect();
        if(0===depth){
          callee.top = m.top;
          callee.bottom = m.bottom;
        }else{
          callee.top = m.top ? Math.min(callee.top, m.top) : callee.top;
          callee.bottom = Math.max(callee.bottom, m.bottom);
        }
        Array.prototype.forEach.call(e.children,(e)=>callee(e,depth+1));
        if(0===depth){
          //console.debug("measure() height:",e.className, callee.top, callee.bottom, (callee.bottom - callee.top));
          f.extra += callee.bottom - callee.top;
        }
        return f.extra;
      };
    }
    f.extra = 0;
    f.measure(e,0);
    return f.extra;
  };

  /**
     Parses a string as HTML.

     Usages:

     Array (htmlString)
     DOMElement (DOMElement target, htmlString)

     The first form parses the string as HTML and returns an Array of
     all elements parsed from it. If string is falsy then it returns
     an empty array.

     The second form parses the HTML string and appends all elements
     to the given target element using dom.append(), then returns the
     first argument.

     Caveats:

     - It expects a partial HTML document as input, not a full HTML
     document with a HEAD and BODY tags. Because of how DOMParser
     works, only children of the parsed document's (virtual) body are
     acknowledged by this routine.
  */
  dom.parseHtml = function(){
    let childs, string, tgt;
    if(1===arguments.length){
      string = arguments[0];
    }else if(2==arguments.length){
      tgt = arguments[0];
      string  = arguments[1];
    }
    if(string){
      const newNode = new DOMParser().parseFromString(string, 'text/html');
      childs = newNode.documentElement.querySelector('body');
      childs = childs ? Array.prototype.slice.call(childs.childNodes, 0) : [];
      /* ^^^ we need a clone of the list because reparenting them
         modifies a NodeList they're in. */
    }else{
      childs = [];
    }
    return tgt ? this.moveTo(tgt, childs) : childs;
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

     - data-f-preview-from: is either a DOM element id, WITH a leading
     '#' prefix, or the name of a method (see below). If it's an ID,
     the DOM element must support .value to get the content.

     - data-f-preview-to: the DOM element id of the target "previewer"
       element, WITH a leading '#', or the name of a method (see below).

     - data-f-preview-via: the name of a method (see below).

     - OPTIONAL data-f-preview-as-text: a numeric value. Explained below.

     Each element gets a click handler added to it which does the
     following:

     1) Reads the content from its data-f-preview-from element or, if
     that property refers to a method, calls the method without
     arguments and uses its result as the content.

     2) Passes the content to
     methodNamespace[f-data-post-via](content,callback). f-data-post-via
     is responsible for submitting the preview HTTP request, including
     any parameters the request might require. When the response
     arrives, it must pass the content of the response to its 2nd
     argument, an auto-generated callback installed by this mechanism
     which...

     3) Assigns the response text to the data-f-preview-to element or
     passes it to the function
     methodNamespace[f-data-preview-to](content), as appropriate. If
     data-f-preview-to is a DOM element and data-f-preview-as-text is
     '0' (the default) then the target elements contents are replaced
     with the given content as HTML, else the content is assigned to
     the target's textContent property. (Note that this routine uses
     DOMParser, rather than assignment to innerHTML, to apply
     HTML-format content.)

     The methodNamespace (2nd argument) defaults to fossil.page, and
     any method-name data properties, e.g. data-f-preview-via and
     potentially data-f-preview-from/to, must be a single method name,
     not a property-access-style string. e.g. "myPreview" is legal but
     "foo.myPreview" is not (unless, of course, the method is actually
     named "foo.myPreview" (which is legal but would be
     unconventional)). All such methods are called with
     methodNamespace as their "this".

     An example...

     First an input button:

     <button id='test-preview-connector'
       data-f-preview-from='#fileedit-content-editor' // elem ID or method name
       data-f-preview-via='myPreview' // method name
       data-f-preview-to='#fileedit-tab-preview-wrapper' // elem ID or method name
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

     Maintenance reminder: this method is not strictly part of
     fossil.dom, but is in its file because it needs access to
     dom.parseHtml() to avoid an innerHTML assignment and all code
     which uses this routine also needs fossil.dom.
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
          const eTo = '#'===e.dataset.fPreviewTo[0]
                ? document.querySelector(e.dataset.fPreviewTo)
                : methodNamespace[e.dataset.fPreviewTo],
                eFrom = '#'===e.dataset.fPreviewFrom[0]
                ? document.querySelector(e.dataset.fPreviewFrom)
                : methodNamespace[e.dataset.fPreviewFrom],
                asText = +(e.dataset.fPreviewAsText || 0);
          eTo.textContent = "Fetching preview...";
          methodNamespace[e.dataset.fPreviewVia](
            (eFrom instanceof Function ? eFrom.call(methodNamespace) : eFrom.value),
            function(r){
              if(eTo instanceof Function) eTo.call(methodNamespace, r||'');
              else if(!r){
                dom.clearElement(eTo);
              }else if(asText){
                eTo.textContent = r;
              }else{
                dom.parseHtml(dom.clearElement(eTo), r);
              }
            }
          );
        }, false
      );
    });
    return this;
  }/*F.connectPagePreviewers()*/;

  return F.dom = dom;
})(window.fossil);
