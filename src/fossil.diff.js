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
     Extracts either the starting or ending line number from a
     line-numer column in the given tr. isSplit must be true if tr
     represents a split diff, else false. Expects its tr to be valid:
     GIGO applies.  Returns the starting line number if getStart, else
     the ending line number. Returns the line number from the LHS file
     if getLHS is true, else the RHS.
  */
  const extractLineNo = function f(getLHS, getStart, tr, isSplit){
    if(!f.rx){
      f.rx = {
        start: /^\s*(\d+)/,
        end: /(\d+)\n?$/
      }
    }
    const td = tr.querySelector('td:nth-child('+(
      /* TD element with the line numbers */
      getLHS ? 1 : (isSplit ? 4 : 2)
    )+')');
    const m = f.rx[getStart ? 'start' : 'end'].exec(td.innerText);
    return m ? +m[1] : undefined/*"shouldn't happen"*/;
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
    var lineFrom = +tr.dataset.startln;
    /* TODO: for the time being, for simplicity, we'll read the whole
       [startln, endln] chunk. "Later on" we'll maybe want to read it in
       chunks of, say, 20 lines or so, adjusting lineFrom to be 1 if it would
       be less than 1. */
    Diff.fetchArtifactChunk({
      urlParams:{
        name: hash,
        from: lineFrom,
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
        /* Replace content of tr.diffskip with the fetches result.
           When we refactor this to load in smaller chunks, we'll instead
           need to keep this skipper in place and:

           - Add a new TR above or above it, as apropriate.

           - Change the TR.dataset.startln/endln values to account for
           the just-fetched set.
         */
        D.clearElement(tr);
        const cols = [], preCode = [D.pre()], preLines = [D.pre(), D.pre()];
        if(isSbs){
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnl'));
          cols.push(D.addClass(D.td(tr), 'difftxt', 'difftxtl'));
          cols.push(D.addClass(D.td(tr), 'diffsep'));
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnr'));
          cols.push(D.addClass(D.td(tr), 'difftxt', 'difftxtr'));
          D.append(cols[0], preLines[0]);
          D.append(cols[1], preCode[0]);
          D.append(cols[3], preLines[1]);
          preCode.push(D.pre());
          D.append(cols[4], preCode[1]);
        }else{
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnl'));
          cols.push(D.addClass(D.td(tr), 'diffln', 'difflnr'));
          cols.push(D.addClass(D.td(tr), 'diffsep'));
          cols.push(D.addClass(D.td(tr), 'difftxt', 'difftxtu'));
          D.append(cols[0], preLines[0]);
          D.append(cols[1], preLines[1]);
          D.append(cols[3], preCode[0]);
        }
        let lineno = [], i;
        for( i = lineFrom; i <= lineTo; ++i ){
          lineno.push(i);
        }
        preLines[0].append(lineno.join('\n')+'\n');
        if(1){
          const code = result.join('\n')+'\n';
          preCode.forEach((e)=>e.innerText = code);
        }
        //console.debug("Updated TR",tr);
        Diff.initTableDiff(table).checkTableWidth(true);
        /*
          Reminders to self during development:

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
          number ranges in all elements which have that state handy.
         */
      }
    });
  };
  
  /**
     Installs chunk-loading controls into TR element tr. isSplit is true
     if the parent table is a split diff, else false.

     The goal is to base these controls closely on github's, a good example
     of which, for use as a model, is:

     https://github.com/msteveb/autosetup/commit/235925e914a52a542

     Each instance corresponds to a single TR.diffskip element.
  */
  Diff.ChunkLoadControls = function(isSplit, tr){
    this.isSplit = isSplit;
    this.e = {/*DOM elements*/
      tr: tr,
      table: tr.parentElement/*TBODY*/.parentElement
    };
    this.fileHash = this.e.table.dataset.lefthash;
    tr.$chunker = this /* keep GC from reaping this */;
    this.pos = {
      //hash: F.hashDigits(this.fileHash),
      /* These line numbers correspond to the LHS file. Because the
         contents are common to both sides, we have the same number
         for the RHS, but need to extract those line numbers from the
         neighboring TR blocks */
      startLhs: +tr.dataset.startln,
      endLhs: +tr.dataset.endln
    };
    D.clearElement(tr);
    this.e.td = D.addClass(
      /* Holder for our UI controls */
      D.attr(D.td(tr), 'colspan', isSplit ? 5 : 4),
      'chunkctrl'
    );
    this.e.btnWrapper = D.div();
    D.append(this.e.td, this.e.btnWrapper);
    /**
       Depending on various factors, we need one or more of:

       - A single button to load the initial chunk incrementally

       - A single button to load all lines then remove this control

       - Two buttons: one to load upwards, one to load downwards

       - A single button to load the final chunk incrementally
    */
    if(tr.nextElementSibling){
      this.pos.next = {
        startLhs: extractLineNo(true, true, tr.nextElementSibling, isSplit),
        startRhs: extractLineNo(false, true, tr.nextElementSibling, isSplit)
      };
    }
    if(tr.previousElementSibling){
      this.pos.prev = {
        endLhs: extractLineNo(true, false, tr.previousElementSibling, isSplit),
        endRhs: extractLineNo(false, false, tr.previousElementSibling, isSplit)
      };
    }
    let btnUp = false, btnDown = false;
    /**
       this.pos.next refers to the line numbers in the next TR's chunk.
       this.pos.prev refers to the line numbers in the previous TR's chunk.
    */
    if((this.pos.startLhs + Diff.config.chunkLoadLines
        >= this.pos.endLhs )
       || (this.pos.prev && this.pos.next
           && ((this.pos.next.startLhs - this.pos.prev.endLhs)
               <= Diff.config.chunkLoadLines))){
      /* Place a single button to load the whole block, rather
         than separate up/down buttons. */
      //delete this.pos.next;
      btnDown = false;
      btnUp = D.append(
        D.addClass(D.span(), 'button', 'up', 'down'),
        D.span(/*glyph holder*/)
        //D.append(D.span(), this.config.glyphDown, this.config.glyphUp)
      );
    }else{
      /* Figure out which chunk-load buttons to add... */
      if(this.pos.prev){
        btnDown = D.append(
          D.addClass(D.span(), 'button', 'down'),
          D.span(/*glyph holder*/)
          //D.append(D.span(), this.config.glyphDown)
        );
      }
      if(this.pos.next){
        btnUp = D.append(
          D.addClass(D.span(), 'button', 'up'),
          D.span(/*glyph holder*/)
          //D.append(D.span(), this.config.glyphUp)
        );
      }
    }
    if(btnDown){
      D.append(this.e.btnWrapper, btnDown);
      btnDown.addEventListener('click', ()=>this.fetchChunk(1),false);
    }
    if(btnUp){
      D.append(this.e.btnWrapper, btnUp);
      btnUp.addEventListener(
        'click', ()=>this.fetchChunk(btnUp.classList.contains('down') ? 0 : -1),
        false);
    }
    /* For debugging only... */
    this.e.posState = D.span();
    D.append(this.e.btnWrapper, this.e.posState);
    this.updatePosDebug();
  };

  Diff.ChunkLoadControls.prototype = {
    config: {
      /*
      glyphUp: '⇡', //'&#uarr;',
      glyphDown: '⇣' //'&#darr;'
      */
    },
    updatePosDebug: function(){
      if(this.e.posState){
        D.append(D.clearElement(this.e.posState), JSON.stringify(this.pos));
      }
      return this;
    },
    
    destroy: function(){
      D.remove(this.e.tr);
      delete this.e.tr.$chunker;
      delete this.e.tr;
      delete this.e;
      delete this.pos;
    },
    /**
       Creates a new TR element, including its TD elements (depending
       on this.isSplit), but does not fill it with any information nor
       inject it into the table (it doesn't know where to do
       so). Returns an object containing the TR element and various TD
       elements which will likely be needed by the routine which
       called this. See this code for details.
    */
    newTR: function(){
      const tr = D.addClass(D.tr(),'fetched'), rc = {
        tr,
        preLnL: D.pre(),
        preLnR: D.pre()
      };
      if(this.isSplit){
        D.append(D.addClass( D.td(tr), 'diffln', 'difflnl' ), rc.preLnL);
        rc.preTxtL = D.pre();
        D.append(D.addClass( D.td(tr), 'difftxt', 'difftxtl' ), rc.preTxtL);
        D.addClass( D.td(tr), 'diffsep' );
        D.append(D.addClass( D.td(tr), 'diffln', 'difflnr' ), rc.preLnR);
        rc.preTxtR = D.pre();
        D.append(D.addClass( D.td(tr), 'difftxt', 'difftxtr' ), rc.preTxtR);
      }else{
        D.append(D.addClass( D.td(tr), 'diffln', 'difflnl' ), rc.preLnL);
        D.append(D.addClass( D.td(tr), 'diffln', 'difflnr' ), rc.preLnR);
        D.addClass( D.td(tr), 'diffsep' );
        rc.preTxtU = D.pre();
        D.append(D.addClass( D.td(tr), 'difftxt', 'difftxtu' ), rc.preTxtU);
      }
      return rc;
    },

    injectResponse: function(direction/*as for fetchChunk()*/,
                             urlParam/*from fetchChunk()*/,
                             lines/*response lines*/){
      console.debug("Loading line range ",urlParam.from,"-",urlParam.to);
      const row = this.newTR();
      const lineno = [];
      let i;
      for( i = urlParam.from; i <= urlParam.to; ++i ){
        /* TODO: space-pad numbers, but we don't know the proper length from here. */
        lineno.push(i);
      }
      row.preLnL.innerText = lineno.join('\n')+'\n';
      if(row.preTxtU){//unified diff
        row.preTxtU.innerText = lines.join('\n')+'\n';
      }else{//split diff
        const code = lines.join('\n')+'\n';
        row.preTxtL.innerText = code;
        row.preTxtR.innerText = code;
      }
      if(0===direction){
        /* Closing the whole gap between two chunks or a whole gap
           at the start or end of a diff. */
        let startLnR = this.pos.prev
            ? this.pos.prev.endRhs+1 /* Closing the whole gap between two chunks
                                        or end-of-file gap. */
            : this.pos.next.startRhs - lines.length /* start-of-file gap */;
        lineno.length = 0;
        for( i = startLnR; i < startLnR + lines.length; ++i ){
          /* TODO? space-pad numbers, but we don't know the proper length from here. */
          lineno.push(i);
        }
        row.preLnR.innerText = lineno.join('\n')+'\n';
        this.e.tr.parentNode.insertBefore(row.tr, this.e.tr);
        Diff.initTableDiff(this.e.table/*fix scrolling*/).checkTableWidth(true);
        this.destroy();
        return this;
      }else{
        console.debug("TODO: handle load of partial next/prev");
        this.updatePosDebug();
      }
    },

    fetchChunk: function(direction/*-1=prev, 1=next, 0=both*/){
      /* Forewarning, this is a bit confusing: when fetching the
         previous lines, we're doing so on behalf of the *next* diff
         chunk (this.pos.next), and vice versa. */
      if(this.$isFetching){
        console.debug("Cannot load chunk while a load is pending.");
        return this;
      }
      if(direction<0/*prev chunk*/ && !this.pos.next){
        console.error("Attempt to fetch previous diff lines but don't have any.");
        return this;
      }else if(direction>0/*next chunk*/ && !this.pos.prev){
        console.error("Attempt to fetch next diff lines but don't have any.");
        return this;
      }
      console.debug("Going to fetch in direction",direction);
      const fOpt = {
        urlParams:{
          name: this.fileHash, from: 0, to: 0
        },
        aftersend: ()=>delete this.$isFetching
      };
      const self = this;
      if(direction!=0){
        console.debug("Skipping fetch for now.");
        return this;
      }else{
        fOpt.urlParams.from = this.pos.startLhs;
        fOpt.urlParams.to = this.pos.endLhs;
        fOpt.onload = function(list){
          self.injectResponse(direction,fOpt.urlParams,list);
        };
      }
      this.$isFetching = true;
      Diff.fetchArtifactChunk(fOpt);
      return this;
    }
  };

  Diff.addDiffSkipHandlers = function(){
    const tables = document.querySelectorAll('table.diff[data-lefthash]:not(.diffskipped)');
    /* Potential performance-related TODO: instead of installing all
       of these at once, install them as the corresponding TR is
       scrolled into view. */
    tables.forEach(function(table){
      D.addClass(table, 'diffskipped'/*avoid processing these more than once */);
      const isSplit = table.classList.contains('splitdiff')/*else udiff*/;
      table.querySelectorAll('tr.diffskip[data-startln]').forEach(function(tr){
        new Diff.ChunkLoadControls(isSplit, D.addClass(tr, 'jchunk'));
      });
    });
    return F;
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
    table.$txtPres.forEach((e)=>(e===this) ? 1 : (e.scrollLeft = this.scrollLeft));
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
        this.$txtPres[0].scrollLeft += len;
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

