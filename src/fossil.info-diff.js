"use strict";
window.fossil.onPageLoad(function(){
  const F = window.fossil, D = F.dom;
  const addToggle = function(diffElem){
    const cs = diffElem.getClientRects()[0];
    if(cs.height < 150/*arbitrary*/) return;
    const btn = D.addClass(D.button("Toggle diff view"), 'diff-toggle'),
          p = diffElem.parentElement;
    p.insertBefore(btn, diffElem);
    btn.addEventListener('click', function(){
      diffElem.classList.toggle('hidden');
    }, false);
    if(cs.height > 700/*arbitrary!*/){
      btn.click();
    }
  };
  document.querySelectorAll('pre.udiff, table.sbsdiffcols').forEach(addToggle);
});
