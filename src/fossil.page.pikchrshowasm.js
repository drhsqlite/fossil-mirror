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

  API dependencies: fossil.dom, fossil.storage
*/
(function(F/*fossil object*/){
  'use strict';

  /* Recall that the 'self' symbol, except where locally
     overwritten, refers to the global window or worker object. */

  /** Name of the stored copy of this app's config. */
  const configStorageKey = 'pikchrshow-config';

  /**
     The PikchrFiddle object is intended to be the primary app-level
     object for the main-thread side of the fiddle application. It
     uses a worker thread to load the WASM module and communicate
     with it.
  */
  const PS/*local convenience alias*/ = F.PikchrShow/*canonical name*/ = {
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
      renderAutoScale: false,
      /* If true, automatically render while the user is typing. */
      renderWhileTyping: false
    },
    renderMode: 'html'/*one of: 'text','html'*/,
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

  PS.worker = new Worker('builtin/extsrc/pikchr-worker.js');
  PS.worker.onmessage = (ev)=>PS.runMsgHandlers(ev.data);
  PS.addMsgHandler('stdout', console.log.bind(console));
  PS.addMsgHandler('stderr', console.error.bind(console));

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

  /**
     The 'pikchrshow-ready' event is fired (with no payload) when the
     wasm module has finished loading. */
  PS.addMsgHandler('pikchrshow-ready', function(){
    PS.clearMsgHandlers('pikchrshow-ready');
    F.page.onPikchrshowLoaded();
  });

  /**
     Performs all app initialization which must wait until after the
     worker module is loaded. This function removes itself when it's
     called.
  */
  F.page.onPikchrshowLoaded = function(){
    delete this.onPikchrshowLoaded;
    // Unhide all elements which start out hidden
    EAll('.initially-hidden').forEach((e)=>e.classList.remove('initially-hidden'));
    const taInput = E('#input');
    const btnClearIn = E('#btn-clear');
    btnClearIn.addEventListener('click',function(){
      taInput.value = '';
    },false);
    const taOutput = E('#output');
    const btnRender = E('#btn-render');
    const getCurrentText = function(){
      let text;
      if(taInput.selectionStart<taInput.selectionEnd){
        text = taInput.value.substring(taInput.selectionStart,taInput.selectionEnd).trim();
      }else{
        text = taInput.value.trim();
      }
      return text;;
    }
    const renderCurrentText = function(){
      const text = getCurrentText();
      if(text) PS.render(text);
    };
    btnRender.addEventListener('click',function(ev){
      ev.preventDefault();
      renderCurrentText();
    },false);

    0 && (function(){
      /* Set up split-view controls... This _almost_ works correctly,
         just needs some tweaking to account for switching between
         side-by-side and top-bottom views. */
      // adapted from https://htmldom.dev/create-resizable-split-views/
      const Split = {
        e:{
          left: E('.zone-wrapper.input'),
          right: E('.zone-wrapper.output'),
          handle: E('.splitter-handle'),
          parent: E('#main-wrapper')
        },
        x: 0, y: 0,
        widthLeft: 0,
        heightLeft: 0
      };
      Split.mouseDownHandler = function(e){
        this.x = e.clientX;
        this.y = e.clientY;
        const r = this.e.left.getBoundingClientRect();
        this.widthLeft = r.width;
        this.heightLeft = r.height;
        document.addEventListener('mousemove', this.mouseMoveHandler);
        document.addEventListener('mouseup', this.mouseUpHandler);
      }.bind(Split);
      Split.mouseMoveHandler = function(e){
        const isHorizontal = this.e.parent.classList.contains('side-by-side');
        const dx = e.clientX - this.x;
        const dy = e.clientY - this.y;
        if(isHorizontal){
          const w = ((this.widthLeft + dx) * 100)
                / this.e.parent.getBoundingClientRect().width;
          this.e.left.style.width = w+'%';
        }else{
          const h = ((this.heightLeft + dy) * 100)
                / this.e.parent.getBoundingClientRect().height;
          this.e.left.style.height = h+'%';
        }
        document.body.style.cursor = isHorizontal ? 'col-resize' : 'row-resize';
        this.e.left.style.userSelect = 'none';
        this.e.left.style.pointerEvents = 'none';
        this.e.right.style.userSelect = 'none';
        this.e.right.style.pointerEvents = 'none';
      }.bind(Split);
      Split.mouseUpHandler = function(e){
        this.e.handle.style.removeProperty('cursor');
        document.body.style.removeProperty('cursor');
        this.e.left.style.removeProperty('user-select');
        this.e.left.style.removeProperty('pointer-events');
        this.e.right.style.removeProperty('user-select');
        this.e.right.style.removeProperty('pointer-events');
        document.removeEventListener('mousemove', this.mouseMoveHandler);
        document.removeEventListener('mouseup', this.mouseUpHandler);
      }.bind(Split);
      Split.e.handle.addEventListener('mousedown', Split.mouseDownHandler);
    })();

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
          btnLabel: btnRender.innerText,
          pageTitle: title,
          pageTitleOrig: title.innerText
        };
      }
      //f._.pageTitle.innerText = "[working...] "+f._.pageTitleOrig;
      btnRender.setAttribute('disabled','disabled');
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

    const eOut = E('#pikchr-output');
    const eOutWrapper = E('#pikchr-output-wrapper');
    PS.addMsgHandler('pikchr', function(ev){
      const m = ev.data;
      eOut.classList[m.isError ? 'add' : 'remove']('error');
      eOut.dataset.pikchr = m.pikchr;
      let content;
      let sz;
      switch(PS.renderMode){
          case 'text':
            content = '<textarea>'+m.result+'</textarea>';
            eOut.classList.add('text');
            eOutWrapper.classList.add('text');
            break;
          default:
            content = m.result;
            eOut.classList.remove('text');
            eOutWrapper.classList.remove('text');
            break;
      }
      eOut.innerHTML = content;
      let vw = null, vh = null;
      if(!PS.config.renderAutoScale
         && !m.isError && 'html'===PS.renderMode){
        const svg = E(eOut,':scope > svg');
        const vb = svg ? svg.getAttribute('viewBox').split(' ') : false;
        if(vb && 4===vb.length){
          vw = (+vb[2] + 10)+'px';
          vh = (+vb[3] + 10)+'px';
        }else if(svg){
          console.warn("SVG element is missing viewBox attribute.");
        }
      }
      eOut.style.width = vw;
      eOut.style.height = vh;
    })/*'pikchr' msg handler*/;

    E('#btn-render-mode').addEventListener('click',function(){
      let mode = PS.renderMode;
      const modes = ['text','html'];
      let ndx = modes.indexOf(mode) + 1;
      if(ndx>=modes.length) ndx = 0;
      PS.renderMode = modes[ndx];
      if(eOut.dataset.pikchr){
        PS.render(eOut.dataset.pikchr);
      }
    });

    PS.addMsgHandler('working',function f(ev){
      switch(ev.data){
          case 'start': /* See notes in preStartWork(). */; return;
          case 'end':
            //preStartWork._.pageTitle.innerText = preStartWork._.pageTitleOrig;
            btnRender.innerText = preStartWork._.btnLabel;
            btnRender.removeAttribute('disabled');
            return;
      }
      console.warn("Unhandled 'working' event:",ev.data);
    });

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
    E('#opt-cb-autoscale').addEventListener('change',function(){
      /* PS.config.renderAutoScale was set by the data-config
         event handler. */
      if('html'==PS.renderMode && eOut.dataset.pikchr){
        PS.render(eOut.dataset.pikchr);
      }
    });
    /* For each button with data-cmd=X, map a click handler which
       calls PS.render(X). */
    const cmdClick = function(){PS.render(this.dataset.cmd);};
    EAll('button[data-cmd]').forEach(
      e => e.addEventListener('click', cmdClick, false)
    );

    /**
       TODO: Handle load/import of an external pikchr file.
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
      const btnToggle = E(fs,'legend > #btn-options-toggle'),
            content = EAll(fs,':scope > div');
      btnToggle.addEventListener('click', function(){
        fs.classList.toggle('collapsed');
        content.forEach((d)=>d.classList.toggle('hidden'));
      }, false);
    });

    btnRender.click();
    
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
        E('body > div.header'),
        E('body > div.mainmenu'),
        E('body > div.footer')
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
})(window.fossil);
