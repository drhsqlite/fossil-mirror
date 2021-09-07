/**
   Adds toggle checkboxes to each file entry in the diff views for
   /info and similar pages.
*/
"use strict";
window.fossil.onPageLoad(function(){
  const F = window.fossil, D = F.dom;
  const addToggle = function(diffElem){
    const sib = diffElem.previousElementSibling,
          btn = sib ? D.addClass(D.checkbox(true), 'diff-toggle') : 0;
    if(!sib) return;
    D.append(sib,btn);
    btn.addEventListener('click', function(){
      diffElem.classList.toggle('hidden');
    }, false);
  };
  document.querySelectorAll('table.diff').forEach(addToggle);
});
