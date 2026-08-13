/*
  2022-05-20

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  This is the main entry point for the WASM rendition of fossil's
  /pikchrshow app. It sets up the various UI bits, loads a Worker for
  the pikchr process, and manages the communication between the UI and
  worker.

  API dependencies: fossil.dom, fossil.copybutton, fossil.storage
*/
(function(F/*fossil object*/){
  'use strict';

  /* Recall that the 'self' symbol, except where locally
     overwritten, refers to the global window or worker object. */

  const D = F.dom;
  /** Name of the stored copy of this app's config. */
  const configStorageKey = 'pikchrshow-config';

  /* querySelectorAll() proxy */
  const EAll = function(/*[element=document,] cssSelector*/){
    return (arguments.length>1 ? arguments[0] : document)
      .querySelectorAll(arguments[arguments.length-1]);
  };
  /* querySelector() proxy */
  const E = function(/*[element=document,] cssSelector*/){
    return (arguments.length>1 ? arguments[0] : document)
      .querySelector(arguments[arguments.length-1]);
  };

  /** The main application object. */
  const PS = {
    /* Config options. */
    config: {
      /* If true, display input/output areas side-by-side, else stack
         them vertically. */
      sideBySide: true,
      /* If true, swap positions of the input/output areas. */
      swapInOut: false,
      /* If true, the SVG is allowed to resize to fit the parent
         content area, else the parent is resized to fit the rendered
         SVG (as sized by pikchr). */
      renderAutofit: false,
      /* If true, automatically render while the user is typing. */
      renderWhileTyping: false
    },
    /* Various DOM elements. */
    e: {
      previewCopyButton: E('#preview-copy-button'),
      previewModeLabel: E('label[for=preview-copy-button]'),
      zoneInputButtons: E('.zone-wrapper.input > legend > .button-bar'),
      zoneOutputButtons: E('.zone-wrapper.output > legend > .button-bar'),
      outText: E('#pikchr-output-text'),
      pikOutWrapper: E('#pikchr-output-wrapper'),
      pikOut: E('#pikchr-output'),
      btnRender: E('#btn-render')
    },
    renderModes: ['svg'/*SVG must be at index 0*/,'markdown', 'wiki', 'text'],
    renderModeLabels: {
      svg: 'SVG', markdown: 'Markdown', wiki: 'Fossil Wiki', text: 'Text'
    },
    _msgMap: {},
    /** Adds a worker message handler for messages of the given
        type. */
    addMsgHandler: function f(type,callback){
      if(Array.isArray(type)){
        type.forEach((t)=>this.addMsgHandler(t, callback));
        return this;
      }
      (this._msgMap.hasOwnProperty(type)
       ? this._msgMap[type]
       : (this._msgMap[type] = [])).push(callback);
      return this;
    },
    /** Given a worker message, runs all handlers for msg.type. */
    runMsgHandlers: function(msg){
      const list = (this._msgMap.hasOwnProperty(msg.type)
                    ? this._msgMap[msg.type] : false);
      if(!list){
        console.warn("No handlers found for message type:",msg);
        return false;
      }
      list.forEach((f)=>f(msg));
      return true;
    },
    /** Removes all message handlers for the given message type. */
    clearMsgHandlers: function(type){
      delete this._msgMap[type];
      return this;
    },
    /* Posts a message in the form {type, data} to the db worker. Returns this. */
    wMsg: function(type,data){
      this.worker.postMessage({type, data});
      return this;
    },
    /** Stores this object's config in the browser's storage. */
    storeConfig: function(){
      F.storage.setJSON(configStorageKey,this.config);
    }
  };
  PS.renderModes.selectedIndex = 0;
  PS._config = F.storage.getJSON(configStorageKey);
  if(PS._config){
    /* Copy all properties to PS.config which are currently in
       PS._config. We don't bother copying any other properties: those
       would be stale/removed config entries. */
    Object.keys(PS.config).forEach(function(k){
      if(PS._config.hasOwnProperty(k)){
        PS.config[k] = PS._config[k];
      }
    });
    delete PS._config;
  }

  /* Randomize the name of the worker script so that it is never cached.
  ** The Fossil /builtin method will automatically remove the "-v000000000"
  ** part of the filename, resolving it to just "pikchr-worker.js". */
  PS.worker = new Worker('builtin/extsrc/pikchr-worker-v'+
                         (Math.floor(Math.random()*10000000000) + 1000000000)+
                        '.js');
  PS.worker.onmessage = (ev)=>PS.runMsgHandlers(ev.data);
  PS.addMsgHandler('stdout', console.log.bind(console));
  PS.addMsgHandler('stderr', console.error.bind(console));

  /** Handles status updates from the Module object. */
  PS.addMsgHandler('module', function f(ev){
    ev = ev.data;
    if('status'!==ev.type){
      console.warn("Unexpected module-type message:",ev);
      return;
    }
    if(!f.ui){
      f.ui = {
        status: E('#module-status'),
        progress: E('#module-progress'),
        spinner: E('#module-spinner')
      };
    }
    const msg = ev.data;
    if(f.ui.progres){
      progress.value = msg.step;
      progress.max = msg.step + 1/*we don't know how many steps to expect*/;
    }
    if(1==msg.step){
      f.ui.progress.classList.remove('hidden');
      f.ui.spinner.classList.remove('hidden');
    }
    if(msg.text){
      f.ui.status.classList.remove('hidden');
      f.ui.status.innerText = msg.text;
    }else{
      if(f.ui.progress){
        f.ui.progress.remove();
        f.ui.spinner.remove();
        delete f.ui.progress;
        delete f.ui.spinner;
      }
      f.ui.status.classList.add('hidden');
      /* The module can post messages about fatal problems,
         e.g. an exit() being triggered or assertion failure,
         after the last "load" message has arrived, so
         leave f.ui.status and message listener intact. */
    }
  });

  PS.e.previewModeLabel.innerText =
    PS.renderModeLabels[PS.renderModes[PS.renderModes.selectedIndex]];

  /**
     The 'pikchr-ready' event is fired (with no payload) when the
     wasm module has finished loading. */
  PS.addMsgHandler('pikchr-ready', function(event){
    PS.clearMsgHandlers('pikchr-ready');
    F.page.onPikchrshowLoaded(event.data);
  });

  /**
     Performs all app initialization which must wait until after the
     worker module is loaded. This function removes itself when it's
     called.
  */
  F.page.onPikchrshowLoaded = function(pikchrVersion){
    delete this.onPikchrshowLoaded;
    // Unhide all elements which start out hidden
    EAll('.initially-hidden').forEach((e)=>e.classList.remove('initially-hidden'));
    const taInput = E('#input');
    const btnClearIn = E('#btn-clear');
    btnClearIn.addEventListener('click',function(){
      taInput.value = '';
    },false);
    const getCurrentText = function(){
      let text;
      if(taInput.selectionStart<taInput.selectionEnd){
        text = taInput.value.substring(taInput.selectionStart,taInput.selectionEnd).trim();
      }else{
        text = taInput.value.trim();
      }
      return text;;
    };
    const renderCurrentText = function(){
      const text = getCurrentText();
      if(text) PS.render(text);
    };
    const setCurrentText = function(txt){
      taInput.value = txt;
      renderCurrentText();
    };
    PS.e.btnRender.addEventListener('click',function(ev){
      ev.preventDefault();
      renderCurrentText();
    },false);

    /** To be called immediately before work is sent to the
        worker. Updates some UI elements. The 'working'/'end'
        event will apply the inverse, undoing the bits this
        function does. This impl is not in the 'working'/'start'
        event handler because that event is given to us
        asynchronously _after_ we need to have performed this
        work.
    */
    const preStartWork = function f(){
      if(!f._){
        const title = E('title');
        f._ = {
          pageTitle: title,
          pageTitleOrig: title.innerText
        };
      }
      //f._.pageTitle.innerText = "[working...] "+f._.pageTitleOrig;
      PS.e.btnRender.setAttribute('disabled','disabled');
    };

    /**
       Submits the current input text to pikchr and renders the
       result. */
    PS.render = function f(txt){
      preStartWork();
      this.wMsg('pikchr',{
        pikchr: txt,
        darkMode: !!window.fossil.config.skin.isDark
      });
    };

    /**
       Event handler for 'pikchr' messages from the Worker thread.
    */
    PS.addMsgHandler('pikchr', function(ev){
      const m = ev.data, pikOut = this.e.pikOut;
      pikOut.classList[m.isError ? 'add' : 'remove']('error');
      pikOut.dataset.pikchr = m.pikchr;
      const mode = this.renderModes[this.renderModes.selectedIndex];
      switch(mode){
          case 'text': case 'markdown': case 'wiki': {
            let body;
            switch(mode){
                case 'markdown':
                  body = ['```pikchr', m.pikchr, '```'].join('\n');
                  break;
                case 'wiki':
                  body = ['<verbatim type="pikchr">', m.pikchr, '</verbatim>'].join('');
                  break;
                default:
                  body = m.result;
            }
            this.e.outText.value = body;
            this.e.outText.classList.remove('hidden');
            pikOut.classList.add('hidden');
            this.e.pikOutWrapper.classList.add('text');
            break;
          }
          case 'svg':
            this.e.outText.classList.add('hidden');
            pikOut.classList.remove('hidden');
            this.e.pikOutWrapper.classList.remove('text');
            pikOut.innerHTML = m.result;
            this.e.outText.value = m.result/*for clipboard copy*/;
            break;
          default: throw new Error("Unhandled render mode: "+mode);
      }
      let vw = null, vh = null;
      if('svg'===mode){
        if(m.isError){
          vw = vh = '100%';
        }else if(this.config.renderAutofit){
          /* FIXME: current behavior doesn't work as desired when width>height
             (e.g. non-side-by-side mode).*/
          vw = vh = '98%';
        }else{
          vw = m.width+1+'px'; vh = m.height+1+'px';
          /* +1 is b/c the SVG uses floating point sizes but pikchr()
             returns truncated integers. */
        }
        pikOut.style.width = vw;
        pikOut.style.height = vh;
      }
    }.bind(PS))/*'pikchr' msg handler*/;

    E('#btn-render-mode').addEventListener('click',function(){
      const modes = this.renderModes;
      modes.selectedIndex = (modes.selectedIndex + 1) % modes.length;
      this.e.previewModeLabel.innerText = this.renderModeLabels[modes[modes.selectedIndex]];
      if(this.e.pikOut.dataset.pikchr){
        this.render(this.e.pikOut.dataset.pikchr);
      }
    }.bind(PS));
    F.copyButton(PS.e.previewCopyButton, {copyFromElement: PS.e.outText});

    PS.addMsgHandler('working',function f(ev){
      switch(ev.data){
          case 'start': /* See notes in preStartWork(). */; return;
          case 'end':
            //preStartWork._.pageTitle.innerText = preStartWork._.pageTitleOrig;
            this.e.btnRender.removeAttribute('disabled');
            this.e.pikOutWrapper.classList[this.config.renderAutofit ? 'add' : 'remove']('autofit');
            return;
      }
      console.warn("Unhandled 'working' event:",ev.data);
    }.bind(PS));

    /* For each checkbox with data-csstgt, set up a handler which
       toggles the given CSS class on the element matching
       E(data-csstgt). */
    EAll('input[type=checkbox][data-csstgt]')
      .forEach(function(e){
        const tgt = E(e.dataset.csstgt);
        const cssClass = e.dataset.cssclass || 'error';
        e.checked = tgt.classList.contains(cssClass);
        e.addEventListener('change', function(){
          tgt.classList[
            this.checked ? 'add' : 'remove'
          ](cssClass)
        }, false);
      });
    /* For each checkbox with data-config=X, set up a binding to
       PS.config[X]. These must be set up AFTER data-csstgt
       checkboxes so that those two states can be synced properly. */
    EAll('input[type=checkbox][data-config]')
      .forEach(function(e){
        const confVal = !!PS.config[e.dataset.config];
        if(e.checked !== confVal){
          /* Ensure that data-csstgt mappings (if any) get
             synced properly. */
          e.checked = confVal;
          e.dispatchEvent(new Event('change'));
        }
        e.addEventListener('change', function(){
          PS.config[this.dataset.config] = this.checked;
          PS.storeConfig();
        }, false);
      });
    E('#opt-cb-autofit').addEventListener('change',function(){
      /* PS.config.renderAutofit was set by the data-config
         event handler. */
      if(0==PS.renderModes.selectedIndex && PS.e.pikOut.dataset.pikchr){
        PS.render(PS.e.pikOut.dataset.pikchr);
      }
    });
    /* For each button with data-cmd=X, map a click handler which
       calls PS.render(X). */
    const cmdClick = function(){PS.render(this.dataset.cmd);};
    EAll('button[data-cmd]').forEach(
      e => e.addEventListener('click', cmdClick, false)
    );


    ////////////////////////////////////////////////////////////
    // Set up selection list of predefined scripts...
    if(true){
      const selectScript = PS.e.selectScript = D.select();
      D.append(PS.e.zoneInputButtons, selectScript);
      PS.predefinedPiks.forEach(function(script,ndx){
        const opt = D.option(script.code ? script.code.trim() :'', script.name);
        D.append(selectScript, opt);
        if(!ndx) selectScript.selectedIndex = 0 /*timing/ordering workaround*/;
        if(ndx && !script.code){
          /* Treat entries w/ no code as separators EXCEPT for the
             first one, which we want to keep selectable solely for
             cosmetic reasons. */
          D.disable(opt);
        }
      });
      delete PS.predefinedPiks;
      selectScript.addEventListener('change', function(ev){
        const val = ev.target.value;
        if(!val) return;
        setCurrentText(val);
      }, false);
    }/*Examples*/

    /**
       TODO? Handle load/import of an external pikchr file.
    */
    if(0) E('#load-pikchr').addEventListener('change',function(){
      const f = this.files[0];
      const r = new FileReader();
      const status = {loaded: 0, total: 0};
      this.setAttribute('disabled','disabled');
      const that = this;
      r.addEventListener('load', function(){
        that.removeAttribute('disabled');
        stdout("Loaded",f.name+". Opening pikchr...");
        PS.wMsg('open',{
          filename: f.name,
          buffer: this.result
        });
      });
      r.addEventListener('error',function(){
        that.removeAttribute('disabled');
        stderr("Loading",f.name,"failed for unknown reasons.");
      });
      r.addEventListener('abort',function(){
        that.removeAttribute('disabled');
        stdout("Cancelled loading of",f.name+".");
      });
      r.readAsArrayBuffer(f);
    });

    EAll('fieldset.collapsible').forEach(function(fs){
      const btnToggle = E(fs,'legend > .fieldset-toggle'),
            content = EAll(fs,':scope > div');
      btnToggle.addEventListener('click', function(){
        fs.classList.toggle('collapsed');
        content.forEach((d)=>d.classList.toggle('hidden'));
      }, false);
    });

    if(window.sessionStorage){
      /* If sessionStorage['pikchr-xfer'] exists and the "fromSession"
         URL argument was passed to this page, load the pikchr source
         from the session. This is used by the "open in pikchrshow"
         link in the forum. */
      const src = window.sessionStorage.getItem('pikchr-xfer');
      if( src && (new URL(self.location.href).searchParams).has('fromSession') ){
        taInput.value =  src;
        window.sessionStorage.removeItem('pikchr-xfer');
      }
    }
    D.append(E('fieldset.options > div'),
             D.append(D.addClass(D.span(), 'labeled-input'),
                      'pikchr v. '+pikchrVersion));

    PS.e.btnRender.click();

    /** Debounce handler for auto-rendering while typing. */
    const debounceAutoRender = F.debounce(function f(){
      if(!PS._isDirty) return;
      const text = getCurrentText();
      if(f._ === text){
        PS._isDirty = false;
        return;
      }
      f._ = text;
      PS._isDirty = false;
      PS.render(text || '');
    }, 800, false);

    taInput.addEventListener('keydown',function f(ev){
      if((ev.ctrlKey || ev.shiftKey) && 13 === ev.keyCode){
        // Ctrl-enter and shift-enter both run the current input
        PS._isDirty = false/*prevent a pending debounce from re-rendering*/;
        ev.preventDefault();
        ev.stopPropagation();
        renderCurrentText();
        return;
      }
      if(!PS.config.renderWhileTyping) return;
      /* Auto-render while typing... */
      switch(ev.keyCode){
          case (ev.keyCode<32): /*any ctrl char*/
            /* ^^^ w/o that, simply tapping ctrl is enough to
               force a re-render. Similarly, TAB-ing focus away
               should not re-render. */
          case 33: case 34: /* page up/down */
          case 35: case 36: /* home/end */
          case 37: case 38: case 39: case 40: /* arrows */
            return;
      }
      PS._isDirty = true;
      debounceAutoRender();
    }, false);

    const ForceResizeKludge = (function(){
      /* Workaround for Safari mayhem regarding use of vh CSS
         units....  We cannot use vh units to set the main view
         size because Safari chokes on that, so we calculate
         that height here. Larger than ~95% is too big for
         Firefox on Android, causing the input area to move
         off-screen. */
      const appViews = EAll('.app-view');
      const elemsToCount = [
        /* Elements which we need to always count in the
           visible body size. */
        E('body > header'),
        E('body > nav.mainmenu'),
        E('body > footer')
      ];
      const resized = function f(){
        if(f.$disabled) return;
        const wh = window.innerHeight;
        var ht;
        var extra = 0;
        elemsToCount.forEach((e)=>e ? extra += F.dom.effectiveHeight(e) : false);
        ht = wh - extra;
        appViews.forEach(function(e){
          e.style.height =
            e.style.maxHeight = [
              "calc(", (ht>=100 ? ht : 100), "px",
              " - 2em"/*fudge value*/,")"
              /* ^^^^ hypothetically not needed, but both
                 Chrome/FF on Linux will force scrollbars on the
                 body if this value is too small. */
            ].join('');
        });
      };
      resized.$disabled = true/*gets deleted when setup is finished*/;
      window.addEventListener('resize', F.debounce(resized, 250), false);
      return resized;
    })()/*ForceResizeKludge*/;

    delete ForceResizeKludge.$disabled;
    ForceResizeKludge();
  }/*onPikchrshowLoaded()*/;


  /**
     Predefined example pikchr scripts. Each entry is an object:

     {
     name: required string,
     code: optional code string. An entry with a falsy code is treated
           like a separator in the resulting SELECT element (a
           disabled OPTION).
     }
  */
  PS.predefinedPiks = [
    {name: "-- Example Scripts --", code: false},
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
`},{
  name: "The Stuff of Dreams",
  code:`
O: text "DREAMS" color grey
circle rad 0.9 at 0.6 above O thick color red
text "INEXPENSIVE" big bold at 0.9 above O color red

circle rad 0.9   at 0.6 heading  120 from O thick color green
text "FAST" big bold at 0.9 heading  120 from O  color green

circle rad 0.9 at 0.6 heading -120 from O thick color blue
text "HIGH" big bold "QUALITY" big bold at 0.9 heading  -120 from O  color blue

text "EXPENSIVE" at 0.55 below O  color cyan
text "SLOW" at 0.55 heading  -60 from O  color magenta
text "POOR" "QUALITY" at 0.55 heading   60 from O  color gold
`},{name:"Precision Arrows",code:`
# Source: https://pikchr.org/home/forumpost/7f2f9a03eb
define quiver {
	dot invis at 0.5 < $1.ne , $1.e >
	dot invis at 0.5 < $1.nw , $1.w >
	dot invis at 0.5 < $1.se , $1.e >
	dot invis at 0.5 < $1.sw , $1.w >

	dot at $2 right of 4th previous dot
        dot at $3 right of 4th previous dot
	dot at $4 right of 4th previous dot
        dot at $5 right of 4th previous dot
	arrow <- from previous dot to 2nd previous dot
	arrow -> from 3rd previous dot to 4th previous dot
}

define show_compass_l {
	dot color red  at $1.e " .e" ljust
	dot same at $1.ne " .ne" ljust above
	line thick color green from previous to 2nd last dot
}

define show_compass_r {
	dot color red  at $1.w " .w" ljust
	dot same at $1.nw " .nw" ljust above
	line thick color green from previous to 2nd last dot
}

PROGRAM: file "Program" rad 45px
show_compass_l(PROGRAM)
QUIVER: box invis ht 0.75
DATABASE: oval "Database" ht 0.75 wid 1.1
show_compass_r(DATABASE)

quiver(QUIVER, 5px, -5px, 5px, 0px)

text "Query" with .c at 0.1in above last arrow
text "Records" with .c at 0.1in below 2nd last arrow
`}
  ];


})(window.fossil);
