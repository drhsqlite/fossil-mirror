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
          wh = '0.75cm',
          styleCSS = `
.pikchr-src-button {
  min-height: ${wh}; max-height: ${wh};
  min-width: ${wh}; max-width: ${wh};
  font-size: ${wh};
  position: absolute;
  top: calc(-${wh} / 2);
  right: calc(-${wh} / 2);
  border: 1px solid black;
  background-color: rgba(255,255,0,0.5);
  border-radius: 0.25cm;
  z-index: 50;
  cursor: pointer;
  text-align: center;
  display: inline-flex;
  align-items: center;
  justify-content: center;
}
.pikchr-src-button.src-active {
  background-color: rgba(0,255,0,0.5);
}
.pikchr-src-button > span {
  vertical-align: middle;
}
.pikchr-copy-button {
  min-width: ${wh}; max-width: ${wh};
  min-height: ${wh}; max-height: ${wh};
  display: inline-block;
  position: absolute;
  top: calc(${wh} / 2 + ${wh} / 4);
  right: calc(-${wh} / 2);
  z-index: 50;
  margin-right: 0;
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
    const srcView = D.textarea(0,0,true);
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
    D.append(parent, D.addClass(srcView, 'hidden'), btnFlip, btnCopy);
    btnFlip.addEventListener('click', function(){
      const svgStyle = window.getComputedStyle(svg);
      srcView.style.width = svgStyle.width;
      srcView.style.height = svgStyle.height;
      btnFlip.classList.toggle('src-active');
      D.toggleClass([svg, srcView], 'hidden');
    }, false);
  };
  
})(window.fossil);
