(function(F/*the fossil object*/){
  "use strict";
  /* JS code for /forumpage and friends. Requires fossil.dom. */
  const P = fossil.page, D = fossil.dom;
 
  F.onPageLoad(function(){
    const scrollbarIsVisible = (e)=>e.scrollHeight > e.clientHeight;
    const getButtonHandler = function(contentElem){
      return function(ev){
        const btn = ev.target;
        const isExpanded = D.hasClass(contentElem,'expanded');
        btn.innerText = isExpanded ? 'Expand...' : 'Collapse';
        contentElem.classList.toggle('expanded');
      };
    };
    const doCollapser = function(forumPostWrapper){
      const content = forumPostWrapper.querySelector('div.forumPostBody');
      if(!scrollbarIsVisible(content)) return;
      const button = D.button('Expand...');
      button.classList.add('forum-post-collapser');
      button.addEventListener('click', getButtonHandler(content), false);
      forumPostWrapper.insertBefore(button, content.nextSibling);
    };
    document.querySelectorAll(
      'div.forumHier, div.forumTime, div.forumHierRoot'
    ).forEach(doCollapser)
  });
  
})(window.fossil);
