(function(){
  "use strict";
  /**
     Code for the /filepage app. Requires that the fossil JS bootstrappin
     is complete and fossil.fetch() has been installed.
  */

  const E = (s)=>document.querySelector(s);
        

  window.addEventListener("load", function() {
    const P = fossil.page;
    P.e = {
      editor: E('#fileedit-content'),
      ajaxContentTarget: E('#ajax-target'),
      form: E('#fileedit-form'),
      btnPreview: E("#fileedit-btn-preview"),
      btnDiffSbs: E("#fileedit-btn-diffsbs"),
      btnDiffU: E("#fileedit-btn-diffu"),
      btnCommit: E("#fileedit-btn-commit")
    };
    const stopEvent = function(e){
      e.preventDefault();
      e.stopPropagation();
      return P;
    };
      
    P.e.form.addEventListener("submit", function(e) {
      e.target.checkValidity();
      stopEvent(e);
    }, false);
    P.e.btnPreview.addEventListener(
      "click",(e)=>stopEvent(e).preview(),false
    );
    P.e.btnDiffSbs.addEventListener(
      "click",(e)=>stopEvent(e).diff(true),false
    );
    P.e.btnDiffU.addEventListener(
      "click",(e)=>stopEvent(e).diff(false), false
    );
  }, false);


  
  /**
     updateVersion() updates filename and version in relevant UI
     elements...

     Returns this object.
  */
  fossil.page.updateVersion = function(file,rev){
    this.finfo = {file,r:rev};
    const E = (s)=>document.querySelector(s),
          euc = encodeURIComponent;
    E('#r-label').innerText=rev;
    E('#finfo-link').setAttribute(
      'href',
      fossil.rootPath+'finfo?name='+euc(file)+'&m='+rev
    );
    E('#finfo-file-name').innerText=file;
    E('#r-link').setAttribute(
      'href',
      fossil.rootPath+'/info/'+rev
    );
    E('#r-label').innerText = rev;
    const purl = fossil.rootPath+'fileedit?file='+euc(file)+
          '&r='+rev;
    var e = E('#permalink');
    e.innerText=purl;
    e.setAttribute('href',purl);
    return this;
  };

  /**
     loadFile() loads (file,version) and updates the relevant UI elements
     to reflect the loaded state.

     Returns this object, noting that the load is async.
  */
  fossil.page.loadFile = function(file,rev){
    delete this.finfo;
    fossil.fetch('fileedit_content',{
      urlParams:{file:file,r:rev},
      onload:(r)=>{
        document.getElementById('fileedit-content').value=r;
        fossil.message('Loaded content.');
        fossil.page.updateVersion(file,rev);
      }
    });
    return this;
  };

  /**
     Fetches the page preview based on the contents and settings of this
     page's form, and updates this.e.ajaxContentTarget with the preview.

     Returns this object, noting that the operation is async.
  */
  fossil.page.preview = function(){
    if(!this.finfo){
      fossil.error("No content is loaded.");
      return this;
    }
    const content = this.e.editor.value,
          target = this.e.ajaxContentTarget;
    const updateView = function(c){
      target.innerHTML = [
        "<div class='fileedit-preview'>",
        "<div>Preview</div>",
        c||'',
        "</div><!--.fileedit-diff-->"
      ].join('');
      fossil.message('Updated preview.');
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
    fossil.message(
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
  fossil.page.diff = function(sbs){
    if(!this.finfo){
      fossil.error("No content is loaded.");
      return this;
    }
    const self = this;
    const content = this.e.editor.value,
          target = this.e.ajaxContentTarget;
    const updateView = function(c){
      target.innerHTML = [
        "<div class='fileedit-diff'>",
        "<div>Diff <code>[",
        self.finfo.r,
        "]</code> &rarr; Local Edits</div>",
        c||'',
        "</div><!--.fileedit-diff-->"
      ].join('');
      fossil.message('Updated diff.');
    };
    if(!content){
      updateView('');
      return this;
    }
    const fd = new FormData();
    fd.append('file',this.finfo.file);
    fd.append('r', this.finfo.r);
    fd.append('sbs', sbs ? 1 : 0);
    fd.append('content',content);
    fossil.message(
      "Fetching diff..."
    ).fetch('fileedit_diff',{
      payload: fd,
      onload: updateView
    });
    return this;
  };

})();
