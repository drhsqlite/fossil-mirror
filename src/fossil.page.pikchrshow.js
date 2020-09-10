(function(F/*the fossil object*/){
  "use strict";
  /**
     Client-side implementation of the /pikchrshow app. Requires that
     the fossil JS bootstrapping is complete and that these fossil
     JS APIs have been installed: fossil.fetch, fossil.dom
  */
  const E = (s)=>document.querySelector(s),
        D = F.dom,
        P = F.page;
  F.onPageLoad(function() {
    document.body.classList.add('pikchrshow');
    P.e = { /* various DOM elements we work with... */
      previewTarget: E('#pikchrshow-output'),
      btnSubmit: E('#pikchr-submit-preview'),
      cbDarkMode: E('#flipcolors-wrapper > input[type=checkbox]'),
      taContent: E('#content')
    };

    P.e.cbDarkMode.addEventListener('change', function(ev){
      if(ev.target.checked) D.addClass(P.e.previewTarget, 'dark-mode');
      else D.removeClass(P.e.previewTarget, 'dark-mode');
    }, false);
    if(P.e.cbDarkMode.checked) D.addClass(P.e.previewTarget, 'dark-mode');

    P.e.btnSubmit.addEventListener('click', function(){
      P.preview();
    }, false);
  }/*F.onPageLoad()*/);

  P.preview = function fp(){
    if(!fp.hasOwnProperty('toDisable')){
      fp.toDisable = [
        P.e.btnSubmit, P.e.taContent
      ];
      fp.target = P.e.previewTarget;
      fp.updateView = function(c,isError){
        D.enable(fp.toDisable);
        fp.target.innerHTML = c || '';
        if(isError) D.addClass(fp.target, 'error');
        else D.removeClass(fp.target, 'error');
      };
    }
    D.disable(fp.toDisable);
    const content = this.e.taContent.value;
    if(!content){
      fp.updateView("No pikchr content!",true);
      return this;
    }
    const self = this;
    const fd = new FormData();
    fd.append('ajax', true);
    fd.append('content',content);
    F.message(
      "Fetching preview..."
    ).fetch('pikchrshow',{
      payload: fd,
      responseHeaders: 'x-pikchrshow-is-error',
      onload: (r,header)=>{
        fp.updateView(r,+header ? true : false);
        F.message('Updated preview.');
      },
      onerror: (e)=>{
        F.fetch.onerror(e);
        fp.updateView("Error fetching preview: "+e, true);
      }
    });
    return this;
  }/*preview()*/;

})(window.fossil);
