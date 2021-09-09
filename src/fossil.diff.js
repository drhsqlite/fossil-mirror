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
    e:{/*certain cached DOM elements*/},
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
        //console.debug("Chunk result: ",result);
        D.clearElement(tr);
        const cols = [], pre = [D.pre()];
        if(isSbs){
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnl'));
          cols.push(D.addClass(D.td(tr), 'difftxt', 'difftxtl'));
          cols.push(D.addClass(D.td(tr), 'diffsep'));
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnr'));
          cols.push(D.addClass(D.td(tr), 'difftxt', 'difftxtr'));
          D.append(cols[1], pre[0]);
          pre.push(D.pre());
          D.append(cols[4], pre[1]);
        }else{
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnl'));
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnr'));
          cols.push(D.addClass(D.td(tr), 'diffsep'));
          cols.push(D.addClass(D.td(tr), 'difftxt', 'difftxtu'));
          D.append(cols[3], pre[0]);
        }
        const code = result.join('\n')+'\n';
        pre.forEach((e)=>e.innerText = code);
        //console.debug("Updated TR",tr);
        Diff.initTableDiff(table).checkTableWidth(true);
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
            <td.diffln.difflnl><pre>...LHS line numbers...</pre></td>
            <td.difftxt.difftxtl><pre>...code lines...</pre></td>
            <td.diffsep>empty for this case (common lines)</td>
            <td.diffln.difflnr><pre>...RHS line numbers...</pre></td>
            <td.difftxt.difftxtr><pre>...dupe of col 2</pre></td>

          Unified diff col layout:
            <td.diffln.difflnl><pre>LHS line numbers</pre></td>
            <td.diffln.difflnr><pre>RHS line numbers</pre></td>
            <td.diffsep>empty in this case (common lines)</td>
            <td.difftxt.difftxtu><pre>code line</pre></td>

          C-side TODOs:

          - If we have that data readily available, it would be a big
          help (simplify our line calculations) if we stored the line
          number ranges in the (td.diffln pre) elements as
          data-startln and data-endln.
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
          const e = this;
          e.removeEventListener('click',ff);
          D.removeClass(e, 'jchunk', 'diffskip');
          fetchTrChunk(e);
        };
      }
      tr.addEventListener('click', f._handler, false);
    };
    tables.forEach(function(t){
      t.querySelectorAll('tr.diffskip[data-startln]').forEach(addDiffSkipToTr);
    });
  };

  Diff.addDiffSkipHandlers();
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
  const SCROLL_LEN = 25;
  const F = window.fossil, D = F.dom, Diff = F.diff;
  Diff.checkTableWidth = function f(force){
    if(undefined === f.lastWidth){
      f.lastWidth = 0;
    }
    if( !force && document.body.clientWidth===f.lastWidth ) return this;
    f.lastWidth = document.body.clientWidth;
    let w = f.lastWidth*0.5 - 100;
    if(force || !f.colsL){
      f.colsL = document.querySelectorAll('td.difftxtl pre');
    }
    f.colsL.forEach(function(e){
      e.style.width = w + "px";
      e.style.maxWidth = w + "px";
    });
    if(force || !f.colsR){
      f.colsR = document.querySelectorAll('td.difftxtr pre');
    }
    f.colsR.forEach(function(e){
      e.style.width = w + "px";
      e.style.maxWidth = w + "px";
    });
    if(!f.allDiffs){
      f.allDiffs = document.querySelectorAll('table.diff');
    }
    w = f.lastWidth;
    f.allDiffs.forEach((e)=>e.style.maxWidth = w + "px");
    return this;
  };

  const scrollLeft = function(event){
    //console.debug("scrollLeft",this,event);
    const table = this.parentElement/*TD*/.parentElement/*TR*/.
      parentElement/*TBODY*/.parentElement/*TABLE*/;
    table.$txtPres.forEach((e)=>e.scrollLeft = this.scrollLeft);
    return false;
  };
  Diff.initTableDiff = function f(diff){
    if(!diff){
      let i, diffs = document.querySelectorAll('table.splitdiff');
      for(i=0; i<diffs.length; ++i){
        f.call(this, diffs[i]);
      }
      return this;
    }
    diff.$txtCols = diff.querySelectorAll('td.difftxt');
    diff.$txtPres = diff.querySelectorAll('td.difftxt pre');
    var width = 0;
    diff.$txtPres.forEach(function(e){
      if(width < e.scrollWidth) width = e.scrollWidth;
    });
    //console.debug("diff.$txtPres =",diff.$txtPres);
    diff.$txtCols.forEach((e)=>e.style.width = width + 'px');
    diff.$txtPres.forEach(function(e){
      e.style.maxWidth = width + 'px';
      e.style.width = width + 'px';
      if(!e.classList.contains('scroller')){
        D.addClass(e, 'scroller');
        e.addEventListener('scroll', scrollLeft, false);
      }
    });
    diff.tabIndex = 0;
    if(!diff.classList.contains('scroller')){
      D.addClass(diff, 'scroller');
      diff.addEventListener('keydown', function(e){
        e = e || event;
        const len = {37: -SCROLL_LEN, 39: SCROLL_LEN}[e.keyCode];
        if( !len ) return;
        diff.$txtCols[0].scrollLeft += len;
        return false;
      }, false);
    }
    return this;
  }
  window.fossil.page.tweakSbsDiffs = function(){
    document.querySelectorAll('table.splitdiff').forEach((e)=>Diff.initTableDiff);
  };
  Diff.initTableDiff().checkTableWidth();
  window.addEventListener('resize', ()=>Diff.checkTableWidth());
}, false);

