(function(F/*window.fossil object*/){
  "use strict";
  const D = F.dom, P = F.pikchr = {};

  /**
     Initializes pikchr-rendered elements with the ability to
     toggle between their SVG and source code.

     The first argument may be any of:

     - A single SVG.pikchr element.

     - A collection (with a forEach method) of such elements.

     - A CSS selector string for one or more such elements.

     - An array of such strings.

     Passing no value is equivalent to passing 'svg.pikchr'.

     For each SVG in the resulting set, this function sets up event
     handlers which allow the user to toggle the SVG between image and
     source code modes. The image will switch modes in response to
     cltr-click and, if its *parent* element has the "toggle" CSS
     class, it will also switch modes in response to single-click.

     If the parent element has the "source" CSS class, the image
     starts off with its source code visible and the image hidden,
     instead of the default of the other way around.

     Returns this object.

     Each element will only be processed once by this routine, even if
     it is passed to this function multiple times. Each processed
     element gets a "data" attribute set to it to indicate that it was
     already dealt with.

     This code expects the following structure around the SVGs, and
     will not process any which don't match this:

     <DIV><SVG.pikchr></SVG><PRE.pikchr-src></PRE></DIV>
  */
  P.addSrcView = function f(svg){
    if(!f.hasOwnProperty('parentClick')){
      f.parentClick = function(ev){
        if(ev.ctrlKey || this.classList.contains('toggle')){
          this._childs.forEach((e)=>e.classList.toggle('hidden'));
        }
        /* For the sake of small pics, we have to eliminate the
           parent element's max-width... */
        const src = this._childs[1];
        if(src.classList.contains('hidden')){
          this.style.maxWidth = this.dataset.origMaxWidth;
        }else{
          this.style.maxWidth = "unset";
        }
      };
    };
    if(!svg) svg = 'svg.pikchr';
    if('string' === typeof svg){
      document.querySelectorAll(svg).forEach((e)=>f.call(this, e));
      return this;
    }else if(svg.forEach){
      svg.forEach((e)=>f.call(this, e));
      return this;
    }
    if(svg.dataset.pikchrProcessed){
      return this;
    }
    svg.dataset.pikchrProcessed = 1;
    const parent = svg.parentNode;
    const srcView = svg.nextElementSibling;
    if(!srcView || !srcView.classList.contains('pikchr-src')){
      /* Without this element, there's nothing for us to do here. */
      return this;
    }
    console.debug(svg, parent, srcView);
    parent.dataset.origMaxWidth = parent.style.maxWidth;
    parent._childs = [svg, srcView];
    D.addClass(srcView, 'hidden');
    D.removeClass(svg, 'hidden');
    parent.addEventListener('click', f.parentClick, false);
    if(parent.classList.contains('source')){
      /* Start off in source-view mode via a very fake click event */
      f.parentClick.call(parent, {ctrlKey:true});
    }
  };
})(window.fossil);
