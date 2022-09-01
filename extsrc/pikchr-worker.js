/*
  2022-05-20

  The author disclaims copyright to this source code.  In place of a
  legal notice, here is a blessing:

  *   May you do good and not evil.
  *   May you find forgiveness for yourself and forgive others.
  *   May you share freely, never taking more than you give.

  ***********************************************************************

  This is a JS Worker file for use with the pikchr wasm build. It
  loads the pikchr wasm module and offers access to it via the Worker
  message-passing interface.

  Because we can have only a single message handler, as opposed to an
  arbitrary number of discrete event listeners like with DOM elements,
  we have to define a lower-level message API. Messages abstractly
  look like:

  { type: string, data: type-specific value }

  Where 'type' is used for dispatching and 'data' is a
  'type'-dependent value.

  The 'type' values expected by each side of the main/worker
  connection vary. The types are described below but subject to
  change at any time as this experiment evolves.

  Main-to-Worker message types:

  - pikchr: data=pikchr-format text to render or an object:

  {
    pikchr: source code for the pikchr,
    darkMode: boolean true to adjust colors for a dark color scheme,
    cssClass: CSS class name to add to the SVG
  }

  Workers-to-Main types

  - stdout, stderr: indicate stdout/stderr output from the wasm
  layer. The data property is the string of the output, noting
  that the emscripten binding emits these one line at a time. Thus,
  if a C-side puts() emits multiple lines in a single call, the JS
  side will see that as multiple calls. Example:

  {type:'stdout', data: 'Hi, world.'}

  - module: Status text. This is intended to alert the main thread
  about module loading status so that, e.g., the main thread can
  update a progress widget and DTRT when the module is finished
  loading and available for work. Status messages come in the form
  
  {type:'module', data:{
  type:'status',
  data: {text:string|null, step:1-based-integer}
  }

  with an incrementing step value for each subsequent message. When
  the module loading is complete, a message with a text value of
  null is posted.

  - pikchr: 

  {type: 'pikchr',
    data:{
      pikchr: input text,
      result: rendered result (SVG on success, HTML on error),
      isError: bool, true if .pikchr holds an error report,
      flags: integer: flags used to configure the pikchr rendering,
      width: if !isError, width (integer pixels) of the SVG,
      height: if !isError, height (integer pixels) of the SVG
    }
  }

*/

"use strict";
(function(){
  /**
     Posts a message in the form {type,data} unless passed more than
     2 args, in which case it posts {type, data:[arg1...argN]}.
  */
  const wMsg = function(type,data){
    postMessage({
      type,
      data: arguments.length<3
        ? data
        : Array.prototype.slice.call(arguments,1)
    });
  };

  const stderr = function(){wMsg('stderr', Array.prototype.slice.call(arguments));};

  self.onerror = function(/*message, source, lineno, colno, error*/) {
    const err = arguments[4];
    if(err && 'ExitStatus'==err.name){
      /* This "cannot happen" for this wasm binding, but just in
         case... */
      pikchrModule.isDead = true;
      stderr("FATAL ERROR:", err.message);
      stderr("Restarting the app requires reloading the page.");
      wMsg('error', err);
    }
    pikchrModule.setStatus('Exception thrown, see JavaScript console: '+err);
  };

  self.onmessage = function f(ev){
    ev = ev.data;
    switch(ev.type){
          /**
             Runs the given text through pikchr and emits a 'pikchr'
             message result (output format documented above).

             Fires a working/start event before it starts and
             working/end event when it finishes.
          */
        case 'pikchr':
          if(pikchrModule.isDead){
            stderr("wasm module has exit()ed. Cannot pikchr.");
            return;
          }
          if(!f._){
            f._ = pikchrModule.cwrap('pikchr', 'string', [
              'string'/*script*/, 'string'/*CSS class*/, 'number'/*flags*/,
              'number'/*output: SVG width*/, 'number'/*output: SVG height*/
            ]);
          }
          wMsg('working','start');
          const stack = pikchrModule.stackSave();
          try {
            const pnWidth = pikchrModule.stackAlloc(4),
                  pnHeight = pikchrModule.stackAlloc(4);
            let script = '', flags = 0, cssClass = null;
            if('string'===typeof ev.data){
              script = ev.data;
            }else if(ev.data && 'object'===typeof ev.data){
              script = ev.data.pikchr;
              flags = ev.data.darkMode ? 0x02 : 0;
              if(ev.data.cssClass) cssClass = ev.data.cssClass;
            }
            pikchrModule.setValue(pnWidth, 0, "i32");
            pikchrModule.setValue(pnHeight, 0, "i32");
            const msg = {
              pikchr: script,
              result: (f._(script, cssClass, flags, pnWidth, pnHeight) || "").trim(),
              flags: flags
            };
            msg.isError = !!(msg.result && msg.result.startsWith('<div'));
            if(msg.isError){
              msg.width = msg.height = null;
            }else{
              msg.width = pikchrModule.getValue(pnWidth, "i32");
              msg.height = pikchrModule.getValue(pnHeight, "i32");
            }
            wMsg('pikchr', msg);
          } finally {
            pikchrModule.stackRestore(stack);
            wMsg('working','end');
          }
          return;
    };
    console.warn("Unknown pikchr-worker message type:",ev);
  };
  
  /**
     emscripten module for use with build mode -sMODULARIZE.
  */
  const pikchrModule = {
    print: function(){wMsg('stdout', Array.prototype.slice.call(arguments));},
    printErr: stderr,
    /**
       Intercepts status updates from the emscripting module init
       and fires worker events with a type of 'status' and a
       payload of:

       {
       text: string | null, // null at end of load process
       step: integer // starts at 1, increments 1 per call
       }

       We have no way of knowing in advance how many steps will
       be processed/posted, so creating a "percentage done" view is
       not really practical. One can be approximated by giving it a
       current value of message.step and max value of message.step+1,
       though.

       When work is finished, a message with a text value of null is
       submitted.

       After a message with text==null is posted, the module may later
       post messages about fatal problems, e.g. an exit() being
       triggered, so it is recommended that UI elements for posting
       status messages not be outright removed from the DOM when
       text==null, and that they instead be hidden until/unless
       text!=null.
    */
    setStatus: function f(text){
      if(!f.last) f.last = { step: 0, text: '' };
      else if(text === f.last.text) return;
      f.last.text = text;
      wMsg('module',{
        type:'status',
        data:{step: ++f.last.step, text: text||null}
      });
    }
  };

  importScripts('pikchr.js');
  /**
     initPikchrModule() is installed via pikchr.js due to
     building with:

     emcc ... -sMODULARIZE=1 -sEXPORT_NAME=initPikchrModule
  */
  initPikchrModule(pikchrModule).then(function(thisModule){
    wMsg('pikchr-ready');
  });
})();
