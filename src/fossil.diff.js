/**
   diff-related JS APIs for fossil.
*/
"use strict";
window.fossil.onPageLoad(function(){
  /**
     Adds toggle checkboxes to each file entry in the diff views for
     /info and similar pages.
  */
  const D = window.fossil.dom;
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

window.fossil.onPageLoad(function(){
  const F = window.fossil, D = F.dom;

  /**
     Uses the /jchunk AJAX route to fetch specific lines of a given
     artifact. The first argument must be an Object with these
     properties:

     {
       name: full hash of the target file,
       from: first 1-based line number of the file to fetch (inclusive),
       to: last 1-based line number of the file to fetch (inclusive)
     }

     onload and onerror are optional callback functions to be called
     on success resp. error, as documented for window.fossil.fetch().
     Note that onload is ostensibly optional but this function is not
     of much use without an onload handler. Conversely, the default
     onerror handler is often customized on a per-page basis to send
     the error output somewhere where the user can see it.

     The response, on success, will be an array of strings, each entry
     being one line from the requested artifact. If the 'to' line is
     greater than the length of the file, the array will be shorter
     than (to-from) lines.

     The /jchunk route reports errors via JSON objects with
     an "error" string property describing the problem.

     This is an async operation. Returns this object.
  */
  F.fetchArtifactLines = function(urlParams, onload, onerror){
    const opt = {urlParams};
    if(onload) opt.onload = onload;
    if(onerror) opt.onerror = onerror;
    return this.fetch('jchunk', opt);
  };
});

/**
   2021-09-07: refactoring the following for use in the higher-level
   fossil.*.js framework is pending. For now it's a copy/paste copy
   of diff.js.
*/
/* Refinements to the display of unified and side-by-side diffs.
**
** In all cases, the table columns tagged with "difftxt" are expanded,
** where possible, to fill the width of the screen.
**
** For a side-by-side diff, if either column is two wide to fit on the
** display, scrollbars are added.  The scrollbars are linked, so that
** both sides scroll together.  Left and right arrows also scroll.
*/
window.fossil.onPageLoad(function(){
  var SCROLL_LEN = 25;
  function initDiff(diff){
    var txtCols = diff.querySelectorAll('td.difftxt');
    var txtPres = diff.querySelectorAll('td.difftxt pre');
    var width = 0;
    if(txtPres.length>=2)Math.max(txtPres[0].scrollWidth, txtPres[1].scrollWidth);
    var i;
    for(i=0; i<txtCols.length; i++){
      txtCols[i].style.width = width + 'px';
      txtPres[i].style.maxWidth = width + 'px';
      txtPres[i].style.width = width + 'px';
      txtPres[i].onscroll = function(e){
        for(var j=0; j<txtPres.length; j++) txtPres[j].scrollLeft = this.scrollLeft;
      };
    }
    diff.tabIndex = 0;
    diff.onkeydown = function(e){
      e = e || event;
      var len = {37: -SCROLL_LEN, 39: SCROLL_LEN}[e.keyCode];
      if( !len ) return;
      txtCols[0].scrollLeft += len;
      return false;
    };
  }
  window.fossil.page.tweakSbsDiffs = function(){
    document.querySelectorAll('table.splitdiff').forEach(initDiff);
  };
  var i, diffs = document.querySelectorAll('table.splitdiff')
  for(i=0; i<diffs.length; i++){
    initDiff(diffs[i]);
  }
  const checkWidth = function f(){
    if(undefined === f.lastWidth){
      f.lastWidth = 0;
    }
    if( document.body.clientWidth===f.lastWidth ) return;
    f.lastWidth = document.body.clientWidth;
    var w = f.lastWidth*0.5 - 100;
    if(!f.colsL){
      f.colsL = document.querySelectorAll('td.difftxtl pre');
    }
    for(let i=0; i<f.colsL.length; i++){
      f.colsL[i].style.width = w + "px";
      f.colsL[i].style.maxWidth = w + "px";
    }
    if(!f.colsR){
      f.colsR = document.querySelectorAll('td.difftxtr pre');
    }
    for(let i=0; i<f.colsR.length; i++){
      f.colsR[i].style.width = w + "px";
      f.colsR[i].style.maxWidth = w + "px";
    }
    if(!f.allDiffs){
      f.allDiffs = document.querySelectorAll('table.diff');
    }
    w = f.lastWidth;
    for(let i=0; i<f.allDiffs.length; i++){
      f.allDiffs[i].style.maxWidth = w + "px";
    }
  };
  checkWidth();
  window.addEventListener('resize', checkWidth);
}, false);

