"use strict";
/**************************************************************
Confirmer is a utility which provides an alternative to confirmation
dialog boxes and "check this checkbox to confirm action" widgets. It
acts by modifying a button to require two clicks within a certain
time, with the second click acting as a confirmation of the first. If
the second click does not come within a specified timeout then the
action is not confirmed.

Usage:

fossil.confirmer(domElement, options);

Usually:

fossil.confirmer(element, {
  onconfirm: function(){
    // this === the element.
    // Do whatever the element would normally do when
    // clicked.
  }
});

Options:

  .initialText = initial text of the element. Defaults to the result
  of the element's .value (for INPUT tags) or innerHTML (for
  everything else). After the timeout/tick count expires, or if the
  user confirms the operation, the element's text is re-set to this
  value.

  .confirmText = text to show when in "confirm mode".
  Default=("Confirm: "+initialText), or something similar.

  .timeout = Number of milliseconds to wait for confirmation.
  Default=3000. Alternately, use a combination of .ticks and
  .ticktime.

  .onconfirm = function to call when clicked in confirm mode. Default
  = undefined. The function's "this" is the DOM element to which
  the countdown applies.

  .ontimeout = function to call when confirm is not issued. Default =
  undefined. The function's "this" is the DOM element to which the
  countdown applies.

  .onactivate = function to call when item is clicked, but only if the
  item is not currently in countdown mode. This is called (and must
  return) before the countdown starts. The function's "this" is the
  DOM element to which the countdown applies. This can be used, e.g.,
  to change the element's text or CSS classes.

  .classInitial = optional CSS class string (default='') which is
  added to the element during its "initial" state (the state it is in
  when it is not waiting on a timeout). When the target is activated
  (waiting on a timeout) this class is removed.  In the case of a
  timeout, this class is added *before* the .ontimeout handler is
  called.

  .classWaiting = optional CSS class string (default='') which is
  added to the target when it is waiting on a timeout. When the target
  leaves timeout-wait mode, this class is removed.  When timeout-wait
  mode is entered, this class is added *before* the .onactivate
  handler is called.

  .ticktime = a number of ms to wait per tick (see the next item).
  Default = 1000.

  .ticks = a number of "ticks" to wait, as an alternative to .timeout.
  When this mode is active, the ontick callback will be triggered
  immediately before each tick, including the first one. If both
  .ticks and .timeout are set, only one will be used, but which one is
  unspecified. If passed a ticks value with a truncated integer value
  of 0 or less, it will throw an exception (e.g. that also applies if
  it's passed 0.5).

  .ontick = when using .ticks, this callback is passed the current
  tick number before each tick, and its "this" is the target
  element. On each subsequent call, the tick count will be reduced by
  1, and it is passed 0 after the final tick expires or when the
  action has been confirmed, immediately before the onconfirm or
  ontimeout callback. The intention of the callback is to update the
  label of the target element. If .ticks is set but .ontick is not
  then a default implementation is used which updates the element with
  the .confirmText, prepending a countdown to it.

  .pinSize = if true AND confirmText is set, calculate the larger of
  the element's original and confirmed size and pin it to the larger
  of those sizes to avoid layout reflows when confirmation is
  running. The pinning is implemented by setting its minWidth and
  maxWidth style properties to the same value. This does not work if
  the element text is updated dynamically via ontick(). This ONLY
  works if the element is in the DOM and is not hidden (e.g. via
  display:none) at the time this routine is called, otherwise we
  cannot calculate its size. If the element needs to be hidden, hide
  it after initializing the confirmer.

  .debug = boolean. If truthy, it sends some debug output to the dev
  console to track what it's doing.

Various notes:

- To change the default option values, modify the
  fossil.confirmer.defaultOpts object.

- Exceptions triggered via the callbacks are caught and emitted to the
  dev console if the debug option is enabled, but are otherwise
  ignored.

- Due to the nature of multi-threaded code, it is potentially possible
  that confirmation and timeout actions BOTH happen if the user
  triggers the associated action at "just the right millisecond"
  before the timeout is triggered.

TODO:

- Add an invert option which activates if the timeout is reached and
"times out" if the element is clicked again. e.g. a button which says
"Saving..." and cancels the op if it's clicked again, else it saves
after X time/ticks.

- Internally we save/restore the initial text of non-INPUT elements
using a relatively expensive bit of DOMParser hoop-jumping. We
"should" instead move their child nodes aside (into an internal
out-of-DOM element) and restore them as needed.

Terse Change history:

- 20200811
  - Added pinSize option.

- 20200507:
  - Add a tick-based countdown in order to more easily support
    updating the target element with the countdown.

- 20200506:
  - Ported from jQuery to plain JS.

- 20181112:
  - extended to support certain INPUT elements.
  - made default opts configurable.

- 20070717: initial jQuery-based impl.
*/
(function(F/*the fossil object*/){
  F.confirmer = function f(elem,opt){
    const dbg = opt.debug
          ? function(){console.debug.apply(console,arguments)}
          : function(){};
    dbg("confirmer opt =",opt);
    if(!f.Holder){
      f.isInput = (e)=>/^(input|textarea)$/i.test(e.nodeName);
      f.Holder = function(target,opt){
        const self = this;
        this.target = target;
        this.opt = opt;
        this.timerID = undefined;
        this.state = this.states.initial;
        const isInput = f.isInput(target);
        const updateText = function(msg){
          if(isInput) target.value = msg;
          else{
            /* Jump through some hoops to avoid assigning to innerHTML... */
            const newNode = new DOMParser().parseFromString(msg, 'text/html');
            let childs = newNode.documentElement.querySelector('body');
            childs = childs ? Array.prototype.slice.call(childs.childNodes, 0) : [];
            target.innerText = '';
            childs.forEach((e)=>target.appendChild(e));
          }
        }
        const formatCountdown = (txt, number) => txt + " ["+number+"]";
        if(opt.pinSize && opt.confirmText){
          /* Try to pin the element's width the the greater of its
             current width or its waiting-on-confirmation width
             to avoid layout reflow when it's activated. */
          const digits = (''+(opt.timeout/1000 || opt.ticks)).length;
          const lblLong = formatCountdown(opt.confirmText, "00000000".substr(0,digits+1));
          const w1 = parseInt(target.getBoundingClientRect().width);
          updateText(lblLong);
          const w2 = parseInt(target.getBoundingClientRect().width);
          if(w1 || w2){
            /* If target is not in visible part of the DOM, those values may be 0. */
            target.style.minWidth = target.style.maxWidth = (w1>w2 ? w1 : w2)+"px";
          }
        }
        updateText(this.opt.initialText);
        if(this.opt.ticks && !this.opt.ontick){
          this.opt.ontick = function(tick){
            updateText(formatCountdown(self.opt.confirmText,tick));
          };
        }
        this.setClasses(false);
        this.doTimeout = function() {
          if(this.timerID){
            clearTimeout( this.timerID );
            delete this.timerID;
          }
          if( this.state != this.states.waiting ) {
            // it was already confirmed
            return;
          }
          this.setClasses( false );
          this.state = this.states.initial;
          dbg("Timeout triggered.");
          if( this.opt.ontick ){
            try{this.opt.ontick.call(this.target, 0)}
            catch(e){dbg("ontick EXCEPTION:",e)}
          }
          if( this.opt.ontimeout ) {
            try{this.opt.ontimeout.call(this.target)}
            catch(e){dbg("ontimeout EXCEPTION:",e)}
          }
          updateText(this.opt.initialText);
        };
        target.addEventListener(
          'click', function(){
            switch( self.state ) {
            case( self.states.waiting ):
              /* Cancel the wait on confirmation */
              if( undefined !== self.timerID ){
                clearTimeout( self.timerID );
                delete self.timerID;
              }
              self.state = self.states.initial;
              self.setClasses( false );
              dbg("Confirmed");
              if( self.opt.ontick ){
                try{self.opt.ontick.call(self.target,0)}
                catch(e){dbg("ontick EXCEPTION:",e)}
              }
              if( self.opt.onconfirm ){
                try{self.opt.onconfirm.call(self.target)}
                catch(e){dbg("onconfirm EXCEPTION:",e)}
              }
              updateText(self.opt.initialText);
              break;
            case( self.states.initial ):
              /* Enter the waiting-on-confirmation state... */
              if(self.opt.ticks) self.opt.currentTick = self.opt.ticks;
              self.setClasses( true );
              self.state = self.states.waiting;
              updateText( self.opt.confirmText );
              if( self.opt.onactivate ) self.opt.onactivate.call( self.target );
              if( self.opt.ontick ) self.opt.ontick.call(self.target, self.opt.currentTick);
              if(self.opt.timeout){
                dbg("Waiting "+self.opt.timeout+"ms on confirmation...");
                self.timerID =
                  setTimeout(()=>self.doTimeout(),self.opt.timeout );
              }else if(self.opt.ticks){
                dbg("Waiting on confirmation for "+self.opt.ticks
                    +" ticks of "+self.opt.ticktime+"ms each...");
                self.timerID =
                  setInterval(function(){
                    if(0===--self.opt.currentTick) self.doTimeout();
                    else{
                      try{self.opt.ontick.call(self.target,
                                               self.opt.currentTick)}
                      catch(e){dbg("ontick EXCEPTION:",e)}
                    }
                  },self.opt.ticktime);
              }
              break;
            default: // can't happen.
              break;
            }
          }, false
        );
      };
      f.Holder.prototype = {
        states:{initial: 0, waiting: 1},
        setClasses: function(activated) {
          if(activated) {
            if( this.opt.classWaiting ) {
              this.target.classList.add( this.opt.classWaiting );
            }
            if( this.opt.classInitial ) {
              this.target.classList.remove( this.opt.classInitial );
            }
          }else{
            if( this.opt.classInitial ) {
              this.target.classList.add( this.opt.classInitial );
            }
            if( this.opt.classWaiting ) {
              this.target.classList.remove( this.opt.classWaiting );
            }
          }
        }
      };
    }/*static init*/
    opt = F.mergeLastWins(f.defaultOpts,{
      initialText: (
        f.isInput(elem) ? elem.value : elem.innerHTML
      ) || "PLEASE SET .initialText"
    },opt);
    if(!opt.confirmText){
      opt.confirmText = "Confirm: "+opt.initialText;
    }
    if(opt.ticks){
      delete opt.timeout;
      opt.ticks = 0 | opt.ticks /* ensure it's an integer */;
      if(opt.ticks<=0){
        throw new Error("ticks must be >0");
      }
      if(opt.ticktime <= 0) opt.ticktime = 1000;
    }else{
      delete opt.ontick;
      delete opt.ticks;
    }
    new f.Holder(elem,opt);
    return this;
  };
  /**
     The default options for initConfirmer(). Tweak them to set the
     defaults. A couple of them (initialText and confirmText) are
     dynamically-generated, and can't reasonably be set in the
     defaults. Some, like ticks, cannot be set here because that would
     end up indirectly replacing non-tick timeouts with ticks.
  */
  F.confirmer.defaultOpts = {
    timeout:undefined,
    ticks: 3,
    ticktime: 998/*not *quite* 1000*/,
    onconfirm: undefined,
    ontimeout: undefined,
    onactivate: undefined,
    classInitial: '',
    classWaiting: '',
    debug: false
  };

})(window.fossil);
