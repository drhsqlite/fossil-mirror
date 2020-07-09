(function(F/*the fossil object*/){
  "use strict";
  /* JS code for /forumpage and friends. Requires fossil.dom. */
  const P = fossil.page, D = fossil.dom;
 
  F.onPageLoad(function(){
    const scrollbarIsVisible = (e)=>e.scrollHeight > e.clientHeight;
    const doCollapser = function(forumPostWrapper){
      const content = forumPostWrapper.querySelector('div.forumPostBody');
      if(!scrollbarIsVisible(content)) return;
      const fid = forumPostWrapper.getAttribute('id');
      const cb = D.input('checkbox'), lbl = D.label(),
            cbId = fid+'-expand';
      D.addClass([cb,lbl], 'forum-post-collapser');
      cb.setAttribute('id',cbId);
      lbl.setAttribute('for', cbId)
      forumPostWrapper.insertBefore(cb, content);
      forumPostWrapper.insertBefore(lbl, content);
    };
    document.querySelectorAll(
      'div.forumHier, div.forumTime, div.forumHierRoot'
    ).forEach(doCollapser)
  });
  
})(window.fossil);
