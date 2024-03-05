(function(F/*the fossil object*/){
  "use strict";
  /**
     Client-side implementation of the /pikchrshowcs app. Requires that
     the fossil JS bootstrapping is complete and that these fossil JS
     APIs have been installed: fossil.fetch, fossil.dom,
     fossil.copybutton, fossil.popupwidget, fossil.storage

     Maintenance funkiness note: this file is for the legacy
     /pikchrshowcs app, which was formerly named /pikchrshow.  This
     file and its replacement were not renamed because the replacement
     impl would end up getting this file's name and cause confusion in
     the file history. Whether that confusion would be less than this
     file's name matching the _other_ /pikchrshow impl will cause more
     or less confusion than that remains to be seen.
  */
  const E = (s)=>document.querySelector(s),
        D = F.dom,
        P = F.page;

  P.previewMode = 0 /*0==rendered SVG, 1==pikchr text markdown,
                      2==pikchr text fossil, 3==raw SVG. */
  P.response = {/*stashed state for the server's preview response*/
    isError: false,
    inputText: undefined /* value of the editor field at render-time */,
    raw: undefined /* raw response text/HTML from server */,
    rawSvg: undefined /* plain-text SVG part of responses. Required
                         because the browser will convert \u00a0 to
                         &nbsp; if we extract the SVG from the DOM,
                         resulting in illegal SVG. */
  };

  /**
     If string r contains an SVG element, this returns that section
     of the string, else it returns falsy.
   */
  const getResponseSvg = function(r){
    const i0 = r.indexOf("<svg");
    if(i0>=0){
      const i1 = r.indexOf("</svg");
      return r.substring(i0,i1+6);
    }
    return '';
  };

  F.onPageLoad(function() {
    document.body.classList.add('pikchrshow');
    P.e = { /* various DOM elements we work with... */
      previewTarget: E('#pikchrshow-output'),
      previewLegend: E('#pikchrshow-output-wrapper > legend'),
      previewCopyButton: D.attr(
        D.addClass(D.span(),'copy-button'),
        'id','preview-copy-button' 
      ),
      previewModeLabel: D.label('preview-copy-button'),
      btnSubmit: E('#pikchr-submit-preview'),
      btnStash: E('#pikchr-stash'),
      btnUnstash: E('#pikchr-unstash'),
      btnClearStash: E('#pikchr-clear-stash'),
      cbDarkMode: E('#flipcolors-wrapper > input[type=checkbox]'),
      taContent: E('#content'),
      taPreviewText: D.textarea(20,0,true),
      uiControls: E('#pikchrshow-controls'),
      previewModeToggle: D.button("Preview mode"),
      markupAlignDefault: D.attr(D.radio('markup-align','',true),
                                 'id','markup-align-default'),
      markupAlignCenter: D.attr(D.radio('markup-align','center'),
                                'id','markup-align-center'),
      markupAlignIndent: D.attr(D.radio('markup-align','indent'),
                                'id','markup-align-indent'),
      markupAlignWrapper: D.addClass(D.span(), 'input-with-label')
    };

    ////////////////////////////////////////////////////////////
    // Setup markup alignment selection...
    const alignEvent = function(ev){
      /* Update markdown/fossil wiki preview if it's active */
      if(P.previewMode==1 || P.previewMode==2){
        P.renderPreview();
      }
    };
    P.e.markupAlignRadios = [
      P.e.markupAlignDefault,
      P.e.markupAlignCenter,
      P.e.markupAlignIndent
    ];
    D.append(P.e.markupAlignWrapper,
             D.addClass(D.append(D.span(),"align:"),
                        'v-align-middle'));
    P.e.markupAlignRadios.forEach(
      function(e){
        e.addEventListener('change', alignEvent, false);
        D.append(P.e.markupAlignWrapper,
                 D.addClass([
                   e,
                   D.label(e, e.value || "left")
                 ], 'v-align-middle'));
      }
    );

    ////////////////////////////////////////////////////////////
    // Setup the preview fieldset's LEGEND element...
    D.append( P.e.previewLegend,
              P.e.previewModeToggle,
              '\u00a0',
              P.e.previewCopyButton,
              P.e.previewModeLabel,
              P.e.markupAlignWrapper );

    ////////////////////////////////////////////////////////////
    // Trigger preview on Shift-Enter.
    P.e.taContent.addEventListener('keydown',function(ev){
      if(ev.shiftKey && 13 === ev.keyCode){
        ev.preventDefault();
        ev.stopPropagation();
        P.preview();
        return false;
      }
    }, false);

    ////////////////////////////////////////////////////////////
    // Setup clipboard-copy of markup/SVG...
    F.copyButton(P.e.previewCopyButton, {copyFromElement: P.e.taPreviewText});
    P.e.previewModeLabel.addEventListener('click', ()=>P.e.previewCopyButton.click(), false);

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
            D.attr(D.checkbox(true),'id', 'cb-auto-preview'),
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
                   'the local filesystem into the text area, if the ',
                   'environment supports it, but the auto-preview ',
                   'option does not apply to them.'
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
          D.div(),
          'Dark mode changes the colors of rendered SVG to ',
          'make them more visible in dark-themed skins. ',
          'This only changes (using CSS) how they are rendered, ',
          'not any actual colors written in the script.',
          D.br(), D.br(),
          'In some color combinations, certain browsers might ',
          'cause the SVG image to blur considerably with this ',
          'setting enabled!'
        )
      )
    );

    ////////////////////////////////////////////////////////////
    // File drag/drop pikchr scripts into P.e.taContent.
    // Adapted from: https://stackoverflow.com/a/58677161
    const dropHighlight = P.e.taContent;
    const dropEvents = {
      drop: function(ev){
        //ev.stopPropagation();
        ev.preventDefault();
        D.removeClass(dropHighlight, 'dragover');
        const file = ev.dataTransfer.files[0];
        if(file) {
          const reader = new FileReader();
          reader.addEventListener(
            'load', function(e) {P.e.taContent.value = e.target.result}, false
          );
          reader.readAsText(file, "UTF-8");
        }
      },
      dragenter: function(ev){
        //ev.stopPropagation();
        ev.preventDefault();
        ev.dataTransfer.dropEffect = "copy";
        D.addClass(dropHighlight, 'dragover');
        //console.debug("dragenter");
      },
      dragover: function(ev){
        //ev.stopPropagation();
        ev.preventDefault();
        //console.debug("dragover");
      },
      dragend: function(ev){
        //ev.stopPropagation();
        ev.preventDefault();
        //console.debug("dragend");
      },
      dragleave: function(ev){
        //ev.stopPropagation();
        ev.preventDefault();
        D.removeClass(dropHighlight, 'dragover');
        //console.debug("dragleave");
      }
    };
    /*
      The idea here is to accept drops at multiple points or, ideally,
      document.body, and apply them to P.e.taContent, but the precise
      combination of event handling needed to pull this off is eluding
      me.
    */
    [P.e.taContent
     //P.e.previewTarget,// works only until we drag over the SVG element!
     //document.body
     /* ideally we'd link only to document.body, but the events seem to
        get out of whack, with dropleave being triggered
        at unexpected points. */
    ].forEach(function(e){
        Object.keys(dropEvents).forEach(
          (k)=>e.addEventListener(k, dropEvents[k], true)
        );
    });

    ////////////////////////////////////////////////////////////
    // Setup stash/unstash
    const stashKey = 'pikchrshow-stash';
    P.e.btnStash.addEventListener('click', function(){
      const val = P.e.taContent.value;
      if(val){
        F.storage.set(stashKey, val);
        D.enable(P.e.btnUnstash);
        F.toast.message("Stashed pikchr.");
      }
    }, false);
    P.e.btnUnstash.addEventListener('click', function(){
      const val = F.storage.get(stashKey);
      P.e.taContent.value = val || '';
    }, false);
    P.e.btnClearStash.addEventListener('click', function(){
      F.storage.remove(stashKey);
      D.disable(P.e.btnUnstash);
      F.toast.message("Cleared pikchr stash.");
    }, false);
    F.helpButtonlets.create(P.e.btnClearStash.nextElementSibling);
    // If we have stashed contents, enable Unstash, else disable it:
    if(F.storage.contains(stashKey)) D.enable(P.e.btnUnstash);
    else D.disable(P.e.btnUnstash);

    ////////////////////////////////////////////////////////////
    // If we start with content, get it in sync with the state
    // generated by P.preview(). Normally the server pre-populates it
    // with an example.
    let needsPreview;
    if(!P.e.taContent.value){
      P.e.taContent.value = F.storage.get(stashKey,'');
      needsPreview = true;
    }
    if(P.e.taContent.value){
      /* Fill our "response" state so that renderPreview() can work */
      P.response.inputText = P.e.taContent.value;
      P.response.raw = P.e.previewTarget.innerHTML;
      P.response.rawSvg = getResponseSvg(
        P.response.raw /*note that this is already in the DOM,
                         which means that the browser has already mangled
                         \u00a0 to &nbsp;, so...*/.split('&nbsp;').join('\u00a0'));
      if(needsPreview) P.preview();
      else{
        /*If it's from the server, it's already rendered, but this
          gets all labels/headers in sync.*/
        P.renderPreview();
      }
    }
  }/*F.onPageLoad()*/);

  /**
     Updates the preview view based on the current preview mode and
     error state.
  */
  P.renderPreview = function f(){
    if(!f.hasOwnProperty('rxNonce')){
      f.rxNonce = /<!--.+-->\r?\n?/g /*pikchr nonce comments*/;
      f.showMarkupAlignment = function(showIt){
        P.e.markupAlignWrapper.classList[showIt ? 'remove' : 'add']('hidden');
      };
      f.getMarkupAlignmentClass = function(){
        if(P.e.markupAlignCenter.checked) return ' center';
        else if(P.e.markupAlignIndent.checked) return ' indent';
        return '';
      };
      f.getSvgNode = function(txt){
        const childs = D.parseHtml(txt);
        const wrapper = childs.filter((e)=>'DIV'===e.tagName)[0];
        return wrapper ? wrapper.querySelector('svg.pikchr') : undefined;
      };
    }
    const preTgt = this.e.previewTarget;
    if(this.response.isError){
      D.append(D.clearElement(preTgt), D.parseHtml(P.response.raw));
      D.addClass(preTgt, 'error');
      this.e.previewModeLabel.innerText = "Error";
      return;
    }
    D.removeClass(preTgt, 'error');
    D.removeClass(this.e.previewCopyButton, 'disabled');
    D.removeClass(this.e.markupAlignWrapper, 'hidden');
    D.enable(this.e.previewModeToggle, this.e.markupAlignRadios);
    let label, svg;
    switch(this.previewMode){
    case 0:
      label = "SVG";
      f.showMarkupAlignment(false);
      D.parseHtml(D.clearElement(preTgt), P.response.raw);
      svg = preTgt.querySelector('svg.pikchr');
      if(svg && P.response.rawSvg){ /*for copy button*/
        this.e.taPreviewText.value = P.response.rawSvg;
        F.pikchr.addSrcView(svg);
      }
      break;
    case 1:
      label = "Markdown";
      f.showMarkupAlignment(true);
      this.e.taPreviewText.value = [
        '```pikchr'+f.getMarkupAlignmentClass(),
        this.response.inputText.trim(), '```'
      ].join('\n');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    case 2:
      label = "Fossil wiki";
      f.showMarkupAlignment(true);
      this.e.taPreviewText.value = [
        '<verbatim type="pikchr',
        f.getMarkupAlignmentClass(),
        '">', this.response.inputText.trim(), '</verbatim>'
      ].join('');
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    case 3:
      label = "Raw SVG";
      f.showMarkupAlignment(false);
      svg = f.getSvgNode(this.response.raw);
      if(svg){
        this.e.taPreviewText.value =
          P.response.rawSvg || "Error extracting SVG element.";
      }else{
        this.e.taPreviewText.value = "ERROR parsing response HTML:\n"+
          this.response.raw;
        console.error("svg parsed HTML nodes:",childs);
      }
      D.append(D.clearElement(preTgt), this.e.taPreviewText);
      break;
    }
    this.e.previewModeLabel.innerText = label;
    this.e.taContent.focus(/*not sure why this gets lost on preview!*/);
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
        this.e.cbAutoPreview, this.e.selectScript,
        this.e.btnStash, this.e.btnClearStash
        /* handled separately: previewModeToggle, previewCopyButton,
           markupAlignRadios */
      ];
      fp.target = this.e.previewTarget;
      fp.updateView = function(c,isError){
        P.previewMode = 0;
        P.response.raw = c;
        P.response.rawSvg = getResponseSvg(c);
        P.response.isError = isError;
        D.enable(fp.toDisable);
        P.renderPreview();
      };
    }
    D.disable(fp.toDisable, this.e.previewModeToggle, this.e.markupAlignRadios);
    D.addClass(this.e.markupAlignWrapper, 'hidden');
    D.addClass(this.e.previewCopyButton, 'disabled');
    const content = this.e.taContent.value.trim();
    this.response.raw = this.response.rawSvg = undefined;
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
    F.fetch('pikchrshow',{
      payload: fd,
      responseHeaders: 'x-pikchrshow-is-error',
      onload: (r,isErrHeader)=>{
        const isErr = +isErrHeader ? true : false;
        if(!isErr && sampleScript){
          sampleScript.cached = r;
        }
        fp.updateView(r,isErr);
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

  https://fossil-scm.org/pikchr/dir/examples
*/
{name:"Cardinal headings",code:`   linerad = 5px
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
`},{name:"Core object types",code:`AllObjects: [

# First row of objects
box "box"
box rad 10px "box (with" "rounded" "corners)" at 1in right of previous
circle "circle" at 1in right of previous
ellipse "ellipse" at 1in right of previous

# second row of objects
OVAL1: oval "oval" at 1in below first box
oval "(tall &" "thin)" "oval" width OVAL1.height height OVAL1.width \
    at 1in right of previous
cylinder "cylinder" at 1in right of previous
file "file" at 1in right of previous

# third row shows line-type objects
dot "dot" above at 1in below first oval
line right from 1.8cm right of previous "lines" above
arrow right from 1.8cm right of previous "arrows" above
spline from 1.8cm right of previous \
   go right .15 then .3 heading 30 then .5 heading 160 then .4 heading 20 \
   then right .15
"splines" at 3rd vertex of previous

# The third vertex of the spline is not actually on the drawn
# curve.  The third vertex is a control point.  To see its actual
# position, uncomment the following line:
#dot color red at 3rd vertex of previous spline

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
spline same from .4cm below start of first spline ->
spline same from .4cm below start of previous <-
spline same from .4cm below start of previous <->

] # end of AllObjects

# Label the whole diagram
text "Examples Of Pikchr Objects" big bold  at .8cm above north of AllObjects
`},{name:"Swimlanes",code:`    $laneh = 0.75

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
      "back online" above "pushes 5" below "pulls 3 & 4" below
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
