(function(F/*the fossil object*/){
  "use strict";
  /**
     Client-side implementation of the /wikiedit app. Requires that
     the fossil JS bootstrapping is complete and that several fossil
     JS APIs have been installed: fossil.fetch, fossil.dom,
     fossil.tabs, fossil.storage, fossil.confirmer, fossil.popupwidget.

     Custom events which can be listened for via
     fossil.page.addEventListener():

     - Event 'wiki-page-loaded': passes on information when it
     loads a wiki (whether from the network or its internal local-edit
     cache), in the form of an "winfo" object:

     {
       name: string,
       mimetype: mimetype string,
       type: "normal" | "tag" | "checkin" | "branch" | "ticket" | "sandbox",
       version: UUID string or null for a sandbox page or new page,
       parent: parent UUID string or null if no parent,
       isEmpty: true if page has no content (is "deleted").
       content: string, optional in most contexts
     }

     The internal docs and code frequently use the term "winfo", and such
     references refer to an object with that form.

     The fossil.page.wikiContent() method gets or sets the current
     file content for the page.

     - Event 'wiki-saved': is fired when a commit completes,
     passing on the same info as wiki-page-loaded.

     - Event 'wiki-content-replaced': when the editor's content is
     replaced, as opposed to it being edited via user
     interaction. This normally happens via selecting a file to
     load. The event detail is the fossil.page object, not the current
     file content.

     - Event 'wiki-preview-updated': when the preview is refreshed
     from the server, this event passes on information about the preview
     change in the form of an object:

     {
     element: the DOM element which contains the content preview.
     mimetype: the page's mimetype.
     }

     Here's an example which can be used with the highlightjs code
     highlighter to update the highlighting when the preview is
     refreshed in "wiki" mode (which includes fossil-native wiki and
     markdown):

     fossil.page.addEventListener(
       'wiki-preview-updated',
       (ev)=>{
         if(ev.detail.mimetype!=='text/plain'){
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
    /* Max number of locally-edited pages to stash, after which we
       drop the least-recently used. */
    defaultMaxStashSize: 10,
    useConfirmerButtons:{
    /* If true during fossil.page setup, certain buttons will use a
       "confirmer" step, else they will not. The confirmer topic has
       been the source of much contention in the forum. */
      save: false,
      reload: true,
      discardStash: true
    },
    /**
       If true, a keyboard combo of shift-enter (from the editor)
       toggles between preview and edit modes.  This is normally
       desired but at least one software keyboard is known to
       misinteract with this, treating an Enter after
       automatically-capitalized letters as a shift-enter:

       https://fossil-scm.org/forum/forumpost/dbd5b68366147ce8

       Maintenance note: /fileedit also uses this same key for the
       same purpose.
    */
    shiftEnterPreview: F.storage.getBool('edit-shift-enter-preview', true)
  };

  /**
     $stash is an internal-use-only object for managing "stashed"
     local edits, to help avoid that users accidentally lose content
     by switching tabs or following links or some such. The basic
     theory of operation is...

     All "stashed" state is stored using fossil.storage.

     - When the current wiki content is modified by the user, the
       current state of the page is stashed.

     - When saving, the stashed entry for the previous version is
       removed from the stash.

     - When "loading", we use any stashed state for the given
       checkin/file combination. When forcing a re-load of content,
       any stashed entry for that combination is removed from the
       stash.

     - Every time P.stashContentChange() updates the stash, it is
       pruned to $stash.prune.defaultMaxCount most-recently-updated
       entries.

     - This API often refers to "winfo objects." Those are objects
       with a minimum of {page,mimetype} properties (which must be
       valid), and the page name is used as basis for the stash keys
       for any given page.

     The structure of the stash is a bit convoluted for efficiency's
     sake: we store a map of file info (winfo) objects separately from
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
       "PAGE_NAME": {wiki page info w/o content}
       ...
       }

       In F.storage we...

       - Store this.index under the key this.keys.index.

       - Store each page's content under the key
       (P.name+'/PAGE_NAME'). These are stored separately from the
       index entries to avoid having to JSONize/de-JSONize the
       content. The assumption/hope is that the browser can store
       those records "directly," without any intermediary
       encoding/decoding going on.
    */
    indexKey: function(winfo){return winfo.name},
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
      else F.page.dispatchEvent('wiki-stash-updated', this);
    },
    /**
       Returns the stashed version, if any, for the given winfo object.
    */
    getWinfo: function(winfo){
      const ndx = this.getIndex();
      return ndx[this.indexKey(winfo)];
    },
    /** Serializes this object's index to F.storage. Returns this. */
    storeIndex: function(){
      if(this.index) F.storage.setJSON(this.keys.index,this.index);
      return this;
    },
    /** Updates the stash record for the given winfo
        and (optionally) content. If passed 1 arg, only
        the winfo stash is updated, else both the winfo
        and its contents are (re-)stashed. Returns this.
    */
    updateWinfo: function(winfo,content){
      const ndx = this.getIndex(),
            key = this.indexKey(winfo),
            old = ndx[key];
      const record = old || (ndx[key]={
        name: winfo.name
      });
      record.mimetype = winfo.mimetype;
      record.type = winfo.type;
      record.parent = winfo.parent;
      record.version = winfo.version;
      record.stashTime = new Date().getTime();
      record.isEmpty = !!winfo.isEmpty;
      record.attachments = winfo.attachments;
      this.storeIndex();
      if(arguments.length>1){
        if(content) delete record.isEmpty;
        F.storage.set(this.contentKey(key), content);
      }
      this._fireStashEvent();
      return this;
    },
    /**
       Returns the stashed content, if any, for the given winfo
       object.
    */
    stashedContent: function(winfo){
      return F.storage.get(this.contentKey(this.indexKey(winfo)));
    },
    /** Returns true if we have stashed content for the given winfo
        record or page name. */
    hasStashedContent: function(winfo){
      if('string'===typeof winfo) winfo = {name: winfo};
      return F.storage.contains(this.contentKey(this.indexKey(winfo)));
    },
    /** Unstashes the given winfo record and its content.
        Returns this. */
    unstash: function(winfo){
      const ndx = this.getIndex(),
            key = this.indexKey(winfo);
      delete winfo.stashTime;
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
  $stash.prune.defaultMaxCount = P.config.defaultMaxStashSize || 10;
  P.$stash = $stash /* we have to expose this for the new-page case :/ */;

  /**
     Internal workaround to select the current preview mode
     and fire a change event if the value actually changes
     or if forceEvent is truthy.
  */
  P.selectMimetype = function(modeValue, forceEvent){
    const s = this.e.selectMimetype;
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
     Internal helper to get an edit status indicator for the given
     winfo object. Pass it a winfo object or one of the "constants"
     which are assigned as member properties of this function (see
     below its definition).
  */
  const getEditMarker = function f(winfo, textOnly){
    const esm = F.config.editStateMarkers;
    if(f.NEW===winfo){ /* force is-new */
        return textOnly ? esm.isNew :
        D.addClass(D.append(D.span(),esm.isNew), 'is-new');
    }else if(f.MODIFIED===winfo){ /* force is-modified */
        return textOnly ? esm.isModified :
        D.addClass(D.append(D.span(),esm.isModified), 'is-modified');
    }else if(f.DELETED===winfo){/* force is-deleted */
        return textOnly ? esm.isDeleted :
        D.addClass(D.append(D.span(),esm.isDeleted), 'is-deleted');
    }else if(winfo && winfo.version){ /* is existing page modified? */
      if($stash.getWinfo(winfo)){
        return textOnly ? esm.isModified :
          D.addClass(D.append(D.span(),esm.isModified), 'is-modified');
      }
      /*fall through*/
    }
    else if(winfo){ /* is new non-sandbox or is modified sandbox? */
      if('sandbox'!==winfo.type){
        return textOnly ? esm.isNew :
          D.addClass(D.append(D.span(),esm.isNew), 'is-new');
      }else if($stash.getWinfo(winfo)){
        return textOnly ? esm.isModified :
          D.addClass(D.append(D.span(),esm.isModified), 'is-modified');
      }
    }
    return textOnly ? '' : D.span();
  };
  getEditMarker.NEW = 1;
  getEditMarker.MODIFIED = 2;
  getEditMarker.DELETED = 3;

  /**
     Returns undefined if winfo is falsy, true if the given winfo
     object appears to be "new", else returns false.
  */
  const winfoIsNew = function(winfo){
    if(!winfo) return undefined;
    else if('sandbox' === winfo.type) return false;
    else return !winfo.version;
  };

  /**
     Sets up and maintains the widgets for the list of wiki pages.
  */
  const WikiList = {
    e: {
      filterCheckboxes: {
        /*map of wiki page type to checkbox for list filtering purposes,
          except for "sandbox" type, which is assumed to be covered by
          the "normal" type filter. */},
    },
    cache: {
      pageList: [],
      optByName:{/*map of page names to OPTION object, to speed up
                   certain operations.*/},
      names: {
        /* Map of page names to "something." We don't map to their
           winfo bits because those regularly get swapped out via
           de/serialization. We need this map to support the add-new-page
           feature, to give us a way to check for dupes without asking
           the server or walking through the whole selection list.
        */}
    },
    /**
       Updates OPTION elements to reflect whether the page has local
       changes or is new/unsaved. This implementation is horribly
       inefficient, in that we have to walk and validate the whole
       list for each stash-level change.

       If passed an argument, it is assumed to be an OPTION element
       and only that element is updated, else all OPTION elements
       in this.e.select are updated.
 
       Reminder to self: in order to mark is-edited/is-new state we
       have to update the OPTION element's inner text to reflect the
       is-modified/is-new flags, rather than use CSS classes to tag
       them, because mobile Chrome can neither restyle OPTION elements
       no render ::before content on them. We *also* use CSS tags, but
       they aren't sufficient for the mobile browsers.
    */
    _refreshStashMarks: function callee(option){
      if(!callee.eachOpt){
        const self = this;
        callee.eachOpt = function(keyOrOpt){
          const opt = 'string'===typeof keyOrOpt ? self.e.select.options[keyOrOpt] : keyOrOpt;
          const stashed = $stash.getWinfo({name:opt.value});
          var prefix = '';
          D.removeClass(opt, 'stashed', 'stashed-new', 'deleted');
          if(stashed){
            const isNew = winfoIsNew(stashed);
            prefix = getEditMarker(isNew ? getEditMarker.NEW : getEditMarker.MODIFIED, true);
            D.addClass(opt, isNew ? 'stashed-new' : 'stashed');
            D.removeClass(opt, 'deleted');
          }else if(opt.dataset.isDeleted){
            prefix = getEditMarker(getEditMarker.DELETED,true);
            D.addClass(opt, 'deleted');
          }
          opt.innerText = prefix + opt.value;
          self.cache.names[opt.value] = true;
        };
      }
      if(arguments.length){
        callee.eachOpt(option);
      }else{
        this.cache.names = {/*must reset it to acount for local page removals*/};
        Object.keys(this.e.select.options).forEach(callee.eachOpt);
      }
    },
    /** Removes the given wiki page entry from the page selection
        list, if it's in the list. */
    removeEntry: function(name){
      const sel = this.e.select;
      var ndx = sel.selectedIndex;
      sel.value = name;
      if(sel.selectedIndex>-1){
        if(ndx === sel.selectedIndex) ndx = -1;
        sel.options.remove(sel.selectedIndex);
      }
      sel.selectedIndex = ndx;
      delete this.cache.names[name];
      delete this.cache.optByName[name];
      this.cache.pageList = this.cache.pageList.filter((wi)=>name !== wi.name);
    },

    /**
       Rebuilds the selection list. Necessary when it's loaded from
       the server, we locally create a new page, or we remove a
       locally-created new page.
    */
    _rebuildList: function callee(){
      /* Jump through some hoops to integrate new/unsaved
         pages into the list of existing pages... We use a map
         as an intermediary in order to filter out any local-stash
         dupes from server-side copies. */
      const list = this.cache.pageList;
      if(!list) return;
      if(!callee.sorticase){
        callee.sorticase = function(l,r){
          if(l===r) return 0;
          l = l.toLowerCase();
          r = r.toLowerCase();
          return l<=r ? -1 : 1;
        };
      }
      const map = {}, ndx = $stash.getIndex(), sel = this.e.select;
      D.clearElement(sel);
      list.forEach((winfo)=>map[winfo.name] = winfo);
      Object.keys(ndx).forEach(function(key){
        const winfo = ndx[key];
        if(!winfo.version/*new page*/) map[winfo.name] = winfo;
      });
      const self = this;
      Object.keys(map)
        .sort(callee.sorticase)
        .forEach(function(name){
          const winfo = map[name];
          const opt = D.option(sel, winfo.name);
          const wtype = opt.dataset.wtype =
                winfo.type==='sandbox' ? 'normal' : (winfo.type||'normal');
          const cb = self.e.filterCheckboxes[wtype];
          self.cache.optByName[winfo.name] = opt;
          if(cb && !cb.checked) D.addClass(opt, 'hidden');
          if(winfo.isEmpty){
            opt.dataset.isDeleted = true;
          }
          self._refreshStashMarks(opt);
        });
      D.enable(sel);
      if(P.winfo) sel.value = P.winfo.name;
    },

    /** Loads the page list and populates the selection list. */
    loadList: function callee(){
      if(!callee.onload){
        const self = this;
        callee.onload = function(list){
          self.cache.pageList = list;
          self._rebuildList();
          F.message("Loaded page list.");
        };
      }
      if(P.initialPageList){
        /* ^^^ injected at page-creation time. */
        const list = P.initialPageList;
        delete P.initialPageList;
        callee.onload(list);
      }else{
        F.fetch('wikiajax/list',{
          urlParams:{verbose:true},
          responseType: 'json',
          onload: callee.onload
        });
      }
      return this;
    },

    /**
       Returns true if the given name appears to be a valid
       wiki page name, noting that the final arbitrator is the
       server. On validation error it emits a message via fossil.error()
       and returns false.
    */
    validatePageName: function(name){
      var err;
      if(!name){
        err = "may not be empty";
      }else if(this.cache.names.hasOwnProperty(name)){
        err = "page already exists: "+name;
      }else if(name.length>100){
        err = "too long (limit is 100)";
      }else if(/\s{2,}/.test(name)){
        err = "multiple consecutive spaces";
      }else if(/[\t\r\n]/.test(name)){
        err = "contains control character(s)";
      }else{
        let i = 0, n = name.length, c;
        for( ; i < n; ++i ){
          if(name.charCodeAt(i)<0x20){
            err = "contains control character(s)";
            break;
          }
        }
      }
      if(err){
        F.error("Invalid name:",err);
      }
      return !err;
    },

    /**
       If the given name is valid, a new page with that (trimmed) name
       is added to the local stash.
    */
    addNewPage: function(name){
      name = name.trim();
      if(!this.validatePageName(name)) return false;
      var wtype = 'normal';
      if(0===name.indexOf('checkin/')) wtype = 'checkin';
      else if(0===name.indexOf('branch/')) wtype = 'branch';
      else if(0===name.indexOf('ticket/')) wtype = 'ticket';
      else if(0===name.indexOf('tag/')) wtype = 'tag';
      /* ^^^ note that we're not validating that, e.g., checkin/XYZ
         has a full artifact ID after "checkin/". */
      const winfo = {
        name: name, type: wtype, mimetype: 'text/x-markdown',
        version: null, parent: null
      };
      this.cache.pageList.push(
        winfo/*keeps entry from getting lost from the list on save*/
      );
      $stash.updateWinfo(winfo, '');
      this._rebuildList();
      P.loadPage(winfo.name);
      return true;
    },

    /**
       Installs a wiki page selection list into the given parent DOM
       element and loads the page list from the server.
    */
    init: function(parentElem){
      const sel = D.select(), btn = D.addClass(D.button("Reload page list"), 'save');
      this.e.select = sel;
      D.addClass(parentElem, 'WikiList');
      D.clearElement(parentElem);
      D.append(
        parentElem,
        D.append(D.fieldset("Select a page to edit"),
                 sel)
      );
      D.attr(sel, 'size', 12);
      D.option(D.disable(D.clearElement(sel)), undefined, "Loading...");

      /** Set up filter checkboxes for the various types
          of wiki pages... */
      const fsFilter = D.addClass(D.fieldset("Page types"),"page-types-list"),
            fsFilterBody = D.div(),
            filters = ['normal', 'branch/...', 'tag/...', 'checkin/...', 'ticket/...']
      ;
      D.append(fsFilter, fsFilterBody);
      D.addClass(fsFilterBody, 'flex-container', 'flex-column', 'stretch');

      // Add filters by page type...
      const self = this;
      const filterByType = function(wtype, show){
        sel.querySelectorAll('option[data-wtype='+wtype+']').forEach(function(opt){
          if(show) opt.classList.remove('hidden');
          else opt.classList.add('hidden');
        });
      };
      filters.forEach(function(label){
        const wtype = label.split('/')[0];
        const cbId = 'wtype-filter-'+wtype,
              lbl = D.attr(D.append(D.label(),label),
                           'for', cbId),
              cb = D.attr(D.input('checkbox'), 'id', cbId);
        D.append(fsFilterBody, D.append(D.span(), cb, lbl));
        self.e.filterCheckboxes[wtype] = cb;
        cb.checked = true;
        filterByType(wtype, cb.checked);
        cb.addEventListener(
          'change',
          function(ev){filterByType(wtype, ev.target.checked)},
          false
        );
      });
      { /* add filter for "deleted" pages */
        const cbId = 'wtype-filter-deleted',
              lbl = D.attr(D.append(D.label(),
                                    getEditMarker(getEditMarker.DELETED,false),
                                    'deleted'),
                           'for', cbId),
              cb = D.attr(D.input('checkbox'), 'id', cbId);
        cb.checked = false;
        D.addClass(parentElem,'hide-deleted');
        D.attr(lbl);
        const deletedTip = F.helpButtonlets.create(
          D.span(),
          'Fossil considers empty pages to be "deleted" in some contexts.'
        );
        D.append(fsFilterBody, D.append(
          D.span(), cb, lbl, deletedTip
        ));
        cb.addEventListener(
          'change',
          function(ev){
            if(ev.target.checked) D.removeClass(parentElem,'hide-deleted');
            else D.addClass(parentElem,'hide-deleted');
          },
          false);
      }
      /* A legend of the meanings of the symbols we use in
         the OPTION elements to denote certain state. */
      const fsLegend = D.fieldset("Edit status"),
            fsLegendBody = D.div();
      D.append(fsLegend, fsLegendBody);
      D.addClass(fsLegendBody, 'flex-container', 'flex-column', 'stretch');
      D.append(
        fsLegendBody,
        D.append(D.span(), getEditMarker(getEditMarker.NEW,false)," = new/unsaved"),
        D.append(D.span(), getEditMarker(getEditMarker.MODIFIED,false)," = has local edits"),
        D.append(D.span(), getEditMarker(getEditMarker.DELETED,false)," = is empty (deleted)")
      );

      const fsNewPage = D.fieldset("Create new page"),
            fsNewPageBody = D.div(),
            newPageName = D.input('text'),
            newPageBtn = D.button("Add page locally")
            ;
      D.append(parentElem, fsNewPage);
      D.append(fsNewPage, fsNewPageBody);
      D.addClass(fsNewPageBody, 'flex-container', 'flex-column', 'new-page');
      D.append(
        fsNewPageBody, newPageName, newPageBtn,
        D.append(D.addClass(D.span(), 'mini-tip'),
                 "New pages exist only in this browser until they are saved.")
      );
      newPageBtn.addEventListener('click', function(){
        if(self.addNewPage(newPageName.value)){
          newPageName.value = '';
        }
      }, false);

      D.append(
        parentElem,
        D.append(D.addClass(D.div(), 'fieldset-wrapper'),
                 fsFilter, fsNewPage, fsLegend)
      );

      D.append(parentElem, btn);
      btn.addEventListener('click', ()=>this.loadList(), false);
      this.loadList();
      const onSelect = (e)=>P.loadPage(e.target.value);
      sel.addEventListener('change', onSelect, false);
      sel.addEventListener('dblclick', onSelect, false);
      F.page.addEventListener('wiki-stash-updated', ()=>{
        if(P.winfo) this._refreshStashMarks();
        else this._rebuildList();
      });
      F.page.addEventListener('wiki-page-loaded', function(ev){
        /* Needed to handle the saved-an-empty-page case. */
        const page = ev.detail,
              opt = self.cache.optByName[page.name];
        if(opt){
          if(page.isEmpty) opt.dataset.isDeleted = true;
          else delete opt.dataset.isDeleted;
          self._refreshStashMarks(opt);
        }else if('sandbox'!==page.type){
          F.error("BUG: internal mis-handling of page object: missing OPTION for page "+page.name);
        }
      });

      const cbEditPreview = E('#edit-shift-enter-preview');
      cbEditPreview.addEventListener('change', function(e){
        F.storage.set('edit-shift-enter-preview',
                      P.config.shiftEnterPreview = e.target.checked);
      }, false);
      cbEditPreview.checked = P.config.shiftEnterPreview;
      delete this.init;
    }/*init()*/
  };

  /**
     Widget for listing and selecting $stash entries.
  */
  P.stashWidget = {
    e:{/*DOM element(s)*/},
    init: function(domInsertPoint/*insert widget BEFORE this element*/){
      const wrapper = D.addClass(
        D.attr(D.div(),'id','wikiedit-stash-selector'),
        'input-with-label'
      );
      const sel = this.e.select = D.select(),
            btnClear = this.e.btnClear = D.button("Discard Edits"),
            btnHelp = D.append(
              D.addClass(D.div(), "help-buttonlet"),
              'Locally-edited wiki pages. Timestamps are the last local edit time. ',
              'Only the ',P.config.defaultMaxStashSize,' most recent pages ',
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
      P.addEventListener('wiki-stash-updated',(e)=>this.updateList(e.detail));
      P.addEventListener('wiki-page-loaded',(e)=>this.updateList($stash, e.detail));
      sel.addEventListener('change',function(e){
        const opt = this.selectedOptions[0];
        if(opt && opt._winfo) P.loadPage(opt._winfo);
      });
      if(F.storage.isTransient()){/*Warn if our storage is particularly transient...*/
        D.append(wrapper, D.append(
          D.addClass(D.span(),'warning'),
          "Warning: persistent storage is not available, "+
            "so uncomitted edits will not survive a page reload."
        ));
      }
      domInsertPoint.parentNode.insertBefore(wrapper, domInsertPoint);
      if(P.config.useConfirmerButtons.discardStash){
        /* Must come after btnClear is in the DOM AND the button must
           not be hidden, else pinned sizing won't work. */
        F.confirmer(btnClear, {
          pinSize: true,
          confirmText: "DISCARD all local edits?",
          onconfirm: ()=>P.clearStash(),
          ticks: F.config.confirmerButtonTicks
        });
      }else{
        btnClear.addEventListener('click', ()=>P.clearStash(), false);
      }
      D.addClass(btnClear,'hidden');
      $stash._fireStashEvent(/*read the page-load-time stash*/);
      delete this.init;
    },
    /**
       Regenerates the edit selection list.
    */
    updateList: function f(stasher,theWinfo){
      if(!f.compare){
        const cmpBase = (l,r)=>l<r ? -1 : (l===r ? 0 : 1);
        f.compare = (l,r)=>cmpBase(l.name.toLowerCase(), r.name.toLowerCase());
        f.rxZ = /\.\d+Z$/ /* ms and 'Z' part of date string */;
        const pad=(x)=>(''+x).length>1 ? x : '0'+x;
        f.timestring = function(d){
          return [
            d.getFullYear(),'-',pad(d.getMonth()+1/*sigh*/),'-',pad(d.getDate()),
            '@',pad(d.getHours()),':',pad(d.getMinutes())
          ].join('');
        };
      }
      const index = stasher.getIndex(), ilist = [];
      Object.keys(index).forEach((winfo)=>{
        ilist.push(index[winfo]);
      });
      const self = this;
      D.clearElement(this.e.select);
      if(0===ilist.length){
        D.addClass(this.e.btnClear, 'hidden');
        D.option(D.disable(this.e.select),undefined,"No local edits");
        return;
      }
      D.enable(this.e.select);
      if(true){
        /* The problem with this Clear button is that it allows the
           user to nuke a non-empty newly-added page without the
           failsafe confirmation we have if they use
           P.e.btnReload. Not yet sure how best to resolve that. */
        D.removeClass(this.e.btnClear, 'hidden');
      }
      D.disable(D.option(this.e.select,undefined,"Select a local edit..."));
      const currentWinfo = theWinfo || P.winfo || {name:''};
      ilist.sort(f.compare).forEach(function(winfo,n){
        const key = stasher.indexKey(winfo),
              rev = winfo.version || '';
        const opt = D.option(
          self.e.select, n+1/*value is (almost) irrelevant*/,
          [winfo.name,
           ' [',
           rev ? F.hashDigits(rev) : (
             winfo.type==='sandbox' ? 'sandbox' : 'new/local'
           ),'] ',
           f.timestring(new Date(winfo.stashTime))
          ].join('')
        );
        opt._winfo = winfo;
        if(0===f.compare(currentWinfo, winfo)){
          D.attr(opt, 'selected', true);
        }
      });
    }
  }/*P.stashWidget*/;

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
     possible, corner case).
  */
  const ajaxState = {
    count: 0 /* in-flight F.fetch() requests */,
    toDisable: undefined /* elements to disable during ajax activity */
  };
  F.fetch.beforesend = function f(){
    if(!ajaxState.toDisable){
      ajaxState.toDisable = document.querySelectorAll(
        ['button:not([disabled])',
         'input:not([disabled])',
         'select:not([disabled])',
         'textarea:not([disabled])',
         'fieldset:not([disabled])'
        ].join(',')
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
      delete ajaxState.toDisable /* required to avoid enable/disable
                                    race condition with the save button */;
    }
  };

  F.onPageLoad(function() {
    document.body.classList.add('wikiedit');
    P.base = {tag: E('base'), wikiUrl: F.repoUrl('wiki')};
    P.base.originalHref = P.base.tag.href;
    P.e = { /* various DOM elements we work with... */
      taEditor: E('#wikiedit-content-editor'),
      btnReload: E("#wikiedit-tab-content button.wikiedit-content-reload"),
      btnSave: E("button.wikiedit-save"),
      btnSaveClose: E("button.wikiedit-save-close"),
      selectMimetype: E('select[name=mimetype]'),
      selectFontSizeWrap: E('#select-font-size'),
//      selectDiffWS:  E('select[name=diff_ws]'),
      cbAutoPreview: E('#cb-preview-autorefresh'),
      previewTarget: E('#wikiedit-tab-preview-wrapper'),
      diffTarget: E('#wikiedit-tab-diff-wrapper'),
      editStatus: E('#wikiedit-edit-status'),
      tabContainer: E('#wikiedit-tabs'),
      attachmentContainer: E("#attachment-wrapper"),
      tabs:{
        pageList: E('#wikiedit-tab-pages'),
        content: E('#wikiedit-tab-content'),
        preview: E('#wikiedit-tab-preview'),
        diff: E('#wikiedit-tab-diff'),
        misc: E('#wikiedit-tab-misc')
        //commit: E('#wikiedit-tab-commit')
      }
    };
    P.tabs = new F.TabManager(D.clearElement(P.e.tabContainer));
    /* Move the status bar between the tab buttons and
       tab panels. Seems to be the best fit in terms of
       functionality and visibility. */
    P.tabs.addCustomWidget( E('#fossil-status-bar') ).addCustomWidget(P.e.editStatus);
    let currentTab/*used for ctrl-enter switch between editor and preview*/;
    P.tabs.addEventListener(
      /* Set up some before-switch-to tab event tasks... */
      'before-switch-to', function(ev){
        const theTab = currentTab = ev.detail, btnSlot = theTab.querySelector('.save-button-slot');
        if(btnSlot){
          /* Several places make sense for a save button, so we'll
             move that button around to those tabs where it makes sense. */
          btnSlot.parentNode.insertBefore( P.e.btnSave.parentNode, btnSlot );
          btnSlot.parentNode.insertBefore( P.e.btnSaveClose.parentNode, btnSlot );
          P.updateSaveButton();
        }
        if(theTab===P.e.tabs.preview){
          P.baseHrefForWiki();
          if(P.previewNeedsUpdate && P.e.cbAutoPreview.checked) P.preview();
        }else if(theTab===P.e.tabs.diff){
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
        const theTab = ev.detail;
        if(theTab===P.e.tabs.preview){
          P.baseHrefRestore();
        }else if(theTab===P.e.tabs.diff){
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
                              forces a document reflow. */);
          //console.debug("BODY ctrl-enter");
          return false;
        }
      }
    }, true);

    F.connectPagePreviewers(
      P.e.tabs.preview.querySelector(
        '#btn-preview-refresh'
      )
    );

    const diffButtons = E('#wikiedit-tab-diff-buttons');
    diffButtons.querySelector('button.sbs').addEventListener(
      "click",(e)=>P.diff(true), false
    );
    diffButtons.querySelector('button.unified').addEventListener(
      "click",(e)=>P.diff(false), false
    );
    if(0) P.e.btnCommit.addEventListener(
      "click",(e)=>P.commit(), false
    );
    const doSave = function(alsoClose){
      const w = P.winfo;
      if(!w){
        F.error("No page loaded.");
        return;
      }
      if(alsoClose){
        P.save(()=>window.location.href=F.repoUrl('wiki',{name: w.name}));
      }else{
        P.save();
      }
    };
    const doReload = function(e){
      const w = P.winfo;
      if(!w){
        F.error("No page loaded.");
        return;
      }
      if(!w.version/* new/unsaved page */
         && w.type!=='sandbox'
         && P.wikiContent()){
        F.error("This new/unsaved page has content.",
                "To really discard this page,",
                "first clear its content",
                "then use the Discard button.");
        return;
      }
      P.unstashContent();
      if(w.version || w.type==='sandbox'){
        P.loadPage(w);
      }else{
        WikiList.removeEntry(w.name);
        delete P.winfo;
        P.updatePageTitle();
        F.message("Discarded new page ["+w.name+"].");
      }
    };

    if(P.config.useConfirmerButtons.reload){
      P.tabs.switchToTab(1/*DOM visibility workaround*/);
      F.confirmer(P.e.btnReload, {
        pinSize: true,
        confirmText: "Really reload, losing edits?",
        onconfirm: doReload,
        ticks: F.config.confirmerButtonTicks
      });
    }else{
      P.e.btnReload.addEventListener('click', doReload, false);
    }
    if(P.config.useConfirmerButtons.save){
      P.tabs.switchToTab(1/*DOM visibility workaround*/);
      F.confirmer(P.e.btnSave, {
        pinSize: true,
        confirmText: "Really save changes?",
        onconfirm: ()=>doSave(),
        ticks: F.config.confirmerButtonTicks
      });
      F.confirmer(P.e.btnSaveClose, {
        pinSize: true,
        confirmText: "Really save changes?",
        onconfirm: ()=>doSave(true),
        ticks: F.config.confirmerButtonTicks
      });
    }else{
      P.e.btnSave.addEventListener('click', ()=>doSave(), false);
      P.e.btnSaveClose.addEventListener('click', ()=>doSave(true), false);
    }

    P.e.taEditor.addEventListener('change', ()=>P.notifyOfChange(), false);

    P.selectMimetype(false, true);
    P.e.selectMimetype.addEventListener(
      'change',
      function(e){
        if(P.winfo && P.winfo.mimetype !== e.target.value){
          P.winfo.mimetype = e.target.value;
          P._isDirty = true;
          P.stashContentChange(true);
        }
      },
      false
    );

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
      'wiki-content-replaced',
      ()=>{
        P.previewNeedsUpdate = true;
        D.clearElement(P.e.diffTarget, P.e.previewTarget);
      }
    );
    P.addEventListener(
      // Clear certain views after a save
      'wiki-saved',
      (e)=>{
        D.clearElement(P.e.diffTarget, P.e.previewTarget);
        // TODO: replace preview with new content
      }
    );
    P.addEventListener('wiki-stash-updated',function(){
      /* MUST come before WikiList.init() and P.stashWidget.init() so
         that interwoven event handlers get called in the right
         order. */
      if(P.winfo && !P.winfo.version && !$stash.getWinfo(P.winfo)){
        // New local page was removed.
        delete P.winfo;
        P.wikiContent('');
        P.updatePageTitle();
      }
      P.updateSaveButton();
    }).updatePageTitle().updateSaveButton();

    P.addEventListener(
      // Update various state on wiki page load
      'wiki-page-loaded',
      function(ev){
        delete P._isDirty;
        const winfo = ev.detail;
        P.winfo = winfo;
        P.previewNeedsUpdate = true;
        P.e.selectMimetype.value = winfo.mimetype;
        P.tabs.switchToTab(P.e.tabs.content);
        P.wikiContent(winfo.content || '');
        WikiList.e.select.value = winfo.name;
        if(!winfo.version && winfo.type!=='sandbox'){
          F.message('You are editing a new, unsaved page:',winfo.name);
        }
        P.updatePageTitle().updateSaveButton(/* b/c save() routes through here */);
      },
      false
    );
    /* These init()s need to come after P's event handlers are registered.
       The tab-switching is a workaround for the pinSize option of the confirmer widgets:
       it does not work if the confirmer button being initialized is in a hidden
       part of the DOM :/. */
    P.tabs.switchToTab(0);
    WikiList.init( P.e.tabs.pageList.firstElementChild );
    P.tabs.switchToTab(1);
    P.stashWidget.init(P.e.tabs.content.lastElementChild);
    P.tabs.switchToTab(0);
    //P.$wikiList = WikiList/*only for testing/debugging*/;
  }/*F.onPageLoad()*/);

  /**
     Returns true if fossil.page.winfo is set, indicating that a page
     has been loaded, else it reports an error and returns false.

     If passed a truthy value any error message about not having
     a wiki page loaded is suppressed.
  */
  const affirmPageLoaded = function(quiet){
    if(!P.winfo && !quiet) F.error("No wiki page is loaded.");
    return !!P.winfo;
  };

  /**
     Updates the attachments list from this.winfo.
  */
  P.updateAttachmentsView = function f(){
    if(!f.eAttach){
      f.eAttach = P.e.attachmentContainer.querySelector('div');
    }
    D.clearElement(f.eAttach);
    const wi = this.winfo;
    if(!wi){
      D.append(f.eAttach,"No page loaded.");
      return this;
    }
    else if(!wi.version){
      D.append(f.eAttach,
               "Page ["+wi.name+"] cannot have ",
               "attachments until it is saved once.");
      return this;
    }
    const btnReload = D.button("Reload list");
    const self = this;
    btnReload.addEventListener('click', function(){
      const isStashed = $stash.hasStashedContent(wi);
      F.fetch('wikiajax/attachments',{
        responseType: 'json',
        urlParams: {page: wi.name},
        onload: function(r){
          wi.attachments = r;
          if(isStashed) self.stashContentChange(true);
          F.message("Reloaded attachment list for ["+wi.name+"].");
          self.updateAttachmentsView();
        }
      });
    });
    if(!wi.attachments || !wi.attachments.length){
      D.append(f.eAttach,
               btnReload,
               " No attachments found for page ["+wi.name+"]. ",
               D.a(F.repoUrl('attachadd',{
                 page: wi.name,
                 from: F.repoUrl('wikiedit',{name: wi.name})}),
                   "Add attachments..." )
              );
      return this;
    }
    D.append(
      f.eAttach,
      D.append(D.p(),
               btnReload," ",
               D.a(F.repoUrl('attachlist',{page:wi.name}),
                   "Attachments for page ["+wi.name+"]."),
               " ",
               D.a(F.repoUrl('attachadd',{
                 page:wi.name,
                 from: F.repoUrl('wikiedit',{name: wi.name})}),
                   "Add attachments..." )
              )
    );
    wi.attachments.forEach(function(a){
      const wrap = D.div();
      D.append(f.eAttach, wrap);
      D.append(wrap,
               D.append(D.div(),
                        "Attachment ",
                        D.addClass(
                          D.a(F.repoUrl('ainfo',{name:a.uuid}),
                              F.hashDigits(a.uuid,true)),
                          'monospace'),
                        " ",
                        a.filename,
                        (a.isLatest ? " (latest)" : "")
                       )
              );
      //D.append(wrap,D.append(D.div(), "URLs:"));
      const ul = D.ul();
      D.append(wrap, ul);
      [ // List download URL variants for each attachment:
        [
          "attachdownload?page=",
          encodeURIComponent(wi.name),
          "&file=",
          encodeURIComponent(a.filename)
        ].join(''),
        "raw/"+a.src
      ].forEach(function(url){
        const imgUrl = D.append(D.addClass(D.span(), 'monospace'), url);
        const urlCopy = D.span();
        const li = D.li(ul);
        D.append(li, urlCopy, " ", imgUrl);
        F.copyButton(urlCopy, {copyFromElement: imgUrl});
      });
    });
    return this;
  };

  /** Updates the in-tab title/edit status information */
  P.updateEditStatus = function f(){
    if(!f.eLinks){
      f.eName = P.e.editStatus.querySelector('span.name');
      f.eLinks = P.e.editStatus.querySelector('span.links');
    }
    const wi = this.winfo;
    D.clearElement(f.eName, f.eLinks);
    if(!wi){
      D.append(f.eName, '(no page loaded)');
      this.updateAttachmentsView();
      return this;
    }
    D.append(f.eName,getEditMarker(wi, false),wi.name);
    this.updateAttachmentsView();
    if(!wi.version) return this;
    D.append(
      f.eLinks,
      D.a(F.repoUrl('wiki',{name:wi.name}),"viewer"),
      D.a(F.repoUrl('whistory',{name:wi.name}),'history'),
      D.a(F.repoUrl('attachlist',{page:wi.name}),"attachments"),
      D.a(F.repoUrl('attachadd',{page:wi.name,from: F.repoUrl('wikiedit',{name: wi.name})}), "attach"),
      D.a(F.repoUrl('wikiedit',{name:wi.name}),"editor permalink")
    );
    return this;
  };

  /**
     Update the page title and header based on the state of
     this.winfo. A no-op if this.winfo is not set. Returns this.
  */
  P.updatePageTitle = function f(){
    if(!f.titleElement){
      f.titleElement = document.head.querySelector('title');
    }
    const wi = P.winfo, marker = getEditMarker(wi, true),
          title = wi ? wi.name : 'no page loaded';
    f.titleElement.innerText = 'Wiki Editor: ' + marker + title;
    this.updateEditStatus();
    return this;
  };

  /**
     Change the save button depending on whether we have stuff to save
     or not.
  */
  P.updateSaveButton = function(){
    /**
    // Currently disabled, per forum feedback and platform-level
    // event-handling compatibility, but might be revisited. We now
    // use an is-dirty flag instead to prevent saving when no change
    // event has fired for the current doc.
    if(!this.winfo || !this.getStashedWinfo(this.winfo)){
      D.disable(this.e.btnSave).innerText =
        "No changes to save";
      D.disable(this.e.btnSaveClose);
    }else{
      D.enable(this.e.btnSave).innerText = "Save";
      D.enable(this.e.btnSaveClose);
    }*/
    return this;
  };

  /**
     Getter (if called with no args) or setter (if passed an arg) for
     the current file content.

     The setter form sets the content, dispatches a
     'wiki-content-replaced' event, and returns this object.
  */
  P.wikiContent = function f(){
    if(0===arguments.length){
      return f.get();
    }else{
      f.set(arguments[0] || '');
      this.dispatchEvent('wiki-content-replaced', this);
      return this;
    }
  };
  /* Default get/set impls for file content */
  P.wikiContent.get = function(){return P.e.taEditor.value};
  P.wikiContent.set = function(content){P.e.taEditor.value = content};

  /**
     For use when installing a custom editor widget. Pass it the
     getter and setter callbacks to fetch resp. set the content of the
     custom widget. They will be triggered via
     P.wikiContent(). Returns this object.
  */
  P.setContentMethods = function(getter, setter){
    this.wikiContent.get = getter;
    this.wikiContent.set = setter;
    return this;
  };

  /**
     Alerts the editor app that a "change" has happened in the editor.
     When connecting 3rd-party editor widgets to this app, it is
     necessary to call this for any "change" events the widget emits.
     Whether or not "change" means that there were "really" edits is
     irrelevant, but this app will not allow saving unless it believes
     at least one "change" has been made (by being signaled through
     this method).

     This function may perform an arbitrary amount of work, so it
     should not be called for every keypress within the editor
     widget. Calling it for "blur" events is generally sufficient, and
     calling it for each Enter keypress is generally reasonable but
     also computationally costly.
  */
  P.notifyOfChange = function(){
    P._isDirty = true;
    P.stashContentChange();
  };

  /**
     Removes the default editor widget (and any dependent elements)
     from the DOM, adds the given element in its place, removes this
     method from this object, and returns this object. This is not
     needed if the 3rd-party widget replaces or hides this app's
     editor widget (e.g. TinyMCE).
  */
  P.replaceEditorElement = function(newEditor){
    P.e.taEditor.parentNode.insertBefore(newEditor, P.e.taEditor);
    P.e.taEditor.remove();
    P.e.selectFontSizeWrap.remove();
    delete this.replaceEditorElement;
    return P;
  };

  /**
     Sets the current page's base.href to {g.zTop}/wiki.
  */
  P.baseHrefForWiki = function f(){
    this.base.tag.href = this.base.wikiUrl;
    return this;
  };

  /**
     Sets the document's base.href value to its page-load-time
     setting.
  */
  P.baseHrefRestore = function(){
    this.base.tag.href = this.base.originalHref;
  };
  

  /**
     loadPage() loads the given wiki page and updates the relevant
     UI elements to reflect the loaded state. If passed no arguments
     then it re-uses the values from the currently-loaded page, reloading
     it (emitting an error message if no file is loaded).

     Returns this object, noting that the load is async. After loading
     it triggers a 'wiki-page-loaded' event, passing it this.winfo.

     If a locally-edited copy of the given file/rev is found, that
     copy is used instead of one fetched from the server, but it is
     still treated as a load event.

     Alternate call forms:

     - no arguments: re-loads from this.winfo.

     - 1 non-string argument: assumed to be an winfo-style
     object. Must have at least the {name} property, but need not have
     other winfo state.
  */
  P.loadPage = function(name){
    if(0===arguments.length){
      /* Reload from this.winfo */
      if(!affirmPageLoaded()) return this;
      name = this.winfo.name;
    }else if(1===arguments.length && 'string' !== typeof name){
      /* Assume winfo-like object */
      const arg = arguments[0];
      name = arg.name;
    }
    const onload = (r)=>{
      this.dispatchEvent('wiki-page-loaded', r);
    };
    const stashWinfo = this.getStashedWinfo({name: name});
    if(stashWinfo){ // fake a response from the stash...
      F.message("Fetched from the local-edit storage:", stashWinfo.name);
      onload({
        name: stashWinfo.name,
        mimetype: stashWinfo.mimetype,
        type: stashWinfo.type,
        version: stashWinfo.version,
        parent: stashWinfo.parent,
        isEmpty: !!stashWinfo.isEmpty,
        content: $stash.stashedContent(stashWinfo),
        attachments: stashWinfo.attachments
      });
      this._isDirty = true/*b/c loading normally clears that flag*/;
      return this;
    }
    F.message(
      "Loading content..."
    ).fetch('wikiajax/fetch',{
      urlParams: {
        page: name
      },
      responseType: 'json',
      onload:(r)=>{
        F.message('Loaded page ['+r.name+'].');
        onload(r);
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
    if(!affirmPageLoaded()) return this;
    return this._postPreview(this.wikiContent(), function(c){
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
    if(!affirmPageLoaded()) return this;
    if(!content){
      callback(content);
      return this;
    }
    const fd = new FormData();
    const mimetype = this.e.selectMimetype.value;
    fd.append('page', this.winfo.name);
    fd.append('mimetype',mimetype);
    fd.append('content',content || '');
    F.message(
      "Fetching preview..."
    ).fetch('wikiajax/preview',{
      payload: fd,
      onload: (r,header)=>{
        callback(r);
        F.message('Updated preview.');
        P.previewNeedsUpdate = false;
        P.dispatchEvent('wiki-preview-updated',{
          mimetype: mimetype,
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
    if(!affirmPageLoaded()) return this;
    const content = this.wikiContent(),
          self = this,
          target = this.e.diffTarget;
    const fd = new FormData();
    fd.append('page',this.winfo.name);
    fd.append('sbs', sbs ? 1 : 0);
    fd.append('content',content);
    if(this.e.selectDiffWS) fd.append('ws',this.e.selectDiffWS.value);
    F.message(
      "Fetching diff..."
    ).fetch('wikiajax/diff',{
      payload: fd,
      onload: function(c){
        D.parseHtml(D.clearElement(target), [
          "<div>Diff <code>[",
          self.winfo.name,
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
     Saves the current wiki page and re-populates the editor
     with the saved state. If passed an argument, it is
     expected to be a function, which is called only if
     saving succeeds, after all other post-save processing.
  */
  P.save = function callee(onSuccessCallback){
    if(!affirmPageLoaded()) return this;
    else if(!this._isDirty){
      F.error("There are no changes to save.");
      return this;
    }
    const content = this.wikiContent();
    const self = this;
    callee.onload = function(w){
      const oldWinfo = self.winfo;
      self.unstashContent(oldWinfo);
      self.dispatchEvent('wiki-page-loaded', w)/* will reset save buttons */;
      F.message("Saved page: ["+w.name+"].");
      if('function'===typeof onSuccessCallback){
        onSuccessCallback();
      }
    };
    const fd = new FormData(), w = P.winfo;
    fd.append('page',w.name);
    fd.append('mimetype', w.mimetype);
    fd.append('isnew', w.version ? 0 : 1);
    fd.append('content', P.wikiContent());
    F.message(
      "Saving page..."
    ).fetch('wikiajax/save',{
      payload: fd,
      responseType: 'json',
      onload: callee.onload
    });
    return this;
  };

  /**
     Updates P.winfo for certain state and stashes P.winfo, with the
     current content fetched via P.wikiContent().

     If passed truthy AND the stash already has stashed content for
     the current page, only the stashed winfo record is updated, else
     both the winfo and content are updated.
  */
  P.stashContentChange = function(onlyWinfo){
    if(affirmPageLoaded(true)){
      const wi = this.winfo;
      wi.mimetype = P.e.selectMimetype.value;
      if(onlyWinfo && $stash.hasStashedContent(wi)){
        $stash.updateWinfo(wi);
      }else{
        $stash.updateWinfo(wi, P.wikiContent());
      }
      F.message("Stashed changes to page ["+wi.name+"].");
      P.updatePageTitle();
      $stash.prune();
      this.previewNeedsUpdate = true;
    }
    return this;
  };

  /**
     Removes any stashed state for the current P.winfo (if set) from
     F.storage. Returns this.
  */
  P.unstashContent = function(){
    const winfo = arguments[0] || this.winfo;
    if(winfo){
      this.previewNeedsUpdate = true;
      $stash.unstash(winfo);
      //console.debug("Unstashed",winfo);
      F.message("Unstashed page ["+winfo.name+"].");
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
     If stashed content for P.winfo exists, it is returned, else
     undefined is returned.
  */
  P.contentFromStash = function(){
    return affirmPageLoaded(true) ? $stash.stashedContent(this.winfo) : undefined;
  };

  /**
     If a stashed version of the given winfo object exists (same
     filename/checkin values), return it, else return undefined.
  */
  P.getStashedWinfo = function(winfo){
    return $stash.getWinfo(winfo);
  };
})(window.fossil);
