(function(F/*the fossil object*/){
  "use strict";
  /**
     Client-side implementation of the /pikchrshow app. Requires that
     the fossil JS bootstrapping is complete and that these fossil JS
     APIs have been installed: fossil.fetch, fossil.dom,
     fossil.copybutton
  */
  const E = (s)=>document.querySelector(s),
        D = F.dom,
        P = F.page;

  P.previewMode = 0 /*0==rendered SVG, 1==pikchr text markdown,
                      2==pikchr text fossil, 3==raw SVG. */
  P.response = {/*stashed state for the server's preview response*/
    isError: false,
    inputText: undefined /* value of the editor field at render-time */,
    raw: undefined /* raw response text/HTML from server */
  };
  F.onPageLoad(function() {
    document.body.classList.add('pikchrshow');
    P.e = { /* various DOM elements we work with... */
      previewTarget: E('#pikchrshow-output'),
      previewModeLabel: E('#pikchrshow-output-wrapper > legend'),
      btnCopy: E('#pikchrshow-output-wrapper > legend > .copy-button'),
      btnSubmit: E('#pikchr-submit-preview'),
      cbDarkMode: E('#flipcolors-wrapper > input[type=checkbox]'),
      taContent: E('#content'),
      taPreviewText: D.attr(D.textarea(), 'rows', 20, 'cols', 60,
                            'readonly', true),
      divControls: E('#pikchrshow-controls'),
      btnTogglePreviewMode: D.button("Preview mode"),
      selectMarkupAlignment: D.select()
    };
    D.append(P.e.divControls, P.e.btnTogglePreviewMode);

    // Setup markup alignment selection...
    D.append(P.e.divControls, P.e.selectMarkupAlignment);
    D.disable(D.option(P.e.selectMarkupAlignment, '', 'Markup Alignment'));
    ['left', 'center'].forEach(function(val,ndx){
      D.option(P.e.selectMarkupAlignment, ndx ? val : '', val);
    });

    // Setup clipboard-copy of markup/SVG...
    F.copyButton(P.e.btnCopy, {copyFromElement: P.e.taPreviewText});
    P.e.btnCopy.addEventListener('text-copied',function(ev){
       D.flashOnce(ev.target);
    },false);

    // Set up dark mode simulator...
    P.e.cbDarkMode.addEventListener('change', function(ev){
      if(ev.target.checked) D.addClass(P.e.previewTarget, 'dark-mode');
      else D.removeClass(P.e.previewTarget, 'dark-mode');
    }, false);
    if(P.e.cbDarkMode.checked) D.addClass(P.e.previewTarget, 'dark-mode');

    // Set up preview update and preview mode toggle...
    P.e.btnSubmit.addEventListener('click', ()=>P.preview(), false);
    P.e.btnTogglePreviewMode.addEventListener('click', function(){
      /* Rotate through the 4 available preview modes */
      P.previewMode = ++P.previewMode % 4;
      P.renderPreview();
    }, false);
    P.e.selectMarkupAlignment.addEventListener('change', function(ev){
      /* Update markdown/fossil wiki preview if it's active */
      if(P.previewMode==1 || P.previewMode==2){
        P.renderPreview();
      }
    }, false);

    if(P.e.taContent.value/*was pre-filled server-side*/){
      /* Fill our "response" state so that renderPreview() can work */
      P.response.inputText = P.e.taContent.value;
      P.response.raw = P.e.previewTarget.innerHTML;
      P.renderPreview()/*not strictly necessary, but gets all
                         labels/headers in alignment.*/;
    }    
  }/*F.onPageLoad()*/);

  /**
     Updates the preview view based on the current preview mode and
     error state.
  */
  P.renderPreview = function f(){
    if(!f.hasOwnProperty('rxNonce')){
      f.rxNonce = /<!--.+-->\r?\n?/g /*nonce comments*/;
    }
    const preTgt = this.e.previewTarget;
    if(this.response.isError){
      preTgt.innerHTML = this.response.raw;
      D.addClass(preTgt, 'error');
      this.e.previewModeLabel.innerText = "Error";
      return;
    }
    D.removeClass(preTgt, 'error');
    D.removeClass(this.e.btnTogglePreviewMode, 'hidden');
    let label;
    switch(this.previewMode){
    case 0:
      label = "Rendered SVG";
      preTgt.innerHTML = this.response.raw;
      this.e.taPreviewText.value = this.response.raw.replace(f.rxNonce, '')/*for copy button*/;
      break;
    case 1:
      label = "Markdown";
      this.e.taPreviewText.value = [
        '```pikchr'+(this.e.selectMarkupAlignment.value
                     ? ' '+this.e.selectMarkupAlignment.value : ''),
        this.response.inputText, '```'
      ].join('\n');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    case 2:
      label = "Fossil wiki";
      this.e.taPreviewText.value = [
        '<verbatim type="pikchr',
        this.e.selectMarkupAlignment.value ? ' '+this.e.selectMarkupAlignment.value : '',
        '">', this.response.inputText, '</verbatim>'
      ].join('');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    case 3:
      label = "Raw SVG";
      this.e.taPreviewText.value = this.response.raw.replace(f.rxNonce, '');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    }
    D.append(D.clearElement(this.e.previewModeLabel),
             label, this.e.btnCopy);
  };

  /** Fetches the preview from the server and updates the preview to
      the rendered SVG content or error report. */
  P.preview = function fp(){
    if(!fp.hasOwnProperty('toDisable')){
      fp.toDisable = [ /* elements to disable during ajax operations */
        this.e.btnSubmit, this.e.taContent,
        this.e.btnTogglePreviewMode, this.e.selectMarkupAlignment,
      ];
      fp.target = this.e.previewTarget;
      fp.updateView = function(c,isError){
        P.previewMode = 0;
        P.response.raw = c;
        P.response.isError = isError;
        D.enable(fp.toDisable);
        P.renderPreview();
      };
    }
    D.disable(fp.toDisable);
    D.addClass(this.e.btnTogglePreviewMode, 'hidden');
    const content = this.e.taContent.value.trim();
    this.response.raw = undefined;
    this.response.inputText = content;
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
