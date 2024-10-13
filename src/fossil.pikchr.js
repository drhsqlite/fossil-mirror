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

     <DIV.pikchr-wrapper>
       <DIV.pikchr-svg><SVG.pikchr></SVG></DIV>
       <DIV.pikchr-src>
         <PRE>pikchr source code</PRE>
         <SPAN class='hidden'><A>link to open pikchr in /pikchrshow</A></SPAN>
       </DIV>
     </DIV>
  */
  P.addSrcView = function f(svg){
    if(!f.hasOwnProperty('parentClick')){
      f.parentClick = function(ev){
        if(ev.altKey || ev.metaKey || ev.ctrlKey
           /* Every combination of special key (alt, shift, ctrl,
              meta) is handled differently everywhere. Shift is used
              by the browser, Ctrl doesn't work on an iMac, and Alt is
              intercepted by most Linux window managers to control
              window movement! So...  we just listen for *any* of them
              (except Shift) and the user will need to find one which
              works on on their environment. */
           || this.classList.contains('toggle')){
          this.classList.toggle('source');
          ev.stopPropagation();
          ev.preventDefault();
        }
      };
      /**
         Event handler for the "open in pikchrshow" links: store the
         source code for the link's pikchr in
         window.sessionStorage['pikchr-xfer'] then open
         /pikchrshow?fromSession to trigger loading of that pikchr.
      */
      f.clickPikchrShow = function(ev){
        const pId = this.dataset['pikchrid'] /* ID of the associated pikchr source code element */;
        if(!pId) return;
        const ePikchr = this.parentNode.parentNode.querySelector('#'+pId);
        if(!ePikchr) return;
        ev.stopPropagation() /* keep pikchr source view from toggling */;
        window.sessionStorage.setItem('pikchr-xfer', ePikchr.innerText);
        /*
          After returning from this function the link element will
          open [/pikchrshow?fromSession], and pikchrshow will extract
          the pikchr source code from sessionStorage['pikchr-xfer'].

          Quirks of this ^^^ design:

          We use only a single slot in sessionStorage. We could
          alternately use a key like pikchr-$pId and pass that key on
          to /pikchrshow via fromSession=pikchr-$pId, but that would
          eventually lead to stale session entries if loading of
          pikchrshow were interrupted at an untimely point. The
          down-side of _not_ doing that is that some user (or
          automation) options multiple "open in pikchrshow" links
          rapidly enough, the will open the same pikchr (the one which
          was stored in the session's slot most recently).  The
          current approach should be fine for normal human interaction
          speeds, but if it proves to be a problem we can instead use
          the above-described approach of storing each pikchr in its
          own session slot and simply accept that there may be stale
          entries at some point.
        */
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
    const parent = svg.parentNode.parentNode /* outermost DIV.pikchr-wrapper */;
    const srcView = parent ? svg.parentNode.nextElementSibling /* DIV.pikchr-src */ : undefined;
    if(srcView && srcView.classList.contains('pikchr-src')){
      /* Without this element, there's nothing for us to do here. */
      parent.addEventListener('click', f.parentClick, false);
      const eSpan = window.sessionStorage
            ? srcView.querySelector('span') /* "open in..." link wrapper */
            : undefined;
      if(eSpan){
        const openLink = eSpan.querySelector('a');
        if(openLink){
          openLink.addEventListener('click', f.clickPikchrShow, false);
          eSpan.classList.remove('hidden');
        }
      }
    }
    return this;
  };
})(window.fossil);
