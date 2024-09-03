(function(F/*the fossil object*/){
  "use strict";
  /**
     Client-side implementation of the /filepage app. Requires that
     the fossil JS bootstrapping is complete and that several fossil
     JS APIs have been installed: fossil.fetch, fossil.dom,
     fossil.tabs, fossil.storage, fossil.confirmer.

     Custom events which can be listened for via
     fossil.page.addEventListener():

     - Event 'fileedit-file-loaded': passes on information when it
     loads a file (whether from the network or its internal local-edit
     cache), in the form of an "finfo" object:

     {
       filename: string,
       checkin: UUID string,
       branch: branch name of UUID,
       isExe: bool, true only for executable files
       mimetype: mimetype string, as determined by the fossil server.
     }

     The internal docs and code frequently use the term "finfo", and such
     references refer to an object with that form.

     The fossil.page.fileContent() method gets or sets the current file
     content for the page.

     - Event 'fileedit-committed': is fired when a commit completes,
     passing on the same info as fileedit-file-loaded.

     - Event 'fileedit-content-replaced': when the editor's content is
     replaced, as opposed to it being edited via user
     interaction. This normally happens via selecting a file to
     load. The event detail is the fossil.page object, not the current
     file content.

     - Event 'fileedit-preview-updated': when the preview is refreshed
     from the server, this event passes on information about the preview
     change in the form of an object:

     {
     element: the DOM element which contains the content preview.

     mimetype: the fossil-reported content mimetype.

     previewMode: a string describing the preview mode: see
       the fossil.page.previewModes map for the values. This can
       be used to determine whether, e.g., the content is suitable
       for applying a 3rd-party code highlighting API to.
     }

     Here's an example which can be used with the highlightjs code
     highlighter to update the highlighting when the preview is
     refreshed in "wiki" mode (which includes fossil-native wiki and
     markdown):

     fossil.page.addEventListener(
       'fileedit-preview-updated',
       (ev)=>{
         if(ev.detail.previewMode==='wiki'){
           ev.detail.element.querySelectorAll(
             'code[class^=language-]'
           ).forEach((e)=>hljs.highlightBlock(e));
         }
       }
     );
  */
  const E = (s)=>document.querySelector(s),
        D = F.dom,
        P = F.page;

  P.config = {
    defaultMaxStashSize: 7,
    /**
       See notes for this setting in fossil.page.wikiedit.js. Both
       /wikiedit and /fileedit share this persistent config option
       under the same storage key.
    */
    shiftEnterPreview: F.storage.getBool('edit-shift-enter-preview', true)
  };

  /**
     $stash is an internal-use-only object for managing "stashed"
     local edits, to help avoid that users accidentally lose content
     by switching tabs or following links or some such. The basic
     theory of operation is...

     All "stashed" state is stored using fossil.storage.

     - When the current file content is modified by the user, the
       current stathe of the current P.finfo and its the content
       is stashed. For the built-in editor widget, "changes" is
       notified via a 'change' event.  For a client-side custom
       widget, the client needs to call P.stashContentChange() when
       their widget triggers the equivalent of a 'change' event.

     - For certain non-content updates (as of this writing, only the
       is-executable checkbox), only the P.finfo stash entry is
       updated, not the content (unless the content has not yet been
       stashed, in which case it is also stashed so that the stash
       always has matching pairs of finfo/content).

     - When saving, the stashed entry for the previous version is removed
       from the stash.

     - When "loading", we use any stashed state for the given
       checkin/file combination. When forcing a re-load of content,
       any stashed entry for that combination is removed from the
       stash.

     - Every time P.stashContentChange() updates the stash, it is
       pruned to $stash.prune.defaultMaxCount most-recently-updated
       entries.

     - This API often refers to "finfo objects." Those are objects
       with a minimum of {checkin,filename} properties (which must be
       valid), and a combination of those two properties is used as
       basis for the stash keys for any given checkin/filename
       combination.

     The structure of the stash is a bit convoluted for efficiency's
     sake: we store a map of file info (finfo) objects separately from
     those files' contents because otherwise we would be required to
     JSONize/de-JSONize the file content when stashing/restoring it,
     and that would be horribly inefficient (meaning "battery-consuming"
     on mobile devices).
  */
  const $stash = {
    keys: {
      index: F.page.name+'.index'
    },
    /**
       index: {
       "CHECKIN_HASH:FILENAME": {file info w/o content}
       ...
       }

       In F.storage we...

       - Store this.index under the key this.keys.index.

       - Store each file's content under the key
       (P.name+'/CHECKIN_HASH:FILENAME'). These are stored separately
       from the index entries to avoid having to JSONize/de-JSONize
       the content. The assumption/hope is that the browser can store
       those records "directly," without any intermediary
       encoding/decoding going on.
    */
    indexKey: function(finfo){return finfo.checkin+':'+finfo.filename},
    /** Returns the key for storing content for the given key suffix,
        by prepending P.name to suffix. */
    contentKey: function(suffix){return P.name+'/'+suffix},
    /** Returns the index object, fetching it from the stash or creating
        it anew on the first call. */
    getIndex: function(){
      if(!this.index){
        this.index = F.storage.getJSON(
          this.keys.index, {}
        );
      }
      return this.index;
    },
    _fireStashEvent: function(){
      if(this._disableNextEvent) delete this._disableNextEvent;
      else F.page.dispatchEvent('fileedit-stash-updated', this);
    },
    /**
       Returns the stashed version, if any, for the given finfo object.
    */
    getFinfo: function(finfo){
      const ndx = this.getIndex();
      return ndx[this.indexKey(finfo)];
    },
    /** Serializes this object's index to F.storage. Returns this. */
    storeIndex: function(){
      if(this.index) F.storage.setJSON(this.keys.index,this.index);
      return this;
    },
    /** Updates the stash record for the given finfo
        and (optionally) content. If passed 1 arg, only
        the finfo stash is updated, else both the finfo
        and its contents are (re-)stashed. Returns this.
    */
    updateFile: function(finfo,content){
      const ndx = this.getIndex(),
            key = this.indexKey(finfo),
            old = ndx[key];
      const record = old || (ndx[key]={
        checkin: finfo.checkin,
        filename: finfo.filename,
        mimetype: finfo.mimetype
      });
      record.isExe = !!finfo.isExe;
      record.stashTime = new Date().getTime();
      if(!record.branch) record.branch=finfo.branch;
      this.storeIndex();
      if(arguments.length>1){
        F.storage.set(this.contentKey(key), content);
      }
      this._fireStashEvent();
      return this;
    },
    /**
       Returns the stashed content, if any, for the given finfo
       object.
    */       
    stashedContent: function(finfo){
      return F.storage.get(this.contentKey(this.indexKey(finfo)));
    },
    /** Returns true if we have stashed content for the given finfo
        record. */
    hasStashedContent: function(finfo){
      return F.storage.contains(this.contentKey(this.indexKey(finfo)));
    },
    /** Unstashes the given finfo record and its content.
        Returns this. */
    unstash: function(finfo){
      const ndx = this.getIndex(),
            key = this.indexKey(finfo);
      delete finfo.stashTime;
      delete ndx[key];
      F.storage.remove(this.contentKey(key));
      this.storeIndex();
      this._fireStashEvent();
      return this;
    },
    /**
       Clears all $stash entries from F.storage. Returns this.
     */
    clear: function(){
      const ndx = this.getIndex(),
            self = this;
      let count = 0;
      Object.keys(ndx).forEach(function(k){
        ++count;
        const e = ndx[k];
        delete ndx[k];
        F.storage.remove(self.contentKey(k));
      });
      F.storage.remove(this.keys.index);
      delete this.index;
      if(count) this._fireStashEvent();
      return this;
    },
    /**
       Removes all but the maxCount most-recently-updated stash
       entries, where maxCount defaults to this.prune.defaultMaxCount.
    */
    prune: function f(maxCount){
      const ndx = this.getIndex();
      const li = [];
      if(!maxCount || maxCount<0) maxCount = f.defaultMaxCount;
      Object.keys(ndx).forEach((k)=>li.push(ndx[k]));
      li.sort((l,r)=>l.stashTime - r.stashTime);
      let n = 0;
      while(li.length>maxCount){
        ++n;
        const e = li.shift();
        this._disableNextEvent = true;
        this.unstash(e);
        console.warn("Pruned oldest local file edit entry:",e);
      }
      if(n) this._fireStashEvent();
    }
  };
  $stash.prune.defaultMaxCount = P.config.defaultMaxStashSize;

  /**
     Widget for the checkin/file selection list.
  */
  P.fileSelectWidget = {
    e:{
      container: E('#fileedit-file-selector')
    },
    finfo: {},
    cache: {
      checkins: undefined,
      files:{},
      branchKey: 'fileedit/uuid-branches',
      branchNames: {}
    },
    /**
       Fetches the list of leaf checkins from the server and updates
       the UI with that list.
    */
    loadLeaves: function(){
      D.append(D.clearElement(
        this.e.ciListLabel,
        this.e.selectCi,
        this.e.selectFiles
      ),"Loading leaves...");
      D.disable(this.e.btnLoadFile, this.e.selectFiles, this.e.selectCi); 
      const self = this;
      const onload = function(list){
        D.append(D.clearElement(self.e.ciListLabel),
                 "Open leaves (newest first):");
        self.cache.checkins = list;
        D.clearElement(D.enable(self.e.selectCi));
        let loadThisOne = P.initialFiles/*possibly injected at page-load time*/;
        if(loadThisOne){
          self.cache.files[loadThisOne.checkin] = loadThisOne;
          delete P.initialFiles;
        }
        list.forEach(function(o,n){
          if(!n && !loadThisOne) loadThisOne = o;
          self.cache.branchNames[F.hashDigits(o.checkin,true)] = o.branch;
          D.option(self.e.selectCi, o.checkin,
                   o.timestamp+' ['+o.branch+']: '
                   +F.hashDigits(o.checkin));
        });
        F.storage.setJSON(self.cache.branchKey, self.cache.branchNames);
        if(loadThisOne){
          self.e.selectCi.value = loadThisOne.checkin;
        }
        self.loadFiles(loadThisOne ? loadThisOne.checkin : false);
      };
      if(P.initialLeaves/*injected at page-load time.*/){
        const lv = P.initialLeaves;
        delete P.initialLeaves;
        onload(lv);
      }else{
        F.fetch('fileedit/filelist',{
          urlParams:'leaves',
          responseType: 'json',
          onload: onload
        });
      }
    },
    /**
       Loads the file list for the given checkin UUID. It uses a
       cached copy on subsequent calls for the same UUID. If passed a
       falsy value, it instead clears and disables the file selection
       list.
    */
    loadFiles: function(ciUuid){
      delete this.finfo.filename;
      this.finfo.checkin = ciUuid;
      const selFiles = this.e.selectFiles;
      if(!ciUuid){
        D.clearElement(D.disable(selFiles, this.e.btnLoadFile));
        return this;
      }
      const onload = (response)=>{
        D.clearElement(selFiles);
        D.append(
          D.clearElement(this.e.fileListLabel),
          "Editable files for ",
          D.append(
            D.code(), "[",
            D.a(F.repoUrl('timeline',{
              c: ciUuid
            }), F.hashDigits(ciUuid)),"]"
          ), ":"
        );
        this.cache.files[response.checkin] = response;
        response.editableFiles.forEach(function(fn,n){
          D.option(selFiles, fn);
        });
        if(selFiles.options.length){
          D.enable(selFiles, this.e.btnLoadFile);
        }
      };
      const got = this.cache.files[ciUuid];
      if(got){
        onload(got);
        return this;
      }
      D.disable(selFiles,this.e.btnLoadFile);
      D.clearElement(selFiles);
      D.append(D.clearElement(this.e.fileListLabel),
               "Loading files for "+F.hashDigits(ciUuid)+"...");
      F.fetch('fileedit/filelist',{
        urlParams:{checkin: ciUuid},
        responseType: 'json',
        onload
      });
      return this;
    },

    /**
       If this object has ever loaded the given checkin version via
       loadLeaves(), this returns the branch name associated with that
       version, else returns undefined;
     */
    checkinBranchName: function(uuid){
      return this.cache.branchNames[F.hashDigits(uuid,true)];
    },

    /**
       Initializes the checkin/file selector widget. Must only be
       called once.
    */
    init: function(){
      this.cache.branchNames = F.storage.getJSON(this.cache.branchKey, {});
      const selCi = this.e.selectCi = D.addClass(D.select(), 'flex-grow'),
            selFiles = this.e.selectFiles
            = D.addClass(D.select(), 'file-list'),
            btnLoad = this.e.btnLoadFile =
            D.addClass(D.button("Load file"), "flex-shrink"),
            filesLabel = this.e.fileListLabel =
            D.addClass(D.div(),'flex-shrink','file-list-label'),
            ciLabelWrapper = D.addClass(
              D.div(), 'flex-container','flex-row', 'flex-shrink',
              'stretch', 'child-gap-small'
            ),
            btnReload = D.addClass(
              D.button('Reload'), 'flex-shrink'
            ),
            ciLabel = this.e.ciListLabel =
            D.addClass(D.span(),'flex-shrink','checkin-list-label')
      ;
      D.attr(selCi, 'title',"The list of opened leaves.");
      D.attr(selFiles, 'title',
             "The list of editable files for the selected checkin.");
      D.attr(btnLoad, 'title',
             "Load the selected file into the editor.");
      D.disable(selCi, selFiles, btnLoad);
      D.attr(selFiles, 'size', 12);
      D.append(
        this.e.container,
        ciLabel,
        D.append(ciLabelWrapper,
                 selCi,
                 btnReload),
        filesLabel,
        selFiles,
        /* Use a wrapper for btnLoad so that the button itself does not
          stretch to fill the parent width: */
        D.append(D.addClass(D.div(), 'flex-shrink'), btnLoad)
      );
      if(F.config['fileedit-glob']){
        D.append(
          this.e.container,
          D.append(
            D.span(),
            D.append(D.code(),"fileedit-glob"),
            " config setting = ",
            D.append(D.code(), JSON.stringify(F.config['fileedit-glob']))
          )
        );
      }

      this.loadLeaves();
      selCi.addEventListener(
        'change', (e)=>this.loadFiles(e.target.value), false
      );
      const doLoad = (e)=>{
        this.finfo.filename = selFiles.value;
        if(this.finfo.filename){
          P.loadFile(this.finfo.filename, this.finfo.checkin);
        }
      };
      btnLoad.addEventListener('click', doLoad, false);
      selFiles.addEventListener('dblclick', doLoad, false);
      btnReload.addEventListener(
        'click', (e)=>this.loadLeaves(), false
      );
      delete this.init;
    }
  }/*P.fileSelectWidget*/;

  /**
     Widget for listing and selecting $stash entries.
  */
  P.stashWidget = {
    e:{/*DOM element(s)*/},
    init: function(domInsertPoint/*insert widget BEFORE this element*/){
      const wrapper = D.addClass(
        D.attr(D.div(),'id','fileedit-stash-selector'),
        'input-with-label'
      );
      const sel = this.e.select = D.select();
      const btnClear = this.e.btnClear = D.button("Discard Edits"),
            btnHelp = D.append(
              D.addClass(D.div(), "help-buttonlet"),
              'Locally-edited files. Timestamps are the last local edit time. ',
              'Only the ',P.config.defaultMaxStashSize,' most recent files ',
              'are retained. Saving or reloading a file removes it from this list. ',
              D.append(D.code(),F.storage.storageImplName()),
              ' = ',F.storage.storageHelpDescription()
            );

      D.append(wrapper, "Local edits (",
               D.append(D.code(),
                        F.storage.storageImplName()),
               "):",
               btnHelp, sel, btnClear);
      F.helpButtonlets.setup(btnHelp);
      D.option(D.disable(sel), undefined, "(empty)");
      F.page.addEventListener('fileedit-stash-updated',(e)=>this.updateList(e.detail));
      F.page.addEventListener('fileedit-file-loaded',(e)=>this.updateList($stash, e.detail));
      sel.addEventListener('change',function(e){
        const opt = this.selectedOptions[0];
        if(opt && opt._finfo) P.loadFile(opt._finfo);
      });
      if(F.storage.isTransient()){/*Warn if our storage is particularly transient...*/
        D.append(wrapper, D.append(
          D.addClass(D.span(),'warning'),
          "Warning: persistent storage is not available, "+
            "so uncomitted edits will not survive a page reload."
        ));
      }
      domInsertPoint.parentNode.insertBefore(wrapper, domInsertPoint);
      P.tabs.switchToTab(1/*DOM visibility workaround*/);
      F.confirmer(btnClear, {
        /* must come after insertion into the DOM for the pinSize option to work. */
        pinSize: true,
        confirmText: "DISCARD all local edits?",
        onconfirm: function(e){
          if(P.finfo){
            const stashed = P.getStashedFinfo(P.finfo);
            P.clearStash();
            if(stashed) P.loadFile(/*reload after discarding edits*/);
          }else{
            P.clearStash();
          }
        },
        ticks: F.config.confirmerButtonTicks
      });
      D.addClass(this.e.btnClear,'hidden' /* must not be set until after confirmer is set up!*/);
      $stash._fireStashEvent(/*read the page-load-time stash*/);
      P.tabs.switchToTab(0/*DOM visibility workaround*/);
      delete this.init;
    },
    /**
       Regenerates the edit selection list.
     */
    updateList: function f(stasher,theFinfo){
      if(!f.compare){
        const cmpBase = (l,r)=>l<r ? -1 : (l===r ? 0 : 1);
        f.compare = function(l,r){
          const cmp = cmpBase(l.filename, r.filename);
          return cmp ? cmp : cmpBase(l.checkin, r.checkin);
        };
        f.rxZ = /\.\d+Z$/ /* ms and 'Z' part of date string */;
        const pad=(x)=>(''+x).length>1 ? x : '0'+x;
        f.timestring = function ff(d){
          return [
            d.getFullYear(),'-',pad(d.getMonth()+1/*sigh*/),'-',pad(d.getDate()),
            '@',pad(d.getHours()),':',pad(d.getMinutes())
          ].join('');
        };
      }
      const index = stasher.getIndex(), ilist = [];
      Object.keys(index).forEach((finfo)=>{
        ilist.push(index[finfo]);
      });
      const self = this;
      D.clearElement(this.e.select);
      if(0===ilist.length){
        D.addClass(this.e.btnClear, 'hidden');
        D.option(D.disable(this.e.select),undefined,"No local edits");
        return;
      }
      D.enable(this.e.select);
      D.removeClass(this.e.btnClear, 'hidden');
      D.disable(D.option(this.e.select,0,"Select a local edit..."));
      const currentFinfo = theFinfo || P.finfo || {filename:''};
      ilist.sort(f.compare).forEach(function(finfo,n){
        const key = stasher.indexKey(finfo),
              branch = finfo.branch
              || P.fileSelectWidget.checkinBranchName(finfo.checkin)||'';
        /* Remember that we don't know the branch name for non-leaf versions
           which P.fileSelectWidget() has never seen/cached. */
        const opt = D.option(
          self.e.select, n+1/*value is (almost) irrelevant*/,
          [F.hashDigits(finfo.checkin), ' [',branch||'?branch?','] ',
           f.timestring(new Date(finfo.stashTime)),' ',
           false ? finfo.filename : F.shortenFilename(finfo.filename)
          ].join('')
        );
        opt._finfo = finfo;
        if(0===f.compare(currentFinfo, finfo)){
          D.attr(opt, 'selected', true);
        }
      });
    }
  }/*P.stashWidget*/;

  /**
     Internal workaround to select the current preview mode
     and fire a change event if the value actually changes
     or if forceEvent is truthy.
  */
  P.selectPreviewMode = function(modeValue, forceEvent){
    const s = this.e.selectPreviewMode;
    if(!modeValue) modeValue = s.value;
    else if(s.value != modeValue){
      s.value = modeValue;
      forceEvent = true;
    }
    if(forceEvent){
      // Force UI update
      s.dispatchEvent(new Event('change',{target:s}));
    }
  };

  /**
     Keep track of how many in-flight AJAX requests there are so we
     can disable input elements while any are pending. For
     simplicity's sake we simply disable ALL OF IT while any AJAX is
     pending, rather than disabling operation-specific UI elements,
     which would be a huge maintenance hassle.

     Noting, however, that this global on/off is not *quite*
     pedantically correct. Pedantically speaking. If an element is
     disabled before an XHR starts, this code "should" notice that and
     not include it in the to-re-enable list. That would be annoying
     to do, and becomes impossible to do properly once multiple XHRs
     are in transit and an element is disabled seprately between two
     of those in-transit requests (that would be an unlikely, but
     possible, corner case). As of this writing, the only elements
     which are ever normally programmatically toggled between
     enabled/disabled...

     1) Belong to the file selection list and remain disabled until
     the list of leaves and files are loaded. i.e. they would be
     disabled *anyway* during their own XHR requests.

     2) The stashWidget's SELECT list when no local edits are
     stashed. Curiously, the all-or-nothing re-enabling implemented
     here does not re-enable that particular selection list. That's
     because of timing, though: that widget is "manually" disabled
     when the list is empty, and that list is normally emptied in
     conjunction with an XHR request.
  */
  const ajaxState = {
    count: 0 /* in-flight F.fetch() requests */,
    toDisable: undefined /* elements to disable during ajax activity */
  };
  F.fetch.beforesend = function f(){
    if(!ajaxState.toDisable){
      ajaxState.toDisable = document.querySelectorAll(
        'button, input, select, textarea'
      );
    }
    if(1===++ajaxState.count){
      D.addClass(document.body, 'waiting');
      D.disable(ajaxState.toDisable);
    }
  };
  F.fetch.aftersend = function(){
    if(0===--ajaxState.count){
      D.removeClass(document.body, 'waiting');
      D.enable(ajaxState.toDisable);
    }
  };

  F.onPageLoad(function() {
    P.base = {tag: E('base')};
    P.base.originalHref = P.base.tag.href;
    P.tabs = new F.TabManager('#fileedit-tabs');
    P.e = { /* various DOM elements we work with... */
      taEditor: E('#fileedit-content-editor'),
      taCommentSmall: E('#fileedit-comment'),
      taCommentBig: E('#fileedit-comment-big'),
      taComment: undefined/*gets set to one of taComment{Big,Small}*/,
      ajaxContentTarget: E('#ajax-target'),
      btnCommit: E("#fileedit-btn-commit"),
      btnReload: E("#fileedit-tab-content button.fileedit-content-reload"),
      selectPreviewMode: E('#select-preview-mode select'),
      selectHtmlEmsWrap: E('#select-preview-html-ems'),
      selectEolWrap:  E('#select-eol-style'),
      selectEol:  E('#select-eol-style select[name=eol]'),
      selectFontSizeWrap: E('#select-font-size'),
      selectDiffWS:  E('select[name=diff_ws]'),
      cbLineNumbersWrap: E('#cb-line-numbers'),
      cbAutoPreview: E('#cb-preview-autorefresh'),
      previewTarget: E('#fileedit-tab-preview-wrapper'),
      manifestTarget: E('#fileedit-manifest'),
      diffTarget: E('#fileedit-tab-diff-wrapper'),
      cbIsExe: E('input[type=checkbox][name=exec_bit]'),
      cbManifest: E('input[type=checkbox][name=include_manifest]'),
      editStatus: E('#fileedit-edit-status'),
      tabs:{
        content: E('#fileedit-tab-content'),
        preview: E('#fileedit-tab-preview'),
        diff: E('#fileedit-tab-diff'),
        commit: E('#fileedit-tab-commit'),
        fileSelect: E('#fileedit-tab-fileselect')
      }
    };
    /* Figure out which comment editor to show by default and
       hide the other one. By default we take the one which does
       not have the 'hidden' CSS class. If neither do, we default
       to single-line mode. */
    if(D.hasClass(P.e.taCommentSmall, 'hidden')){
      P.e.taComment = P.e.taCommentBig;
    }else if(D.hasClass(P.e.taCommentBig,'hidden')){
      P.e.taComment = P.e.taCommentSmall;
    }else{
      P.e.taComment = P.e.taCommentSmall;
      D.addClass(P.e.taCommentBig, 'hidden');
    }
    D.removeClass(P.e.taComment, 'hidden');
    P.tabs.addCustomWidget( E('#fossil-status-bar') ).addCustomWidget(P.e.editStatus);
    let currentTab/*used for ctrl-enter switch between editor and preview*/;
    P.tabs.addEventListener(
      /* Set up auto-refresh of the preview tab... */
      'before-switch-to', function(ev){
        currentTab = ev.detail;
        if(ev.detail===P.e.tabs.preview){
          P.baseHrefForFile();
          if(P.previewNeedsUpdate && P.e.cbAutoPreview.checked) P.preview();
        }else if(ev.detail===P.e.tabs.diff){
          /* Work around a weird bug where the page gets wider than
             the window when the diff tab is NOT in view and the
             current SBS diff widget is wider than the window. When
             the diff IS in view then CSS overflow magically reduces
             the page size again. Weird. Maybe FF-specific. Note that
             this weirdness happens even though P.e.diffTarget's parent
             is hidden (and therefore P.e.diffTarget is also hidden).
          */
          D.removeClass(P.e.diffTarget, 'hidden');
        }
      }
    );
    P.tabs.addEventListener(
      /* Set up auto-refresh of the preview tab... */
      'before-switch-from', function(ev){
        if(ev.detail===P.e.tabs.preview){
          P.baseHrefRestore();
        }else if(ev.detail===P.e.tabs.diff){
          /* See notes in the before-switch-to handler. */
          D.addClass(P.e.diffTarget, 'hidden');
        }
      }
    );
    ////////////////////////////////////////////////////////////
    // Trigger preview on Ctrl-Enter. This only works on the built-in
    // editor widget, not a client-provided one.
    P.e.taEditor.addEventListener('keydown',function(ev){
      if(P.config.shiftEnterPreview && ev.shiftKey && 13===ev.keyCode){
        ev.preventDefault();
        ev.stopPropagation();
        P.e.taEditor.blur(/*force change event, if needed*/);
        P.tabs.switchToTab(P.e.tabs.preview);
        if(!P.e.cbAutoPreview.checked){/* If NOT in auto-preview mode, trigger an update. */
          P.preview();
        }
        return false;
      }
    }, false);
    // If we're in the preview tab, have ctrl-enter switch back to the editor.
    document.body.addEventListener('keydown',function(ev){
      if(ev.shiftKey && 13 === ev.keyCode){
        if(currentTab === P.e.tabs.preview){
          ev.preventDefault();
          ev.stopPropagation();
          P.tabs.switchToTab(P.e.tabs.content);
          P.e.taEditor.focus(/*doesn't work for client-supplied editor widget!
                              And it's slow as molasses for long docs, as focus()
                              forces a document reflow.*/);
          return false;
        }
      }
    }, true);

    F.connectPagePreviewers(
      P.e.tabs.preview.querySelector(
        '#btn-preview-refresh'
      )
    );

    const diffButtons = E('#fileedit-tab-diff-buttons');
    diffButtons.querySelector('button.sbs').addEventListener(
      "click",(e)=>P.diff(true), false
    );
    diffButtons.querySelector('button.unified').addEventListener(
      "click",(e)=>P.diff(false), false
    );
    P.e.btnCommit.addEventListener(
      "click",(e)=>P.commit(), false
    );
    P.tabs.switchToTab(1/*DOM visibility workaround*/);
    F.confirmer(P.e.btnReload, {
      pinSize: true,
      confirmText: "Really reload, losing edits?",
      onconfirm: (e)=>P.unstashContent().loadFile(),
      ticks: F.config.confirmerButtonTicks
    });
    E('#comment-toggle').addEventListener(
      "click",(e)=>P.toggleCommentMode(), false
    );

    P.e.taEditor.addEventListener('change', ()=>P.notifyOfChange(), false);
    P.e.cbIsExe.addEventListener(
      'change', ()=>P.stashContentChange(true), false
    );
    
    /**
       Cosmetic: jump through some hoops to enable/disable
       certain preview options depending on the current
       preview mode...
    */
    P.e.selectPreviewMode.addEventListener(
      "change", function(e){
        const mode = e.target.value,
              name = P.previewModes[mode],
              hide = [], unhide = [];
        P.previewModes.current = name;
        if('guess'===name){
          unhide.push(P.e.cbLineNumbersWrap,
                      P.e.selectHtmlEmsWrap);
        }else{
          if('text'===name) unhide.push(P.e.cbLineNumbersWrap);
          else hide.push(P.e.cbLineNumbersWrap);
          if('htmlIframe'===name) unhide.push(P.e.selectHtmlEmsWrap);
          else hide.push(P.e.selectHtmlEmsWrap);
        }
        hide.forEach((e)=>e.classList.add('hidden'));
        unhide.forEach((e)=>e.classList.remove('hidden'));
      }, false
    );
    P.selectPreviewMode(false, true);
    const selectFontSize = E('select[name=editor_font_size]');
    if(selectFontSize){
      selectFontSize.addEventListener(
        "change",function(e){
          const ed = P.e.taEditor;
          ed.className = ed.className.replace(
              /\bfont-size-\d+/g, '' );
          ed.classList.add('font-size-'+e.target.value);
        }, false
      );
      selectFontSize.dispatchEvent(
        // Force UI update
        new Event('change',{target:selectFontSize})
      );
    }

    P.addEventListener(
      // Clear certain views when new content is loaded/set
      'fileedit-content-replaced',
      ()=>{
        P.previewNeedsUpdate = true;
        D.clearElement(P.e.diffTarget, P.e.previewTarget, P.e.manifestTarget);
      }
    );
    P.addEventListener(
      // Clear certain views after a non-dry-run commit
      'fileedit-committed',
      (e)=>{
        if(!e.detail.dryRun){
          D.clearElement(P.e.diffTarget, P.e.previewTarget);
        }
      }
    );

    P.fileSelectWidget.init();
    P.stashWidget.init(
      P.e.tabs.content.lastElementChild
    );

    const cbEditPreview = E('#edit-shift-enter-preview');
    cbEditPreview.addEventListener('change', function(e){
      F.storage.set('edit-shift-enter-preview',
                    P.config.shiftEnterPreview = e.target.checked);
    }, false);
    cbEditPreview.checked = P.config.shiftEnterPreview;
  }/*F.onPageLoad()*/);

  /**
     Getter (if called with no args) or setter (if passed an arg) for
     the current file content.

     The setter form sets the content, dispatches a
     'fileedit-content-replaced' event, and returns this object.
  */
  P.fileContent = function f(){
    if(0===arguments.length){
      return f.get();
    }else{
      f.set(arguments[0] || '');
      this.dispatchEvent('fileedit-content-replaced', this);
      return this;
    }
  };
  /* Default get/set impls for file content */
  P.fileContent.get = function(){return P.e.taEditor.value};
  P.fileContent.set = function(content){P.e.taEditor.value = content};

  /**
     For use when installing a custom editor widget. Pass it the
     getter and setter callbacks to fetch resp. set the content of the
     custom widget. They will be triggered via
     P.fileContent(). Returns this object.
  */
  P.setContentMethods = function(getter, setter){
    this.fileContent.get = getter;
    this.fileContent.set = setter;
    return this;
  };

  /**
     Alerts the editor app that a "change" has happened in the editor.
     When connecting 3rd-party editor widgets to this app, it is (or
     may be) necessary to call this for any "change" events the widget
     emits.  Whether or not "change" means that there were "really"
     edits is irrelevant.

     This function may perform an arbitrary amount of work, so it
     should not be called for every keypress within the editor
     widget. Calling it for "blur" events is generally sufficient, and
     calling it for each Enter keypress is generally reasonable but
     also computationally costly.
  */
  P.notifyOfChange = function(){
    P.stashContentChange();
  };

  /**
     Removes the default editor widget (and any dependent elements)
     from the DOM, adds the given element in its place, removes this
     method from this object, and returns this object.
  */
  P.replaceEditorElement = function(newEditor){
    P.e.taEditor.parentNode.insertBefore(newEditor, P.e.taEditor);
    P.e.taEditor.remove();
    P.e.selectFontSizeWrap.remove();
    delete this.replaceEditorElement;
    return P;
  };

  /**
     If either of...

     - P.previewModes.current==='wiki'

     - P.previewModes.current==='guess' AND the currently-loaded file
     has a mimetype of "text/x-fossil-wiki" or "text/x-markdown".

     ... then this function updates the document's base.href to a
     repo-relative /doc/{{this.finfo.checkin}}/{{directory part of
     this.finfo.filename}}/

     If neither of those conditions applies, this is a no-op.
  */
  P.baseHrefForFile = function f(){
    const fn = this.finfo ? this.finfo.filename : undefined;
    if(!fn) return this;
    if(!f.wikiMimeTypes){
      f.wikiMimeTypes = ["text/x-fossil-wiki", "text/x-markdown"];
    }
    if('wiki'===P.previewModes.current
       || ('guess'===P.previewModes.current
           && f.wikiMimeTypes.indexOf(this.finfo.mimetype)>=0)){
      const a = fn.split('/');
      a.pop();
      this.base.tag.href = F.repoUrl(
        'doc/'+F.hashDigits(this.finfo.checkin)
          +'/'+(a.length ? a.join('/')+'/' : '')
      );
    }
    return this;
  };

  /**
     Sets the document's base.href value to its page-load-time
     setting.
  */
  P.baseHrefRestore = function(){
    P.base.tag.href = P.base.originalHref;
  };

  /**
     Toggles between single- and multi-line comment
     mode.
  */
  P.toggleCommentMode = function(){
    var s, h, c = this.e.taComment.value;
    if(this.e.taComment === this.e.taCommentSmall){
      s = this.e.taCommentBig;
      h = this.e.taCommentSmall;
    }else{
      s = this.e.taCommentSmall;
      h = this.e.taCommentBig;
      /*
        Doing (input[type=text].value = textarea.value) unfortunately
        strips all newlines. To compensate we'll replace each EOL with
        a space. Not ideal. If we were to instead escape them as \n,
        and do the reverse when toggling again, then they would get
        committed as escaped newlines if the user did not first switch
        back to multi-line mode. We cannot blindly unescape the
        newlines, in the off chance that the user actually enters \n
        in the comment.
      */
      c = c.replace(/\r?\n/g,' ');
    }
    s.value = c;
    this.e.taComment = s;
    D.addClass(h, 'hidden');
    D.removeClass(s, 'hidden');
  };

  /**
     Returns true if fossil.page.finfo is set, indicating that a file
     has been loaded, else it reports an error and returns false.

     If passed a truthy value any error message about not having
     a file loaded is suppressed.
  */
  const affirmHasFile = function(quiet){
    if(!P.finfo){
      if(!quiet) F.error("No file is loaded.");
    }
    return !!P.finfo;
  };

  /**
     updateVersion() updates the filename and version in various UI
     elements...

     Returns this object.
  */
  P.updateVersion = function f(file,rev){
    if(!f.eLinks){
      f.eName = P.e.editStatus.querySelector('span.name');
      f.eLinks = P.e.editStatus.querySelector('span.links');
    }
    if(1===arguments.length){/*assume object*/
      this.finfo = arguments[0];
      file = this.finfo.filename;
      rev = this.finfo.checkin;
    }else if(0===arguments.length){
      if(affirmHasFile()){
        file = this.finfo.filename;
        rev = this.finfo.checkin;
      }
    }else{
      this.finfo = {filename:file,checkin:rev};
    }
    const fi = this.finfo;
    D.clearElement(f.eName, f.eLinks);
    if(!fi){
      D.append(f.eName, '(no file loaded)');
      return this;
    }
    const rHuman = F.hashDigits(rev),
          rUrl = F.hashDigits(rev,true);

    //TODO? port over is-edited marker from /wikiedit
    //var marker = getEditMarker(wi, false);
    D.append(f.eName/*,marker*/,D.a(F.repoUrl('finfo',{name:file, m:rUrl}), file));

    D.append(
      f.eLinks,
      D.append(D.span(), fi.mimetype||'?mimetype?'),
      D.a(F.repoUrl('info/'+rUrl), rHuman),
      D.a(F.repoUrl('timeline',{m:rUrl}), "timeline"),
      D.a(F.repoUrl('annotate',{filename:file, checkin:rUrl}),'annotate'),
      D.a(F.repoUrl('blame',{filename:file, checkin:rUrl}),'blame')
    );
    const purlArgs = F.encodeUrlArgs({
      filename: this.finfo.filename,
      checkin: rUrl
    },false,true);
    const purl = F.repoUrl('fileedit',purlArgs);
    D.append( f.eLinks, D.a(purl,"editor permalink") );
    this.setPageTitle("Edit: "+fi.filename);
    return this;
  };

  /**
     loadFile() loads (file,checkinVersion) and updates the relevant
     UI elements to reflect the loaded state. If passed no arguments
     then it re-uses the values from the currently-loaded file, reloading
     it (emitting an error message if no file is loaded).

     Returns this object, noting that the load is async. After loading
     it triggers a 'fileedit-file-loaded' event, passing it
     this.finfo.

     If a locally-edited copy of the given file/rev is found, that
     copy is used instead of one fetched from the server, but it is
     still treated as a load event.

     Alternate call forms:

     - no arguments: re-loads from this.finfo.

     - 1 argument: assumed to be an finfo-style object. Must have at
     least {filename, checkin} properties, but need not have other
     finfo state.
  */
  P.loadFile = function(file,rev){
    if(0===arguments.length){
      /* Reload from this.finfo */
      if(!affirmHasFile()) return this;
      file = this.finfo.filename;
      rev = this.finfo.checkin;
    }else if(1===arguments.length){
      /* Assume finfo-like object */
      const arg = arguments[0];
      file = arg.filename;
      rev = arg.checkin;
    }
    const self = this;
    const onload = (r,headers)=>{
      delete self.finfo;
      self.updateVersion({
        filename: file,
        checkin: rev,
        branch: headers['x-fileedit-checkin-branch'],
        isExe: ('x'===headers['x-fileedit-file-perm']),
        mimetype: headers['content-type'].split(';').shift()
      });
      self.tabs.switchToTab(self.e.tabs.content);
      self.e.cbIsExe.checked = self.finfo.isExe;
      self.fileContent(r);
      P.previewNeedsUpdate = true;
      self.dispatchEvent('fileedit-file-loaded', self.finfo);
    };
    const semiFinfo = {filename: file, checkin: rev};
    const stashFinfo = this.getStashedFinfo(semiFinfo);
    if(stashFinfo){ // fake a response from the stash...
      this.finfo = stashFinfo;
      this.e.cbIsExe.checked = !!stashFinfo.isExe;
      onload(this.contentFromStash()||'',{
        'x-fileedit-file-perm': stashFinfo.isExe ? 'x' : undefined,
        'content-type': stashFinfo.mimetype,
        'x-fileedit-checkin-branch': stashFinfo.branch
      });
      F.message("Fetched from the local-edit storage:",
                F.hashDigits(stashFinfo.checkin),
                stashFinfo.filename);
      return this;
    }
    F.message(
      "Loading content..."
    ).fetch('fileedit/content',{
      urlParams: {
        filename:file,
        checkin:rev
      },
      responseHeaders: [
        'x-fileedit-file-perm',
        'x-fileedit-checkin-branch',
        'content-type'],
      onload:(r,headers)=>{
        onload(r,headers);
        F.message('Loaded content for',
                  F.hashDigits(self.finfo.checkin),
                  self.finfo.filename);
      }
    });
    return this;
  };

  /**
     Fetches the page preview based on the contents and settings of
     this page's input fields, and updates the UI with with the
     preview.

     Returns this object, noting that the operation is async.
  */
  P.preview = function f(switchToTab){
    if(!affirmHasFile()) return this;
    return this._postPreview(this.fileContent(), function(c){
      P._previewTo(c);
      if(switchToTab) self.tabs.switchToTab(self.e.tabs.preview);
    });
  };

    /**
     Callback for use with F.connectPagePreviewers(). Gets passed
     the preview content.
  */
  P._previewTo = function(c){
    const target = this.e.previewTarget;
    D.clearElement(target);
    if('string'===typeof c) D.parseHtml(target,c);
    if(F.pikchr){
      F.pikchr.addSrcView(target.querySelectorAll('svg.pikchr'));
    }
  };

  /**
     Callback for use with F.connectPagePreviewers()
  */
  P._postPreview = function(content,callback){
    if(!affirmHasFile()) return this;
    if(!content){
      callback(content);
      return this;
    }
    const fd = new FormData();
    fd.append('render_mode',this.e.selectPreviewMode.value);
    fd.append('filename',this.finfo.filename);
    fd.append('ln',E('[name=preview_ln]').checked ? 1 : 0);
    fd.append('iframe_height', E('[name=preview_html_ems]').value);
    fd.append('content',content || '');
    F.message(
      "Fetching preview..."
    ).fetch('ajax/preview-text',{
      payload: fd,
      responseHeaders: 'x-ajax-render-mode',
      onload: (r,header)=>{
        P.selectPreviewMode(P.previewModes[header]);
        if('wiki'===header) P.baseHrefForFile();
        else P.baseHrefRestore();
        callback(r);
        F.message('Updated preview.');
        P.previewNeedsUpdate = false;
        P.dispatchEvent('fileedit-preview-updated',{
          previewMode: P.previewModes.current,
          mimetype: P.finfo.mimetype,
          element: P.e.previewTarget
        });
      },
      onerror: (e)=>{
        F.fetch.onerror(e);
        callback("Error fetching preview: "+e);
      }
    });
    return this;
  };

  /**
     Fetches the content diff based on the contents and settings of
     this page's input fields, and updates the UI with the diff view.

     Returns this object, noting that the operation is async.
  */
  P.diff = function f(sbs){
    if(!affirmHasFile()) return this;
    const content = this.fileContent(),
          self = this,
          target = this.e.diffTarget;
    const fd = new FormData();
    fd.append('filename',this.finfo.filename);
    fd.append('checkin', this.finfo.checkin);
    fd.append('sbs', sbs ? 1 : 0);
    fd.append('content',content);
    if(this.e.selectDiffWS) fd.append('ws',this.e.selectDiffWS.value);
    F.message(
      "Fetching diff..."
    ).fetch('fileedit/diff',{
      payload: fd,
      onload: function(c){
        D.parseHtml(D.clearElement(target),[
          "<div>Diff <code>[",
          self.finfo.checkin,
          "]</code> &rarr; Local Edits</div>",
          c||'No changes.'
        ].join(''));
        F.diff.setupDiffContextLoad();
        if(sbs) P.tweakSbsDiffs();
        F.message('Updated diff.');
        self.tabs.switchToTab(self.e.tabs.diff);
      }
    });
    return this;
  };

  /**
     Performs an async commit based on the form contents and updates
     the UI.

     Returns this object.
  */
  P.commit = function f(){
    if(!affirmHasFile()) return this;
    const self = this;
    const content = this.fileContent(),
          target = D.clearElement(P.e.manifestTarget),
          cbDryRun = E('[name=dry_run]'),
          isDryRun = cbDryRun.checked,
          filename = this.finfo.filename;
    if(!f.onload){
      f.onload = function(c){
        const oldFinfo = JSON.parse(JSON.stringify(self.finfo))
        if(c.manifest){
          D.parseHtml(D.clearElement(target), [
            "<h3>Manifest",
            (c.dryRun?" (dry run)":""),
            ": ", F.hashDigits(c.checkin),"</h3>",
            "<pre><code class='fileedit-manifest'>",
            c.manifest.replace(/</g,'&lt;'),
            /* ^^^ replace() necessary or this breaks if the manifest
               comment contains an unclosed HTML tags,
               e.g. <script> */
            "</code></pre>"
          ].join(''));
          delete c.manifest/*so we don't stash this with finfo*/;
        }
        const msg = [
          'Committed',
          c.dryRun ? '(dry run)' : '',
          '[', F.hashDigits(c.checkin) ,'].'
        ];
        if(!c.dryRun){
          self.unstashContent(oldFinfo);
          self.finfo = c;
          self.e.taComment.value = '';
          self.updateVersion();
          self.fileSelectWidget.loadLeaves();
        }
        self.dispatchEvent('fileedit-committed', c);
        F.message.apply(F, msg);
        self.tabs.switchToTab(self.e.tabs.commit);
      };
    }
    const fd = new FormData();
    fd.append('filename',filename);
    fd.append('checkin', this.finfo.checkin);
    fd.append('content',content);
    fd.append('dry_run',isDryRun ? 1 : 0);
    fd.append('eol', this.e.selectEol.value || 0);
    /* Text fields or select lists... */
    fd.append('comment', this.e.taComment.value);
    if(0){
      // Comment mimetype is currently not supported by the UI...
      ['comment_mimetype'
      ].forEach(function(name){
        var e = E('[name='+name+']');
        if(e) fd.append(name,e.value);
      });
    }
    /* Checkboxes: */
    ['allow_fork',
     'allow_older',
     'exec_bit',
     'allow_merge_conflict',
     'include_manifest',
     'prefer_delta'
    ].forEach(function(name){
      var e = E('[name='+name+']');
      if(e){
        fd.append(name, e.checked ? 1 : 0);
      }else{
        console.error("Missing checkbox? name =",name);
      }
    });
    F.message(
      "Checking in..."
    ).fetch('fileedit/commit',{
      payload: fd,
      responseType: 'json',
      onload: f.onload
    });
    return this;
  };

  /**
     Updates P.finfo for certain state and stashes P.finfo, with the
     current content fetched via P.fileContent().

     If passed truthy AND the stash already has stashed content for
     the current file, only the stashed finfo record is updated, else
     both the finfo and content are updated.
  */
  P.stashContentChange = function(onlyFinfo){
    if(affirmHasFile(true)){
      const fi = this.finfo;
      fi.isExe = this.e.cbIsExe.checked;
      if(!fi.branch) fi.branch = this.fileSelectWidget.checkinBranchName(fi.checkin);
      if(onlyFinfo && $stash.hasStashedContent(fi)){
        $stash.updateFile(fi);
      }else{
        $stash.updateFile(fi, P.fileContent());
      }
      F.message("Stashed change to",F.hashDigits(fi.checkin),fi.filename);
      $stash.prune();
      this.previewNeedsUpdate = true;
    }
    return this;
  };

  /**
     Removes any stashed state for the current P.finfo (if set) from
     F.storage. Returns this.
  */
  P.unstashContent = function(){
    const finfo = arguments[0] || this.finfo;
    if(finfo){
      this.previewNeedsUpdate = true;
      $stash.unstash(finfo);
      //console.debug("Unstashed",finfo);
      F.message("Unstashed",F.hashDigits(finfo.checkin),finfo.filename);
    }
    return this;
  };

  /**
     Clears all stashed file state from F.storage. Returns this.
  */
  P.clearStash = function(){
    $stash.clear();
    return this;
  };

  /**
     If stashed content for P.finfo exists, it is returned, else
     undefined is returned.
  */
  P.contentFromStash = function(){
    return affirmHasFile(true) ? $stash.stashedContent(this.finfo) : undefined;
  };

  /**
     If a stashed version of the given finfo object exists (same
     filename/checkin values), return it, else return undefined.
  */
  P.getStashedFinfo = function(finfo){
    return $stash.getFinfo(finfo);
  };

  P.$stash = $stash /*only for testing/debugging - not part of the API.*/;

})(window.fossil);
