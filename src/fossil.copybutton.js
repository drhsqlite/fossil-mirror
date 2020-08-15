(function(F/*fossil object*/){
  /**
     A basic API for creating and managing a copy-to-clipboard button.

     Requires: fossil.bootstrap, fossil.dom
  */
  const D = F.dom;

  const config = {
    blinkTimeMs: 400
  };

  /**
     Initializes element e as a copy button using the given options
     object.

     The first argument may be a DOM element or a string (CSS selector
     suitable for use with document.querySelector()).

     Options:

     .copyFromElement: DOM element

     .copyFromId: DOM element ID

     One of copyFromElement or copyFromId must be provided, but copyFromId
     may optionally be provided via e.dataset.copyFromId.

     .extractText: optional callback which is triggered when the copy
     button is clicked. I tmust return the text to copy to the
     clipboard. The default is to extract it from the copy-from
     element, using its [value] member, if it has one, else its
     [innerText]. A client-provided callback may use any data source
     it likes, so long as it's synchronous. If this function returns a
     falsy value then the clipboard is not modified. This function is
     called with the fully expanded/resolved options object as its
     "this" (that's a different instance than the one passed to this
     function!).

     .cssClass: optional CSS class, or list of classes, to apply to e.

     .style: optional object of properties to copy directly into
     e.style.


     Note that this function's own defaultOptions object holds default
     values for some options. Any changes made to that object affect
     any future calls to this function.

     Be aware that clipboard functionality might or might not be
     available in any given environment. If this button appears to
     have no effect, that may be because it is not enabled/available
     in the current platform.

     Example:

     fossil.copyButton('#my-copy-button', {
       copyFromId: 'some-other-element-id'
     });
  */
  F.copyButton = function f(e, opt){
    if('string'===typeof e){
      e = document.querySelector(e);
    }    
    opt = F.mergeLastWins(f.defaultOptions, opt);
    if(opt.cssClass){
      D.addClass(e, opt.cssClass);
    }
    var srcId, srcElem;
    if(opt.copyFromElement){
      srcElem = opt.copyFromElement;
    }else if((srcId = opt.copyFromId || e.dataset.copyFromId)){
      srcElem = document.querySelector('#'+srcId);
    }
    const extract = opt.extractText || (
      undefined===srcElem.value ? ()=>srcElem.innerText : ()=>srcElem.value
    );
    D.copyStyle(e, opt.style);
    e.addEventListener(
      'click',
      function(){
        const txt = extract.call(opt);
        //console.debug("extracted ",txt);
        if(txt && D.copyTextToClipboard(txt)){
          D.flashOnce(e, config.blinkTimeMs);
        }
      },
      false
    );
  };

  F.copyButton.defaultOptions = {
    cssClass: 'copy-button',
    style: {/*properties copied as-is into element.style*/}
  };
  
})(window.fossil);
