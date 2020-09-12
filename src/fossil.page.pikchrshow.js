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
      previewLegend: E('#pikchrshow-output-wrapper > legend'),
      previewModeLabel: D.span(/*holds the text for the preview mode label*/),
      previewCopyButton: D.addClass(D.span(), 'copy-button'),
      btnSubmit: E('#pikchr-submit-preview'),
      cbDarkMode: E('#flipcolors-wrapper > input[type=checkbox]'),
      taContent: E('#content'),
      taPreviewText: D.attr(D.textarea(), 'rows', 20, 'cols', 60,
                            'readonly', true),
      uiControls: E('#pikchrshow-controls'),
      previewModeToggle: D.button("Preview mode"),
      markupAlignCenter: D.attr(D.checkbox(), 'id','markup-align-center'),
      markupAlignWrapper: D.span()//D.addClass(D.span(), 'input-with-label')
    };

    ////////////////////////////////////////////////////////////
    // Setup markup alignment selection...
    P.e.markupAlignCenter.addEventListener('change', function(ev){
      /* Update markdown/fossil wiki preview if it's active */
      if(P.previewMode==1 || P.previewMode==2){
        P.renderPreview();
      }
    }, false);
    D.append(P.e.markupAlignWrapper,
             P.e.markupAlignCenter,
             D.label(P.e.markupAlignCenter, "Align center?"));

    ////////////////////////////////////////////////////////////
    // Setup the preview fieldset's LEGEND element...
    D.append( P.e.previewLegend,
              P.e.previewModeToggle,
              '\u00a0',
              P.e.previewCopyButton,
              P.e.previewModeLabel,
              P.e.markupAlignWrapper );

    ////////////////////////////////////////////////////////////
    // Setup clipboard-copy of markup/SVG...
    F.copyButton(P.e.previewCopyButton, {copyFromElement: P.e.taPreviewText});
    P.e.previewCopyButton.addEventListener('text-copied', D.flashOnce.eventHandler, false);

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
    P.e.previewModeToggle.addEventListener('click', function(){
      /* Rotate through the 4 available preview modes */
      P.previewMode = ++P.previewMode % 4;
      P.renderPreview();
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
      )/*.childNodes.forEach(function(ch){
        ch.style.margin = "0 0.25em";
      })*/;
      D.append(P.e.uiControls, cbWrap);
      P.predefinedPiks.forEach(function(script,ndx){
        const opt = D.option(script.code ? script.code.trim() :'', script.name);
        D.append(selectScript, opt);
        opt.$_sampleScript = script /* for response caching purposes */;
        if(!ndx) selectScript.selectedIndex = 0 /*timing/ordering workaround*/;
        if(!script.code) D.disable(opt);
      });
      delete P.predefinedPiks;
      selectScript.addEventListener('change', function(ev){
        const val = ev.target.value;
        if(!val) return;
        const opt = ev.target.selectedOptions[0];
        P.e.taContent.value = val;
        if(cbAutoPreview.checked){
          P.preview.$_sampleScript = opt.$_sampleScript;
          P.preview();
        }
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

  /* Shows or hides P.e.markupAlignWrapper */
  const showMarkupAlignment = function(showIt){
    P.e.markupAlignWrapper.classList[showIt ? 'remove' : 'add']('hidden');
  };

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
    D.removeClass(P.e.previewCopyButton, 'disabled');
    D.enable(this.e.previewModeToggle, this.e.markupAlignCenter);
    let label;
    switch(this.previewMode){
    case 0:
      label = "SVG";
      showMarkupAlignment(false);
      preTgt.innerHTML = this.response.raw;
      this.e.taPreviewText.value = this.response.raw.replace(f.rxNonce, '')/*for copy button*/;
      break;
    case 1:
      label = "Markdown";
      showMarkupAlignment(true);
      this.e.taPreviewText.value = [
        '```pikchr'+(this.e.markupAlignCenter.checked? ' center' : ''),
        this.response.inputText, '```'
      ].join('\n');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    case 2:
      label = "Fossil wiki";
      showMarkupAlignment(true);
      this.e.taPreviewText.value = [
        '<verbatim type="pikchr',
        this.e.markupAlignCenter.checked ? ' center' : '',
        '">', this.response.inputText, '</verbatim>'
      ].join('');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    case 3:
      label = "Raw SVG";
      showMarkupAlignment(false);
      this.e.taPreviewText.value = this.response.raw.replace(f.rxNonce, '');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    }
    this.e.previewModeLabel.innerText = label;
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
        this.e.cbAutoPreview, this.e.selectScript
        /* handled separately: previewModeToggle, previewCopyButton,
           markupAlignCenter */
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
    D.disable(fp.toDisable, this.e.previewModeToggle, this.e.markupAlignCenter);
    D.addClass(this.e.previewCopyButton, 'disabled');
    const content = this.e.taContent.value.trim();
    this.response.raw = undefined;
    this.response.inputText = content;
    const sampleScript = fp.$_sampleScript;
    delete fp.$_sampleScript;
    if(sampleScript && sampleScript.cached){
      fp.updateView(sampleScript.cached, false);
      return this;
    }
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
      onload: (r,isErrHeader)=>{
        const isErr = +isErrHeader ? true : false;
        if(!isErr && sampleScript){
          sampleScript.cached = r;
        }
        fp.updateView(r,isErr);
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
    {name: "-- Example Scripts --"},
/*
  The following were imported from the pikchr test scripts:

  https://fossil-scm.org/pikchr/dir/tests
*/

{name: "headings01", code:`   linerad = 5px
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
{name: "objects", code:`AllObjects: [

# First row of objects
box "box"
box rad 10px "box (with" "rounded" "corners)" at 1in right of previous
circle "circle" at 1in right of previous
ellipse "ellipse" at 1in right of previous

# second row of objects
oval "oval" at 1in below first box
oval "(tall &amp;" "thin)" "oval" wid ovalht ht ovalwid at 1in right of previous
cylinder "cylinder" at 1in right of previous
file "file" at 1in right of previous

# third row shows line-type objects
dot "dot" above at 1in below first oval
line right from 1.8cm right of previous "lines" above
arrow right from 1.8cm right of previous "arrows" above
spline from 2cm right of previous \
   right .15 up .25 then right .3 down .5 then up .25 right .15 \
   "splines" above ljust

# Draw various lines below the first line
line dashed right from 0.3cm below start of previous line
line dotted right from 0.3cm below start of previous
line thin   right from 0.3cm below start of previous
line thick  right from 0.3cm below start of previous


# Draw arrows with different arrowhead configurations below
# the first arrow
arrow <-  right from 0.4cm below start of previous arrow
arrow <-> right from 0.4cm below start of previous

# Draw splines with different arrowhead configurations below
# the first spline
spline ->  from .4cm below start of first spline \
           right .15 up .25 then right .3 down .5 then up .25 right .15
spline <-  from .4cm below start of previous spline \
           right .15 up .25 then right .3 down .5 then up .25 right .15
spline <-> from .4cm below start of previous spline \
           right .15 up .25 then right .3 down .5 then up .25 right .15

] # end of All Objects

# Label the whole diagram
text "Examples Of Pikchr Objects" big bold  at .8cm above north of AllObjects
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
    arrow right until even with first ellipse.w \
      "back online" above "pushes 5" below "pulls 3 &amp; 4" below
    ellipse same "future"

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
`}

  ];
  
})(window.fossil);
