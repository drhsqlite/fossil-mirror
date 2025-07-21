/**
   diff-related JS APIs for fossil.
*/
"use strict";
/* Locate the UI element (if any) into which we can inject some diff-related
   UI controls. */
window.fossil.onPageLoad(function(){
  const potentialParents = window.fossil.page.diffControlContainers = [
    /* CSS selectors for possible parents for injected diff-related UI
       controls. */
    /* Put the most likely pages at the end, as array.pop() is more
       efficient than array.shift() (see loop below). */
    /* /filedit */ 'body.cpage-fileedit #fileedit-tab-diff-buttons',
    /* /wikiedit */ 'body.cpage-wikiedit #wikiedit-tab-diff-buttons',
    /* /fdiff */ 'body.fdiff form div.submenu',
    /* /vdiff */ 'body.vdiff form div.submenu',
    /* /info, /vinfo, /ckout */ 'body.vinfo div.sectionmenu.info-changes-menu'
  ];
  window.fossil.page.diffControlContainer = undefined;
  while( potentialParents.length ){
    if( (window.fossil.page.diffControlContainer
         = document.querySelector(potentialParents.pop())) ){
      break;
    }
  }
});

window.fossil.onPageLoad(function(){
  /**
     Adds toggle checkboxes to each file entry in the diff views for
     /info and similar pages.
  */
  if( !window.fossil.page.diffControlContainer ){
    return;
  }
  const D = window.fossil.dom;
  const allToggles = [/*collection of all diff-toggle checkboxes*/];
  let checkedCount =
      0 /* When showing more than one diff, keep track of how many
           "show/hide" checkboxes are are checked so we can update the
           "show/hide all" label dynamically. */;
  let btnAll /* UI control to show/hide all diffs */;
  /* Install a diff-toggle button for the given diff table element. */
  const addToggle = function(diffElem){
    const sib = diffElem.previousElementSibling,
          ckbox = sib ? D.addClass(D.checkbox(true), 'diff-toggle') : 0;
    if(!sib) return;
    const lblToggle = D.label();
    D.append(lblToggle, ckbox, D.text(" show/hide "));
    allToggles.push(ckbox);
    ++checkedCount;
    /* Make all of the available empty space a click zone for the checkbox */
    lblToggle.style.flexGrow = 1;
    lblToggle.style.textAlign = 'right';
    D.append(sib, lblToggle);
    ckbox.addEventListener('change', function(){
      diffElem.classList[this.checked ? 'remove' : 'add']('hidden');
      if(btnAll){
        checkedCount += (this.checked ? 1 : -1);
        btnAll.innerText = (checkedCount < allToggles.length)
          ? "Show diffs" : "Hide diffs";
      }
    }, false);
    /* Extend the toggle click zone to all of the non-hyperlink
       elements in the left of this area (filenames and hashes). */
    sib.firstElementChild.addEventListener('click', (event)=>{
      if( event.target===sib.firstElementChild ){
        /* Don't respond to clicks bubbling via hyperlink children */
        ckbox.click();;
      }
    }, false);
  };
  if( !document.querySelector('body.fdiff') ){
    /* Don't show the diff toggle button for /fdiff because it only
       has a single file to show (and also a different DOM layout). */
    document.querySelectorAll('table.diff').forEach(addToggle);
  }
  /**
     Set up a "toggle all diffs" button which toggles all of the
     above-installed checkboxes, but only if more than one diff is
     rendered.
  */
  const icm = allToggles.length>1 ? window.fossil.page.diffControlContainer : 0;
  if(icm) {
    btnAll = D.addClass(D.a("#", "Hide diffs"), "button");
    D.append( icm, btnAll );
    btnAll.addEventListener('click', function(ev){
      ev.preventDefault();
      ev.stopPropagation();
      const show = checkedCount < allToggles.length;
      for( const ckbox of allToggles ){
        /* Toggle all entries to match this new state. We use click()
           instead of ckbox.checked=... so that the on-change event handler
           fires. */
        if(ckbox.checked!==show) ckbox.click();
      }
    }, false);
  }
});

window.fossil.onPageLoad(function(){
  const F = window.fossil, D = F.dom;
  const Diff = F.diff = {
    e:{/*certain cached DOM elements*/},
    config: {
      chunkLoadLines: (
        F.config.diffContextLines * 3
        /*per /chat discussion*/
      ) || 20,
      chunkFetch: {
        /* Default callack handlers for Diff.fetchArtifactChunk(),
           unless overridden by options passeed to that function. */
        beforesend: function(){},
        aftersend: function(){},
        onerror: function(e){
          console.error("XHR error: ",e);
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
     Installs chunk-loading controls into TR.diffskip element tr.
     Each instance corresponds to a single TR.diffskip element.

     The goal is to base these controls roughly on github's, a good
     example of which, for use as a model, is:

     https://github.com/msteveb/autosetup/commit/235925e914a52a542
  */
  const ChunkLoadControls = function(tr){
    this.$fetchQueue = [];
    this.e = {/*DOM elements*/
      tr: tr,
      table: tr.parentElement/*TBODY*/.parentElement
    };
    this.isSplit = this.e.table.classList.contains('splitdiff')/*else udiff*/;
    this.fileHash = this.e.table.dataset.lefthash;
    tr.$chunker = this /* keep GC from reaping this */;
    this.pos = {
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
      D.attr(D.td(tr), 'colspan', this.isSplit ? 5 : 4),
      'chunkctrl'
    );
    this.e.msgWidget = D.addClass(D.span(), 'hidden');
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
        startLhs: extractLineNo(true, true, tr.nextElementSibling, this.isSplit),
        startRhs: extractLineNo(false, true, tr.nextElementSibling, this.isSplit)
      };
    }
    if(tr.previousElementSibling){
      this.pos.prev = {
        endLhs: extractLineNo(true, false, tr.previousElementSibling, this.isSplit),
        endRhs: extractLineNo(false, false, tr.previousElementSibling, this.isSplit)
      };
    }
    let btnUp = false, btnDown = false;
    /**
       this.pos.next refers to the line numbers in the next TR's chunk.
       this.pos.prev refers to the line numbers in the previous TR's chunk.
       this.pos corresponds to the line numbers of the gap.
    */
    if(this.pos.prev && this.pos.next
       && ((this.pos.endLhs - this.pos.startLhs)
           <= Diff.config.chunkLoadLines)){
      /* Place a single button to load the whole block, rather
         than separate up/down buttons. */
      btnDown = false;
      btnUp = this.createButton(this.FetchType.FillGap);
    }else{
      /* Figure out which chunk-load buttons to add... */
      if(this.pos.prev){
        btnDown = this.createButton(this.FetchType.PrevDown);
      }
      if(this.pos.next){
        btnUp = this.createButton(this.FetchType.NextUp);
      }
    }
    //this.e.btnUp = btnUp;
    //this.e.btnDown = btnDown;
    if(btnUp) D.append(this.e.btnWrapper, btnUp);
    if(btnDown) D.append(this.e.btnWrapper, btnDown);
    D.append(this.e.btnWrapper, this.e.msgWidget);
    /* For debugging only... */
    this.e.posState = D.span();
    D.append(this.e.btnWrapper, this.e.posState);
    this.updatePosDebug();
  };

  ChunkLoadControls.prototype = {
    /** An "enum" of values describing the types of context
        fetches/operations performed by this type. The values in this
        object must not be changed without modifying all logic which
        relies on their relative order. */
    FetchType:{
      /** Append context to the bottom of the previous diff chunk. */
      PrevDown: 1,
      /** Fill a complete gap between the previous/next diff chunks
          or at the start of the next chunk or end of the previous
          chunks. */
      FillGap: 0,
      /** Prepend context to the start of the next diff chunk. */
      NextUp: -1,
      /** Process the next queued action. */
      ProcessQueue: 0x7fffffff
    },

    /**
       Creates and returns a button element for fetching a chunk in
       the given fetchType (as documented for fetchChunk()).
    */
    createButton: function(fetchType){
      let b;
      switch(fetchType){
      case this.FetchType.PrevDown:
        b = D.append(
          D.addClass(D.span(), 'down'),
          D.span(/*glyph holder*/)
        );
        break;
      case this.FetchType.FillGap:
        b = D.append(
          D.addClass(D.span(), 'up', 'down'),
          D.span(/*glyph holder*/)
        );
        break;
      case this.FetchType.NextUp:
        b = D.append(
          D.addClass(D.span(), 'up'),
          D.span(/*glyph holder*/)
        );
        break;
      default:
        throw new Error("Internal API misuse: unexpected fetchType value "+fetchType);
      }
      D.addClass(b, 'jcbutton');
      b.addEventListener('click', ()=>this.fetchChunk(fetchType),false);
      return b;
    },

    updatePosDebug: function(){
      if(this.e.posState){
        D.clearElement(this.e.posState);
        //D.append(D.clearElement(this.e.posState), JSON.stringify(this.pos));
      }
      return this;
    },

    /* Attempt to clean up resources and remove some circular references to
       that GC can do the right thing. */
    destroy: function(){
      delete this.$fetchQueue;
      D.remove(this.e.tr);
      delete this.e.tr.$chunker;
      delete this.e.tr;
      delete this.e;
      delete this.pos;
    },

    /**
       If the gap between this.pos.endLhs/startLhs is less than or equal to
       Diff.config.chunkLoadLines then this function replaces any up/down buttons
       with a gap-filler button, else it's a no-op. Returns this object.

       As a special case, do not apply this at the start or bottom
       of the diff, only between two diff chunks.
    */
    maybeReplaceButtons: function(){
      if(this.pos.next && this.pos.prev
         && (this.pos.endLhs - this.pos.startLhs <= Diff.config.chunkLoadLines)){
        D.clearElement(this.e.btnWrapper);
        D.append(this.e.btnWrapper, this.createButton(this.FetchType.FillGap));
        if( this.$fetchQueue && this.$fetchQueue.length>1 ){
          this.$fetchQueue[1] = this.FetchType.FillGap;
          this.$fetchQueue.length = 2;
        }
      }
      return this;
    },

    /**
       Callack for /jchunk responses.
    */
    injectResponse: function f(fetchType/*as for fetchChunk()*/,
                               urlParam/*from fetchChunk()*/,
                               lines/*response lines*/){
      if(!lines.length){
        /* No more data to load */
        this.destroy();
        return this;
      }
      this.msg(false);
      //console.debug("Loaded line range ",
      //urlParam.from,"-",urlParam.to, "fetchType ",fetchType);
      const lineno = [],
            trPrev = this.e.tr.previousElementSibling,
            trNext = this.e.tr.nextElementSibling,
            doAppend = (
              !!trPrev && fetchType>=this.FetchType.FillGap
            ) /* true to append to previous TR, else prepend to NEXT TR */;
      const tr = doAppend ? trPrev : trNext;
      const joinTr = (
        this.FetchType.FillGap===fetchType && trPrev && trNext
      ) ? trNext : false
      /* Truthy if we want to combine trPrev, the new content, and
         trNext into trPrev and then remove trNext. */;
      let i, td;
      if(!f.convertLines){
        /* Reminder: string.replaceAll() is a relatively new
           JS feature, not available in some still-widely-used
           browser versions. */
        f.rx = [[/&/g, '&amp;'], [/</g, '&lt;']];
        f.convertLines = function(li){
          var s = li.join('\n');
          f.rx.forEach((a)=>s=s.replace(a[0],a[1]));
          return s + '\n';
        };
      }
      if(1){ // LHS line numbers...
        const selector = '.difflnl > pre';
        td = tr.querySelector(selector);
        const lnTo = Math.min(urlParam.to,
                              urlParam.from +
                              lines.length - 1/*b/c request range is inclusive*/);
        for( i = urlParam.from; i <= lnTo; ++i ){
          lineno.push(i);
        }
        const lineNoTxt = lineno.join('\n')+'\n';
        const content = [td.innerHTML];
        if(doAppend) content.push(lineNoTxt);
        else content.unshift(lineNoTxt);
        if(joinTr){
          content.push(trNext.querySelector(selector).innerHTML);
        }
        td.innerHTML = content.join('');
      }

      if(1){// code block(s)...
        const selector = '.difftxt > pre';
        td = tr.querySelectorAll(selector);
        const code = f.convertLines(lines);
        let joinNdx = 0/*selector[X] index to join together*/;
        td.forEach(function(e){
          const content = [e.innerHTML];
          if(doAppend) content.push(code);
          else content.unshift(code);
          if(joinTr){
            content.push(trNext.querySelectorAll(selector)[joinNdx++].innerHTML)
          }
          e.innerHTML = content.join('');
        });
      }

      if(1){// Add blank lines in (.diffsep>pre)
        const selector = '.diffsep > pre';
        td = tr.querySelector(selector);
        for(i = 0; i < lineno.length; ++i) lineno[i] = '';
        const blanks = lineno.join('\n')+'\n';
        const content = [td.innerHTML];
        if(doAppend) content.push(blanks);
        else content.unshift(blanks);
        if(joinTr){
          content.push(trNext.querySelector(selector).innerHTML);
        }
        td.innerHTML = content.join('');
      }

      if(this.FetchType.FillGap===fetchType){
        /* Closing the whole gap between two chunks or a whole gap
           at the start or end of a diff. */
        // RHS line numbers...
        let startLnR = this.pos.prev
            ? this.pos.prev.endRhs+1 /* Closing the whole gap between two chunks
                                        or end-of-file gap. */
            : this.pos.next.startRhs - lines.length /* start-of-file gap */;
        lineno.length = lines.length;
        for( i = startLnR; i < startLnR + lines.length; ++i ){
          lineno[i-startLnR] = i;
        }
        const selector = '.difflnr > pre';
        td = tr.querySelector(selector);
        const lineNoTxt = lineno.join('\n')+'\n';
        lineno.length = 0;
        const content = [td.innerHTML];
        if(doAppend) content.push(lineNoTxt);
        else content.unshift(lineNoTxt);
        if(joinTr){
          content.push(trNext.querySelector(selector).innerHTML);
        }
        td.innerHTML = content.join('');
        if(joinTr) D.remove(joinTr);
        this.destroy();
        return this;
      }else if(this.FetchType.PrevDown===fetchType){
        /* Append context to previous TR. */
        // RHS line numbers...
        let startLnR = this.pos.prev.endRhs+1;
        lineno.length = lines.length;
        for( i = startLnR; i < startLnR + lines.length; ++i ){
          lineno[i-startLnR] = i;
        }
        this.pos.startLhs += lines.length;
        this.pos.prev.endRhs += lines.length;
        this.pos.prev.endLhs += lines.length;
        const selector = '.difflnr > pre';
        td = tr.querySelector(selector);
        const lineNoTxt = lineno.join('\n')+'\n';
        lineno.length = 0;
        const content = [td.innerHTML];
        if(doAppend) content.push(lineNoTxt);
        else content.unshift(lineNoTxt);
        td.innerHTML = content.join('');
        if(lines.length < (urlParam.to - urlParam.from)){
          /* No more data. */
          this.destroy();
        }else{
          this.maybeReplaceButtons();
          this.updatePosDebug();
        }
        return this;
      }else if(this.FetchType.NextUp===fetchType){
        /* Prepend content to next TR. */
        // RHS line numbers...
        if(doAppend){
          throw new Error("Internal precondition violation: doAppend is true.");
        }
        let startLnR = this.pos.next.startRhs - lines.length;
        lineno.length = lines.length;
        for( i = startLnR; i < startLnR + lines.length; ++i ){
          lineno[i-startLnR] = i;
        }
        this.pos.endLhs -= lines.length;
        this.pos.next.startRhs -= lines.length;
        this.pos.next.startLhs -= lines.length;
        const selector = '.difflnr > pre';
        td = tr.querySelector(selector);
        const lineNoTxt = lineno.join('\n')+'\n';
        lineno.length = 0;
        td.innerHTML = lineNoTxt + td.innerHTML;
        if(this.pos.endLhs<1
           || lines.length < (urlParam.to - urlParam.from)){
          /* No more data. */
          this.destroy();
        }else{
          this.maybeReplaceButtons();
          this.updatePosDebug();
        }
        return this;
      }else{
        throw new Error("Unexpected 'fetchType' value.");
      }
    },

    /**
       Sets this widget's message to the given text. If the message
       represents an error, the first argument must be truthy, else it
       must be falsy. Returns this object.
    */
    msg: function(isError,txt){
      if(txt){
        if(isError) D.addClass(this.e.msgWidget, 'error');
        else D.removeClass(this.e.msgWidget, 'error');
        D.append(
          D.removeClass(D.clearElement(this.e.msgWidget), 'hidden'),
          txt);
      }else{
        D.addClass(D.clearElement(this.e.msgWidget), 'hidden');
      }
      return this;
    },

    /**
       Fetches and inserts a line chunk. fetchType is:

       this.FetchType.NextUp = upwards from next chunk (this.pos.next)

       this.FetchType.FillGap = the whole gap between this.pos.prev
       and this.pos.next, or the whole gap before/after the
       initial/final chunk in the diff.

       this.FetchType.PrevDown = downwards from the previous chunk
       (this.pos.prev)

       Those values are set at the time this object is initialized but
       one instance of this class may have 2 buttons, one each for
       fetchTypes NextUp and PrevDown.

       This is an async operation. While it is in transit, any calls
       to this function will have no effect except (possibly) to emit
       a warning. Returns this object.
    */
    fetchChunk: function(fetchType){
      if( !this.$fetchQueue ) return this;  // HACKHACK: are we destroyed?
      if( fetchType==this.FetchType.ProcessQueue ){
        this.$fetchQueue.shift();
        if( this.$fetchQueue.length==0 ) return this;
        //console.log('fetchChunk: processing queue ...');
      }
      else{
        this.$fetchQueue.push(fetchType);
        if( this.$fetchQueue.length!=1 ) return this;
        //console.log('fetchChunk: processing user input ...');
      }
      fetchType = this.$fetchQueue[0];
      if( fetchType==this.FetchType.ProcessQueue ){
        /* Unexpected! Clear queue so recovery (manual restart) is possible. */
        this.$fetchQueue.length = 0;
        return this;
      }
      /* Forewarning, this is a bit confusing: when fetching the
         previous lines, we're doing so on behalf of the *next* diff
         chunk (this.pos.next), and vice versa. */
      if(fetchType===this.FetchType.NextUp && !this.pos.next
        || fetchType===this.FetchType.PrevDown && !this.pos.prev){
        console.error("Attempt to fetch diff lines but don't have any.");
        return this;
      }
      this.msg(false,"Fetching diff chunk...");
      const self = this;
      const fOpt = {
        urlParams:{
          name: this.fileHash, from: 0, to: 0
        },
        aftersend: ()=>this.msg(false),
        onload: function(list){
          self.injectResponse(fetchType,up,list);
          if( !self.$fetchQueue || self.$fetchQueue.length==0 ) return;
          /* Keep queue length > 0, or clicks stalled during (unusually lengthy)
             injectResponse() may sneak in as soon as setTimeout() allows, find
             an empty queue, and therefore start over with queue processing. */
          self.$fetchQueue[0] = self.FetchType.ProcessQueue;
          setTimeout(self.fetchChunk.bind(self,self.FetchType.ProcessQueue));
        }
      };
      const up = fOpt.urlParams;
      if(fetchType===this.FetchType.FillGap){
        /* Easiest case: filling a whole gap. */
        up.from = this.pos.startLhs;
        up.to = this.pos.endLhs;
      }else if(this.FetchType.PrevDown===fetchType){
        /* Append to previous TR. */
        if(!this.pos.prev){
          console.error("Attempt to fetch next diff lines but don't have any.");
          return this;
        }
        up.from = this.pos.prev.endLhs + 1;
        up.to = up.from +
          Diff.config.chunkLoadLines - 1/*b/c request range is inclusive*/;
        if( this.pos.next && this.pos.next.startLhs <= up.to ){
          up.to = this.pos.next.startLhs - 1;
          fetchType = this.FetchType.FillGap;
        }
      }else{
        /* Prepend to next TR */
        if(!this.pos.next){
          console.error("Attempt to fetch previous diff lines but don't have any.");
          return this;
        }
        up.to = this.pos.next.startLhs - 1;
        up.from = Math.max(1, up.to - Diff.config.chunkLoadLines + 1);
        if( this.pos.prev && this.pos.prev.endLhs >= up.from ){
          up.from = this.pos.prev.endLhs + 1;
          fetchType = this.FetchType.FillGap;
        }
      }
      //console.debug("fetchChunk(",fetchType,")",up);
      fOpt.onerror = function(err){
        if(self.e/*guard against a late-stage onerror() call*/){
          self.msg(true,err.message);
          self.$fetchQueue.length = 0;
        }else{
          Diff.config.chunkFetch.onerror.call(this,err);
        }
      };
      Diff.fetchArtifactChunk(fOpt);
      return this;
    }
  };

  /**
     Adds context-loading buttons to one or more tables. The argument
     may be a forEach-capable list of diff table elements, a query
     selector string matching 0 or more diff tables, or falsy, in
     which case all relevant diff tables are set up. It tags each
     table it processes to that it will not be processed multiple
     times by subsequent calls to this function.

     Note that this only works for diffs which have been marked up
     with certain state, namely table.dataset.lefthash and TR
     entries which hold state related to browsing context.
  */
  Diff.setupDiffContextLoad = function(tables){
    if('string'===typeof tables){
      tables = document.querySelectorAll(tables);
    }else if(!tables){
      tables = document.querySelectorAll('table.diff[data-lefthash]:not(.diffskipped)');
    }
    /* Potential performance-related TODO: instead of installing all
       of these at once, install them as the corresponding TR is
       scrolled into view. */
    tables.forEach(function(table){
      if(table.classList.contains('diffskipped') || !table.dataset.lefthash) return;
      D.addClass(table, 'diffskipped'/*avoid processing these more than once */);
      table.querySelectorAll('tr.diffskip[data-startln]').forEach(function(tr){
        new ChunkLoadControls(D.addClass(tr, 'jchunk'));
      });
    });
    return F;
  };
  Diff.setupDiffContextLoad();
});
/*
** For a side-by-side diff, ensure that horizontal scrolling of either
** side of the diff is synchronized with the other side.
*/
window.fossil.onPageLoad(function(){
  const F = window.fossil, D = F.dom, Diff = F.diff;

  /* Look for a parent element to hold the sbs-sync-scroll toggle
     checkbox.  This differs per page. If we don't find one, simply
     elide that toggle and use whatever preference the user last
     specified (defaulting to on). */
  let cbSync /* scroll-sync checkbox */;
  let eToggleParent = /* element to put the sync-scroll checkbox in */
      document.querySelector('table.diff.splitdiff')
      ? window.fossil.page.diffControlContainer
      : undefined;
  const keySbsScroll = 'sync-diff-scroll' /* F.storage key for persistent user preference */;
  if( eToggleParent ){
    /* Add a checkbox to toggle sbs scroll sync. Remember that in
       order to be UI-consistent in the /vdiff page we have to ensure
       that the checkbox is to the LEFT of of its label. We store the
       sync-scroll preference in F.storage (not a cookie) so that it
       persists across page loads and different apps. */
    cbSync = D.checkbox(keySbsScroll, F.storage.getBool(keySbsScroll,true));
    D.append(eToggleParent, D.append(
      D.addClass(D.create('span'), 'input-with-label'),
      D.append(D.create('label'),
               cbSync, "Scroll Sync")
    ));
    cbSync.addEventListener('change', function(e){
      F.storage.set(keySbsScroll, e.target.checked);
    });
  }
  const useSync = cbSync ? ()=>cbSync.checked : ()=>F.storage.getBool(keySbsScroll,true);

  /* Now set up the events to enable syncronized scrolling... */
  const scrollLeft = function(event){
    const table = this.parentElement/*TD*/.parentElement/*TR*/.
      parentElement/*TBODY*/.parentElement/*TABLE*/;
    if( useSync() ){
      table.$txtPres.forEach((e)=>(e===this) ? 1 : (e.scrollLeft = this.scrollLeft));
    }
    return false;
  };
  const SCROLL_LEN = 64/* pixels to scroll for keyboard events */;
  const keycodes = Object.assign(Object.create(null),{
    37/*cursor left*/: -SCROLL_LEN, 39/*cursor right*/: SCROLL_LEN
  });
  /** keydown handler for a diff element */
  const handleDiffKeydown = function(e){
    const len = keycodes[e.keyCode];
    if( !len ) return false;
    if( useSync() ){
      this.$txtPres[0].scrollLeft += len;
    }else if( this.$preCurrent ){
      this.$preCurrent.scrollLeft += len;
    }
    return false;
  };
  /**
     Sets up synchronized scrolling of table.splitdiff element
     `diff`. If passed no argument, it scans the dom for elements to
     initialize. The second argument is for this function's own
     internal use.

     It's okay (but wasteful) to pass the same element to this
     function multiple times: it will only be set up for sync
     scrolling the first time it's passed to this function.

     Note that this setup is ignorant of the cbSync toggle: the toggle
     is checked when scrolling, not when initializing the sync-scroll
     capability.
  */
  const initTableDiff = function f(diff, unifiedDiffs){
    if(!diff){
      let i, diffs;
      diffs = document.querySelectorAll('table.splitdiff');
      for(i=0; i<diffs.length; ++i){
        f.call(this, diffs[i], false);
      }
      diffs = document.querySelectorAll('table.udiff');
      for(i=0; i<diffs.length; ++i){
        f.call(this, diffs[i], true);
      }
      return this;
    }
    diff.$txtCols = diff.querySelectorAll('td.difftxt');
    diff.$txtPres = diff.querySelectorAll('td.difftxt pre');
    if(!unifiedDiffs){
      diff.$txtPres.forEach(function(e){
        if(!e.classList.contains('scroller')){
          D.addClass(e, 'scroller');
          e.addEventListener('scroll', scrollLeft, false);
        }
      });
      diff.tabIndex = 0;
      if(!diff.classList.contains('scroller')){
        /* Keyboard-based scrolling requires special-case handling to
           ensure that we scroll the proper side of the diff when sync
           is off. */
        D.addClass(diff, 'scroller');
        diff.addEventListener('keydown', handleDiffKeydown, false);
        const handleLRClick = function(ev){
          diff.$preCurrent = this /* NOT ev.target, which is probably a child element */;
        };
        diff.$txtPres.forEach((e)=>e.addEventListener('click', handleLRClick, false));
      }
    }
    return this;
  }
  window.fossil.page.tweakSbsDiffs = function(){
    document.querySelectorAll('table.splitdiff').forEach((e)=>initTableDiff(e));
  };
  initTableDiff();
}, false);
