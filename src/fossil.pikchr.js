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
          wh = '1cm' /* fixed width/height of buttons */,
          styleCSS = `
.pikchr-button-bar {
  position: absolute;
  position: absolute;
  top: 0;
  left: 0;
  display: inline-flex;
  flex-direction: column;
}
.pikchr-src-button {
  min-height: ${wh}; max-height: ${wh};
  min-width: ${wh}; max-width: ${wh};
  font-size: ${wh};
  border: 1px solid black;
  background-color: rgba(255,255,0,0.7);
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
/* MIT-licensed SVG from: https://github.com/leungwensen/svg-icon/blob/master/dist/svg/ant/code.svg */
  background-image: url("data:image/svg+xml;charset=UTF-8,%3csvg viewBox='0 0 1195 1195' \
xmlns='http:/\x2fwww.w3.org/2000/svg'%3e%3cpath d='M321.333 440q-9-10-22.5-10t-22.5 \
10l-182 181q-9 9-9 22.5t9 22.5l182 182q9 10 22.5 10t22.5-10q10-9 10-22.5t-10-22.5l-159-159 \
159-159q10-10 10-23t-10-22zm552 0q9-10 22.5-10t22.5 10l182 181q9 9 9 22.5t-9 \
22.5l-182 182q-9 10-22.5 10t-22.5-10q-10-9-10-22.5t10-22.5l159-159-159-159q-10-10-10-23t10-22zm-97-180q12 \
6 16 19t-2 24l-371 704q-7 12-19.5 16t-24.5-2q-11-7-15-19.5t2-24.5l371-703q6-12 \
18.5-16t24.5 2z'/%3e%3c/svg%3e");
  background-size: contain;
}
.pikchr-src-button.src-active {
  transform: scaleX(-1);
/* MIT-licensed SVG from: https://github.com/leungwensen/svg-icon/blob/master/dist/svg/ant/picture.svg */
  background-image: url("data:image/svg+xml;charset=UTF-8,%3csvg viewBox='0 0 1195 1195' \
xmlns='http:/\x2fwww.w3.org/2000/svg'%3e%3cpath d='M1045.333 192h-896q-26 0-45 \
19t-19 45v768q0 26 19 45t45 19h896q26 0 45-19t19-45V256q0-26-19-45t-45-19zm-896 \
64h896v714l-236-348q-7-12-21-14t-25 7l-154 125-174-243q-10-14-28-13t-26 17l-232 \
448V256zm855 768h-822l231-447 184 255 179-145zm-182-642q13 0 22.5 9.5t9.5 22.5-9.5 \
22.5-22.5 9.5-22.5-9.5-9.5-22.5 9.5-22.5 22.5-9.5zm0-64q-40 0-68 28t-28 68 28 68 68 \
28 68-28 28-68-28-68-68-28z'/%3e%3c/svg%3e");
}
textarea.pikchr-src-text {
  box-sizing: border-box/*reduces UI shift*/;
}
.pikchr-copy-button {
  min-width: ${wh}; max-width: ${wh};
  min-height: ${wh}; max-height: ${wh};
  display: inline-block;
}
.pikchr-button-bar > .pikchr-src-button,
.pikchr-button-bar > .pikchr-copy-button {
  margin-bottom: 0.3em;
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

     Each element will only be processed once by this routine, even if
     it is passed to this function multiple times. Each processed
     element gets a "data" attribute set to it to indicate that it was
     already dealt with.
  */
  P.addSrcView = function f(svg,opt){
    if(!f.hasOwnProperty('bodyClick')){
      f.bodyClick = function(){
        D.addClass(document.querySelectorAll('.pikchr-button-bar'), 'hidden');
      };
      document.body.addEventListener('click', f.bodyClick, false);
    }
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
    if(svg.dataset.pikchrProcessed){
      return this;
    }
    svg.dataset.pikchrProcessed = 1;
    const parent = svg.parentNode;
    parent.style.position = 'relative' /* REQUIRED for btn placement */;
    const srcView = parent.querySelector('.pikchr-src');
    if(!srcView){
      console.warn("No pikchr source node found in",parent);
      return this;
    }
    const buttonBar = D.addClass(D.span(), 'pikchr-button-bar');
    const btnFlip = D.append(
      D.addClass(D.span(), 'pikchr-src-button'),
    );
    const btnCopy = F.copyButton(D.span(), {
      cssClass: ['copy-button', 'pikchr-copy-button'],
      extractText: ()=>(srcView.classList.contains('hidden') ? svg.outerHTML : srcView.value),
      oncopy: ()=>D.flashOnce(btnCopy, ()=>btnFlip.click())
      // ^^^ after copying and flashing, flip back to SVG mode. */
    });
    D.append(buttonBar, btnFlip, btnCopy);
    // not yet sure which options we can/should support:
    // opt = F.mergeLastWins({},opt);
    D.addClass(srcView, 'hidden')/*should already be so, but just in case*/;
    D.append(parent, D.addClass(buttonBar, 'hidden'));

    parent.addEventListener('click', function f(ev){
      ev.preventDefault();
      ev.stopPropagation();
      D.toggleClass(buttonBar, 'hidden');
    }, false);

    /** Show the mode-switch buttons only in source view, and switch to
        source view if the SVG is tapped. This allows easy switching to
        source view while also keeping the buttons out of the way in
        SVG mode and giving the user the option of select/copy in the
        source mode via normal text-selection approaches. */
    svg.addEventListener('click', function(ev){
      D.removeClass(buttonBar, 'hidden');
      btnFlip.click();
    }, false);

    /** Toggle the source/SVG view on click. */
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
      }else if(1){
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
      D.toggleClass([svg, srcView, buttonBar], 'hidden');
    }, false);
  };
  
})(window.fossil);
