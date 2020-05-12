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
      if(e.forEach){
        e.forEach(
          (x)=>x.parentNode.removeChild(x)
        );
      }else{
        e.parentNode.removeChild(e);
      }
      return e;
    },
    /**
       Removes all child DOM elements from the given element
       and returns that element.

       If e has a forEach method (is an array or DOM element
       collection), this function instead clears each element
       in the collection and returns e.
    */
    clearElement: function f(e){
      if(e.forEach){
        e.forEach((x)=>f(x));
        return e;
      }
      while(e.firstChild) e.removeChild(e.firstChild);
      return e;
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
  dom.img = function(src){
    const e = dom.create('img');
    if(src) e.setAttribute('src',src);
    return e;
  };
  /**
     Creates and returns a new anchor element with the given
     optional href and label. If label===true then href is used
     as the label.
  */
  dom.a = function(href,label){
    const e = dom.create('a');
    if(href) e.setAttribute('href',href);
    if(label) e.appendChild(dom.text(true===label ? href : label));
    return e;
  };
  dom.hr = dom.createElemFactory('hr');
  dom.br = dom.createElemFactory('br');
  dom.text = (t)=>document.createTextNode(t||'');
  dom.button = function(label){
    const b = this.create('button');
    if(label) b.appendChild(this.text(label));
    return b;
  };
  dom.select = dom.createElemFactory('select');
  /**
     Returns an OPTION element with the given value and label
     text (which defaults to the value).

     May be called as (value), (selectElement), (selectElement,
     value), (value, label) or (selectElement, value,
     label). The latter appends the new element to the given
     SELECT element.

     If the value has the undefined value then it is NOT
     assigned as the option element's value.
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
     Creates and returns a FIELDSET element, optionaly with a
     LEGEND element added to it.
  */
  dom.fieldset = function(legendText){
    const fs = this.create('fieldset');
    if(legendText){
      this.append(
        fs,
        this.append(
          this.create('legend'),
          legendText
        )
      );
    }
    return fs;
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
        e.forEach((x)=>f.call(this, parent,e));
        continue;
      }
      if('string'===typeof e || 'number'===typeof e) e = this.text(e);
      parent.appendChild(e);
    }
    return parent;
  };

  dom.input = function(type){
    return this.attr(this.create('input'), 'type', type);
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

  dom.hasClass = function(e,c){
    return (e && e.classList) ? e.classList.contains(c) : false;
  };

  /**
     Each argument after the first may be a single DOM element
     or a container of them with a forEach() method. All such
     elements are appended, in the given order, to the dest
     element.

     Returns dest.
  */
  dom.moveTo = function(dest,e){
    const n = arguments.length;
    var i = 1;
    for( ; i < n; ++i ){
      e = arguments[i];
      if(e.forEach){
        e.forEach((x)=>dest.appendChild(x));
      }else{
        dest.appendChild(e);
      }
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
        console.warn("Achtung: dom.moveChildrenTo() passed a falsy value at argment",i,"of",
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

     Three == setter: (e,key,val), returns e. If val===null
     or val===undefined then the attribute is removed. If (e)
     has a forEach method then this routine is applied to each
     element of that collection via that method.           
  */
  dom.attr = function f(e){
    if(2===arguments.length) return e.getAttribute(arguments[1]);
    if(e.forEach){
      e.forEach((x)=>f(x,arguments[1],arguments[2]));
      return e;
    }            
    const key = arguments[1], val = arguments[2];
    if(null===val || undefined===val){
      e.removeAttribute(key);
    }else{
      e.setAttribute(key,val);
    }
    return e;
  };

  const enableDisable = function f(enable){
    var i = 1, n = arguments.length;
    for( ; i < n; ++i ){
      let e = arguments[i];
      if(e.forEach){
        e.forEach((x)=>f(enable,x));
        return e;
      }
      e.disabled = !enable;
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

  return F.dom = dom;
})(window.fossil);
