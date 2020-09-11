(function(F/*the fossil object*/){
  "use strict";
  /**
     Client-side implementation of the /pikchrshow app. Requires that
     the fossil JS bootstrapping is complete and that these fossil JS
     APIs have been installed: fossil.fetch, fossil.dom,
     fossil.copybutton, fossil.popupwidget
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
      uiControls: E('#pikchrshow-controls'),
      btnTogglePreviewMode: D.button("Preview mode"),
      selectMarkupAlignment: D.select()
    };
    D.append(P.e.uiControls, P.e.btnTogglePreviewMode);

    ////////////////////////////////////////////////////////////
    // Setup markup alignment selection...
    D.append(P.e.uiControls, P.e.selectMarkupAlignment);
    D.disable(D.option(P.e.selectMarkupAlignment, '', 'Markup Alignment'));
    ['left', 'center'].forEach(function(val,ndx){
      D.option(P.e.selectMarkupAlignment, ndx ? val : '', val);
    });

    ////////////////////////////////////////////////////////////
    // Setup clipboard-copy of markup/SVG...
    F.copyButton(P.e.btnCopy, {copyFromElement: P.e.taPreviewText});
    P.e.btnCopy.addEventListener('text-copied',function(ev){
       D.flashOnce(ev.target);
    },false);

    ////////////////////////////////////////////////////////////
    // Set up dark mode simulator...
    P.e.cbDarkMode.addEventListener('change', function(ev){
      if(ev.target.checked) D.addClass(P.e.previewTarget, 'dark-mode');
      else D.removeClass(P.e.previewTarget, 'dark-mode');
    }, false);
    if(P.e.cbDarkMode.checked) D.addClass(P.e.previewTarget, 'dark-mode');

    ////////////////////////////////////////////////////////////
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

    ////////////////////////////////////////////////////////////
    // Set up selection list of predefined scripts...
    if(true){
      const selectScript = P.e.selectScript = D.select(),
            cbAutoPreview = P.e.cbAutoPreview =
            D.attr(D.checkbox(),'id', 'cb-auto-preview','checked',true),
            cbWrap = D.addClass(D.div(),'input-with-label')
      ;
      D.append(
        cbWrap,
        selectScript,
        cbAutoPreview,
        D.label(cbAutoPreview,"Auto-preview?"),
        F.helpButtonlets.create(
          D.append(D.div(),
                   'Auto-preview automatically previews selected ',
                   'built-in pikchr scripts by sending them to ',
                   'the server for rendering. Not recommended on a ',
                   'slow connection/server.',
                   D.br(),D.br(),
                   'Pikchr scripts may also be dragged/dropped from ',
                   'the local filesystem into the text area, but ',
                   'the auto-preview option does not apply to them.'
                  )
        )
      ).childNodes.forEach(function(ch){
        ch.style.margin = "0 0.25em";
      });
      D.append(P.e.uiControls, cbWrap);
      P.predefinedPiks.forEach(function(script,ndx){
        const opt = D.option(script.code ? script.code.trim() :'', script.name);
        D.append(selectScript, opt);
        if(!ndx) selectScript.selectedIndex = 0 /*timing/ordering workaround*/;
        if(!script.code) D.disable(opt);
      });
      delete P.predefinedPiks;
      selectScript.addEventListener('change', function(ev){
        const val = ev.target.value;
        if(!val) return;
        P.e.taContent.value = val;
        if(cbAutoPreview.checked) P.preview();
      }, false);
    }
    
    ////////////////////////////////////////////////////////////
    // Move dark mode checkbox to the end and add a help buttonlet
    D.append(
      P.e.uiControls,
      D.append(
        P.e.cbDarkMode.parentNode/*the .input-with-label element*/,
        F.helpButtonlets.create(
          D.span(),
          'Dark mode changes the colors of rendered SVG to ',
          'make them more visible in dark-themed skins. ',
          'This only changes (using CSS) how they are rendered, ',
          'not any actual colors written in the script.'
        )
      )
    );

    ////////////////////////////////////////////////////////////
    // File drag/drop pikchr scripts into P.e.taContent.
    // Adapted from: https://stackoverflow.com/a/58677161
    const dropfile = function(file){
      const reader = new FileReader();
      reader.onload = function(e) {P.e.taContent.value = e.target.result};
      reader.readAsText(file, "UTF-8");
    };
    const dropTarget = P.e.taContent /* document.body does not behave how i'd like */;
    dropTarget.addEventListener('drop', function(ev){
      D.removeClass(dropTarget, 'dragover');
      ev.preventDefault();
      const file = ev.dataTransfer.files[0];
      if(file) dropfile(file);
    }, false);
    dropTarget.addEventListener('dragenter', function(ev){
      ev.stopPropagation();
      ev.preventDefault();
      console.debug("dragenter");
      D.addClass(dropTarget, 'dragover');
    }, false);
    dropTarget.addEventListener('dragleave', function(ev){
      ev.stopPropagation();
      ev.preventDefault();
      console.debug("dragleave");
      D.removeClass(dropTarget, 'dragover');
    }, false);

    ////////////////////////////////////////////////////////////
    // If we start with content, get it in sync with the state
    // generated by P.preview().
    if(P.e.taContent.value/*was pre-filled server-side*/){
      /* Fill our "response" state so that renderPreview() can work */
      P.response.inputText = P.e.taContent.value;
      P.response.raw = P.e.previewTarget.innerHTML;
      P.renderPreview()/*it's already rendered, but this gets all
                         labels/headers in sync.*/;
    }    
  }/*F.onPageLoad()*/);

  /**
     Updates the preview view based on the current preview mode and
     error state.
  */
  P.renderPreview = function f(){
    if(!f.hasOwnProperty('rxNonce')){
      f.rxNonce = /<!--.+-->\r?\n?/g /*pikchr nonce comments*/;
    }
    const preTgt = this.e.previewTarget;
    if(this.response.isError){
      preTgt.innerHTML = this.response.raw;
      D.addClass(preTgt, 'error');
      this.e.previewModeLabel.innerText = "Error";
      return;
    }
    D.removeClass(preTgt, 'error');
    D.enable(this.e.btnTogglePreviewMode);
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

  /**
     Fetches the preview from the server and updates the preview to
     the rendered SVG content or error report.
  */
  P.preview = function fp(){
    if(!fp.hasOwnProperty('toDisable')){
      fp.toDisable = [
        /* input elements to disable during ajax operations */
        this.e.btnSubmit, this.e.taContent,
        this.e.selectMarkupAlignment,
        this.e.cbAutoPreview, this.e.selectScript
        /* this.e.btnTogglePreviewMode is handled separately */
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
    D.disable(fp.toDisable, this.e.btnTogglePreviewMode);
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

  /**
     Predefined scripts. Each entry is an object:

     {
     name: required string,

     code: optional code string. An entry with no code
           is treated like a separator in the resulting
           SELECT element (a disabled OPTION).

     }
  */
  P.predefinedPiks = [
    {name: "--Predefined Scripts--"},
/*
  The following were imported from the pikchr test scripts:

  https://fossil-scm.org/pikchr/dir/tests
*/
{name: "autochop01", code:`C: box "box"

line from C to 3cm heading  00 from C chop;
line from C to 3cm heading  10 from C chop;
line from C to 3cm heading  20 from C chop;
line from C to 3cm heading  30 from C chop;
line from C to 3cm heading  40 from C chop;
line from C to 3cm heading  50 from C chop;
line from C to 3cm heading  60 from C chop;
line from C to 3cm heading  70 from C chop;
line from C to 3cm heading  80 from C chop;
line from C to 3cm heading  90 from C chop;
line from C to 3cm heading 100 from C chop;
line from C to 3cm heading 110 from C chop;
line from C to 3cm heading 120 from C chop;
line from C to 3cm heading 130 from C chop;
line from C to 3cm heading 140 from C chop;
line from C to 3cm heading 150 from C chop;
line from C to 3cm heading 160 from C chop;
line from C to 3cm heading 170 from C chop;
line from C to 3cm heading 180 from C chop;
line from C to 3cm heading 190 from C chop;
line from C to 3cm heading 200 from C chop;
line from C to 3cm heading 210 from C chop;
line from C to 3cm heading 220 from C chop;
line from C to 3cm heading 230 from C chop;
line from C to 3cm heading 240 from C chop;
line from C to 3cm heading 250 from C chop;
line from C to 3cm heading 260 from C chop;
line from C to 3cm heading 270 from C chop;
line from C to 3cm heading 280 from C chop;
line from C to 3cm heading 290 from C chop;
line from C to 3cm heading 300 from C chop;
line from C to 3cm heading 310 from C chop;
line from C to 3cm heading 320 from C chop;
line from C to 3cm heading 330 from C chop;
line from C to 3cm heading 340 from C chop;
line from C to 3cm heading 350 from C chop;
`},
{name: "autochop02", code:`C: box "box" radius 10px

line from C to 3cm heading  00 from C chop;
line from C to 3cm heading  10 from C chop;
line from C to 3cm heading  20 from C chop;
line from C to 3cm heading  30 from C chop;
line from C to 3cm heading  40 from C chop;
line from C to 3cm heading  50 from C chop;
line from C to 3cm heading  60 from C chop;
line from C to 3cm heading  70 from C chop;
line from C to 3cm heading  80 from C chop;
line from C to 3cm heading  90 from C chop;
line from C to 3cm heading 100 from C chop;
line from C to 3cm heading 110 from C chop;
line from C to 3cm heading 120 from C chop;
line from C to 3cm heading 130 from C chop;
line from C to 3cm heading 140 from C chop;
line from C to 3cm heading 150 from C chop;
line from C to 3cm heading 160 from C chop;
line from C to 3cm heading 170 from C chop;
line from C to 3cm heading 180 from C chop;
line from C to 3cm heading 190 from C chop;
line from C to 3cm heading 200 from C chop;
line from C to 3cm heading 210 from C chop;
line from C to 3cm heading 220 from C chop;
line from C to 3cm heading 230 from C chop;
line from C to 3cm heading 240 from C chop;
line from C to 3cm heading 250 from C chop;
line from C to 3cm heading 260 from C chop;
line from C to 3cm heading 270 from C chop;
line from C to 3cm heading 280 from C chop;
line from C to 3cm heading 290 from C chop;
line from C to 3cm heading 300 from C chop;
line from C to 3cm heading 310 from C chop;
line from C to 3cm heading 320 from C chop;
line from C to 3cm heading 330 from C chop;
line from C to 3cm heading 340 from C chop;
line from C to 3cm heading 350 from C chop;
`},
{name: "autochop03", code:`C: circle "circle"

line from C to 3cm heading  00 from C chop;
line from C to 3cm heading  10 from C chop;
line from C to 3cm heading  20 from C chop;
line from C to 3cm heading  30 from C chop;
line from C to 3cm heading  40 from C chop;
line from C to 3cm heading  50 from C chop;
line from C to 3cm heading  60 from C chop;
line from C to 3cm heading  70 from C chop;
line from C to 3cm heading  80 from C chop;
line from C to 3cm heading  90 from C chop;
line from C to 3cm heading 100 from C chop;
line from C to 3cm heading 110 from C chop;
line from C to 3cm heading 120 from C chop;
line from C to 3cm heading 130 from C chop;
line from C to 3cm heading 140 from C chop;
line from C to 3cm heading 150 from C chop;
line from C to 3cm heading 160 from C chop;
line from C to 3cm heading 170 from C chop;
line from C to 3cm heading 180 from C chop;
line from C to 3cm heading 190 from C chop;
line from C to 3cm heading 200 from C chop;
line from C to 3cm heading 210 from C chop;
line from C to 3cm heading 220 from C chop;
line from C to 3cm heading 230 from C chop;
line from C to 3cm heading 240 from C chop;
line from C to 3cm heading 250 from C chop;
line from C to 3cm heading 260 from C chop;
line from C to 3cm heading 270 from C chop;
line from C to 3cm heading 280 from C chop;
line from C to 3cm heading 290 from C chop;
line from C to 3cm heading 300 from C chop;
line from C to 3cm heading 310 from C chop;
line from C to 3cm heading 320 from C chop;
line from C to 3cm heading 330 from C chop;
line from C to 3cm heading 340 from C chop;
line from C to 3cm heading 350 from C chop;
`},
{name: "autochop04", code:`C: ellipse "ellipse"

line from C to 3cm heading  00 from C chop;
line from C to 3cm heading  10 from C chop;
line from C to 3cm heading  20 from C chop;
line from C to 3cm heading  30 from C chop;
line from C to 3cm heading  40 from C chop;
line from C to 3cm heading  50 from C chop;
line from C to 3cm heading  60 from C chop;
line from C to 3cm heading  70 from C chop;
line from C to 3cm heading  80 from C chop;
line from C to 3cm heading  90 from C chop;
line from C to 3cm heading 100 from C chop;
line from C to 3cm heading 110 from C chop;
line from C to 3cm heading 120 from C chop;
line from C to 3cm heading 130 from C chop;
line from C to 3cm heading 140 from C chop;
line from C to 3cm heading 150 from C chop;
line from C to 3cm heading 160 from C chop;
line from C to 3cm heading 170 from C chop;
line from C to 3cm heading 180 from C chop;
line from C to 3cm heading 190 from C chop;
line from C to 3cm heading 200 from C chop;
line from C to 3cm heading 210 from C chop;
line from C to 3cm heading 220 from C chop;
line from C to 3cm heading 230 from C chop;
line from C to 3cm heading 240 from C chop;
line from C to 3cm heading 250 from C chop;
line from C to 3cm heading 260 from C chop;
line from C to 3cm heading 270 from C chop;
line from C to 3cm heading 280 from C chop;
line from C to 3cm heading 290 from C chop;
line from C to 3cm heading 300 from C chop;
line from C to 3cm heading 310 from C chop;
line from C to 3cm heading 320 from C chop;
line from C to 3cm heading 330 from C chop;
line from C to 3cm heading 340 from C chop;
line from C to 3cm heading 350 from C chop;
`},
{name: "autochop05", code:`C: oval "oval"

line from C to 3cm heading  00 from C chop;
line from C to 3cm heading  10 from C chop;
line from C to 3cm heading  20 from C chop;
line from C to 3cm heading  30 from C chop;
line from C to 3cm heading  40 from C chop;
line from C to 3cm heading  50 from C chop;
line from C to 3cm heading  60 from C chop;
line from C to 3cm heading  70 from C chop;
line from C to 3cm heading  80 from C chop;
line from C to 3cm heading  90 from C chop;
line from C to 3cm heading 100 from C chop;
line from C to 3cm heading 110 from C chop;
line from C to 3cm heading 120 from C chop;
line from C to 3cm heading 130 from C chop;
line from C to 3cm heading 140 from C chop;
line from C to 3cm heading 150 from C chop;
line from C to 3cm heading 160 from C chop;
line from C to 3cm heading 170 from C chop;
line from C to 3cm heading 180 from C chop;
line from C to 3cm heading 190 from C chop;
line from C to 3cm heading 200 from C chop;
line from C to 3cm heading 210 from C chop;
line from C to 3cm heading 220 from C chop;
line from C to 3cm heading 230 from C chop;
line from C to 3cm heading 240 from C chop;
line from C to 3cm heading 250 from C chop;
line from C to 3cm heading 260 from C chop;
line from C to 3cm heading 270 from C chop;
line from C to 3cm heading 280 from C chop;
line from C to 3cm heading 290 from C chop;
line from C to 3cm heading 300 from C chop;
line from C to 3cm heading 310 from C chop;
line from C to 3cm heading 320 from C chop;
line from C to 3cm heading 330 from C chop;
line from C to 3cm heading 340 from C chop;
line from C to 3cm heading 350 from C chop;
`},
{name: "autochop06", code:`C: cylinder "cylinder"

line from C to 3cm heading  00 from C chop;
line from C to 3cm heading  10 from C chop;
line from C to 3cm heading  20 from C chop;
line from C to 3cm heading  30 from C chop;
line from C to 3cm heading  40 from C chop;
line from C to 3cm heading  50 from C chop;
line from C to 3cm heading  60 from C chop;
line from C to 3cm heading  70 from C chop;
line from C to 3cm heading  80 from C chop;
line from C to 3cm heading  90 from C chop;
line from C to 3cm heading 100 from C chop;
line from C to 3cm heading 110 from C chop;
line from C to 3cm heading 120 from C chop;
line from C to 3cm heading 130 from C chop;
line from C to 3cm heading 140 from C chop;
line from C to 3cm heading 150 from C chop;
line from C to 3cm heading 160 from C chop;
line from C to 3cm heading 170 from C chop;
line from C to 3cm heading 180 from C chop;
line from C to 3cm heading 190 from C chop;
line from C to 3cm heading 200 from C chop;
line from C to 3cm heading 210 from C chop;
line from C to 3cm heading 220 from C chop;
line from C to 3cm heading 230 from C chop;
line from C to 3cm heading 240 from C chop;
line from C to 3cm heading 250 from C chop;
line from C to 3cm heading 260 from C chop;
line from C to 3cm heading 270 from C chop;
line from C to 3cm heading 280 from C chop;
line from C to 3cm heading 290 from C chop;
line from C to 3cm heading 300 from C chop;
line from C to 3cm heading 310 from C chop;
line from C to 3cm heading 320 from C chop;
line from C to 3cm heading 330 from C chop;
line from C to 3cm heading 340 from C chop;
line from C to 3cm heading 350 from C chop;
`},
{name: "autochop07", code:`C: file "file"

line from C to 3cm heading  00 from C chop;
line from C to 3cm heading  10 from C chop;
line from C to 3cm heading  20 from C chop;
line from C to 3cm heading  30 from C chop;
line from C to 3cm heading  40 from C chop;
line from C to 3cm heading  50 from C chop;
line from C to 3cm heading  60 from C chop;
line from C to 3cm heading  70 from C chop;
line from C to 3cm heading  80 from C chop;
line from C to 3cm heading  90 from C chop;
line from C to 3cm heading 100 from C chop;
line from C to 3cm heading 110 from C chop;
line from C to 3cm heading 120 from C chop;
line from C to 3cm heading 130 from C chop;
line from C to 3cm heading 140 from C chop;
line from C to 3cm heading 150 from C chop;
line from C to 3cm heading 160 from C chop;
line from C to 3cm heading 170 from C chop;
line from C to 3cm heading 180 from C chop;
line from C to 3cm heading 190 from C chop;
line from C to 3cm heading 200 from C chop;
line from C to 3cm heading 210 from C chop;
line from C to 3cm heading 220 from C chop;
line from C to 3cm heading 230 from C chop;
line from C to 3cm heading 240 from C chop;
line from C to 3cm heading 250 from C chop;
line from C to 3cm heading 260 from C chop;
line from C to 3cm heading 270 from C chop;
line from C to 3cm heading 280 from C chop;
line from C to 3cm heading 290 from C chop;
line from C to 3cm heading 300 from C chop;
line from C to 3cm heading 310 from C chop;
line from C to 3cm heading 320 from C chop;
line from C to 3cm heading 330 from C chop;
line from C to 3cm heading 340 from C chop;
line from C to 3cm heading 350 from C chop;
`},
{name: "swimlane", code:`    $laneh = 0.75

    # Draw the lanes
    down
    box width 3.5in height $laneh fill 0xacc9e3
    box same fill 0xc5d8ef
    box same as first box
    box same as 2nd box
    line from 1st box.sw+(0.2,0) up until even with 1st box.n \
      "Alan" above aligned
    line from 2nd box.sw+(0.2,0) up until even with 2nd box.n \
      "Betty" above aligned
    line from 3rd box.sw+(0.2,0) up until even with 3rd box.n \
      "Charlie" above aligned
    line from 4th box.sw+(0.2,0) up until even with 4th box.n \
       "Darlene" above aligned

    # fill in content for the Alice lane
    right
A1: circle rad 0.1in at end of first line + (0.2,-0.2) \
       fill white thickness 1.5px "1" 
    arrow right 50%
    circle same "2"
    arrow right until even with first box.e - (0.65,0.0)
    ellipse "future" fit fill white height 0.2 width 0.5 thickness 1.5px
A3: circle same at A1+(0.8,-0.3) "3" fill 0xc0c0c0
    arrow from A1 to last circle chop "fork!" below aligned

    # content for the Betty lane
B1: circle same as A1 at A1-(0,$laneh) "1"
    arrow right 50%
    circle same "2"
    arrow right until even with first ellipse.w
    ellipse same "future"
B3: circle same at A3-(0,$laneh) "3"
    arrow right 50%
    circle same as A3 "4"
    arrow from B1 to 2nd last circle chop

    # content for the Charlie lane
C1: circle same as A1 at B1-(0,$laneh) "1"
    arrow 50%
    circle same "2"
    arrow right 0.8in "goes" "offline"
C5: circle same as A3 "5"
    arrow right until even with first ellipse.w
    ellipse same "future"
    text "back online" "pushes 5" "pulls 3 &amp; 4" with .n at last arrow

    # content for the Darlene lane
D1: circle same as A1 at C1-(0,$laneh) "1"
    arrow 50%
    circle same "2"
    arrow right until even with C5.w
    circle same "5"
    arrow 50%
    circle same as A3 "6"
    arrow right until even with first ellipse.w
    ellipse same "future"
D3: circle same as B3 at B3-(0,2*$laneh) "3"
    arrow 50%
    circle same "4"
    arrow from D1 to D3 chop
`},
{name: "test01", code:`circle "C0"; arrow; circle "C1"; arrow; circle "C2"; arrow;
    circle "C4";  arrow; circle "C6"
circle "C3" at (C4.x-C2.x) ne of C2; arrow; circle "C5"
arrow from C2.ne to C3.sw
`},
{name: "test02", code:`/* First generate the main graph */
scale = 0.75
Main: [
  circle "C0"
  arrow
  circle "C1"
  arrow
  circle "C2"
  arrow
  circle "C4"
  arrow
  circle "C6"
]
Branch: [
  circle "C3"
  arrow
  circle "C5"
] with .sw at Main.C2.n + (0.35,0.35)
arrow from Main.C2 to Branch.C3 chop

/* Now generate the background colors */
layer = 0
$featurecolor = 0xfedbce
$maincolor = 0xd9fece
$divY = (Branch.y + Main.y)/2
$divH = (Branch.y - Main.y)
box fill $featurecolor color $featurecolor \
    width Branch.width*1.5 height $divH \
    at Branch
box fill $maincolor color $maincolor \
    width Main.width+0.1 height $divH \
    at Main
"main" ljust at 0.1 se of nw of 2nd box
"feature" ljust at 0.1 se of nw of 1st box
`},
{name: "test03", code:`right
B1: box "One"; line
B2: box "Two"; arrow
B3: box "Three"; down; arrow down 50%; circle "Hi!"; left;
spline -> left 2cm then to One.se
Macro: [
  B4: box "four"
  B5: box "five"
  B6: box "six"
] with n at 3cm below s of 2nd box

arrow from s of 2nd box to Macro.B5.n

spline -> from e of last circle right 1cm then down 1cm then to Macro.B4.e

box width Macro.width+0.1 height Macro.height+0.1 at Macro color Red
box width Macro.B5.width+0.05 \
    height Macro.B5.height+0.05 at Macro.B5 color blue
`},
{name: "test04", code:`print 5+8;
print (17.4-5)/(2-2);
`},
{name: "test05", code:`print linewid
linewid=0.8
print linewid
scale=2.0
print scale
print hotpink
print 17 + linewid*1000;
print sin(scale);
print cos(scale)
print sqrt(17)
print min(5,10)
print max(5,10)
print sqrt(-11)
`},
{name: "test06", code:`B1: box "one" width 1 height 1 at 2,2;
B2: box thickness 300% dotted 0.03 "two" at 1,3;
print "B2.n: ",B2.n.x,",",B2.n.y
print "B2.c: ",B2.c.x,",",B2.c.y
print "B2.e: ",B2.e.x,",",B2.e.y
scale = 1
box "three" "four" ljust "five" with .n at 0.1 below B2.s width 50%

#  Text demo: <text x="100" y="100" text-anchor="end" dominant-baseline="central">I love SVG!</text>
`},
{name: "test07", code:`B: box "This is" "<b>" "box \"B\"" color DarkRed

   "B.nw" rjust above at 0.05 left of B.nw
   "B.w" rjust at 0.05 left of B.w
   "B.sw" rjust below at 0.05 left of B.sw;  $abc = DarkBlue
   "B.s" below at B.s
   // C++ style comments allowed.
   "B.se" ljust below at 0.05 right of B.se color DarkBlue
   "B.e" ljust at 0.05 right of B.e  /* C-style comments */
   "B.ne" ljust above at 0.05 right of B.ne
   "B.n" center above at B.n
print "this is a test: " /*comment ignored*/, $abc
print "Colors:", Orange, Black, White, Red, Green, Blue

#   line from B.ne + (0.05,0) right 1.0 then down 2.5 then left 1.5
`},
{name: "test08", code:`     debug = 1;

     box "one" width 80% height 80%
     box "two" width 150% color DarkRed
     arrow "xyz" above
     box "three" height 150% color DarkBlue
     down
     arrow
B4:  box "four"
B45: box "4.5" fill SkyBlue
     move
B5:  box "five"
     left
B6:  box "six"
     up
     box "seven" width 50% height 50%

     line from 0.1 right of B4.e right 1 then down until even with B5 \
         then to B5 rad 0.1 chop

     arrow from B6 left even with 2nd box then up to 2nd box chop rad 0.1
     arrow from 1/2 way between B6.w and B6.sw left until even with first box \
         then up to first box rad 0.1 chop

oval wid 25% ht B4.n.y - B45.s.y at (B6.x,B4.s.y)
arrow from last box to last oval chop
arrow <- from B4.w left until even with last oval.e
arrow <- from B45 left until even with last oval.e chop
`},
{name: "test09", code:`HEAD: circle
      circle radius 50% with .se at HEAD.nw
      circle radius 50% with .sw at HEAD.ne
`},
{name: "test10", code:`C: "+";    $x = 0
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
   $x = $x + 10
   line from 0.5 heading $x from C to 1.0 heading $x from C
`},
{name: "test11", code:`   linerad = 5px
C: circle "Center" rad 150%
   circle "N"  at 1.0 n  of C; arrow from C to last chop ->
   circle "NE" at 1.0 ne of C; arrow from C to last chop <-
   circle "E"  at 1.0 e  of C; arrow from C to last chop <->
   circle "SE" at 1.0 se of C; arrow from C to last chop ->
   circle "S"  at 1.0 s  of C; arrow from C to last chop <-
   circle "SW" at 1.0 sw of C; arrow from C to last chop <->
   circle "W"  at 1.0 w  of C; arrow from C to last chop ->
   circle "NW" at 1.0 nw of C; arrow from C to last chop <-
   arrow from 2nd circle to 3rd circle chop
   arrow from 4th circle to 3rd circle chop
   arrow from SW to S chop <->
   circle "ESE" at 2.0 heading 112.5 from Center \
      thickness 150% fill lightblue radius 75%
   arrow from Center to ESE thickness 150% <-> chop
   arrow from ESE up 1.35 then to NE chop
   line dashed <- from E.e to (ESE.x,E.y)
   line dotted <-> thickness 50% from N to NW chop
`},
{name: "test12", code:`circle "One"
arrow
circle "Two"; down
arrow down 40%
circle "Three"
move
circle "Four"
`},
{name: "test13", code:`// margin = 1
line up 1 right 2
linewid = 0
arrow left 2
move left 0.1
line <-> down 1
"height " rjust at last line
`},
{name: "test14", code:`print "1in=",1in
print "1cm=",1cm
print "1mm=",1mm
print "1pt=",1pt
print "1px=",1px
print "1pc=",1pc
scale = 0.25
circle "in" radius 1in
circle "cm" radius 2.54cm
circle "mm" radius 25.4mm
circle "pt" radius 72pt
circle "px" radius 96px
circle "pc" radius 6pc

circle "in" radius 0.5*1in with .n at s of 1st circle
circle "cm" radius 0.5*2.54cm
circle "mm" radius 25.4mm * 0.5
circle "pt" radius 72pt * 0.5
circle "px" radius 96px * 0.5
circle "pc" radius 6pc  * 0.5
`},
{name: "test15", code:`ellipse "document"
arrow
box "PIC"
arrow
box "TBL/EQN" "(optional)" dashed
arrow
box "TROFF"
arrow
ellipse "typesetter"
`},
{name: "test16", code:`box "this is" "a box"
`},
{name: "test17", code:`line "this is" "a line"
`},
{name: "test18", code:`box width 3 height 0.1; circle radius 0.1
`},
{name: "test19", code:`line up right; line down; line down left; line up
`},
{name: "test20", code:`box dotted; line dotted; move; line dashed; box dashed
`},
{name: "test21", code:`line right 5 dashed; move left 5 down .25; right
line right 5 dashed 0.25; move left 5 down .25; right
line right 5 dashed 0.5; move left 5 down .25; right
line right 5 dashed 1;
`},
{name: "test22", code:`box invisible "input"; arrow; box invisible "output"
`},
{name: "test23b", code:`margin = 24pt;
linewid *= 1.75
charht = 0.14
//thickness *= 8
print charht, thickness
arrow "on top of"; move
arrow "above" "below"; move
arrow "above" above; move
arrow "below" below; move
arrow "above" "on top of" "below"

move to start of first arrow down 1in;
right
arrow "way a||bove" "ab||ove" "on t||he line" "below" "way below"
move; arrow "way above" "above" "below" "way below"
move; arrow "way above" above "above" above
move; arrow "below" below "way below" below
move; arrow "above-1" above "above-2" above "floating"

move to start of first arrow down 2in;
right
arrow "above-1" above "above-2" above "below" below
move; arrow "below-1" below "below-2" below "floating"
move; arrow "below-1" below "below-2" below "above" above

move to start of first arrow down 3in;
right
box "first line" "second line" "third line" "fourth  line" "fifth line" fit
move
box "first line" "second line" "third line" "fourth  line" fit
move
box "first line" "second line" "third line" fit
move
box "first line" "second line" fit
move
box "first line" fit

move to start of first arrow down 4in;
right
box "first above" above "second above" above "third" fit
dot color red at last box
`},
{name: "test23c", code:`linewid *= 2
arrow "Big" big "Small" small thin
box invis "thin" "line" fit
move
arrow "Big Big" big big "Small Small" small small thick
box invis "thick" "line" fit
box thick "Thick" with .nw at .5 below start of 1st arrow
move
box thick thick "Thick" "Thick"
move
box thin "Thin"
move
box thin thin "Thin" "Thin"
`},
{name: "test23", code:`margin = 24pt;
linewid *= 1.75
arrow "on top of"; move
arrow "above" "below"; move
arrow "above" above; move
arrow "below" below; move
arrow "above" "on top of" "below"

move to start of first arrow down 1in;
right
arrow "way above" "above" "on the line" "below" "way below"
move; arrow "way above" "above" "below" "way below"

move to start of first arrow down 2in;
right
box "first line" "second line" "third line" "fourth  line" "fifth line" fit
move
box "first line" "second line" "third line" "fourth  line" fit
move
box "first line" "second line" "third line" fit
move
box "first line" "second line" fit
move
box "first line" fit

move to start of first arrow down 3in;
right
box "first above" above "second above" above "third" fit
dot color red at last box
`},
{name: "test24", code:`arrow left; box; arrow; circle; arrow
`},
{name: "test25", code:`arrow; circle; down; arrow
`},
{name: "test26", code:`line from 0,0  right 1 then up 1 then right 1 then down 1.5 \
   then left 0.6 dashed;
spline from 0,0  right 1 then up 1 then right 1 then down 1.5 \
   then left 0.6
`},
{name: "test27", code:`line dotted right 1 then down 0.5 left 1 then right 1
spline from start of first line color red \
  right 1 then down 0.5 left 1 then right 1 <->
spline from start of first line color blue radius 0.2 \
  right 1 then down 0.5 left 1 then right 1 <->

move down 1 from start of first line
line radius 0.3 \
  right 1 then down 0.5 left 1 then right 1 <->
`},
{name: "test28", code:`box "Box"
arrow
cylinder "One"
down
arrow
ellipse "Ellipse"
up
arrow from One.n
circle "Circle"
right
arrow from One.e <-
circle "E" radius 50%
circle "NE" radius 50% at 0.5 ne of One.ne
arrow from NE.sw to One.ne 
circle "SE" radius 50% at 0.5 se of One.se
arrow from SE.nw to One.se

spline from One.sw left 0.2 down 0.2 then to Box.se ->
spline from Circle.w left 0.3 then left 0.2 down 0.2 then to One.nw ->
`},
{name: "test29", code:`# Demonstrate the ability to close and fill a line with the "fill"
# attribute - treating it as a polygon.
#
line right 1 then down 0.25 then up .5 right 0.5 \
   then up .5 left 0.5 then down 0.25 then left 1 close fill blue
move
box "Box to right" "of the" "polygon"
move
line "not a" "polygon" right 1in fill red
move to w of 1st line then down 3cm
line same as 1st line
`},
{name: "test30", code:`debug = 1
down; box ht 0.2 wid 1.5; move down 0.15; box same; move same; box same
`},
{name: "test31", code:`leftmargin = 1cm
box "1"
[ box "2"; arrow "3" above; box "4" ] with .n at last box.s - (0,0.1)
"Thing 2: "rjust at last [].w

dot at last box.s color red
dot at last [].n color blue
`},
{name: "test32", code:`spline right then up then left then down ->
move right 2cm
spline right then up left then down ->
move right 2cm
dot; dot; dot;

dot rad 150% color red at 1st vertex of 1st spline
dot same color orange at 2nd vertex of 1st spline
dot same color green at 3rd vertex of 1st spline
dot same color blue at 4th vertex of 1st spline

dot same color red at 1st vertex of 2nd spline
dot same color green at 2nd vertex of 2nd spline
dot same color blue at 3rd vertex of 2nd spline

print 2nd vertex of 1st spline.x, 2nd vertex of 1st spline.y
`},
{name: "test33", code:`margin = 1cm
"+" at 0,0
arc -> from 0.5,0 to 0,0.5
arc -> cw from 0,0 to 1,0.5
`},
{name: "test34", code:`line; arc; arc cw; arrow
`},
{name: "test35", code:`A: ellipse
   ellipse ht .2 wid .3 with .se at 1st ellipse.nw
   ellipse ht .2 wid .3 with .sw at 1st ellipse.ne
   circle rad .05 at 0.5 between A.nw and A.c
   circle rad .05 at 0.5 between A.ne and A.c
   arc from 0.25 between A.w and A.e to 0.75 between A.w and A.e

// dot color red at A.nw
// dot color orange at A.c
// dot color purple at A.ne
`},
{name: "test36", code:`h = .5;  dh = .02;  dw = .1
[
    Ptr: [
        boxht = h; boxwid = dw
     A: box
     B: box
     C: box
        box wid 2*boxwid "..."
     D: box
    ]
  Block: [
        boxht = 2*dw;
        boxwid = 2*dw
        movewid = 2*dh
     A: box; move
     B: box; move
     C: box; move
        box invis "..." wid 2*boxwid; move
     D: box
  ] with .n at Ptr.s - (0,h/2)
  arrow from Ptr.A to Block.A.nw + (dh,0)
  arrow from Ptr.B to Block.B.nw + (dh,0)
  arrow from Ptr.C to Block.C.nw + (dh,0)
  arrow from Ptr.D to Block.D.nw + (dh,0)
]
box dashed ht last [].ht+dw wid last [].wid+dw at last []
`},
{name: "test37", code:`# Change from the original:
#    * Expand the macro by hand, as Pikchr does not support
#      macros
# Need fixing:
#    * "bottom of last box"
#    * ".t"
#
#define ndblock {
#  box wid boxwid/2 ht boxht/2
#  down;  box same with .t at bottom of last box;   box same
#}
boxht = .2; boxwid = .3; circlerad = .3; dx = 0.05
down; box; box; box; box ht 3*boxht "." "." "."
L: box; box; box invis wid 2*boxwid "hashtab:" with .e at 1st box .w
right
Start: box wid .5 with .sw at 1st box.ne + (.4,.2) "..."
N1: box wid .2 "n1";  D1: box wid .3 "d1"
N3: box wid .4 "n3";  D3: box wid .3 "d3"
box wid .4 "..."
N2: box wid .5 "n2";  D2: box wid .2 "d2"
arrow right from 2nd box
#ndblock
  box wid boxwid/2 ht boxht/2
  down;  box same with .t at bottom of last box;   box same
spline -> right .2 from 3rd last box then to N1.sw + (dx,0)
spline -> right .3 from 2nd last box then to D1.sw + (dx,0)
arrow right from last box
#ndblock
  box wid boxwid/2 ht boxht/2
  down;  box same with .t at bottom of last box;   box same
spline -> right .2 from 3rd last box to N2.sw-(dx,.2) to N2.sw+(dx,0)
spline -> right .3 from 2nd last box to D2.sw-(dx,.2) to D2.sw+(dx,0)
arrow right 2*linewid from L
#ndblock
  box wid boxwid/2 ht boxht/2
  down;  box same with .t at bottom of last box;   box same
spline -> right .2 from 3rd last box to N3.sw + (dx,0)
spline -> right .3 from 2nd last box to D3.sw + (dx,0)
circlerad = .3
circle invis "ndblock"  at last box.e + (1.2,.2)
arrow dashed from last circle.w to last box chop 0 chop .3
box invis wid 2*boxwid "ndtable:" with .e at Start.w
`},
{name: "test38b", code:`# Need fixing:
#
#    *  ".bot" as an abbreviation for ".bottom"
#    *  "up from top of LA"
#
        arrow "source" "code"
LA:     box "lexical" "analyzer"
        arrow "tokens" above
P:      box "parser"
        arrow "intermediate" "code" wid 180%
Sem:    box "semantic" "checker"
        arrow
        arrow <-> up from top of LA
LC:     box "lexical" "corrector"
        arrow <-> up from top of P
Syn:    box "syntactic" "corrector"
        arrow up
DMP:    box "diagnostic" "message" "printer"
        arrow <-> right  from east of DMP
ST:     box "symbol" "table"
        arrow from LC.ne to DMP.sw
        arrow from Sem.nw to DMP.se
        arrow <-> from Sem.top to ST.bot
`},
{name: "test38", code:`# Need fixing:
#
#    *  ".bot" as an abbreviation for ".bottom"
#    *  "up from top of LA"
#
        arrow "source" "code"
LA:     box "lexical" "analyzer"
        arrow "tokens" above
P:      box "parser"
        arrow "intermediate" "code"
Sem:    box "semantic" "checker"
        arrow
        arrow <-> up from top of LA
LC:     box "lexical" "corrector"
        arrow <-> up from top of P
Syn:    box "syntactic" "corrector"
        arrow up
DMP:    box "diagnostic" "message" "printer"
        arrow <-> right  from east of DMP
ST:     box "symbol" "table"
        arrow from LC.ne to DMP.sw
        arrow from Sem.nw to DMP.se
        arrow <-> from Sem.top to ST.bot
`},
{name: "test39b", code:`textwid = 1mm
        circle "DISK"
        arrow "character" "defns" right 150%
CPU:    box "CPU" "(16-bit mini)"
        arrow <- from top of CPU up "input " rjust
        arrow right from right of CPU
CRT:    "   CRT" ljust
        line from CRT - 0,0.075 up 0.15 \
                then right 0.5 \
                then right 0.5 up 0.25 \
                then down 0.5+0.15 \
                then left 0.5 up 0.25 \
                then left 0.5
Paper:  CRT + 1.05,0.75
        arrow <- from Paper down 1.5
        " ...  paper" ljust at end of last arrow + 0, 0.25
        circle rad 0.05 at Paper + (-0.055, -0.25)
        circle rad 0.05 at Paper + (0.055, -0.25)
        box invis wid 120% "   rollers" ljust at Paper + (0.1, -0.25)
`},
{name: "test39", code:`        margin = 5mm
      # ^^^^^^^^^^^^-- extra margin required for text
        circle "DISK"
        arrow "character" "defns" right 150%
                                # ^^^^^^^^^^---- added for spacing
CPU:    box "CPU" "(16-bit mini)"
        /*{*/ arrow <- from top of CPU up "input " rjust /*}*/
      # ^^^^^--- removed                     remove -----^^^^^
        arrow right from CPU.e
           #  ^^^^^^^^^^^^^^^^--- added to compensate for missing {...} above
CRT:    "   CRT" ljust wid 1px
                     # ^^^^^^^--- added to avoid excess spacing
        line from CRT - 0,0.075 up 0.15 \
                then right 0.5 \
                then right 0.5 up 0.25 \
                then down 0.5+0.15 \
                then left 0.5 up 0.25 \
                then left 0.5
Paper:  CRT + 1.05,0.75
        arrow <- from Paper down 1.5
        " ...  paper" ljust at end of last arrow + 0, 0.25
        circle rad 0.05 at Paper + (-0.055, -0.25)
        circle rad 0.05 at Paper + (0.055, -0.25)
        "   rollers" ljust at Paper + (0.1, -0.25)
`},
{name: "test40", code:`$one = 1.0
$one += 2.0
$two = $one
$two *= 3.0
print $one, $two
$three -= 11
$three /= 2
print $three
`},
{name: "test41", code:`# Corner test

box "C"

$d = 1in
circle rad 50% "N" at $d n of C; arrow from last circle to C.n chop
circle same "NE" at $d ne of C; arrow from last circle to C.ne chop
circle same "E" at $d e of C; arrow from last circle to C.e chop
circle same "SE" at $d se of C; arrow from last circle to C.se chop
circle same "S" at $d s of C; arrow from last circle to C.s chop
circle same "SW" at $d sw of C; arrow from last circle to C.sw chop
circle same "W" at $d w of C; arrow from last circle to C.w chop
circle same "NW" at $d nw of C; arrow from last circle to C.nw chop

box "C" at 3*$d east of C radius 15px

circle rad 50% "N" at $d n of C; arrow from last circle to C.n chop
circle same "NE" at $d ne of C; arrow from last circle to C.ne chop
circle same "E" at $d e of C; arrow from last circle to C.e chop
circle same "SE" at $d se of C; arrow from last circle to C.se chop
circle same "S" at $d s of C; arrow from last circle to C.s chop
circle same "SW" at $d sw of C; arrow from last circle to C.sw chop
circle same "W" at $d w of C; arrow from last circle to C.w chop
circle same "NW" at $d nw of C; arrow from last circle to C.nw chop

ellipse "C" at 2.5*$d south of 1st box

circle rad 50% "N" at $d n of C; arrow from last circle to C.n chop
circle same "NE" at $d ne of C; arrow from last circle to C.ne chop
circle same "E" at $d e of C; arrow from last circle to C.e chop
circle same "SE" at $d se of C; arrow from last circle to C.se chop
circle same "S" at $d s of C; arrow from last circle to C.s chop
circle same "SW" at $d sw of C; arrow from last circle to C.sw chop
circle same "W" at $d w of C; arrow from last circle to C.w chop
circle same "NW" at $d nw of C; arrow from last circle to C.nw chop

circle "C" at 3*$d east of last ellipse

circle rad 50% "N" at $d n of C; arrow from last circle to C.n chop
circle same "NE" at $d ne of C; arrow from last circle to C.ne chop
circle same "E" at $d e of C; arrow from last circle to C.e chop
circle same "SE" at $d se of C; arrow from last circle to C.se chop
circle same "S" at $d s of C; arrow from last circle to C.s chop
circle same "SW" at $d sw of C; arrow from last circle to C.sw chop
circle same "W" at $d w of C; arrow from last circle to C.w chop
circle same "NW" at $d nw of C; arrow from last circle to C.nw chop

cylinder "C" at 2.5*$d south of last ellipse
circle rad 50% "N" at $d n of C; arrow from last circle to C.n chop
circle same "NE" at $d ne of C; arrow from last circle to C.ne chop
circle same "E" at $d e of C; arrow from last circle to C.e chop
circle same "SE" at $d se of C; arrow from last circle to C.se chop
circle same "S" at $d s of C; arrow from last circle to C.s chop
circle same "SW" at $d sw of C; arrow from last circle to C.sw chop
circle same "W" at $d w of C; arrow from last circle to C.w chop
circle same "NW" at $d nw of C; arrow from last circle to C.nw chop

oval "C" at 3*$d east of last cylinder
circle rad 50% "N" at $d n of C; arrow from last circle to C.n chop
circle same "NE" at $d ne of C; arrow from last circle to C.ne chop
circle same "E" at $d e of C; arrow from last circle to C.e chop
circle same "SE" at $d se of C; arrow from last circle to C.se chop
circle same "S" at $d s of C; arrow from last circle to C.s chop
circle same "SW" at $d sw of C; arrow from last circle to C.sw chop
circle same "W" at $d w of C; arrow from last circle to C.w chop
circle same "NW" at $d nw of C; arrow from last circle to C.nw chop
`},
{name: "test42", code:`C: ellipse "ellipse"

line from C to 2cm heading  00 from C chop;
line from C to 2cm heading  10 from C chop;
line from C to 2cm heading  20 from C chop;
line from C to 2cm heading  30 from C chop;
line from C to 2cm heading  40 from C chop;
line from C to 2cm heading  50 from C chop;
line from C to 2cm heading  60 from C chop;
line from C to 2cm heading  70 from C chop;
line from C to 2cm heading  80 from C chop;
line from C to 2cm heading  90 from C chop;
line from C to 2cm heading 100 from C chop;
line from C to 2cm heading 110 from C chop;
line from C to 2cm heading 120 from C chop;
line from C to 2cm heading 130 from C chop;
line from C to 2cm heading 140 from C chop;
line from C to 2cm heading 150 from C chop;
line from C to 2cm heading 160 from C chop;
line from C to 2cm heading 170 from C chop;
line from C to 2cm heading 180 from C chop;
line from C to 2cm heading 190 from C chop;
line from C to 2cm heading 200 from C chop;
line from C to 2cm heading 210 from C chop;
line from C to 2cm heading 220 from C chop;
line from C to 2cm heading 230 from C chop;
line from C to 2cm heading 240 from C chop;
line from C to 2cm heading 250 from C chop;
line from C to 2cm heading 260 from C chop;
line from C to 2cm heading 270 from C chop;
line from C to 2cm heading 280 from C chop;
line from C to 2cm heading 290 from C chop;
line from C to 2cm heading 300 from C chop;
line from C to 2cm heading 310 from C chop;
line from C to 2cm heading 320 from C chop;
line from C to 2cm heading 330 from C chop;
line from C to 2cm heading 340 from C chop;
line from C to 2cm heading 350 from C chop;
`},
{name: "test43", code:`scale = 0.75
box "One"
arrow right 200% "Bold" bold above "Italic" italic below
circle "Two"
circle "Bold-Italic" bold italic aligned fit \
   at 4cm heading 143 from Two
arrow from Two to last circle "above" aligned above "below" aligned below chop
circle "C2" fit at 4cm heading 50 from Two
arrow from Two to last circle "above" aligned above "below "aligned below chop
circle "C3" fit at 4cm heading 200 from Two
arrow from last circle to Two <- \
  "above-rjust" aligned rjust above \
  "below-rjust" aligned rjust below chop
`},
{name: "test44", code:`debug=1
file "*.md" rad 20%
arrow
box rad 10px "Markdown" "Interpreter"
arrow right 120% "HTML" above
file " HTML "
`}

  ];
  
})(window.fossil);
