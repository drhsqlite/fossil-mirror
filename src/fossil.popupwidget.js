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
     revealed or moved. It must refresh the contents of the tooltip,
     if needed, by applying the content to/within this.e, which is the
     base DOM element for the tooltip (and is a child of
     document.body). If the contents are static and set up via the
     .init option then this callback is not needed.

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
     the new element.

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
    const e = this.e = D.addClass(D.div(), opt.cssClass);
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

       Sidebar: showing/hiding the widget is, as is conventional for
       this framework, done by removing/adding the 'hidden' CSS class
       to it, so that class must be defined appropriately.
    */
    show: function(){
      var x = undefined, y = undefined, showIt;
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
        this.refresh();
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
        delete this.e.style.removeProperty('left');
        delete this.e.style.removeProperty('top');
      }
      return this;
    },

    hide: function(){return this.show(false)}
  }/*F.PopupWidget.prototype*/;

  /**
     Convenience wrapper around a PopupWidget which pops up a shared
     PopupWidget instance to show toast-style messages (commonly seen
     on Android). Its arguments may be anything suitable for passing
     to fossil.dom.append(), and each argument is first append()ed to
     the toast widget, then the widget is shown for
     F.toast.config.displayTimeMs milliseconds. This is called while
     a toast is currently being displayed, the first will be overwritten
     and the time until the message is hidden will be reset.

     The toast is always shown at the viewport-relative coordinates
     defined by the F.toast.config.position.

     The toaster's DOM element has the CSS classes fossil-tooltip
     and fossil-toast, so can be style via those.
  */
  F.toast = function f(/*...*/){
    if(!f.toast){
      f.toast = function ff(argsObject){
        if(!ff.toaster) ff.toaster = new F.PopupWidget({
          cssClass: ['fossil-tooltip', 'fossil-toast']
        });
        if(f._timer) clearTimeout(f._timer);
        D.clearElement(ff.toaster.e);
        var i = 0;
        for( ; i < argsObject.length; ++i ){
          D.append(ff.toaster.e, argsObject[i]);
        };
        ff.toaster.show(f.config.position.x, f.config.position.y);
        f._timer = setTimeout(()=>ff.toaster.hide(), f.config.displayTimeMs);
      };
    }
    f.toast(arguments);
  };
  F.toast.config = {
    position: { x: 5, y: 5 /*viewport-relative, pixels*/ },
    displayTimeMs: 2500
  };

})(window.fossil);
