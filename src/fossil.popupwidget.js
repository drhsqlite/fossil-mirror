(function(F/*fossil object*/){
  /**
     A very basic tooltip-like widget. It's intended to be popped up
     to display basic information or basic user interaction
     components, e.g. a copy-to-clipboard button.

     Requires: fossil.bootstrap, fossil.dom
  */
  const D = F.dom;

  /**
     Creates a new tooltip-like widget using the given options object.

     Options:

     .refresh: callback which is called just before the tooltip is
     revealed. It must refresh the contents of the tooltip, if needed,
     by applying the content to/within this.e, which is the base DOM
     element for the tooltip (and is a child of document.body). If the
     contents are static and set up via the .init option then this
     callback is not needed. When moving an already-shown tooltip,
     this is *not* called. It arguably should be, but the fact is that
     we often have to show() a popup twice in a row without hiding it
     between those calls: once to get its computed size and another to
     move it by some amount relative to that size. If the state of the
     popup depends on its position and a "double-show()" is needed
     then the client must hide() the popup between the two calls to
     show() in order to force a call to refresh() on the second
     show().

     .adjustX: an optional callback which is called when the tooltip
     is to be displayed at a given position and passed the X
     viewport-relative coordinate. This routine must either return its
     argument as-is or return an adjusted value. The intent is to
     allow a given tooltip may be positioned more appropriately for a
     given context, if needed (noting that the desired position can,
     and probably should, be passed to the show() method
     instead). This class's API assumes that clients give it
     viewport-relative coordinates, and it will take care to translate
     those to page-relative, so this callback should not do so.

     .adjustY: the Y counterpart of adjustX.

     .init: optional callback called one time to initialize the state
     of the tooltip. This is called after the this.e has been created
     and added (initially hidden) to the DOM. If this is called, it is
     removed from the object immediately after it is called.

     All callback options are called with the PopupWidget object as
     their "this".


     .cssClass: optional CSS class, or list of classes, to apply to
     the new element. In addition to any supplied here (or inherited
     from the default), the class "fossil-PopupWidget" is always set
     in order to allow certain app-internal CSS to account for popup
     windows in special cases.

     .style: optional object of properties to copy directly into
     the element's style object.     

     The options passed to this constructor get normalized into a
     separate object which includes any default values for options not
     provided by the caller. That object is available this the
     resulting PopupWidget's options property. Default values for any
     options not provided by the caller are pulled from
     PopupWidget.defaultOptions, and modifying those affects all
     future calls to this method but has no effect on existing
     instances.


     Example:

     const tip = new fossil.PopupWidget({
       init: function(){
         // optionally populate DOM element this.e with the widget's
         // content.
       },
       refresh: function(){
         // (re)populate/refresh the contents of the main
         // wrapper element, this.e.
       }
     });

     tip.show(50, 100);
     // ^^^ viewport-relative coordinates. See show() for other options.

  */
  F.PopupWidget = function f(opt){
    opt = F.mergeLastWins(f.defaultOptions,opt);
    this.options = opt;
    const e = this.e = D.addClass(D.div(), opt.cssClass,
                                  "fossil-PopupWidget");
    this.show(false);
    if(opt.style){
      let k;
      for(k in opt.style){
        if(opt.style.hasOwnProperty(k)) e.style[k] = opt.style[k];
      }
    }
    D.append(document.body, e/*must be in the DOM for size calc. to work*/);
    D.copyStyle(e, opt.style);
    if(opt.init){
      opt.init.call(this);
      delete opt.init;
    }
  };

  /**
     Default options for the PopupWidget constructor. These values are
     used for any options not provided by the caller. Any changes made
     to this instace affect future calls to PopupWidget() but have no
     effect on existing instances.
  */
  F.PopupWidget.defaultOptions = {
    cssClass: 'fossil-tooltip',
    style: undefined /*{optional properties copied as-is into element.style}*/,
    adjustX: (x)=>x,
    adjustY: (y)=>y,
    refresh: function(){},
    init: undefined /* optional initialization function */
  };

  F.PopupWidget.prototype = {

    /** Returns true if the widget is currently being shown, else false. */
    isShown: function(){return !this.e.classList.contains('hidden')},

    /** Calls the refresh() method of the options object and returns
        this object. */
    refresh: function(){
      if(this.options.refresh){
        this.options.refresh.call(this);
      }
      return this;
    },

    /**
       Shows or hides the tooltip.

       Usages:

       (bool showIt) => hide it or reveal it at its last position.

       (x, y) => reveal/move it at/to the given
       relative-to-the-viewport position, which will be adjusted to make
       it page-relative.

       (DOM element) => reveal/move it at/to a position based on the
       the given element (adjusted slightly).

       For the latter two, this.options.adjustX() and adjustY() will
       be called to adjust it further.

       Returns this object.

       If this call will reveal the element then it calls
       this.refresh() to update the UI state. If the element was
       already revealed, the call to refresh() is skipped.

       Sidebar: showing/hiding the widget is, as is conventional for
       this framework, done by removing/adding the 'hidden' CSS class
       to it, so that class must be defined appropriately.
    */
    show: function(){
      var x = undefined, y = undefined, showIt,
          wasShown = !this.e.classList.contains('hidden');
      if(2===arguments.length){
        x = arguments[0];
        y = arguments[1];
        showIt = true;
      }else if(1===arguments.length){
        if(arguments[0] instanceof HTMLElement){
          const p = arguments[0];
          const r = p.getBoundingClientRect();
          x = r.x + r.x/5;
          y = r.y - r.height/2;
          showIt = true;
        }else{
          showIt = !!arguments[0];
        }
      }
      if(showIt){
        if(!wasShown) this.refresh();
        x = this.options.adjustX.call(this,x);
        y = this.options.adjustY.call(this,y);
        x += window.pageXOffset;
        y += window.pageYOffset;
      }
      if(showIt){
        if('number'===typeof x && 'number'===typeof y){
          this.e.style.left = x+"px";
          this.e.style.top = y+"px";
        }
        D.removeClass(this.e, 'hidden');
      }else{
        D.addClass(this.e, 'hidden');
        this.e.style.removeProperty('left');
        this.e.style.removeProperty('top');
      }
      return this;
    },

    /**
       Equivalent to show(false), but may be overridden by instances,
       so long as they also call this.show(false) to perform the
       actual hiding. Overriding can be used to clean up any state so
       that the next call to refresh() (before the popup is show()n
       again) can recognize whether it needs to do something, noting
       that it's legal, and sometimes necessary, to call show()
       multiple times without needing/wanting to completely refresh
       the popup between each call (e.g. when moving the popup after
       it's been show()n).
    */
    hide: function(){return this.show(false)},

    /**
       A convenience method which adds click handlers to this popup's
       main element and document.body to hide (via hide()) the popup
       when either element is clicked or the ESC key is pressed. Only
       call this once per instance, if at all. Returns this;

       The first argument specifies whether a click handler on this
       object is installed. The second specifies whether a click
       outside of this object should close it. The third specifies
       whether an ESC handler is installed.

       Passing no arguments is equivalent to passing (true,true,true),
       and passing fewer arguments defaults the unpassed parameters to
       true.
    */
    installHideHandlers: function f(onClickSelf, onClickOther, onEsc){
      if(!arguments.length) onClickSelf = onClickOther = onEsc = true;
      else if(1===arguments.length) onClickOther = onEsc = true;
      else if(2===arguments.length) onEsc = true;
      if(onClickSelf) this.e.addEventListener('click', ()=>this.hide(), false);
      if(onClickOther) document.body.addEventListener('click', ()=>this.hide(), true);
      if(onEsc){
        const self = this;
        document.body.addEventListener('keydown', function(ev){
          if(self.isShown() && 27===ev.which) self.hide();
        }, true);
      }
      return this;
    }
  }/*F.PopupWidget.prototype*/;

  /**
     Internal impl for F.toast() and friends.

     args:

     1) CSS class to assign to the outer element, along with
     fossil-toast-message. Must be falsy for the non-warning/non-error
     case.

     2) Multiplier of F.toast.config.displayTimeMs. Should be
     1 for default case and progressively higher for warning/error
     cases.

     3) The 'arguments' object from the function which is calling
     this.

     Returns F.toast.
  */
  const toastImpl = function f(cssClass, durationMult, argsObject){
    if(!f.toaster){
      f.toaster = new F.PopupWidget({
        cssClass: 'fossil-toast-message'
      });
      D.attr(f.toaster.e, 'role', 'alert');
    }
    const T = f.toaster;
    if(f._timer) clearTimeout(f._timer);
    D.clearElement(T.e);
    if(f._prevCssClass) T.e.classList.remove(f._prevCssClass);
    if(cssClass) T.e.classList.add(cssClass);
    f._prevCssClass = cssClass;
    D.append(T.e, Array.prototype.slice.call(argsObject,0));
    T.show(F.toast.config.position.x, F.toast.config.position.y);
    f._timer = setTimeout(
      ()=>T.hide(),
      F.toast.config.displayTimeMs * durationMult
    );
    return F.toast;
  };

  F.toast = {
    config: {
      position: { x: 5, y: 5 /*viewport-relative, pixels*/ },
      displayTimeMs: 5000
    },
    /**
       Convenience wrapper around a PopupWidget which pops up a shared
       PopupWidget instance to show toast-style messages (commonly
       seen on Android). Its arguments may be anything suitable for
       passing to fossil.dom.append(), and each argument is first
       append()ed to the toast widget, then the widget is shown for
       F.toast.config.displayTimeMs milliseconds. If this is called
       while a toast is currently being displayed, the first will be
       overwritten and the time until the message is hidden will be
       reset.

       The toast is always shown at the viewport-relative coordinates
       defined by the F.toast.config.position.

       The toaster's DOM element has the CSS class fossil-tooltip
       and fossil-toast-message, so can be style via those.

       The 3 main message types (message, warning, error) each get a
       CSS class with that same name added to them. Thus CSS can
       select on .fossil-toast-message.error to style error toasts.
    */
    message: function(/*...*/){
      return toastImpl(false,1, arguments);
    },
    /**
       Displays a toast with the 'warning' CSS class assigned to it. It
       displays for 1.5 times as long as a normal toast.
    */
    warning: function(/*...*/){
      return toastImpl('warning',1.5,arguments);
    },
    /**
       Displays a toast with the 'error' CSS class assigned to it. It
       displays for twice as long as a normal toast.
    */
    error: function(/*...*/){
      return toastImpl('error',2,arguments);
    }
  }/*F.toast*/;


  F.helpButtonlets = {
    /**
       Initializes one or more "help buttonlets". It may be passed any of:

       - A string: CSS selector (multiple matches are legal)

       - A single DOM element.

       - A forEach-compatible container of DOM elements.

       - No arguments, which is equivalent to passing the string
       ".help-buttonlet:not(.processed)".

       Passing the same element(s) more than once is a no-op: during
       initialization, each elements get the class'processed' added to
       it, and any elements with that class are skipped.

       All child nodes of a help buttonlet are removed from the button
       during initialization and stashed away for use in a PopupWidget
       when the botton is clicked.

    */
    setup: function f(){
      if(!f.hasOwnProperty('clickHandler')){
        f.clickHandler = function fch(ev){
          ev.preventDefault();
          ev.stopPropagation();
          if(!fch.popup){
            fch.popup = new F.PopupWidget({
              cssClass: ['fossil-tooltip', 'help-buttonlet-content'],
              refresh: function(){
              }
            });
            fch.popup.e.style.maxWidth = '80%'/*of body*/;
            fch.popup.installHideHandlers();
          }
          D.append(D.clearElement(fch.popup.e), ev.target.$helpContent);
          /* Shift the help around a bit to "better" fit the
             screen. However, fch.popup.e.getClientRects() is empty
             until the popup is shown, so we have to show it,
             calculate the resulting size, then move and/or resize it.

             This algorithm/these heuristics can certainly be improved
             upon.
          */
          var popupRect, rectElem = ev.target;
          while(rectElem){
            popupRect = rectElem.getClientRects()[0]/*undefined if off-screen!*/;
            if(popupRect) break;
            rectElem = rectElem.parentNode;
          }
          if(!popupRect) popupRect = {x:0, y:0, left:0, right:0};
          var x = popupRect.left, y = popupRect.top;
          if(x<0) x = 0;
          if(y<0) y = 0;
          if(rectElem){
            /* Try to ensure that the popup's z-level is higher than this element's */
            const rz = window.getComputedStyle(rectElem).zIndex;
            var myZ;
            if(rz && !isNaN(+rz)){
              myZ = +rz + 1;
            }else{
              myZ = 10000/*guess!*/;
            }
            fch.popup.e.style.zIndex = myZ;
          }
          fch.popup.show(x, y);
          x = popupRect.left, y = popupRect.top;
          popupRect = fch.popup.e.getBoundingClientRect();
          const rectBody = document.body.getClientRects()[0];
          if(popupRect.right > rectBody.right){
            x -= (popupRect.right - rectBody.right);
          }
          if(x + popupRect.width > rectBody.right){
            x = rectBody.x + (rectBody.width*0.1);
            fch.popup.e.style.minWidth = '70%';
          }else{
            fch.popup.e.style.removeProperty('min-width');
            x -= popupRect.width/2;
          }
          if(x<0) x = 0;
          //console.debug("dimensions",x,y, popupRect, rectBody);
          fch.popup.show(x, y);
          return false;
        };
        f.foreachElement = function(e){
          if(e.classList.contains('processed')) return;
          e.classList.add('processed');
          e.$helpContent = [];
          /* We have to move all child nodes out of the way because we
             cannot hide TEXT nodes via CSS (which cannot select TEXT
             nodes). We have to do it in two steps to avoid invaliding
             the list during traversal. */
          e.childNodes.forEach((ch)=>e.$helpContent.push(ch));
          e.$helpContent.forEach((ch)=>ch.remove());
          e.addEventListener('click', f.clickHandler, false);
        };
      }/*static init*/
      var elems;
      if(!arguments.length){
        arguments[0] = '.help-buttonlet:not(.processed)';
        arguments.length = 1;
      }
      if(arguments.length){
        if('string'===typeof arguments[0]){
          elems = document.querySelectorAll(arguments[0]);
        }else if(arguments[0] instanceof HTMLElement){
          elems = [arguments[0]];
        }else if(arguments[0].forEach){/* assume DOM element list or array */
          elems = arguments[0];
        }
      }
      if(elems) elems.forEach(f.foreachElement);
    },
    
    /**
       Sets up the given element as a "help buttonlet", adding the CSS
       class help-buttonlet to it. Any (optional) arguments after the
       first are appended to the element using fossil.dom.append(), so
       that they become the content for the buttonlet's popup help.

       The element is then passed to this.setup() before it
       is returned from this function.
    */
    create: function(elem/*...body*/){
      D.addClass(elem, 'help-buttonlet');
      if(arguments.length>1){
        const args = Array.prototype.slice.call(arguments,1);
        D.append(elem, args);
      }
      this.setup(elem);
      return elem;
    }
  }/*helpButtonlets*/;

  F.onDOMContentLoaded( ()=>F.helpButtonlets.setup() );
  
})(window.fossil);
