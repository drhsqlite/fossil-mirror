(function(F/*window.fossil object*/){
  "use strict";

  const D = F.dom;

  const P = F.pikchr = {
  };

  ////////////////////////////////////////////////////////////////////////
  // Install an app-specific stylesheet, just for development, after which
  // it will be moved into default.css
  (function(){
    const head = document.head || document.querySelector('head'),
          styleTag = document.createElement('style'),
          wh = '1.5em' /* fixed width/height of buttons */,
          styleCSS = `
.pikchr-src-button {
  min-height: ${wh}; max-height: ${wh};
  min-width: ${wh}; max-width: ${wh};
  font-size: ${wh};
  position: absolute;
  top: 0;
  left: 0;
  border: 1px solid black;
  background-color: rgba(255,255,0,0.2);
  border-radius: 0.25cm;
  z-index: 50;
  cursor: pointer;
  text-align: center;
  display: inline-flex;
  align-items: center;
  justify-content: center;
  transform-origin: center;
  transition: transform 250ms linear;
  padding: 0; margin: 0;
}
.pikchr-src-button.src-active {
  transform: scaleX(-1);
}
.pikchr-src-button > span {
  vertical-align: middle;
}
textarea.pikchr-src-text {
  box-sizing: border-box/*reduces UI shift*/;
}
.pikchr-copy-button {
  min-width: ${wh}; max-width: ${wh};
  min-height: ${wh}; max-height: ${wh};
  display: inline-block;
  position: absolute;
  top: calc(${wh} * 2);
  left: 0;
  z-index: 50;
  padding: 0; margin: 0;
}
`;
    head.appendChild(styleTag);
    /* Adapted from https://stackoverflow.com/a/524721 */
    styleTag.type = 'text/css';
    D.append(styleTag, styleCSS);
  })();

  /**
     Sets up a "view source" button on one or more pikchr-created SVG
     image elements.

     The first argument may be any of:

     - A single SVG element.

     - A collection (with a forEach method) of such elements.

     - A CSS selector string for one or more such elements.

     - An array of such strings.

     Passing no value is equivalent to passing 'svg.pikchr'.

     For each SVG in the resulting set, this function does the
     following:

     - It sets the "position" value of the element's *parent* node to
     "relative", as that is necessary for what follows.

     - It creates a small pseudo-button, adding it to the SVG
     element's parent node, styled to hover in one of the element's
     corners.

     - That button, when tapped, toggles the SVG on and off
     while revealing or hiding a readonly textarea element
     which contains the source code for that pikchr SVG
     (which pikchr has helpfully embedded in the SVG's
     metadata).

     Returns this object.

     The 2nd argument is intended to be a plain options object, but it
     is currently unused, as it's not yet clear what we can/should
     make configurable.
  */
  P.addSrcView = function f(svg,opt){
    if(!svg) svg = 'svg.pikchr';
    if('string' === typeof svg){
      document.querySelectorAll(svg).forEach(
        (e)=>f.call(this, e, opt)
      );
      return this;
    }else if(svg.forEach){
      svg.forEach((e)=>f.call(this, e, opt));
      return this;
    }
    const src = svg.querySelector('pikchr\\:src');
    if(!src){
      console.warn("No pikchr:src node found in",svg);
      return this;
    }
    opt = F.mergeLastWins({
    },opt);
    const parent = svg.parentNode;
    parent.style.position = 'relative' /* REQUIRED for btn placement */;
    const srcView = D.addClass(D.textarea(0,0,true), 'pikchr-src-text');
    srcView.value = src.textContent;
    const btnFlip = D.append(
      D.addClass(D.span(), 'pikchr-src-button'),
      D.append(D.span(), "⟳")
    );
    const btnCopy = F.copyButton(
      D.span(), {
        cssClass: ['copy-button', 'pikchr-copy-button'],
        extractText: function(){
          return (srcView.classList.contains('hidden')
                  ? svg.outerHTML
                  : srcView.value);
        }
      }
    );
    const buttons = [btnFlip, btnCopy];
    D.addClass(buttons, 'hidden');
    D.append(parent, D.addClass(srcView, 'hidden'), buttons);

    /**
       Toggle the buttons on only when the mouse is in the parent
       widget's area or the user taps on that area. This seems much
       less "busy" than having them always visible and slightly in the way.
       It also means that we can make them a bit larger.
    */
    parent.addEventListener('mouseenter', function(ev){
      if(ev.target === parent) D.removeClass(buttons, 'hidden');
    }, true);
    parent.addEventListener('mouseleave', function(ev){
      if(ev.target === parent) D.addClass(buttons, 'hidden');
    }, true);
    /* mouseenter/leave work well... but only if there's a mouse. */
    parent.addEventListener('click', function(ev){
      ev.preventDefault();
      ev.stopPropagation();
      D.toggleClass(buttons, 'hidden');
    }, false);

    btnFlip.addEventListener('click', function f(ev){
      ev.preventDefault();
      ev.stopPropagation();
      if(!f.hasOwnProperty('origMaxWidth')){
        f.origMaxWidth = parent.style.maxWidth;
      }
      const svgStyle = window.getComputedStyle(svg);
      srcView.style.minWidth = svgStyle.width;
      srcView.style.minHeight = svgStyle.height;
      /* ^^^ The SVG wrapper/parent element has a max-width, so the
         textarea will be too small on tiny images and won't be
         enlargable. */
      if(0){
        /* We seem to have a fundamental incompatibility with how we
           really want to position srcView at the same pos/size as the
           svg and how that interacts with centered items.
           Until/unless this can be solved, we have to decide between
           the lesser of two evils:

           1) This option. Small images have uselessly tiny source
           view which cannot be enlarged because the parent element
           has a width and/or max-width. width/max-width are important
           for center alignment via the margin:auto trick.

           2) Center-aligned images shift all the way to the left when
           the source view is visible, then back to the center when
           source view is hidden. Source views are resizable and may
           even grow a bit automatically for tiny images.
        */
        if(srcView.classList.contains('hidden')){/*initial state*/
          parent.style.width = f.origMaxWidth;
          parent.style.maxWidth = 'unset';
        }else{/*srcView is active*/
          parent.style.maxWidth = f.origMaxWidth;
          parent.style.width = 'unset';
        }
      }else{
        /* Option #2: gives us good results for non-centered items but
           not for centered. We apparently have no(?) reliable way of
           distinguishing centered from left/indented pikchrs here
           unless we add a CSS class to mark them as such in the
           pikchr-to-wiki-image code. */
        if(srcView.classList.contains('hidden')){/*initial state*/
          parent.style.width = 'unset';
          parent.style.maxWidth = 'unset';
        }else{/*srcView is active*/
          parent.style.maxWidth = f.origMaxWidth;
          parent.style.width = 'unset';
        }

      }
      btnFlip.classList.toggle('src-active');
      D.toggleClass([svg, srcView], 'hidden');
    }, false);
  };
  
})(window.fossil);
