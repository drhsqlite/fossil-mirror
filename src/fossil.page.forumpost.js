(function(F/*the fossil object*/){
  "use strict";
  /* JS code for /forumpost and friends. Requires fossil.dom
     and can optionally use fossil.pikchr. */
  const P = F.page, D = F.dom;

  /**
     When the page is loaded, this handler does the following:

     - Installs expand/collapse UI elements on "long" posts and collapses
     them.

     - Any pikchr-generated SVGs get a source-toggle button added to them
     which activates when the mouse is over the image or it is tapped.

     This is a harmless no-op if the current page has neither forum
     post constructs for (1) nor any pikchr images for (2), nor will
     NOT running this code cause any breakage for clients with no JS
     support: this is all "nice-to-have", not required functionality.
  */
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
          contentElem.parentElement.scrollIntoView({
            /* This is non-standard, but !(MSIE, Safari) supposedly support it:
               https://developer.mozilla.org/en-US/docs/Web/API/Element/scrollIntoView#Browser_compatibility
            */ behavior: 'smooth'
          });
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
      'div.forumTime, div.forumEdit'
    ).forEach(function f(forumPostWrapper){
      const content = forumPostWrapper.querySelector('div.forumPostBody');
      if(!content || !scrollbarIsVisible(content)) return;
      const parent = content.parentElement,
            widget =  D.addClass(
              D.div(),
              'forum-post-collapser','bottom'
            ),
            rightTapZone = D.addClass(
              D.div(),
              'forum-post-collapser','right'
            );
      /* Repopulates the rightTapZone with arrow indicators. Because
         of the wildly varying height of these elements, This has to
         be done dynamically at init time and upon collapse/expand. Will not
         work until the rightTapZone has been added to the DOM. */
      const refillTapZone = function f(){
        if(!f.baseTapIndicatorHeight){
          /* To figure out how often to place an arrow in the rightTapZone,
             we simply grab the first header element from the page and use
             its hight as our basis for calculation. */
          const h1 = document.querySelector('h1, h2');
          f.baseTapIndicatorHeight = h1.getBoundingClientRect().height;
        }
        D.clearElement(rightTapZone);
        var rtzHeight = parseInt(window.getComputedStyle(rightTapZone).height);
        do {
          D.append(rightTapZone, D.span());
          rtzHeight -= f.baseTapIndicatorHeight * 8;
        }while(rtzHeight>0);
      };
      const handlerStep1 = getWidgetHandler(widget, content);
      const widgetEventHandler = ()=>{ handlerStep1(); refillTapZone(); };
      content.classList.add('with-expander');
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
      content.appendChild(rightTapZone);
      rightTapZone.addEventListener('click', widgetEventHandler, false);
      refillTapZone();
    })/*F.onPageLoad()*/;

    if(F.pikchr){
      F.pikchr.addSrcView();
    }

    /* Attempt to keep stray double-clicks from double-posting. */
    const formSubmitted = function(event){
      const form = event.target;
      if( form.dataset.submitted ){
        event.preventDefault();
        return;
      }
      form.dataset.submitted = '1';
      /** If the user is left waiting "a long time," disable the
          resubmit protection. If we don't do this and they tap the
          browser's cancel button while waiting, they'll be stuck with
          an unsubmittable form. */
      setTimeout(()=>{delete form.dataset.submitted}, 7000);
      return;
    };
    document.querySelectorAll("form").forEach(function(form){
      form.addEventListener('submit',formSubmitted);
    });
  })/*F.onPageLoad callback*/;
})(window.fossil);
