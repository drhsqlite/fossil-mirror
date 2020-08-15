(function(F/*fossil object*/){
  /**
     A very basic tooltip widget.

     Requires: fossil.bootstrap, fossil.dom
  */
  const D = F.dom;

  /**
     Creates a new tooltip widget using the given options object.

     The options are available to clients after this returns via
     theTooltip.options, and default values for any options not
     provided are pulled from TooltipWidget.defaultOptions.

     Options:

     .refresh: required callback which is called whenever the tooltip
     is revealed or moved. It must refresh the contents of the
     tooltip, if needed, by applying the content to/within this.e,
     which is the base DOM element for the tooltip.

     .adjustX: an optional callback which is called when the tooltip
     is to be displayed at a given position and passed the X
     viewport-relative coordinate. This routine must either return its
     argument as-is or return an adjusted value. This API assumes that
     clients give it viewport-relative coordinates, and it will take
     care to translate those to page-relative.

     .adjustY: the Y counterpart of adjustX.

     .init: optional callback called one time to initialize the
     state of the tooltip. This is called after the this.e has
     been created and added (initially hidden) to the DOM.


     All callback options are called with the TooltipWidget object as
     their "this".


     .cssClass: optional CSS class, or list of classes, to apply to
     the new element.

     .style: optional object of properties to copy directly into
     the element's style object.     


     Example:

     const tip = new fossil.TooltipWidget({
       adjustX: (x)=>x+20,
       adjustY: function(y){return y - this.e.clientHeight/2},
       refresh: function(){
         // (re)populate/refresh the contents of the main
         // wrapper element, this.e.
       }
     });
  */
  F.TooltipWidget = function f(opt){
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
    if(opt.init){
      opt.init.call(this);
    }
  };

  F.TooltipWidget.defaultOptions = {
    cssClass: 'fossil-tooltip',
    style: {/*properties copied as-is into element.style*/},
    adjustX: (x)=>x,
    adjustY: (y)=>y,
    refresh: function(){
      console.error("The TooltipWidget refresh() option must be provided by the client.");
    }
  };

  F.TooltipWidget.prototype = {

    isShown: function(){return !this.e.classList.contains('hidden')},

    /** Calls the refresh() method of the options object and returns
        this object. */
    refresh: function(){
      this.options.refresh.call(this);
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
    */
    show: function(){
      var x = 0, y = 0, showIt;
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
      D[showIt ? 'removeClass' : 'addClass'](this.e, 'hidden');
      if(x || y){
        this.e.style.left = x+"px";
        this.e.style.top = y+"px";
      }
      return this;
    }
  }/*F.TooltipWidget.prototype*/;
  
})(window.fossil);
