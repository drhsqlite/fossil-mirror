(function(F/*the fossil object*/){
  "use strict";
  /* JS code for /forumpage and friends. Requires fossil.dom. */
  const P = fossil.page, D = fossil.dom;

  F.onPageLoad(function(){
    const scrollbarIsVisible = (e)=>e.scrollHeight > e.clientHeight;
    const getButtonHandler = function(btn, contentElem){
      return function(ev){
        if(ev) ev.preventDefault();
        const isExpanded = D.hasClass(contentElem,'expanded');
        btn.innerText = isExpanded ? 'Expand...' : 'Collapse';
        contentElem.classList.toggle('expanded');
        return false;
      };
    };
    /** Install an event handler on element e which calls the given
        callback if the user presses the element for a brief period
        (time is defined a few lines down from here). */
    const addLongpressHandler = function(e, callback){
      const longPressTime = 650 /*ms*/;
      var timer;
      const clearTimer = function(){
        if(timer){
          clearTimeout(timer);
          timer = undefined;
        }
      };
      e.addEventListener('mousedown', function(ev){
        timer = setTimeout(function(){
          clearTimer();
          callback();
        }, longPressTime);
      }, false);
      e.addEventListener('mouseup', clearTimer, false);
      e.addEventListener('mouseout', clearTimer, false);
    };
    /* Adds an Expand/Collapse toggle to all div.forumPostBody
       elements which are deemed "too large" (those for which
       scrolling is currently activated because they are taller than
       their max-height). */
    document.querySelectorAll(
      'div.forumHier, div.forumTime, div.forumHierRoot'
    ).forEach(function(forumPostWrapper){
      const content = forumPostWrapper.querySelector('div.forumPostBody');
      if(!content || !scrollbarIsVisible(content)) return;
      const button = D.button('Expand...'),
            btnEventHandler = getButtonHandler(button, content);
      button.classList.add('forum-post-collapser');
      button.addEventListener('click', btnEventHandler, false);
      if(content.nextSibling){
        forumPostWrapper.insertBefore(button, content.nextSibling);
      }else{
        forumPostWrapper.appendChild(button);
      }
      // uncomment to enable long-press expand/collapse toggle:
      // addLongpressHandler(content, btnEventHandler);
      // It may interfere with default actions on mobile platforms, though.
    });
  })/*onload callback*/;
  
})(window.fossil);
