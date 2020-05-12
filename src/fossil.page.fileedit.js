(function(F/*the fossil object*/){
  "use strict";
  /**
     Code for the /filepage app. Requires that the fossil JS
     bootstrapping is complete and fossil.fetch() has been installed.
  */
  const E = (s)=>document.querySelector(s),
        D = F.dom,
        P = F.page;

  /**
     Widget for the checkin/file selection list.
  */
  P.fileSelector = {
    e:{
      container: E('#fileedit-file-selector')
    },
    finfo: {},
    cache: {
      checkins: undefined,
      files:{}
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
      F.fetch('fileedit',{
        urlParams:'ajax=filelist&leaves',
        responseType: 'json',
        onload: function(list){
          D.append(D.clearElement(self.e.ciListLabel),"Open leaves:");
          self.cache.checkins = list;
          D.clearElement(D.enable(self.e.selectCi));
          let loadThisOne;
          list.forEach(function(o,n){
            if(!n) loadThisOne = o;
            D.option(self.e.selectCi, o.checkin,
                     o.timestamp+' ['+o.branch+']: '
                     +F.hashDigits(o.checkin));
          });
          self.loadFiles(loadThisOne ? loadThisOne.checkin : false);
        }
      });
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
        D.clearElement(selFiles, this.e.btnLoadFile);
        D.append(
          D.clearElement(this.e.fileListLabel),
          "Editable files for ",
          D.a(F.repoUrl('timeline',{
            c: ciUuid
          }), F.hashDigits(ciUuid))
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
      F.fetch('fileedit',{
        urlParams:{ajax:'filelist', checkin: ciUuid},
        responseType: 'json',
        onload
      });
      return this;
    },
    /**
       Initializes the checkin/file selector widget. Must only be
       called once.
    */
    init: function(){
      const selCi = this.e.selectCi = D.select(),
            selFiles = this.e.selectFiles
            = D.addClass(D.select(), 'file-list'),
            btnLoad = this.e.btnLoadFile =
            D.addClass(D.button("Load file"), "flex-shrink"),
            filesLabel = this.e.fileListLabel =
            D.addClass(D.div(),'flex-shrink','file-list-label'),
            ciLabelWrapper = D.addClass(
              D.div(), 'flex-container','flex-row', 'flex-shrink',
              'stretch'
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
      D.attr(selFiles, 'size', 10);
      D.append(
        this.e.container,
        D.append(ciLabelWrapper,
                 btnReload, ciLabel),
        selCi,
        filesLabel,
        selFiles,
        btnLoad
      );
      this.loadLeaves();
      selCi.addEventListener(
        'change', (e)=>this.loadFiles(e.target.value), false
      );
      btnLoad.addEventListener(
        'click', (e)=>{
          this.finfo.filename = selFiles.value;
          if(this.finfo.filename){
            P.loadFile(this.finfo.filename, this.finfo.checkin);
          }
        }, false
      );
      btnReload.addEventListener(
        'click', (e)=>this.loadLeaves(), false
      );
      delete this.init;
    }
  };

  window.addEventListener("load", function() {
    P.tabs = new fossil.TabManager('#fileedit-tabs');
    P.e = {
      taEditor: E('#fileedit-content-editor'),
      taCommentSmall: E('#fileedit-comment'),
      taCommentBig: E('#fileedit-comment-big'),
      ajaxContentTarget: E('#ajax-target'),
      btnCommit: E("#fileedit-btn-commit"),
      btnReload: E("#fileedit-tab-content > .fileedit-options > "
                   +"button.fileedit-content-reload"),
      selectPreviewModeWrap: E('#select-preview-mode'),
      selectHtmlEmsWrap: E('#select-preview-html-ems'),
      selectEolWrap:  E('#select-preview-html-ems'),
      cbLineNumbersWrap: E('#cb-line-numbers'),
      cbAutoPreview: E('#cb-preview-autoupdate > input[type=checkbox]'),
      tabs:{
        content: E('#fileedit-tab-content'),
        preview: E('#fileedit-tab-preview'),
        diff: E('#fileedit-tab-diff'),
        commit: E('#fileedit-tab-commit')
      }
    };
    P.fileSelector.init();
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

    P.tabs.e.container.insertBefore(
      /* Move the status bar between the tab buttons and
         tab panels. Seems to be the best fit in terms of
         functionality and visibility. */
      E('#fossil-status-bar'), P.tabs.e.tabs
    );

    P.tabs.addEventListener(
      /* Set up auto-refresh of the preview tab... */
      'before-switch-to', function(ev){
        if(ev.detail===P.e.tabs.preview
           && P.e.cbAutoPreview.checked){
          P.preview();
        }
      }
    );

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
    F.confirmer(P.e.btnReload, {
      confirmText: "Really reload, losing edits?",
      onconfirm: (e)=>P.loadFile(),
      ticks: 3
    });
    E('#comment-toggle').addEventListener(
      "click",(e)=>P.toggleCommentMode(), false
    );

    /**
       Cosmetic: jump through some hoops to enable/disable
       certain preview options depending on the current
       preview mode...
    */
    const selectPreviewMode =
          P.e.selectPreviewModeWrap.querySelector('select');
    selectPreviewMode.addEventListener(
      "change", function(e){
        const mode = e.target.value,
              name = P.previewModes[mode],
              hide = [], unhide = [];
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
    selectPreviewMode.dispatchEvent(
      // Force UI update
      new Event('change',{target:selectPreviewMode})
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
  }, false)/*onload event handler*/;

  /**
     Getter (if called with no args) or setter (if passed an arg) for
     the current file content. We use a function, rather than direct
     access so that clients can hypothetically swap out this method
     from their skin in order to facilitate plugging-in of fancy
     3rd-party editor widgets.

     The setter form returns this object.
  */
  P.value = function(){
    if(0===arguments.length){
      return this.e.taEditor.value;
    }else{
      this.e.taEditor.value = arguments[0];
      return this;
    }
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
    console.debug(s,h);
  };
  
  /**
     updateVersion() updates the filename and version in various UI
     elements...

     Returns this object.
  */
  P.updateVersion = function(file,rev){
    this.finfo = {filename:file,checkin:rev};
    const E = (s)=>document.querySelector(s),
          euc = encodeURIComponent,
          rHuman = F.hashDigits(rev),
          rUrl = F.hashDigits(rev,true);
    D.append(
      D.clearElement(E('#r-label')),
      rHuman
    );
    var e;
    e = E('#timeline-link');
    D.attr(e, 'href',F.repoUrl('timeline',{c:rUrl}));
    e = E('#finfo-file-name');
    D.append(
      D.clearElement(e),
      D.a(F.repoUrl('finfo',{name:file, m:rUrl}), file)
    );
    e = E('#r-link');
    D.attr(e, 'href', F.repoUrl('info/'+rUrl));
    e = E('#r-label');
    D.append(D.clearElement(e),rHuman);
    const purlArgs = F.encodeUrlArgs({
      filename: this.finfo.filename,
      checkin: rUrl
    },false,true);
    const purl = F.repoUrl('fileedit',purlArgs);
    e = E('#permalink');
    D.attr(D.append(D.clearElement(e),'?'+purlArgs),'href', purl);
    return this;
  };

  const affirmHasFile = function(){
    if(!P.finfo) F.error("No file is loaded.");
    return !!P.finfo;
  };

  /**
     loadFile() loads (file,checkinVersion) and updates the relevant
     UI elements to reflect the loaded state.

     Returns this object, noting that the load is async.
  */
  P.loadFile = function(file,rev){
    if(0===arguments.length){
      if(!affirmHasFile()) return this;
      file = this.finfo.filename;
      rev = this.finfo.checkin;
    }
    delete this.finfo;
    const self = this;
    F.message("Loading content...");
    F.fetch('fileedit',{
      urlParams: {
        ajax: 'content',
        filename:file,
        checkin:rev
      },
      onload:(r)=>{
        F.message('Loaded content.');
        self.value(r);
        self.updateVersion(file,rev);
        self.tabs.switchToTab(self.e.tabs.content);
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
    if(!f.target){
      f.target = this.e.tabs.preview.querySelector(
        '#fileedit-tab-preview-wrapper'
      );
    }
    const self = this;
    const updateView = function(c){
      D.clearElement(f.target);
      if('string'===typeof c) f.target.innerHTML = c;
      if(switchToTab) self.tabs.switchToTab(self.e.tabs.preview);
    };
    return this._postPreview(this.value(), updateView);
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
    fd.append('render_mode',E('select[name=preview_render_mode]').value);
    fd.append('filename',this.finfo.filename);
    fd.append('ln',E('[name=preview_ln]').checked ? 1 : 0);
    fd.append('iframe_height', E('[name=preview_html_ems]').value);
    fd.append('content',content || '');
    F.message(
      "Fetching preview..."
    ).fetch('fileedit',{
      urlParams: {ajax: 'preview'},
      payload: fd,
      onload: (r)=>{
        callback(r);
        F.message('Updated preview.');
      },
      onerror: (e)=>{
        fossil.fetch.onerror(e);
        callback("Error fetching preview: "+e);
      }
    });
    return this;
  };

  
  /**
     Fetches the content diff based on the contents and settings of this
     page's input fields, and updates the UI with the diff view.

     Returns this object, noting that the operation is async.
  */
  P.diff = function f(sbs){
    if(!affirmHasFile()) return this;
    const content = this.value(),
          self = this;
    if(!f.target){
      f.target = this.e.tabs.diff.querySelector(
        '#fileedit-tab-diff-wrapper'
      );
    }
    const fd = new FormData();
    fd.append('filename',this.finfo.filename);
    fd.append('checkin', this.finfo.checkin);
    fd.append('sbs', sbs ? 1 : 0);
    fd.append('content',content);
    F.message(
      "Fetching diff..."
    ).fetch('fileedit',{
      urlParams: {ajax: 'diff'},
      payload: fd,
      onload: function(c){
        f.target.innerHTML = [
          "<div>Diff <code>[",
          self.finfo.checkin,
          "]</code> &rarr; Local Edits</div>",
          c||'No changes.'
        ].join('');
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
    const content = this.value(),
          target = document.querySelector('#fileedit-manifest'),
          cbDryRun = E('[name=dry_run]'),
          isDryRun = cbDryRun.checked,
          filename = this.finfo.filename;
    if(!f.updateView){
      f.updateView = function(c){
        target.innerHTML = [
          "<h3>Manifest",
          (c.dryRun?" (dry run)":""),
          ": ", F.hashDigits(c.uuid),"</h3>",
          "<code class='fileedit-manifest'>",
          c.manifest,
          "</code></pre>"
        ].join('');
        const msg = [
          'Committed',
          c.dryRun ? '(dry run)' : '',
          '[', F.hashDigits(c.uuid) ,'].'
        ];
        if(!c.dryRun){
          msg.push('Re-activating dry-run mode.');
          self.e.taComment.value = '';
          cbDryRun.checked = true;
          self.updateVersion(filename, c.uuid);
          self.fileSelector.loadLeaves();
        }
        F.message.apply(fossil, msg);
        self.tabs.switchToTab(self.e.tabs.commit);
      };
    }
    if(!content){
      f.updateView('');
      return this;
    }
    const fd = new FormData();
    fd.append('filename',filename);
    fd.append('checkin', this.finfo.checkin);
    fd.append('content',content);
    fd.append('dry_run',isDryRun ? 1 : 0);
    /* Text fields or select lists... */
    ['comment_mimetype',
     'comment'
    ].forEach(function(name){
      var e = E('[name='+name+']');
      if(e) fd.append(name,e.value);
    });
    /* Checkboxes: */
    ['allow_fork',
     'allow_older',
     'exec_bit',
     'allow_merge_conflict',
     'prefer_delta'
    ].forEach(function(name){
      var e = E('[name='+name+']');
      if(e){
        if(e.checked) fd.append(name, 1);
      }else{
        console.error("Missing checkbox? name =",name);
      }
    });
    F.message(
      "Checking in..."
    ).fetch('fileedit',{
      urlParams: {ajax: 'commit'},
      payload: fd,
      responseType: 'json',
      onload: f.updateView
    });
    return this;
  };

  
})(window.fossil);
