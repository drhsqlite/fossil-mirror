(function(F/*the fossil object*/){
  "use strict";
  /* JS code for /forumpage and friends. Requires fossil.dom. */
  const P = fossil.page, D = fossil.dom;

  F.onPageLoad(function(){
    const scrollbarIsVisible = (e)=>e.scrollHeight > e.clientHeight;
    /* Returns an event handler which implements the post expand/collapse toggle
       on contentElem when the given widget is activated. */
    const getWidgetHandler = function(widget, contentElem){
      return function(ev){
        if(ev) ev.preventDefault();
        const wasExpanded = widget.classList.contains('expanded');
        widget.classList.toggle('expanded');
        contentElem.classList.toggle('expanded');
        if(wasExpanded){
          contentElem.classList.add('shrunken');
          contentElem.parentElement.scrollIntoView();
        }else{
          contentElem.classList.remove('shrunken');
        }
        return false;
      };
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
      const parent = content.parentElement,
            rightTapZone = D.div(),
            widget = D.div(),
            widgetEventHandler = getWidgetHandler(widget, content);
      content.classList.add('with-expander');
      widget.classList.add('forum-post-collapser');
      widget.addEventListener('click', widgetEventHandler, false);
      /** Append 3 children, which CSS will evenly space across the
          widget. This improves visibility over having the label
          in only the left, right, or center. */
      var i = 0;
      for( ; i < 3; ++i ) D.append(widget, D.span());
      if(content.nextSibling){
        forumPostWrapper.insertBefore(widget, content.nextSibling);
      }else{
        forumPostWrapper.appendChild(widget);
      }
      /** A double-click toggle will select "the current word" on the
          post, which is minorly annoying but otherwise harmless. Such
          a toggle has proven convenient on "excessive" posts,
          though. */
      //content.addEventListener('dblclick', widgetEventHandler);
      content.appendChild(rightTapZone);
      rightTapZone.addEventListener('click', widgetEventHandler, false);
    });
  })/*onload callback*/;
  
})(window.fossil);
