(function(F/*fossil object*/){
  /**
     A basic API for creating and managing a copy-to-clipboard button.

     Requires: fossil.bootstrap, fossil.dom
  */
  const D = F.dom;

  /**
     Initializes element e as a copy button using the given options
     object.

     The first argument may be a DOM element or a string (CSS selector
     suitable for use with document.querySelector()).

     Options:

     .copyFromElement: DOM element

     .copyFromId: DOM element ID

     .extractText: optional callback which is triggered when the copy
     button is clicked. It must return the text to copy to the
     clipboard. The default is to extract it from the copy-from
     element, using its [value] member, if it has one, else its
     [innerText]. A client-provided callback may use any data source
     it likes, so long as it's synchronous. If this function returns a
     falsy value then the clipboard is not modified. This function is
     called with the fully expanded/resolved options object as its
     "this" (that's a different instance than the one passed to this
     function!).

     At least one of copyFromElement, copyFromId, or extractText must
     be provided, but if copyFromId is not set and e.dataset.copyFromId
     is then that value is used in its place. extractText() trumps the
     other two options.

     .cssClass: optional CSS class, or list of classes, to apply to e.

     .style: optional object of properties to copy directly into
     e.style.

     .oncopy: an optional callback function which is added as an event
     listener for the 'text-copied' event (see below). There is
     functionally no difference from setting this option or adding a
     'text-copied' event listener to the element, and this option is
     considered to be a convenience form of that.

     Note that this function's own defaultOptions object holds default
     values for some options. Any changes made to that object affect
     any future calls to this function.

     Be aware that clipboard functionality might or might not be
     available in any given environment. If this button appears to
     have no effect, that may be because it is not enabled/available
     in the current platform.

     The copy button emits custom event 'text-copied' after it has
     successfully copied text to the clipboard. The event's "detail"
     member is an object with a "text" property holding the copied
     text. Other properties may be added in the future. The event is
     not fired if copying to the clipboard fails (e.g. is not
     available in the current environment).

     The copy button's click handler is suppressed (becomes a no-op)
     for as long as the element has the "disabled" attribute.

     Returns the copy-initialized element.

     Example:

     const button = fossil.copyButton('#my-copy-button', {
       copyFromId: 'some-other-element-id'
     });
     button.addEventListener('text-copied',function(ev){
       console.debug("Copied text:",ev.detail.text);
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
      function(ev){
        ev.preventDefault();
        ev.stopPropagation();
        if(e.disabled) return;  /* This check is probably redundant. */
        const txt = extract.call(opt);
        if(txt && D.copyTextToClipboard(txt)){
          e.dispatchEvent(new CustomEvent('text-copied',{
            detail: {text: txt}
          }));
        }
      },
      false
    );
    if('function' === typeof opt.oncopy){
      e.addEventListener('text-copied', opt.oncopy, false);
    }
    /* Make sure the <button> contains a single nested <span>. */
    if(e.childElementCount!=1 || e.firstChild.tagName!='SPAN'){
      D.append(D.clearElement(e), D.span());
    }
    return e;
  };

  F.copyButton.defaultOptions = {
    cssClass: 'copy-button',
    oncopy: undefined,
    style: {/*properties copied as-is into element.style*/}
  };
  
})(window.fossil);
