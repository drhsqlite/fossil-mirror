(function(F/*the fossil object*/){
  "use strict";
  /**
     Code for the /filepage app. Requires that the fossil JS
     bootstrapping is complete and fossil.fetch() has been installed.
  */
  const E = (s)=>document.querySelector(s),
        D = F.dom,
        P = F.page;
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

  /**
     loadFile() loads (file,checkinVersion) and updates the relevant
     UI elements to reflect the loaded state.

     Returns this object, noting that the load is async.
  */
  P.loadFile = function(file,rev){
    if(0===arguments.length){
      if(!this.finfo) return this;
      file = this.finfo.filename;
      rev = this.finfo.checkin;
    }
    delete this.finfo;
    const self = this;
    F.message("Loading content...");
    F.fetch('fileedit_content',{
      urlParams: {filename:file,checkin:rev},
      onload:(r)=>{
        F.message('Loaded content.');
        self.e.taEditor.value = r;
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
    if(!this.finfo){
      F.error("No content is loaded.");
      return this;
    }
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
    return this._postPreview(this.e.taEditor.value, updateView);
  };

  /**
     Callback for use with F.connectPagePreviewers()
  */
  P._postPreview = function(content,callback){
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
    ).fetch('fileedit_preview',{
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
    if(!this.finfo){
      F.error("No content is loaded.");
      return this;
    }
    const content = this.e.taEditor.value,
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
    ).fetch('fileedit_diff',{
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
    if(!this.finfo){
      F.error("No content is loaded.");
      return this;
    }
    const self = this;
    const content = this.e.taEditor.value,
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
          P.updateVersion(filename, c.uuid);
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
    ).fetch('fileedit_commit',{
      payload: fd,
      responseType: 'json',
      onload: f.updateView
    });
    return this;
  };

  
})(window.fossil);
