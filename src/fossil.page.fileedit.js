(function(F/*the fossil object*/){
  "use strict";
  /**
     Code for the /filepage app. Requires that the fossil JS
     bootstrapping is complete and fossil.fetch() has been installed.
  */
  const E = (s)=>document.querySelector(s),
        D = F.dom;
  window.addEventListener("load", function() {
    const P = F.page;
    P.tabs = new fossil.TabManager('#fileedit-tabs');
    P.e = {
      taEditor: E('#fileedit-content-editor'),
      taComment: E('#fileedit-comment'),
      ajaxContentTarget: E('#ajax-target'),
      form: E('#fileedit-form'),
      //btnPreview: E("#fileedit-btn-preview"),
      //btnDiffSbs: E("#fileedit-btn-diffsbs"),
      //btnDiffU: E("#fileedit-btn-diffu"),
      btnCommit: E("#fileedit-btn-commit"),
      selectPreviewModeWrap: E('#select-preview-mode'),
      selectHtmlEmsWrap: E('#select-preview-html-ems'),
      selectEolWrap:  E('#select-preview-html-ems'),
      cbLineNumbersWrap: E('#cb-line-numbers'),
      tabs:{
        content: E('#fileedit-tab-content'),
        preview: E('#fileedit-tab-preview'),
        diff: E('#fileedit-tab-diff'),
        commit: E('#fileedit-tab-commit')
      }
    };
    const stopEvent = function(e){
      //e.preventDefault();
      //e.stopPropagation();
      return P;
    };
      
    P.e.form.addEventListener("submit", function(e) {
      e.target.checkValidity();
      e.preventDefault();
      e.stopPropagation();
      return false;
    }, false);
    //P.tabs.getButtonForTab(P.e.tabs.preview)
    P.e.tabs.preview.querySelector(
      'button'
    ).addEventListener(
      "click",(e)=>P.preview(), false
    );

    document.querySelector('#fileedit-form').addEventListener(
      "click",function(e){
        stopEvent(e);
        return false;
      }
    );
    
    const diffButtons = E('#fileedit-tab-diff-buttons');
    diffButtons.querySelector('button.sbs').addEventListener(
      "click",(e)=>P.diff(true), false
    );
    diffButtons.querySelector('button.unified').addEventListener(
      "click",(e)=>P.diff(false), false
    );
    P.e.btnCommit.addEventListener(
      "click",(e)=>stopEvent(e).commit(), false
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
          if('text'!==name) hide.push(P.e.cbLineNumbersWrap);
          else unhide.push(P.e.cbLineNumbersWrap);
          if('html'!==name) hide.push(P.e.selectHtmlEmsWrap);
          else unhide.push(P.e.selectHtmlEmsWrap);
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
    selectFontSize.addEventListener(
      "change",function(e){
        P.e.taEditor.className = e.target.className.replace(
            /\bfont-size-\d+/g, '' );
        P.e.taEditor.classList.add('font-size-'+e.target.value);
      }, false
    );
    selectFontSize.dispatchEvent(
      // Force UI update
      new Event('change',{target:selectFontSize})
    );

    P.tabs.e.container.insertBefore(
      E('#fossil-status-bar'), P.tabs.e.tabs
    );
  }, false);
  
  /**
     updateVersion() updates filename and version in relevant UI
     elements...

     Returns this object.
  */
  F.page.updateVersion = function(file,rev){
    this.finfo = {file,r:rev};
    const E = (s)=>document.querySelector(s),
          euc = encodeURIComponent,
          rShort = rev.substr(0,16);
    E('#r-label').innerText=rev;
    E('#finfo-link').setAttribute(
      'href',
      F.repoUrl('finfo',{name:file, m:rShort})
    );
    E('#finfo-file-name').innerText=file;
    E('#r-link').setAttribute(
      'href',
      F.repoUrl('info/'+rev)
    );
    E('#r-label').innerText = rev;
    const purlArgs = F.encodeUrlArgs({file, r:rShort});
    const purl = F.repoUrl('fileedit',purlArgs);
    const e = E('#permalink');
    e.innerText='fileedit?'+purlArgs;
    e.setAttribute('href',purl);
    return this;
  };

  /**
     loadFile() loads (file,version) and updates the relevant UI elements
     to reflect the loaded state.

     Returns this object, noting that the load is async.
  */
  F.page.loadFile = function(file,rev){
    delete this.finfo;
    const self = this;
    F.fetch('fileedit_content',{
      urlParams:{file:file,r:rev},
      onload:(r)=>{
        F.message('Loaded content.');
        self.e.taEditor.value = r;
        self.updateVersion(file,rev);
        self.preview();
        self.tabs.switchToTab(self.e.tabs.content);
      }
    });
    return this;
  };

  /**
     Fetches the page preview based on the contents and settings of this
     page's form, and updates this.e.ajaxContentTarget with the preview.

     Returns this object, noting that the operation is async.
  */
  F.page.preview = function(switchToTab){
    if(!this.finfo){
      F.error("No content is loaded.");
      return this;
    }
    const content = this.e.taEditor.value,
          target = this.e.tabs.preview.querySelector(
            '#fileedit-tab-preview-wrapper'
          ),
          self = this;
    const updateView = function(c){
      if(c) target.innerHTML = c;
      else D.clearElement(target);
      F.message('Updated preview.');
      if(switchToTab) self.tabs.switchToTab(self.e.tabs.preview);
    };
    if(!content){
      updateView('');
      return this;
    }
    const fd = new FormData();
    fd.append('render_mode',E('select[name=preview_render_mode]').value);
    fd.append('file',this.finfo.file);
    fd.append('ln',E('[name=preview_ln]').checked ? 1 : 0);
    fd.append('iframe_height', E('[name=preview_html_ems]').value);
    fd.append('content',content);
    target.innerText = "Fetching preview...";
    F.message(
      "Fetching preview..."
    ).fetch('fileedit_preview',{
      payload: fd,
      onload: updateView
    });
    return this;
  };

  /**
     Fetches the page preview based on the contents and settings of this
     page's form, and updates this.e.ajaxContentTarget with the preview.

     Returns this object, noting that the operation is async.
  */
  F.page.diff = function(sbs){
    if(!this.finfo){
      F.error("No content is loaded.");
      return this;
    }
    const content = this.e.taEditor.value,
          target = this.e.tabs.diff.querySelector(
            '#fileedit-tab-diff-wrapper'
          ),
          self = this;
    const fd = new FormData();
    fd.append('file',this.finfo.file);
    fd.append('r', this.finfo.r);
    fd.append('sbs', sbs ? 1 : 0);
    fd.append('content',content);
    F.message(
      "Fetching diff..."
    ).fetch('fileedit_diff',{
      payload: fd,
      onload: function(c){
        target.innerHTML = [
          "<div>Diff <code>[",
          self.finfo.r,
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
  F.page.commit = function f(){
    if(!this.finfo){
      F.error("No content is loaded.");
      return this;
    }
    const self = this;
    const content = this.e.taEditor.value,
          target = document.querySelector('#fileedit-manifest'),
          cbDryRun = E('[name=dry_run]'),
          isDryRun = cbDryRun.checked,
          filename = this.finfo.file;
    if(!f.updateView){
      f.updateView = function(c){
        target.innerHTML = [
          "<h3>Manifest",
          (c.dryRun?" (dry run)":""),
          ": ", c.uuid.substring(0,16),"</h3>",
          "<code class='fileedit-manifest'>",
          c.manifest,
          "</code></pre>"
        ].join('');
        const msg = [
          'Committed',
          c.dryRun ? '(dry run)' : '',
          '[', c.uuid,'].'
        ];
        if(!c.dryRun){
          msg.push('Re-activating dry-run mode.');
          self.e.taComment.value = '';
          cbDryRun.checked = true;
          F.page.updateVersion(filename, c.uuid);
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
    fd.append('file',filename);
    fd.append('r', this.finfo.r);
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
