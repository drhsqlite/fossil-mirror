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

	.initialText = initial text of the element. Defaults
    to the result of the element's .value (for INPUT tags) or
    innerHTML (for everything else).

	.confirmText = text to show when in "confirm mode".
	Default=("Confirm: "+initialText), or something similar.

	.timeout = Number of milliseconds to wait for confirmation.
	Default=3000.

	.onconfirm = function to call when clicked in confirm mode.  Default
	= undefined. The function's "this" is the the DOM element to which the
	countdown applies.

	.ontimeout = function to call when confirm is not issued. Default =
	undefined. The function's "this" is the DOM element to which the
	countdown applies.

	.onactivate = function to call when item is clicked, but only if the
	item is not currently in countdown mode. This is called (and must
	return) before the countdown starts. The function's "this" is the
	DOM element to which the countdown applies. This can be used, e.g.,
  to change the element's text or CSS classes.

	.classInitial = optional CSS class string (default='') which
	is added to the element during its "initial" state (the state
	it is in when it is not waiting on a timeout). When the target
	is activated (waiting on a timeout) this class is removed.
	In the case of a timeout, this class is added *before* the
	.ontimeout handler is called.

	.classActivated = optional CSS class string (default='') which
	is added to the target when it is waiting on a timeout. When
	the target leaves timeout-wait mode, this class is removed.
	When timeout-wait mode is entered, this class is added *before*
	the .onactivate handler is called.

  .debug = boolean. If truthy, it sends some debug output
  to the dev console to track what it's doing.


Due to the nature of multi-threaded code, it is potentially possible
that confirmation and timeout actions BOTH happen if the user triggers
the associated action at "just the right millisecond" before the
timeout is triggered.

To change the default option values, modify the
fossil.confirmer.defaultOpts object.

Terse Change history:

20200506:
  - Ported from jQuery to plain JS.

- 20181112:
  - extended to support certain INPUT elements.
  - made default opts configurable.

- 20070717: initial jQuery-based impl.
*/
(function(F/*the fossil object*/){
  "use strict";

  F.confirmer = function f(elem,opt){

    const dbg = opt.debug
          ? function(){console.debug.apply(console,arguments)}
          : function(){};
    dbg("confirmer opt =",opt);
    if(!f.Holder){
      f.isInput = (e)=>/^(input|textarea)$/i.test(e.nodeName);
      f.Holder = function(target,opt){
        const self = this;
        self.target = target;
        self.opt = opt;
        self.timerID = undefined;
        self.state = this.states.initial;
        const isInput = f.isInput(target);
        const updateText = function(msg){
          if(isInput) target.value = msg;
          else target.innerHTML = msg;
        }
        updateText(self.opt.initialText);
        this.setClasses(false);
		    this.doTimeout = function() {
			    this.timerID = undefined;
			    if( this.state != this.states.waiting ) {
				    // it was already confirmed
				    return;
			    }
			    this.setClasses( false );
			    this.state = this.states.initial;
			    dbg("Timeout triggered.");
			    updateText(this.opt.initialText);
			    if( this.opt.ontimeout ) {
				    this.opt.ontimeout.call(this.target);
			    }
		    };
        target.addEventListener(
          'click', function(){
			      switch( self.state ) {
				    case( self.states.waiting ):
					    if( undefined !== self.timerID ) clearTimeout( self.timerID );
					    self.state = self.states.initial;
					    self.setClasses( false );
					    dbg("Confirmed");
					    updateText(self.opt.initialText);
					    if( self.opt.onconfirm ) self.opt.onconfirm.call(self.target);
					    break;
				    case( self.states.initial ):
					    self.setClasses( true );
					    if( self.opt.onactivate ) self.opt.onactivate.call( self.target );
					    self.state = self.states.waiting;
					    dbg("Waiting "+self.opt.timeout+"ms on confirmation...");
					    updateText( self.opt.confirmText );
					    self.timerID = setTimeout(function(){self.doTimeout();},self.opt.timeout );
					    break;
				    default: // can't happen.
					    break;
			      }
          }, false
        );
      };
      f.Holder.prototype = {
        states:{
          initial: 0, waiting: 1
        },
        setClasses: function(activated) {
			    if( activated ) {
				    if( this.opt.classActivated ) {
					    this.target.addClass( this.opt.classActivated );
				    }
				    if( this.opt.classInitial ) {
					    this.target.removeClass( this.opt.classInitial );
				    }
			    } else {
				    if( this.opt.classInitial ) {
					    this.target.addClass( this.opt.classInitial );
				    }
				    if( this.opt.classActivated ) {
					    this.target.removeClass( this.opt.classActivated );
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
    new f.Holder(elem,opt);
    return this;
  };
  /**
     The default options for initConfirmer(). Tweak them to set the
     defaults. A couple of them (initialText and confirmText) are
     dynamically-generated, and can't reasonably be set in the
     defaults.
  */
  F.confirmer.defaultOpts = {
	  timeout:3000,
	  onconfirm:undefined,
	  ontimeout:undefined,
	  onactivate:undefined,
	  classInitial:'',
	  classActivated:'',
	  debug:true
  };

})(window.fossil);
