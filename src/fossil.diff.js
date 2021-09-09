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
  const Diff = F.diff = {
    config: {
      chunkLoadLines: 20,
      chunkFetch: {
        /* Default callack handlers for Diff.fetchArtifactChunk(),
           unless overridden by options passeed to that function. */
        beforesend: function(){},
        aftersend: function(){},
        onerror: function(e){
          F.toast.error("XHR error: ",e.message);
        }
      }
    }
  };
  /**
     Uses the /jchunk AJAX route to fetch specific lines of a given
     artifact. The argument must be an Object suitable for passing as
     the second argument to fossil.fetch(). Its urlParams property
     must be an object with these properties:

     {
       name: full hash of the target file,
       from: first 1-based line number of the file to fetch (inclusive),
       to: last 1-based line number of the file to fetch (inclusive)
     }

     The fetchOpt object is NOT cloned for use by the call: it is used
     as-is and may be modified by this call. Thus callers "really
     should" pass a temporary object, not a long-lived one.

     If fetchOpt does not define any of the (beforesend, aftersend,
     onerror) callbacks, the defaults from fossil.diff.config.chunkFetch
     are used, so any given client page may override those to provide
     page-level default handling.

     Note that onload callback is ostensibly optional but this
     function is not of much use without an onload
     handler. Conversely, the default onerror handler is often
     customized on a per-page basis to send the error output somewhere
     where the user can see it.

     The response, on success, will be an array of strings, each entry
     being one line from the requested artifact. If the 'to' line is
     greater than the length of the file, the array will be shorter
     than (to-from) lines.

     The /jchunk route reports errors via JSON objects with
     an "error" string property describing the problem.

     This is an async operation. Returns the fossil object.
  */
  Diff.fetchArtifactChunk = function(fetchOpt){
    if(!fetchOpt.beforesend) fetchOpt.beforesend = Diff.config.chunkFetch.beforesend;
    if(!fetchOpt.aftersend) fetchOpt.aftersend = Diff.config.chunkFetch.aftersend;
    if(!fetchOpt.onerror) fetchOpt.onerror = Diff.config.chunkFetch.onerror;
    fetchOpt.responseType = 'json';
    return F.fetch('jchunk', fetchOpt);
  };

  /**
     Fetches /jchunk for the given TR element then replaces the TR's
     contents with data from the result of that request.
  */
  const fetchTrChunk = function(tr){
    if(tr.dataset.xfer /* already being fetched */) return;
    const table = tr.parentElement.parentElement;
    const hash = table.dataset.lefthash;
    if(!hash) return;
    const isSbs = table.classList.contains('splitdiff')/*else udiff*/;
    tr.dataset.xfer = 1 /* sentinel against multiple concurrent ajax requests */;
    const lineTo = +tr.dataset.endln;
    var lnFrom = +tr.dataset.startln;
    /* TODO: for the time being, for simplicity, we'll read the whole
       [startln, endln] chunk. "Later on" we'll maybe want to read it in
       chunks of, say, 20 lines or so, adjusting lnFrom to be 1 if it would
       be less than 1. */
    Diff.fetchArtifactChunk({
      urlParams:{
        name: hash,
        from: lnFrom,
        to: lineTo
      },
      aftersend: function(){
        delete tr.dataset.xfer;
        Diff.config.chunkFetch.aftersend.apply(
          this, Array.prototype.slice.call(arguments,0)
        );
      },
      onload: function(result){
        console.debug("Chunk result: ",result);
        D.clearElement(tr);
        D.append(
          D.attr(D.td(tr), 'colspan', isSbs ? 5 : 4),
          "Fetched chunk of ",result.length," line(s). TODO: insert it here."
        );
        /*
          At this point we need to:

          - Read the previous TR, if any, to get the preceeding LHS/RHS
          line numbers so that we know where to start counting.

          - If there is no previous TR, we're at the top and we
          instead need to get the LHS/RHS line numbers from the
          following TR's children.

          - D.clearElement(tr) and insert columns appropriate for the
          parent table's diff type.

          We can fish the line numbers out of the PRE columns with something
          like this inefficient but effective hack:

          theElement.innerText.split(/\n+/)

          (need /\n+/ instead of '\n' b/c of INS/DEL elements)

          Noting that the result array will end with an empty element
          due to the trailing \n character, so a call to pop() will be
          needed.

          SBS diff col layout:
            <td><pre>...LHS line numbers...</pre></td>
            <td>...code lines...</td>
            <td></td> empty for this case.
            <td><pre>...RHS line numbers...</pre></td>
            <td>...dupe of col 2</td>

          Unified diff col layout:
            <td>LHS line numbers</td>
            <td>RHS line numbers</td>
            <td>blank in this case</td>
            <td>code line</td>
         */
      }
    });
  };
  
  Diff.addDiffSkipHandlers = function(){
    const tables = document.querySelectorAll('table.diff[data-lefthash]');
    if(!tables.length) return F;
    const addDiffSkipToTr = function f(tr){
      D.addClass(tr, 'jchunk');
      if(!f._handler){
        f._handler = function ff(event){
          var e = event.target;
          while(e && 'TR' !== e.tagName) e = e.parentElement;
          if(!e){
            console.error("Internal event-handling error: didn't find TR target.");
            return;
          }
          e.removeEventListener('click',ff);
          D.removeClass(e, 'jchunk');
          //console.debug("addDiffSkipToTr() Event:",e, event);
          fetchTrChunk(e);
        };
      }
      tr.addEventListener('click', f._handler, false);
    };
    tables.forEach(function(t){
      t.querySelectorAll('tr.diffskip[data-startln]').forEach(addDiffSkipToTr);
    });
  };

  F.diff.addDiffSkipHandlers();
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

