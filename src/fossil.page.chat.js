-/**
   This file contains the client-side implementation of fossil's /chat
   application.
*/
window.fossil.onPageLoad(function(){
  const F = window.fossil, D = F.dom;
  const E1 = function(selector){
    const e = document.querySelector(selector);
    if(!e) throw new Error("missing required DOM element: "+selector);
    return e;
  };

  /**
     Returns true if e is entirely within the bounds of the window's viewport.
  */
  const isEntirelyInViewport = function(e) {
    const rect = e.getBoundingClientRect();
    return (
      rect.top >= 0 &&
      rect.left >= 0 &&
      rect.bottom <= (window.innerHeight || document.documentElement.clientHeight) &&
      rect.right <= (window.innerWidth || document.documentElement.clientWidth)
    );
  };

  /**
     Returns true if e's on-screen position vertically overlaps that
     of element v's. Horizontal overlap is ignored (we don't need it
     for our case).
  */
  const overlapsElemView = function(e,v) {
    const r1 = e.getBoundingClientRect(),
          r2 = v.getBoundingClientRect();
    if(r1.top<=r2.bottom && r1.top>=r2.top) return true;
    else if(r1.bottom<=r2.bottom && r1.bottom>=r2.top) return true;
    return false;
  };

  const addAnchorTargetBlank = (e)=>D.attr(e, 'target','_blank');

  /**
     Returns an almost-ISO8601 form of Date object d.
  */
  const iso8601ish = function(d){
    return d.toISOString()
        .replace('T',' ').replace(/\.\d+/,'')
        .replace('Z', ' zulu');
  };
  const pad2 = (x)=>('0'+x).substr(-2);
  /** Returns the local time string of Date object d, defaulting
      to the current time. */
  const localTimeString = function ff(d){
    d || (d = new Date());
    return [
      d.getFullYear(),'-',pad2(d.getMonth()+1/*sigh*/),
      '-',pad2(d.getDate()),
      ' ',pad2(d.getHours()),':',pad2(d.getMinutes()),
      ':',pad2(d.getSeconds())
    ].join('');
  };

  (function(){
    let dbg = document.querySelector('#debugMsg');
    if(dbg){
      /* This can inadvertently influence our flexbox layouts, so move
         it out of the way. */
      D.append(document.body,dbg);
    }
  })();
  /* Returns a list of DOM elements which "frame" the chat UI. These
     elements are considered to _not_ be part of the chat UI and that
     info is used for sizing the chat UI. In chat-only mode, these are
     the elements that get hidden. */
  const GetFramingElements = function() {
    return document.querySelectorAll([
      "body > header",
      "body > nav.mainmenu",
      "body > footer",
      "#debugMsg"
    ].join(','));
  };
  const ForceResizeKludge = (function(){
    /* Workaround for Safari mayhem regarding use of vh CSS units....
       We tried to use vh units to set the content area size for the
       chat layout, but Safari chokes on that, so we calculate that
       height here: 85% when in "normal" mode and 95% in chat-only
       mode. Larger than ~95% is too big for Firefox on Android,
       causing the input area to move off-screen.

       While we're here, we also use this to cap the max-height
       of the input field so that pasting huge text does not scroll
       the upper area of the input widget off-screen. */
    const elemsToCount = GetFramingElements();
    const contentArea = E1('div.content');
    const bcl = document.body.classList;
    const resized = function f(){
      if(f.$disabled) return;
      const wh = window.innerHeight,
            com = bcl.contains('chat-only-mode');
      var ht;
      var extra = 0;
      if(com){
        ht = wh;
      }else{
        elemsToCount.forEach((e)=>e ? extra += D.effectiveHeight(e) : false);
        ht = wh - extra;
      }
      f.chat.e.inputX.style.maxHeight = (ht/2)+"px";
      /* ^^^^ this is a middle ground between having no size cap
         on the input field and having a fixed arbitrary cap. */;
      contentArea.style.height =
        contentArea.style.maxHeight = [
          "calc(", (ht>=100 ? ht : 100), "px",
          " - 0.65em"/*fudge value*/,")"
          /* ^^^^ hypothetically not needed, but both Chrome/FF on
             Linux will force scrollbars on the body if this value is
             too small; current value is empirically selected. */
        ].join('');
      if(false){
        console.debug("resized.",wh, extra, ht,
                      window.getComputedStyle(contentArea).maxHeight,
                      contentArea);
        console.debug("Set input max height to: ",
                      f.chat.e.inputX.style.maxHeight);
      }
    };
    resized.$disabled = true/*gets deleted when setup is finished*/;
    window.addEventListener('resize', F.debounce(resized, 250), false);
    return resized;
  })();
  fossil.FRK = ForceResizeKludge/*for debugging*/;
  const Chat = ForceResizeKludge.chat = (function(){
    const cs = { // the "Chat" object (result of this function)
      beVerbose: false
      //!!window.location.hostname.match("localhost")
      /* if true then certain, mostly extraneous, error messages and
         log messages may be sent to the console. */,
      playedBeep: false /* used for the beep-once setting */,
      e:{/*map of certain DOM elements.*/
        messageInjectPoint: E1('#message-inject-point'),
        pageTitle: E1('head title'),
        loadOlderToolbar: undefined /* the load-posts toolbar (dynamically created) */,
        inputArea: E1("#chat-input-area"),
        inputLineWrapper: E1('#chat-input-line-wrapper'),
        fileSelectWrapper: E1('#chat-input-file-area'),
        viewMessages: E1('#chat-messages-wrapper'),
        btnSubmit: E1('#chat-button-submit'),
        btnAttach: E1('#chat-button-attach'),
        inputX: E1('#chat-input-field-x'),
        input1: E1('#chat-input-field-single'),
        inputM: E1('#chat-input-field-multi'),
        inputFile: E1('#chat-input-file'),
        contentDiv: E1('div.content'),
        viewConfig: E1('#chat-config'),
        viewPreview: E1('#chat-preview'),
        previewContent: E1('#chat-preview-content'),
        viewSearch: E1('#chat-search'),
        searchContent: E1('#chat-search-content'),
        btnPreview: E1('#chat-button-preview'),
        views: document.querySelectorAll('.chat-view'),
        activeUserListWrapper: E1('#chat-user-list-wrapper'),
        activeUserList: E1('#chat-user-list'),
        eMsgPollError: undefined /* current connection error MessageMidget */,
        pollErrorMarker: document.body /* element to toggle 'connection-error' CSS class on */
      },
      me: F.user.name,
      mxMsg: F.config.chat.initSize ? -F.config.chat.initSize : -50,
      mnMsg: undefined/*lowest message ID we've seen so far (for history loading)*/,
      pageIsActive: 'visible'===document.visibilityState,
      changesSincePageHidden: 0,
      notificationBubbleColor: 'white',
      totalMessageCount: 0, // total # of inbound messages
      //! Number of messages to load for the history buttons
      loadMessageCount: Math.abs(F.config.chat.initSize || 20),
      ajaxInflight: 0,
      usersLastSeen:{
        /* Map of user names to their most recent message time
           (JS Date object). Only messages received by the chat client
           are considered. */
        /* Reminder: to convert a Julian time J to JS:
           new Date((J - 2440587.5) * 86400000) */
      },
      filterState:{
        activeUser: undefined,
        match: function(uname){
          return this.activeUser===uname || !this.activeUser;
        }
      },
      /**
         The timer object is used to control connection throttling
         when connection errors arrise. It starts off with a polling
         delay of $initialDelay ms. If there's a connection error,
         that gets bumped by some value for each subsequent error, up
         to some max value.

         The timing of resetting the delay when service returns is,
         because of the long-poll connection and our lack of low-level
         insight into the connection at this level, a bit wonky.
      */
      timer:{
        /* setTimeout() ID for (delayed) starting a Chat.poll(), so
           that it runs at controlled intervals (which change when a
           connection drops and recovers). */
        tidPendingPoll: undefined,
        tidClearPollErr: undefined /*setTimeout() timer id for
                                     reconnection determination. See
                                     clearPollErrOnWait(). */,
        $initialDelay: 1000 /* initial polling interval (ms) */,
        currentDelay: 1000 /* current polling interval */,
        maxDelay: 60000 * 5 /* max interval when backing off for
                              connection errors */,
        minDelay: 5000 /* minimum delay time for a back-off/retry
                          attempt. */,
        errCount: 0 /* Current poller connection error count */,
        minErrForNotify: 4 /* Don't warn for connection errors until this
                              many have occurred */,
        pollTimeout: (1 && window.location.hostname.match(
          "localhost" /*presumably local dev mode*/
        )) ? 15000
          : (+F.config.chat.pollTimeout>0
             ? (1000 * (F.config.chat.pollTimeout - Math.floor(F.config.chat.pollTimeout * 0.1)))
             /* ^^^^^^^^^^^^ we want our timeouts to be slightly shorter
                than the server's so that we can distingished timed-out
                polls on our end from HTTP errors (if the server times
                out). */
             : 30000),
        /** Returns a random fudge value for reconnect attempt times,
            intended to keep the /chat server from getting hammered if
            all clients which were just disconnected all reconnect at
            the same instant. */
        randomInterval: function(factor){
          return Math.floor(Math.random() * factor);
        },
        /** Increments the reconnection delay, within some min/max range. */
        incrDelay: function(){
          if( this.maxDelay > this.currentDelay ){
            if(this.currentDelay < this.minDelay){
              this.currentDelay = this.minDelay + this.randomInterval(this.minDelay);
            }else{
              this.currentDelay = this.currentDelay*2 + this.randomInterval(this.currentDelay);
            }
          }
          return this.currentDelay;
        },
        /** Resets the delay counter to v || its initial value. */
        resetDelay: function(ms=0){
          return this.currentDelay = ms || this.$initialDelay;
        },
        /** Returns true if the timer is set to delayed mode. */
        isDelayed: function(){
          return (this.currentDelay > this.$initialDelay) ? this.currentDelay : 0;
        },
        /**
           Cancels any in-progress pending-poll timer and starts a new
           one with the given delay, defaulting to this.resetDelay().
        */
        startPendingPollTimer: function(delay){
          this.cancelPendingPollTimer().tidPendingPoll
            = setTimeout( Chat.poll, delay || Chat.timer.resetDelay() );
          return this;
        },
        /**
           Cancels any still-active timer set to trigger the next
           Chat.poll().
        */
        cancelPendingPollTimer: function(){
          if( this.tidPendingPoll ){
            clearTimeout(this.tidPendingPoll);
            this.tidPendingPoll = 0;
          }
          return this;
        },
        /**
           Cancels any pending reconnection attempt back-off timer..
        */
        cancelReconnectCheckTimer: function(){
          if( this.tidClearPollErr ){
            clearTimeout(this.tidClearPollErr);
            this.tidClearPollErr = 0;
          }
          return this;
        }
      },
      /**
         Gets (no args) or sets (1 arg) the current input text field
         value, taking into account single- vs multi-line input. The
         getter returns a trim()'d string and the setter returns this
         object. As a special case, if arguments[0] is a boolean
         value, it behaves like a getter and, if arguments[0]===true
         it clears the input field before returning.
      */
      inputValue: function(/*string newValue | bool clearInputField*/){
        const e = this.inputElement();
        if(arguments.length && 'boolean'!==typeof arguments[0]){
          if(e.isContentEditable) e.innerText = arguments[0];
          else e.value = arguments[0];
          return this;
        }
        const rc = e.isContentEditable ? e.innerText : e.value;
        if( true===arguments[0] ){
          if(e.isContentEditable) e.innerText = '';
          else e.value = '';
        }
        return rc && rc.trim();
      },
      /** Asks the current user input field to take focus. Returns this. */
      inputFocus: function(){
        this.inputElement().focus();
        return this;
      },
      /** Returns the current message input element. */
      inputElement: function(){
        return this.e.inputFields[this.e.inputFields.$currentIndex];
      },
      /** Enables (if yes is truthy) or disables all elements in
       * this.disableDuringAjax. */
      enableAjaxComponents: function(yes){
        D[yes ? 'enable' : 'disable'](this.disableDuringAjax);
        return this;
      },
      /* Must be called before any API is used which starts ajax traffic.
         If this call represents the currently only in-flight ajax request,
         all DOM elements in this.disableDuringAjax are disabled.
         We cannot do this via a central API because (1) window.fetch()'s
         Promise-based API seemingly makes that impossible and (2) the polling
         technique holds ajax requests open for as long as possible. A call
         to this method obligates the caller to also call ajaxEnd().

         This must NOT be called for the chat-polling API except, as a
         special exception, the very first one which fetches the
         initial message list.
      */
      ajaxStart: function(){
        if(1===++this.ajaxInflight){
          this.enableAjaxComponents(false);
        }
      },
      /* Must be called after any ajax-related call for which
         ajaxStart() was called, regardless of success or failure. If
         it was the last such call (as measured by calls to
         ajaxStart() and ajaxEnd()), elements disabled by a prior call
         to ajaxStart() will be re-enabled. */
      ajaxEnd: function(){
        if(0===--this.ajaxInflight){
          this.enableAjaxComponents(true);
        }
      },
      disableDuringAjax: [
        /* List of DOM elements disable while ajax traffic is in
           transit. Must be populated before ajax starts. We do this
           to avoid various race conditions in the UI and long-running
           network requests. */
      ],
      /** Either scrolls .message-widget element eMsg into view
          immediately or, if it represents an inlined image, delays
          the scroll until the image is loaded, at which point it will
          scroll to either the newest message, if one is set or to
          eMsg (the liklihood is good, at least on initial page load,
          that the the image won't be loaded until other messages have
          been injected). */
      scheduleScrollOfMsg: function(eMsg){
        if(1===+eMsg.dataset.hasImage){
          eMsg.querySelector('img').addEventListener(
            'load', ()=>(this.e.newestMessage || eMsg).scrollIntoView(false)
          );
        }else{
          eMsg.scrollIntoView(false);
        }
        return this;
      },
      /* Injects DOM element e as a new row in the chat, at the oldest
         end of the list if atEnd is truthy, else at the newest end of
         the list. */
      injectMessageElem: function f(e, atEnd){
        const mip = atEnd ? this.e.loadOlderToolbar : this.e.messageInjectPoint,
              holder = this.e.viewMessages,
              prevMessage = this.e.newestMessage;
        if(!this.filterState.match(e.dataset.xfrom)){
          e.classList.add('hidden');
        }
        if(atEnd){
          const fe = mip.nextElementSibling;
          if(fe) mip.parentNode.insertBefore(e, fe);
          else D.append(mip.parentNode, e);
        }else{
          D.append(holder,e);
          this.e.newestMessage = e;
        }
        if(!atEnd && !this._isBatchLoading
           && e.dataset.xfrom!==this.me
           && (prevMessage
               ? !this.messageIsInView(prevMessage)
               : false)){
          /* If a new non-history message arrives while the user is
             scrolled elsewhere, do not scroll to the latest
             message, but gently alert the user that a new message
             has arrived. */
          if(!f.btnDown){
            f.btnDown = D.button("⇣⇣⇣");
            f.btnDown.addEventListener('click',()=>this.scrollMessagesTo(1),false);
          }
          F.toast.message(f.btnDown," New message has arrived.");
        }else if(!this._isBatchLoading && e.dataset.xfrom===Chat.me){
          this.scheduleScrollOfMsg(e);
        }else if(!this._isBatchLoading){
          /* When a message from someone else arrives, we have to
             figure out whether or not to scroll it into view. Ideally
             we'd just stuff it in the UI and let the flexbox layout
             DTRT, but Safari has expressed, in no uncertain terms,
             some disappointment with that approach, so we'll
             heuristicize it: if the previous last message is in view,
             assume the user is at or near the input element and
             scroll this one into view. If that message is NOT in
             view, assume the user is up reading history somewhere and
             do NOT scroll because doing so would interrupt
             them. There are middle grounds here where the user will
             experience a slight UI jolt, but this heuristic mostly
             seems to work out okay. If there was no previous message,
             assume we don't have any messages yet and go ahead and
             scroll this message into view (noting that that scrolling
             is hypothetically a no-op in such cases).

             The wrench in these works are posts with IMG tags, as
             those images are loaded async and the element does not
             yet have enough information to know how far to scroll!
             For such cases we have to delay the scroll until the
             image loads (and we hope it does so before another
             message arrives).
          */
          if(1===+e.dataset.hasImage){
            e.querySelector('img').addEventListener('load',()=>e.scrollIntoView());
          }else if(!prevMessage || (prevMessage && isEntirelyInViewport(prevMessage))){
            e.scrollIntoView(false);
          }
        }
      },
      /** Returns true if chat-only mode is enabled. */
      isChatOnlyMode: ()=>document.body.classList.contains('chat-only-mode'),
      /**
         Enters (if passed a truthy value or no arguments) or leaves
         "chat-only" mode. That mode hides the page's header and
         footer, leaving only the chat application visible to the
         user.
      */
      chatOnlyMode: function f(yes){
        if(undefined === f.elemsToToggle){
          f.elemsToToggle = [];
          GetFramingElements().forEach((e)=>f.elemsToToggle.push(e));
        }
        if(!arguments.length) yes = true;
        if(yes === this.isChatOnlyMode()) return this;
        if(yes){
          D.addClass(f.elemsToToggle, 'hidden');
          D.addClass(document.body, 'chat-only-mode');
          document.body.scroll(0,document.body.height);
        }else{
          D.removeClass(f.elemsToToggle, 'hidden');
          D.removeClass(document.body, 'chat-only-mode');
        }
        ForceResizeKludge();
        return this;
      },
      /** Tries to scroll the message area to...
          <0 = top of the message list, >0 = bottom of the message list,
          0 == the newest message (normally the same position as >1).
      */
      scrollMessagesTo: function(where){
        if(where<0){
          Chat.e.viewMessages.scrollTop = 0;
        }else if(where>0){
          Chat.e.viewMessages.scrollTop = Chat.e.viewMessages.scrollHeight;
        }else if(Chat.e.newestMessage){
          Chat.e.newestMessage.scrollIntoView(false);
        }
      },
      toggleChatOnlyMode: function(){
        return this.chatOnlyMode(!this.isChatOnlyMode());
      },
      messageIsInView: function(e){
        return e ? overlapsElemView(e, this.e.viewMessages) : false;
      },
      settings:{
        get: (k,dflt)=>F.storage.get(k,dflt),
        getBool: (k,dflt)=>F.storage.getBool(k,dflt),
        set: function(k,v){
          F.storage.set(k,v);
          F.page.dispatchEvent('chat-setting',{key: k, value: v});
        },
        /* Toggles the boolean setting specified by k. Returns the
           new value.*/
        toggle: function(k){
          const v = this.getBool(k);
          this.set(k, !v);
          return !v;
        },
        addListener: function(setting, f){
          F.page.addEventListener('chat-setting', function(ev){
            if(ev.detail.key===setting) f(ev.detail);
          }, false);
        },
        /* Default values of settings. These are used for initializing
           the setting event listeners and config view UI. */
        defaults:{
          /* When on, inbound images are displayed inlined, else as a
             link to download the image. */
          "images-inline": !!F.config.chat.imagesInline,
          /* When on, ctrl-enter sends messages, else enter and
             ctrl-enter both send them. */
          "edit-ctrl-send": false,
          /* When on, the edit field starts as a single line and
             expands as the user types, and the relevant buttons are
             laid out in a compact form. When off, the edit field and
             buttons are larger. */
          "edit-compact-mode": true,
          /* See notes for this setting in fossil.page.wikiedit.js.
             Both /wikiedit and /fileedit share this persistent config
             option under the same storage key. */
          "edit-shift-enter-preview":
            F.storage.getBool('edit-shift-enter-preview', true),
          /* When on, sets the font-family on messages and the edit
             field to monospace. */
          "monospace-messages": false,
          /* When on, non-chat UI elements (page header/footer) are
             hidden */
          "chat-only-mode": false,
          /* When set to a URI, it is assumed to be an audio file,
             which gets played when new messages arrive. When true,
             the first entry in the audio file selection list will be
             used. */
          "audible-alert": true,
          /*
           */
          "beep-once": false,
          /* When on, show the list of "active" users - those from
             whom we have messages in the currently-loaded history
             (noting that deletions are also messages). */
          "active-user-list": false,
          /* When on, the [active-user-list] setting includes the
             timestamp of each user's most recent message. */
          "active-user-list-timestamps": false,
          /* When on, the [audible-alert] is played for one's own
             messages, else it is only played for other users'
             messages. */
          "alert-own-messages": false,
          /* "Experimental mode" input: use a contenteditable field
             for input. This is generally more comfortable to use,
             and more modern, than plain text input fields, but
             the list of browser-specific quirks and bugs is...
             not short. */
          "edit-widget-x": false
        }
      },
      /** Plays a new-message notification sound IF the audible-alert
          setting is true, else this is a no-op. Returns this.
      */
      playNewMessageSound: function f(){
        if(f.uri){
          if(!cs.pageIsActive
             /* ^^^ this could also arguably apply when chat is visible */
             && this.playedBeep && this.settings.getBool('beep-once',false)){
            return;
          }
          try{
            this.playedBeep = true;
            if(!f.audio) f.audio = new Audio(f.uri);
            f.audio.currentTime = 0;
            f.audio.play();
          }catch(e){
            console.error("Audio playblack failed.", f.uri, e);
          }
        }
        return this;
      },
      /**
         Sets the current new-message audio alert URI (must be a
         repository-relative path which responds with an audio
         file). Pass a falsy value to disable audio alerts. Returns
         this.
      */
      setNewMessageSound: function f(uri){
        this.playedBeep = false;
        delete this.playNewMessageSound.audio;
        this.playNewMessageSound.uri = uri;
        this.settings.set('audible-alert', uri);
        return this;
      },
      /**
         Expects e to be one of the elements in this.e.views.
         The 'hidden' class is removed from e and added to
         all other elements in that list. Returns e.
      */
      setCurrentView: function(e){
        if(e===this.e.currentView){
          return e;
        }
        this.e.views.forEach(function(E){
          if(e!==E) D.addClass(E,'hidden');
        });
        this.e.currentView = e;
        if(this.e.currentView.$beforeShow) this.e.currentView.$beforeShow();
        D.removeClass(e,'hidden');
        this.animate(this.e.currentView, 'anim-fade-in-fast');
        return this.e.currentView;
      },
      /**
         Updates the "active user list" view if we are not currently
         batch-loading messages and if the active user list UI element
         is active.
      */
      updateActiveUserList: function callee(){
        if(this._isBatchLoading
           || this.e.activeUserListWrapper.classList.contains('hidden')){
          return this;
        }else if(!callee.sortUsersSeen){
          /** Array.sort() callback. Expects an array of user names and
              sorts them in last-received message order (newest first). */
          const self = this;
          callee.sortUsersSeen = function(l,r){
            l = self.usersLastSeen[l];
            r = self.usersLastSeen[r];
            if(l && r) return r - l;
            else if(l) return -1;
            else if(r) return 1;
            else return 0;
          };
          callee.addUserElem = function(u){
            const uSpan = D.addClass(D.span(), 'chat-user');
            const uDate = self.usersLastSeen[u];
            if(self.filterState.activeUser===u){
              uSpan.classList.add('selected');
            }
            uSpan.dataset.uname = u;
            D.append(uSpan, u, "\n",
                     D.append(
                       D.addClass(D.span(),'timestamp'),
                       localTimeString(uDate)//.substr(5/*chop off year*/)
                     ));
            if(uDate.$uColor){
              uSpan.style.backgroundColor = uDate.$uColor;
            }
            D.append(self.e.activeUserList, uSpan);
          };
        }
        //D.clearElement(this.e.activeUserList);
        D.remove(this.e.activeUserList.querySelectorAll('.chat-user'));
        Object.keys(this.usersLastSeen).sort(
          callee.sortUsersSeen
        ).forEach(callee.addUserElem);
        return this;
      },
      /** Show or hide the active user list. Returns this object. */
      showActiveUserList: function(yes){
        if(0===arguments.length) yes = true;
        this.e.activeUserListWrapper.classList[
          yes ? 'remove' : 'add'
        ]('hidden');
        D.removeClass(Chat.e.activeUserListWrapper, 'collapsed');
        if(Chat.e.activeUserListWrapper.classList.contains('hidden')){
          /* When hiding this element, undo all filtering */
          Chat.setUserFilter(false);
          /*Ideally we'd scroll the final message into view
            now, but because viewMessages is currently hidden behind
            viewConfig, scrolling is a no-op. */
          Chat.scrollMessagesTo(1);
        }else{
          Chat.updateActiveUserList();
          Chat.animate(Chat.e.activeUserListWrapper, 'anim-flip-v');
        }
        return this;
      },
      showActiveUserTimestamps: function(yes){
        if(0===arguments.length) yes = true;
        this.e.activeUserList.classList[yes ? 'add' : 'remove']('timestamps');
        return this;
      },
      /**
         Applies user name filter to all current messages, or clears
         the filter if uname is falsy.
      */
      setUserFilter: function(uname){
        this.filterState.activeUser = uname;
        const mw = this.e.viewMessages.querySelectorAll('.message-widget');
        const self = this;
        let eLast;
        if(!uname){
          D.removeClass(Chat.e.viewMessages.querySelectorAll('.message-widget.hidden'),
                        'hidden');
        }else{
          mw.forEach(function(w){
            if(self.filterState.match(w.dataset.xfrom)){
              w.classList.remove('hidden');
              eLast = w;
            }else{
              w.classList.add('hidden');
            }
          });
        }
        if(eLast) eLast.scrollIntoView(false);
        else this.scrollMessagesTo(1);
        cs.e.activeUserList.querySelectorAll('.chat-user').forEach(function(e){
          e.classList[uname===e.dataset.uname ? 'add' : 'remove']('selected');
        });
        return this;
      },

      /**
         If animations are enabled, passes its arguments
         to D.addClassBriefly(), else this is a no-op.
         If cb is a function, it is called after the
         CSS class is removed. Returns this object;
      */
      animate: function f(e,a,cb){
        if(!f.$disabled){
          D.addClassBriefly(e, a, 0, cb);
        }
        return this;
      }
    }/*Chat object*/;
    cs.e.inputFields = [ cs.e.input1, cs.e.inputM, cs.e.inputX ];
    cs.e.inputFields.$currentIndex = 0;
    cs.e.inputFields.forEach(function(e,ndx){
      if(ndx===cs.e.inputFields.$currentIndex) D.removeClass(e,'hidden');
      else D.addClass(e,'hidden');
    });
    if(D.attr(cs.e.inputX,'contenteditable','plaintext-only').isContentEditable){
      cs.$browserHasPlaintextOnly = true;
    }else{
      /* Only the Chrome family supports contenteditable=plaintext-only */
      cs.$browserHasPlaintextOnly = false;
      D.attr(cs.e.inputX,'contenteditable','true');
    }
    cs.animate.$disabled = true;
    F.fetch.beforesend = ()=>cs.ajaxStart();
    F.fetch.aftersend = ()=>cs.ajaxEnd();
    cs.pageTitleOrig = cs.e.pageTitle.innerText;
    const qs = (e)=>document.querySelector(e);
    const argsToArray = function(args){
      return Array.prototype.slice.call(args,0);
    };
    /**
       Reports an error via console.error() and as a toast message.
       Accepts any argument types valid for fossil.toast.error().
    */
    cs.reportError = function(/*msg args*/){
      const args = argsToArray(arguments);
      console.error("chat error:",args);
      F.toast.error.apply(F.toast, args);
    };

    let InternalMsgId = 0;
    /**
       Reports an error in the form of a new message in the chat
       feed. All arguments are appended to the message's content area
       using fossil.dom.append(), so may be of any type supported by
       that function.
    */
    cs.reportErrorAsMessage = function f(/*msg args*/){
      const args = argsToArray(arguments).map(function(v){
        return (v instanceof Error) ? v.message : v;
      });
      if(Chat.beVerbose){
        console.error("chat error:",args);
      }
      const d = new Date().toISOString(),
            mw = new this.MessageWidget({
              isError: true,
              xfrom: undefined,
              msgid: "error-"+(++InternalMsgId),
              mtime: d,
              lmtime: d,
              xmsg: args
            });
      this.injectMessageElem(mw.e.body);
      mw.scrollIntoView();
      return mw;
    };

    /**
       For use by the connection poller to send a "connection
       restored" message.
    */
    cs.reportReconnection = function f(/*msg args*/){
      const args = argsToArray(arguments).map(function(v){
        return (v instanceof Error) ? v.message : v;
      });
      const d = new Date().toISOString(),
            mw = new this.MessageWidget({
              isError: false,
              xfrom: undefined,
              msgid: "reconnect-"+(++InternalMsgId),
              mtime: d,
              lmtime: d,
              xmsg: args
            });
      this.injectMessageElem(mw.e.body);
      mw.scrollIntoView();
      return mw;
    };

    cs.getMessageElemById = function(id){
      return qs('[data-msgid="'+id+'"]');
    };

    /** Finds the last .message-widget element and returns it or
        the undefined value if none are found. */
    cs.fetchLastMessageElem = function(){
      const msgs = document.querySelectorAll('.message-widget');
      var rc;
      if(msgs.length){
        rc = this.e.newestMessage = msgs[msgs.length-1];
      }
      return rc;
    };

    /**
       LOCALLY deletes a message element by the message ID or passing
       the .message-row element. Returns true if it removes an element,
       else false.
    */
    cs.deleteMessageElem = function(id, silent){
      var e;
      if(id instanceof HTMLElement){
        e = id;
        id = e.dataset.msgid;
        delete e.dataset.msgid;
        if( e?.dataset?.alsoRemove ){
          const xId = e.dataset.alsoRemove;
          delete e.dataset.alsoRemove;
          this.deleteMessageElem( xId );
        }
      }else if(id instanceof Chat.MessageWidget) {
        if( this.e.eMsgPollError === e ){
          this.e.eMsgPollError = undefined;
        }
        if(id.e?.body){
          this.deleteMessageElem(id.e.body);
        }
        return;
      } else{
        e = this.getMessageElemById(id);
      }
      if(e && id){
        D.remove(e);
        if(e===this.e.newestMessage){
          this.fetchLastMessageElem();
        }
        if( !silent ){
          F.toast.message("Deleted message "+id+".");
        }
      }
      return !!e;
    };

    /**
       Toggles the given message between its parsed and plain-text
       representations. It requires a server round-trip to collect the
       plain-text form but caches it for subsequent toggles.

       Expects the ID of a currently-loaded message or a
       message-widget DOM elment from which it can extract an id.
       This is an async operation the first time it's passed a given
       message and synchronous on subsequent calls for that
       message. It is a no-op if id does not resolve to a loaded
       message.
    */
    cs.toggleTextMode = function(id){
      var e;
      if(id instanceof HTMLElement){
        e = id;
        id = e.dataset.msgid;
      }else{
        e = this.getMessageElemById(id);
      }
      if(!e || !id) return false;
      else if(e.$isToggling) return;
      e.$isToggling = true;
      const content = e.querySelector('.content-target');
      if(!content){
        console.warn("Should not be possible: trying to toggle text",
                     "mode of a message with no .content-target.", e);
        return;
      }
      if(!content.$elems){
        content.$elems = [
          content.firstElementChild, // parsed elem
          undefined // plaintext elem
        ];
      }else if(content.$elems[1]){
        // We have both content types. Simply toggle them.
        const child = (
          content.firstElementChild===content.$elems[0]
            ? content.$elems[1]
            : content.$elems[0]
        );
        D.clearElement(content);
        if(child===content.$elems[1]){
          /* When showing the unformatted version, inject a
             copy-to-clipboard button. This is a workaround for
             mouse-copying from that field collecting twice as many
             newlines as it should (for unknown reasons). */
          const cpId = 'copy-to-clipboard-'+id;
          /* ^^^ copy button element ID, needed for LABEL element
             pairing.  Recall that we destroy all child elements of
             `content` each time we hit this block, so we can reuse
             that element ID on subsequent toggles. */
          const btnCp = D.attr(D.addClass(D.span(),'copy-button'), 'id', cpId);
          F.copyButton(btnCp, {extractText: ()=>child._xmsgRaw});
          const lblCp = D.label(cpId, "Copy unformatted text");
          lblCp.addEventListener('click',()=>btnCp.click(), false);
          D.append(content, D.append(D.addClass(D.span(), 'nobr'), btnCp, lblCp));
        }
        delete e.$isToggling;
        D.append(content, child);
        return;
      }
      // We need to fetch the plain-text version...
      const self = this;
      F.fetch('chat-fetch-one',{
        urlParams:{ name: id, raw: true},
        responseType: 'json',
        onload: function(msg){
          reportConnectionOkay('chat-fetch-one');
          content.$elems[1] = D.append(D.pre(),msg.xmsg);
          content.$elems[1]._xmsgRaw = msg.xmsg/*used for copy-to-clipboard feature*/;
          self.toggleTextMode(e);
        },
        aftersend:function(){
          delete e.$isToggling;
          Chat.ajaxEnd();
        }
      });
      return true;
    };

    /** Given a .message-row element, this function returns whethe the
        current user may, at least hypothetically, delete the message
        globally.  A user may always delete a local copy of a
        post. The server may trump this, e.g. if the login has been
        cancelled after this page was loaded.
    */
    cs.userMayDelete = function(eMsg){
      return +eMsg.dataset.msgid>0
        && (this.me === eMsg.dataset.xfrom
            || F.user.isAdmin/*will be confirmed server-side*/);
    };

    /** Returns a new Error() object encapsulating state from the given
        fetch() response promise. */
    cs._newResponseError = function(response){
      return new Error([
        "HTTP status ", response.status,": ",response.url,": ",
        response.statusText].join(''));
    };

    /** Helper for reporting HTTP-level response errors via fetch().
        If response.ok then response.json() is returned, else an Error
        is thrown. */
    cs._fetchJsonOrError = function(response){
      if(response.ok) return response.json();
      else throw cs._newResponseError(response);
    };

    /**
       Removes the given message ID from the local chat record and, if
       the message was posted by this user OR this user in an
       admin/setup, also submits it for removal on the remote.

       id may optionally be a DOM element, in which case it must be a
       .message-row element.
    */
    cs.deleteMessage = function(id){
      var e;
      if(id instanceof HTMLElement){
        e = id;
        id = e.dataset.msgid;
      }else{
        e = this.getMessageElemById(id);
      }
      if(!(e instanceof HTMLElement)) return;
      if(this.userMayDelete(e)){
        F.fetch("chat-delete/" + id, {
          responseType: 'json',
          onload:(r)=>{
            reportConnectionOkay('chat-delete');
            this.deleteMessageElem(r);
          },
          onerror:(err)=>this.reportErrorAsMessage(err)
        });
      }else{
        this.deleteMessageElem(id);
      }
    };
    document.addEventListener('visibilitychange', function(ev){
      cs.pageIsActive = ('visible' === document.visibilityState);
      cs.playedBeep = false;
      if(cs.pageIsActive){
        cs.e.pageTitle.innerText = cs.pageTitleOrig;
        if(document.activeElement!==cs.inputElement()){
          /* An attempt to resolve usability problem reported by Joe
             M. where the Pale Moon browser is giving input focus to
             the Preview button. The down-side of this is that it will
             deselect any text which was previously selected on this
             page. This also, unfortunately, places the focus at the
             start of the element, rather than the last cursor position
             (like a textarea would). */
          setTimeout(()=>cs.inputFocus(), 0);
        }
      }
    }, true);
    cs.setCurrentView(cs.e.viewMessages);

    cs.e.activeUserList.addEventListener('click', function f(ev){
      /* Filter messages on a user clicked in activeUserList */
      ev.stopPropagation();
      ev.preventDefault();
      let eUser = ev.target;
      while(eUser!==this && !eUser.classList.contains('chat-user')){
        eUser = eUser.parentNode;
      }
      if(eUser==this || !eUser) return false;
      const uname = eUser.dataset.uname;
      let eLast;
      cs.setCurrentView(cs.e.viewMessages);
      if(eUser.classList.contains('selected')){
        /* If curently selected, toggle filter off */
        eUser.classList.remove('selected');
        cs.setUserFilter(false);
        delete f.$eSelected;
      }else{
        if(f.$eSelected) f.$eSelected.classList.remove('selected');
        f.$eSelected = eUser;
        eUser.classList.add('selected');
        cs.setUserFilter(uname);
      }
      return false;
    }, false);
    return cs;
  })()/*Chat initialization*/;


  /** Returns the first .message-widget element in DOM element
      e's lineage. */
  const findMessageWidgetParent = function(e){
    while( e && !e.classList.contains('message-widget')){
      e = e.parentNode;
    }
    return e;
  };

  /**
     Custom widget type for rendering messages (one message per
     instance). These are modelled after FIELDSET elements but we
     don't use FIELDSET because of cross-browser inconsistencies in
     features of the FIELDSET/LEGEND combination, e.g. inability to
     align legends via CSS in Firefox and clicking-related
     deficiencies in Safari.
  */
  Chat.MessageWidget = (function(){
    /**
       Constructor. If passed an argument, it is passed to
       this.setMessage() after initialization.
    */
    const ctor = function(){
      this.e = {
        body: D.addClass(D.div(), 'message-widget'),
        tab: D.addClass(D.div(), 'message-widget-tab'),
        content: D.addClass(D.div(), 'message-widget-content')
      };
      D.append(this.e.body, this.e.tab, this.e.content);
      this.e.tab.setAttribute('role', 'button');
      if(arguments.length){
        this.setMessage(arguments[0]);
      }
    };
    /* Left-zero-pad a number to at least 2 digits */
    const dowMap = {
      /* Map of Date.getDay() values to weekday names. */
      0: "Sunday", 1: "Monday", 2: "Tuesday",
      3: "Wednesday", 4: "Thursday", 5: "Friday",
      6: "Saturday"
    };
    /* Given a Date, returns the timestamp string used in the "tab"
       part of message widgets. If longFmt is true then a verbose
       format is used, else a brief format is used. The returned string
       is in client-local time. */
    const theTime = function(d, longFmt=false){
      const li = [];
      if( longFmt ){
        li.push(
          d.getFullYear(),
          '-', pad2(d.getMonth()+1),
          '-', pad2(d.getDate()),
          ' ',
          d.getHours(), ":",
          (d.getMinutes()+100).toString().slice(1,3)
        );
      }else{
        li.push(
          d.getHours(),":",
          (d.getMinutes()+100).toString().slice(1,3),
          ' ', dowMap[d.getDay()]
        );
      }
      return li.join('');
    };

    /**
       Returns true if this page believes it can embed a view of the
       file wrapped by the given message object, else returns false.
    */
    const canEmbedFile = function f(msg){
      if(!f.$rx){
        f.$rx = /\.((html?)|(txt)|(md)|(wiki)|(pikchr))$/i;
        f.$specificTypes = [
          /* Mime types we know we can embed, sans image/... */
          'text/plain',
          'text/html',
          'text/x-markdown',
          /* Firefox sends text/markdown when uploading .md files */
          'text/markdown',
          'text/x-pikchr',
          'text/x-fossil-wiki'
          /* Add more as we discover which ones Firefox won't
             force the user to try to download. */
        ];
      }
      if(msg.fmime){
        if(msg.fmime.startsWith("image/")
           || f.$specificTypes.indexOf(msg.fmime)>=0){
          return true;
        }
      }
      return (msg.fname && f.$rx.test(msg.fname));
    };

    /**
      Returns true if the given message object "should"
      be embedded in fossil-rendered form instead of
      raw content form. This is only intended to be passed
      message objects for which canEmbedFile() returns true.
    */
    const shouldFossilRenderEmbed = function f(msg){
      if(!f.$rx){
        f.$rx = /\.((md)|(wiki)|(pikchr))$/i;
        f.$specificTypes = [
          'text/x-markdown',
          'text/markdown' /* Firefox-uploaded md files */,
          'text/x-pikchr',
          'text/x-fossil-wiki'
        ];
      }
      if(msg.fmime){
        if(f.$specificTypes.indexOf(msg.fmime)>=0) return true;
      }
      return msg.fname && f.$rx.test(msg.fname);
    };

    const adjustIFrameSize = function(msgObj){
      const iframe = msgObj.e.iframe;
      const body = iframe.contentWindow.document.querySelector('body');
      if(body && !body.style.fontSize){
        /** _Attempt_ to force the iframe to inherit the message's text size
            if the body has no explicit size set. On desktop systems
            the size is apparently being inherited in that case, but on mobile
            not. */
        body.style.fontSize = window.getComputedStyle(msgObj.e.content);
      }
      if('' === iframe.style.maxHeight){
        /* Resize iframe height to fit the content. Workaround: if we
           adjust the iframe height while it's hidden then its height
           is 0, so we must briefly unhide it. */
        const isHidden = iframe.classList.contains('hidden');
        if(isHidden) D.removeClass(iframe, 'hidden');
        iframe.style.maxHeight = iframe.style.height
          = iframe.contentWindow.document.documentElement.scrollHeight + 'px';
        if(isHidden) D.addClass(iframe, 'hidden');
      }
    };

    ctor.prototype = {
      scrollIntoView: function(){
        this.e.content.scrollIntoView();
      },
      //remove: function(silent){Chat.deleteMessageElem(this, silent);},
      setMessage: function(m){
        const ds = this.e.body.dataset;
        ds.timestamp = m.mtime;
        ds.lmtime = m.lmtime;
        ds.msgid = m.msgid;
        ds.xfrom = m.xfrom || '';
        if(m.xfrom === Chat.me){
          D.addClass(this.e.body, 'mine');
        }
        if(m.uclr){
          this.e.content.style.backgroundColor = m.uclr;
          this.e.tab.style.backgroundColor = m.uclr;
        }
        const d = new Date(m.mtime);
        D.clearElement(this.e.tab);
        var contentTarget = this.e.content;
        var eXFrom /* element holding xfrom name */;
        if(m.xfrom){
          eXFrom = D.append(D.addClass(D.span(), 'xfrom'), m.xfrom);
          const wrapper = D.append(
            D.span(), eXFrom,
            ' ',
            D.append(D.addClass(D.span(), 'msgid'),
                     '#' + (m.msgid||'???')),
            (m.isSearchResult ? ' ' : ' @ '),
            D.append(D.addClass(D.span(), 'timestamp'),
                     theTime(d,!!m.isSearchResult))
          );
          D.append(this.e.tab, wrapper);
        }else{/*notification*/
          D.addClass(this.e.body, 'notification');
          if(m.isError){
            D.addClass([contentTarget, this.e.tab], 'error');
          }
          D.append(
            this.e.tab,
            D.append(D.code(), 'notification @ ',theTime(d,false))
          );
        }
        if( m.xfrom && m.fsize>0 ){
          if( m.fmime
              && m.fmime.startsWith("image/")
              && Chat.settings.getBool('images-inline',true)
            ){
            const extension = m.fname.split('.').pop();
            contentTarget.appendChild(D.img("chat-download/" + m.msgid +(
              extension ? ('.'+extension) : ''/*So that IMG tag mimetype guessing works*/
            )));
            ds.hasImage = 1;
          }else{
            // Add a download link.
            const downloadUri = window.fossil.rootPath+
                  'chat-download/' + m.msgid+'/'+encodeURIComponent(m.fname);
            const w = D.addClass(D.div(), 'attachment-link');
            const a = D.a(downloadUri,
              // ^^^ add m.fname to URL to cause downloaded file to have that name.
              "(" + m.fname + " " + m.fsize + " bytes)"
            )
            D.attr(a,'target','_blank');
            D.append(w, a);
            if(canEmbedFile(m)){
              /* Add an option to embed HTML attachments in an iframe. The primary
                 use case is attached diffs. */
              const shouldFossilRender = shouldFossilRenderEmbed(m);
              const downloadArgs = shouldFossilRender ? '?render' : '';
              D.addClass(contentTarget, 'wide');
              const embedTarget = this.e.content;
              const self = this;
              const btnEmbed = D.attr(D.checkbox("1", false), 'id',
                                      'embed-'+ds.msgid);
              const btnLabel = D.label(btnEmbed, shouldFossilRender
                                       ? "Embed (fossil-rendered)" : "Embed");
              /* Maintenance reminder: do not disable the toggle
                 button while the content is loading because that will
                 cause it to get stuck in disabled mode if the browser
                 decides that loading the content should prompt the
                 user to download it, rather than embed it in the
                 iframe. */
              btnEmbed.addEventListener('change',function(){
                if(self.e.iframe){
                  if(btnEmbed.checked){
                    D.removeClass(self.e.iframe, 'hidden');
                    if(self.e.$iframeLoaded) adjustIFrameSize(self);
                  }
                  else D.addClass(self.e.iframe, 'hidden');
                  return;
                }
                const iframe = self.e.iframe = document.createElement('iframe');
                D.append(embedTarget, iframe);
                iframe.addEventListener('load', function(){
                  self.e.$iframeLoaded = true;
                  adjustIFrameSize(self);
                });
                iframe.setAttribute('src', downloadUri + downloadArgs);
              });
              D.append(w, btnEmbed, btnLabel);
            }
            contentTarget.appendChild(w);
          }
        }
        if(m.xmsg){
          if(m.fsize>0){
            /* We have file/image content, so need another element for
               the message text. */
            contentTarget = D.div();
            D.append(this.e.content, contentTarget);
          }
          D.addClass(contentTarget, 'content-target'
                     /*target element for the 'toggle text mode' feature*/);
          // The m.xmsg text comes from the same server as this script and
          // is guaranteed by that server to be "safe" HTML - safe in the
          // sense that it is not possible for a malefactor to inject HTML
          // or javascript or CSS.  The m.xmsg content might contain
          // hyperlinks, but otherwise it will be markup-free.  See the
          // chat_format_to_html() routine in the server for details.
          //
          // Hence, even though innerHTML is normally frowned upon, it is
          // perfectly safe to use in this context.
          if(m.xmsg && 'string' !== typeof m.xmsg){
            // Used by Chat.reportErrorAsMessage()
            D.append(contentTarget, m.xmsg);
          }else{
            contentTarget.innerHTML = m.xmsg;
            contentTarget.querySelectorAll('a').forEach(addAnchorTargetBlank);
            if(F.pikchr){
              F.pikchr.addSrcView(contentTarget.querySelectorAll('svg.pikchr'));
            }
          }
        }
        //console.debug("tab",this.e.tab);
        //console.debug("this.e.tab.firstElementChild",this.e.tab.firstElementChild);
        this.e.tab.firstElementChild.addEventListener('click', this._handleLegendClicked, false);
        /*if(eXFrom){
          eXFrom.addEventListener('click', ()=>this.e.tab.click(), false);
        }*/
        return this;
      },
      /* Event handler for clicking .message-user elements to show their
         timestamps and a set of actions. */
      _handleLegendClicked: function f(ev){
        if(!f.popup){
          /* "Popup" widget */
          f.popup = {
            e: D.addClass(D.div(), 'chat-message-popup'),
            refresh:function(){
              const eMsg = this.$eMsg/*.message-widget element*/;
              if(!eMsg) return;
              D.clearElement(this.e);
              const d = new Date(eMsg.dataset.timestamp);
              if(d.getMinutes().toString()!=="NaN"){
                // Date works, render informative timestamps
                const xfrom = eMsg.dataset.xfrom || 'server';
                D.append(this.e,
                         D.append(D.span(), localTimeString(d)," ",Chat.me," time"),
                         D.append(D.span(), iso8601ish(d)));
                if(eMsg.dataset.lmtime && xfrom!==Chat.me){
                  D.append(this.e,
                           D.append(D.span(), localTime8601(
                             new Date(eMsg.dataset.lmtime)
                           ).replace('T',' ')," ",xfrom," time"));
                }
              }else{
                /* This might not be necessary any more: it was
                   initially caused by Safari being stricter than
                   other browsers on its time string input, and that
                   has since been resolved by emiting a stricter
                   format. */
                // Date doesn't work, so dumb it down...
                D.append(this.e, D.append(D.span(), eMsg.dataset.timestamp," zulu"));
              }
              const toolbar = D.addClass(D.div(), 'toolbar');
              D.append(this.e, toolbar);
              const btnDeleteLocal = D.button("Delete locally");
              D.append(toolbar, btnDeleteLocal);
              const self = this;
              btnDeleteLocal.addEventListener('click', function(){
                self.hide();
                Chat.deleteMessageElem(eMsg)
              });
              if( eMsg.classList.contains('notification') ){
                const btnDeletePoll = D.button("Delete /chat notifications?");
                D.append(toolbar, btnDeletePoll);
                btnDeletePoll.addEventListener('click', function(){
                  self.hide();
                  Chat.e.viewMessages.querySelectorAll(
                    '.message-widget.notification:not(.resend-message)'
                  ).forEach(e=>Chat.deleteMessageElem(e, true));
                });
              }
              if(Chat.userMayDelete(eMsg)){
                const btnDeleteGlobal = D.button("Delete globally");
                D.append(toolbar, btnDeleteGlobal);
                F.confirmer(btnDeleteGlobal,{
                  pinSize: true,
                  ticks: F.config.confirmerButtonTicks,
                  confirmText: "Confirm delete?",
                  onconfirm:function(){
                    self.hide();
                    Chat.deleteMessage(eMsg);
                  }
                });
              }
              const toolbar3 = D.addClass(D.div(), 'toolbar');
              D.append(this.e, toolbar3);
              D.append(toolbar3, D.button(
                "Locally remove all previous messages",
                function(){
                  self.hide();
                  Chat.mnMsg = +eMsg.dataset.msgid;
                  var e = eMsg.previousElementSibling;
                  while(e && e.classList.contains('message-widget')){
                    const n = e.previousElementSibling;
                    D.remove(e);
                    e = n;
                  }
                  eMsg.scrollIntoView();
                }
              ));
              const toolbar2 = D.addClass(D.div(), 'toolbar');
              D.append(this.e, toolbar2);
              if(eMsg.querySelector('.content-target')){
                /* ^^^ messages with only an embedded image have no
                   .content-target area. */
                D.append(toolbar2, D.button(
                  "Toggle text mode", function(){
                    self.hide();
                    Chat.toggleTextMode(eMsg);
                  }));
              }
              if(eMsg.dataset.xfrom){
                /* Add a link to the /timeline filtered on this user. */
                const timelineLink = D.attr(
                  D.a(F.repoUrl('timeline',{
                    u: eMsg.dataset.xfrom,
                    y: 'a'
                  }), "User's Timeline"),
                  'target', '_blank'
                );
                D.append(toolbar2, timelineLink);
                if(Chat.filterState.activeUser &&
                   Chat.filterState.match(eMsg.dataset.xfrom)){
                  /* Add a button to clear user filter and jump to
                     this message in its original context. */
                  D.append(
                    this.e,
                    D.append(
                      D.addClass(D.div(), 'toolbar'),
                      D.button(
                        "Message in context",
                        function(){
                          self.hide();
                          Chat.setUserFilter(false);
                          eMsg.scrollIntoView(false);
                          Chat.animate(
                            eMsg.firstElementChild, 'anim-flip-h'
                            //eMsg.firstElementChild, 'anim-flip-v'
                            //eMsg.childNodes, 'anim-rotate-360'
                            //eMsg.childNodes, 'anim-flip-v'
                            //eMsg, 'anim-flip-v'
                          );
                        })
                    )
                  );
                }/*jump-to button*/
              }
              const tab = eMsg.querySelector('.message-widget-tab');
              D.append(tab, this.e);
              D.removeClass(this.e, 'hidden');
              Chat.animate(this.e, 'anim-fade-in-fast');
            }/*refresh()*/,
            hide: function(){
              delete this.$eMsg;
              D.addClass(this.e, 'hidden');
              D.clearElement(this.e);
            },
            show: function(tgtMsg){
              if(tgtMsg === this.$eMsg){
                this.hide();
                return;
              }
              this.$eMsg = tgtMsg;
              this.refresh();
            }
          }/*f.popup*/;
        }/*end static init*/
        const theMsg = findMessageWidgetParent(ev.target);
        if(theMsg) f.popup.show(theMsg);
      }/*_handleLegendClicked()*/
    };
    return ctor;
  })()/*MessageWidget*/;

  /**
     A widget for loading more messages (context) around a /chat-query
     result message.
  */
  Chat.SearchCtxLoader = (function(){
    const nMsgContext = 5;
    const zUpArrow = '\u25B2';
    const zDownArrow = '\u25BC';
    const ctor = function(o){

      /* iFirstInTable:
      **   msgid of first row in chatfts table.
      **
      ** iLastInTable:
      **   msgid of last row in chatfts table.
      **
      ** iPrevId:
      **   msgid of message immediately above this spacer. Or 0 if this
      **   spacer is above all results.
      **
      ** iNextId:
      **   msgid of message immediately below this spacer. Or 0 if this
      **   spacer is below all results.
      **
      ** bIgnoreClick:
      **   ignore any clicks if this is true. This is used to ensure there
      **   is only ever one request belonging to this widget outstanding
      **   at any time.
      */
      this.o = {
        iFirstInTable: o.first,
        iLastInTable: o.last,
        iPrevId: o.previd,
        iNextId: o.nextid,
        bIgnoreClick: false
      };

      this.e = {
        body:    D.addClass(D.div(), 'spacer-widget'),
        up:      D.addClass(
          D.button(zDownArrow+' Load '+nMsgContext+' more '+zDownArrow),
          'up'
        ),
        down:    D.addClass(
          D.button(zUpArrow+' Load '+nMsgContext+' more '+zUpArrow),
          'down'
        ),
        all:     D.addClass(D.button('Load More'), 'all')
      };
      D.append( this.e.body, this.e.up, this.e.down, this.e.all );
      const ms = this;
      this.e.up.addEventListener('click', ()=>ms.load_messages(false));
      this.e.down.addEventListener('click', ()=>ms.load_messages(true));
      this.e.all.addEventListener('click', ()=>ms.load_messages( (ms.o.iPrevId==0) ));
      this.set_button_visibility();
    };

    ctor.prototype = {
      set_button_visibility: function() {
        if( !this.e ) return;
        const o = this.o;

        const iPrevId = (o.iPrevId!=0) ? o.iPrevId : o.iFirstInTable-1;
        const iNextId = (o.iNextId!=0) ? o.iNextId : o.iLastInTable+1;
        let nDiff = (iNextId - iPrevId) - 1;

        for( const x of [this.e.up, this.e.down, this.e.all] ){
          if( x ) D.addClass(x, 'hidden');
        }
        let nVisible = 0;
        if( nDiff>0 ){
          if( nDiff>nMsgContext && (o.iPrevId==0 || o.iNextId==0) ){
            nDiff = nMsgContext;
          }

          if( nDiff<=nMsgContext && o.iPrevId!=0 && o.iNextId!=0 ){
            D.removeClass(this.e.all, 'hidden');
            ++nVisible;
            this.e.all.innerText = (
              zUpArrow + " Load " + nDiff + " more " + zDownArrow
            );
          }else{
            if( o.iPrevId!=0 ){
              ++nVisible;
              D.removeClass(this.e.up, 'hidden');
            }else if( this.e.up ){
              if( this.e.up.parentNode ) D.remove(this.e.up);
              delete this.e.up;
            }
            if( o.iNextId!=0 ){
              ++nVisible;
              D.removeClass(this.e.down, 'hidden');
            }else if( this.e.down ){
              if( this.e.down.parentNode ) D.remove( this.e.down );
              delete this.e.down;
            }
          }
        }
        if( !nVisible ){
          /* The DOM elements can now be disposed of. */
          for( const x of [this.e.up, this.e.down, this.e.all, this.e.body] ){
            if( x?.parentNode ) D.remove(x);
          }
          delete this.e;
        }
      },

      load_messages: function(bDown) {
        if( this.bIgnoreClick ) return;

        var iFirst = 0;           /* msgid of first message to fetch */
        var nFetch = 0;           /* Number of messages to fetch */
        var iEof = 0;             /* last msgid in spacers range, plus 1 */

        const e = this.e, o = this.o;
        this.bIgnoreClick = true;

        /* Figure out the required range of messages. */
        if( bDown ){
          iFirst = this.o.iNextId - nMsgContext;
          if( iFirst<this.o.iFirstInTable ){
            iFirst = this.o.iFirstInTable;
          }
        }else{
          iFirst = this.o.iPrevId+1;
        }
        nFetch = nMsgContext;
        iEof = (this.o.iNextId > 0) ? this.o.iNextId : this.o.iLastInTable+1;
        if( iFirst+nFetch>iEof ){
          nFetch = iEof - iFirst;
        }
        const ms = this;
        F.fetch("chat-query",{
          urlParams:{
            q: '',
            n: nFetch,
            i: iFirst
          },
          responseType: "json",
          onload:function(jx){
            reportConnectionOkay('chat-query');
            if( bDown ) jx.msgs.reverse();
            jx.msgs.forEach((m) => {
              m.isSearchResult = true;
              var mw = new Chat.MessageWidget(m);
              if( bDown ){
                /* Inject the message below this object's body, or
                   append it to Chat.e.searchContent if this element
                   is the final one in its parent (Chat.e.searchContent). */
                const eAnchor = e.body.nextElementSibling;
                if( eAnchor ) Chat.e.searchContent.insertBefore(mw.e.body, eAnchor);
                else D.append(Chat.e.searchContent, mw.e.body);
              }else{
                Chat.e.searchContent.insertBefore(mw.e.body, e.body);
              }
            });
            if( bDown ){
              o.iNextId -= jx.msgs.length;
            }else{
              o.iPrevId += jx.msgs.length;
            }
            ms.set_button_visibility();
            ms.bIgnoreClick = false;
          }
        });
      }
    };

    return ctor;
  })() /*SearchCtxLoader*/;

  const BlobXferState = (function(){
    /* State for paste and drag/drop */
    const bxs = {
      dropDetails: document.querySelector('#chat-drop-details'),
      blob: undefined,
      clear: function(){
        this.blob = undefined;
        D.clearElement(this.dropDetails);
        Chat.e.inputFile.value = "";
      }
    };
    /** Updates the paste/drop zone with details of the pasted/dropped
        data. The argument must be a Blob or Blob-like object (File) or
        it can be falsy to reset/clear that state.*/
    const updateDropZoneContent = bxs.updateDropZoneContent = function(blob){
      //console.debug("updateDropZoneContent()",blob);
      const dd = bxs.dropDetails;
      bxs.blob = blob;
      D.clearElement(dd);
      if(!blob){
        Chat.e.inputFile.value = '';
        return;
      }
      D.append(dd, "Attached: ", blob.name,
               D.br(), "Size: ",blob.size);
      const btn = D.button("Cancel");
      D.append(dd, D.br(), btn);
      btn.addEventListener('click', ()=>updateDropZoneContent(), false);
      if(blob.type && (blob.type.startsWith("image/") || blob.type==='BITMAP')){
        const img = D.img();
        D.append(dd, D.br(), img);
        const reader = new FileReader();
        reader.onload = (e)=>img.setAttribute('src', e.target.result);
        reader.readAsDataURL(blob);
      }
    };
    Chat.e.inputFile.addEventListener('change', function(ev){
      updateDropZoneContent(this?.files[0])
    });
    /* Handle image paste from clipboard. TODO: figure out how we can
       paste non-image binary data as if it had been selected via the
       file selection element. */
    const pasteListener = function(event){
      const items = event.clipboardData.items,
            item = items[0];
      //console.debug("paste event",event.target,item,event);
      //console.debug("paste event item",item);
      if(item && item.type && ('file'===item.kind || 'BITMAP'===item.type)){
        updateDropZoneContent(false/*clear prev state*/);
        updateDropZoneContent(item.getAsFile());
        event.stopPropagation();
        event.preventDefault(true);
        return false;
      }
      /* else continue propagating */
    };
    document.addEventListener('paste', pasteListener, true);
    if(window.Selection && window.Range && !Chat.$browserHasPlaintextOnly){
      /* Acrobatics to keep *some* installations of Firefox
         from pasting formatting into contenteditable fields.
         This also works on Chrome, but chrome has the
         contenteditable=plaintext-only property which does this
         for us. */
      Chat.e.inputX.addEventListener(
        'paste',
        function(ev){
          if (ev.clipboardData && ev.clipboardData.getData) {
            const pastedText = ev.clipboardData.getData('text/plain');
            const selection = window.getSelection();
            if (!selection.rangeCount) return false;
            selection.deleteFromDocument(/*remove selected content*/);
            selection.getRangeAt(0).insertNode(document.createTextNode(pastedText));
            selection.collapseToEnd(/*deselect pasted text and set cursor at the end*/);
            ev.preventDefault();
            return false;
          }
        }, false);
    }
    const noDragDropEvents = function(ev){
      /* contenteditable tries to do its own thing with dropped data,
         which is not compatible with how we use it, so... */
      ev.dataTransfer.effectAllowed = 'none';
      ev.dataTransfer.dropEffect = 'none';
      ev.preventDefault();
      ev.stopPropagation();
      return false;
    };
    ['drop','dragenter','dragleave','dragend'].forEach(
      (k)=>Chat.e.inputX.addEventListener(k, noDragDropEvents, false)
    );
    return bxs;
  })()/*drag/drop/paste*/;

  const tzOffsetToString = function(off){
    const hours = Math.round(off/60), min = Math.round(off % 30);
    return ''+(hours + (min ? '.5' : ''));
  };
  const localTime8601 = function(d){
    return [
      d.getYear()+1900, '-', pad2(d.getMonth()+1), '-', pad2(d.getDate()),
      'T', pad2(d.getHours()),':', pad2(d.getMinutes()),':',pad2(d.getSeconds())
    ].join('');
  };

  /**
     Called by Chat.submitMessage() when message sending failed. Injects a fake message
     containing the content and attachment of the failed message and gives the user buttons
     to discard it or edit and retry.
   */
  const recoverFailedMessage = function(state){
    const w = D.addClass(D.div(), 'failed-message');
    D.append(w, D.append(
      D.span(),"This message was not successfully sent to the server:"
    ));
    if(state.msg){
      const ta = D.textarea();
      ta.value = state.msg;
      ta.setAttribute('readonly','true');
      D.append(w,ta);
    }
    if(state.blob){
      D.append(w,D.append(D.span(),"Attachment: ",(state.blob.name||"unnamed")));
      //console.debug("blob = ",state.blob);
    }
    const buttons = D.addClass(D.div(), 'buttons');
    D.append(w, buttons);
    D.append(buttons, D.button("Discard message?", function(){
      const theMsg = findMessageWidgetParent(w);
      if(theMsg) Chat.deleteMessageElem(theMsg);
    }));
    D.append(buttons, D.button("Edit message and try again?", function(){
      if(state.msg) Chat.inputValue(state.msg);
      if(state.blob) BlobXferState.updateDropZoneContent(state.blob);
      const theMsg = findMessageWidgetParent(w);
      if(theMsg) Chat.deleteMessageElem(theMsg);
    }));
    D.addClass(Chat.reportErrorAsMessage(w).e.body, "resend-message");
  };

  /* Assume the connection has been established, reset the
     Chat.timer.tidClearPollErr, and (if showMsg and
     !!Chat.e.eMsgPollError) alert the user that the outage appears to
     be over. Also schedule Chat.poll() to run in the very near
     future. */
  const reportConnectionOkay = function(dbgContext, showMsg = true){
    if(Chat.beVerbose){
      console.warn('reportConnectionOkay', dbgContext,
                   'Chat.e.pollErrorMarker classes =',
                   Chat.e.pollErrorMarker.classList,
                   'Chat.timer.tidClearPollErr =',Chat.timer.tidClearPollErr,
                   'Chat.timer =',Chat.timer);
    }
    if( Chat.timer.errCount ){
      D.removeClass(Chat.e.pollErrorMarker, 'connection-error');
      Chat.timer.errCount = 0;
    }
    Chat.timer.cancelReconnectCheckTimer().startPendingPollTimer();
    if( Chat.e.eMsgPollError ) {
      const oldErrMsg = Chat.e.eMsgPollError;
      Chat.e.eMsgPollError = undefined;
      if( showMsg ){
        if(Chat.beVerbose){
          console.log("Poller Connection restored.");
        }
        const m = Chat.reportReconnection("Poller connection restored.");
        if( oldErrMsg ){
          D.remove(oldErrMsg.e?.body.querySelector('button.retry-now'));
        }
        m.e.body.dataset.alsoRemove = oldErrMsg?.e?.body?.dataset?.msgid;
        D.addClass(m.e.body,'poller-connection');
      }
    }
  };

  /**
     Submits the contents of the message input field (if not empty)
     and/or the file attachment field to the server. If both are
     empty, this is a no-op.

     If the current view is the history search, this instead sends the
     input text to that widget.
  */
  Chat.submitMessage = function f(){
    if(!f.spaces){
      f.spaces = /\s+$/;
      f.markdownContinuation = /\\\s+$/;
      f.spaces2 = /\s{3,}$/;
    }
    switch( this.e.currentView ){
      case this.e.viewSearch: this.submitSearch();
        return;
      default: break;
    }
    this.setCurrentView(this.e.viewMessages);
    const fd = new FormData();
    const fallback = {msg: this.inputValue()};
    var msg = fallback.msg;
    if(msg && (msg.indexOf('\n')>0 || f.spaces.test(msg))){
      /* Cosmetic: trim most whitespace from the ends of lines to try to
         keep copy/paste from terminals, especially wide ones, from
         forcing a horizontal scrollbar on all clients. This breaks
         markdown's use of blackslash-space-space for paragraph
         continuation, but *not* doing this affects all clients every
         time someone pastes in console copy/paste from an affected
         platform. We seem to have narrowed to the console pasting
         problem to users of tmux together with certain apps (vim, at
         a minimum). Most consoles don't behave that way.

         We retain two trailing spaces so that markdown conventions
         which use end-of-line spacing aren't broken by this
         stripping.
      */
      const xmsg = msg.split('\n');
      xmsg.forEach(function(line,ndx){
        if(!f.markdownContinuation.test(line)){
          xmsg[ndx] = line.replace(f.spaces2, '  ');
        }
      });
      msg = xmsg.join('\n');
    }
    if(msg) fd.set('msg',msg);
    const file = BlobXferState.blob || this.e.inputFile.files[0];
    if(file) fd.set("file", file);
    if( !msg && !file ) return;
    fallback.blob = file;
    const self = this;
    fd.set("lmtime", localTime8601(new Date()));
    F.fetch("chat-send",{
      payload: fd,
      responseType: 'text',
      onerror:function(err){
        self.reportErrorAsMessage(err);
        recoverFailedMessage(fallback);
      },
      onload:function(txt){
        reportConnectionOkay('chat-send');
        if(!txt) return/*success response*/;
        try{
          const json = JSON.parse(txt);
          self.newContent({msgs:[json]});
        }catch(e){
          self.reportError(e);
        }
        recoverFailedMessage(fallback);
      }
    });
    BlobXferState.clear();
    Chat.inputValue("").inputFocus();
  };

  const inputWidgetKeydown = function f(ev){
    if(!f.$toggleCtrl){
      f.$toggleCtrl = function(currentMode){
        currentMode = !currentMode;
        Chat.settings.set('edit-ctrl-send', currentMode);
      };
      f.$toggleCompact = function(currentMode){
        currentMode = !currentMode;
        Chat.settings.set('edit-compact-mode', currentMode);
      };
    }
    if(13 !== ev.keyCode) return;
    const text = Chat.inputValue().trim();
    const ctrlMode = Chat.settings.getBool('edit-ctrl-send', false);
    //console.debug("Enter key event:", ctrlMode, ev.ctrlKey, ev.shiftKey, ev);
    if(ev.shiftKey){
      const compactMode = Chat.settings.getBool('edit-compact-mode', false);
      ev.preventDefault();
      ev.stopPropagation();
      /* Shift-enter will run preview mode UNLESS the input field is empty
         AND (preview or search mode) is active, in which cases it will
         switch back to message view. */
      if(!text &&
         (Chat.e.currentView===Chat.e.viewPreview
          | Chat.e.currentView===Chat.e.viewSearch)){
        Chat.setCurrentView(Chat.e.viewMessages);
      }else if(!text){
        f.$toggleCompact(compactMode);
      }else if(Chat.settings.getBool('edit-shift-enter-preview', true)){
        Chat.e.btnPreview.click();
      }
      return false;
    }
    if(ev.ctrlKey && !text && !BlobXferState.blob){
      /* Ctrl-enter on empty input field(s) toggles Enter/Ctrl-enter mode */
      ev.preventDefault();
      ev.stopPropagation();
      f.$toggleCtrl(ctrlMode);
      return false;
    }
    if(!ctrlMode && ev.ctrlKey && text){
      //console.debug("!ctrlMode && ev.ctrlKey && text.");
      /* Ctrl-enter in Enter-sends mode SHOULD, with this logic add a
         newline, but that is not happening, for unknown reasons
         (possibly related to this element being a contenteditable DIV
         instead of a textarea). Forcibly appending a newline do the
         input area does not work, also for unknown reasons, and would
         only be suitable when we're at the end of the input.

         Strangely, this approach DOES work for shift-enter, but we
         need shift-enter as a hotkey for preview mode.
      */
      //return;
      // return here "should" cause newline to be added, but that doesn't work
    }
    if((!ctrlMode && !ev.ctrlKey) || (ev.ctrlKey/* && ctrlMode*/)){
      /* Ship it! */
      ev.preventDefault();
      ev.stopPropagation();
      Chat.submitMessage();
      return false;
    }
  };
  Chat.e.inputFields.forEach(
    (e)=>e.addEventListener('keydown', inputWidgetKeydown, false)
  );
  Chat.e.btnSubmit.addEventListener('click',(e)=>{
    e.preventDefault();
    Chat.submitMessage();
    return false;
  });
  Chat.e.btnAttach.addEventListener(
    'click', ()=>Chat.e.inputFile.click(), false);

  (function(){/*Set up #chat-button-settings and related bits */
    if(window.innerWidth<window.innerHeight){
      // Must be set up before config view is...
      /* Alignment of 'my' messages: right alignment is conventional
         for mobile chat apps but can be difficult to read in wide
         windows (desktop/tablet landscape mode), so we default to a
         layout based on the apparent "orientation" of the window:
         tall vs wide. Can be toggled via settings. */
      document.body.classList.add('my-messages-right');
    }
    const settingsButton = document.querySelector('#chat-button-settings');
    const optionsMenu = E1('#chat-config-options');
    const eToggleView = function(ev){
      ev.preventDefault();
      ev.stopPropagation();
      Chat.setCurrentView(Chat.e.currentView===Chat.e.viewConfig
                          ? Chat.e.viewMessages : Chat.e.viewConfig);
      return false;
    };
    D.attr(settingsButton, 'role', 'button').addEventListener('click', eToggleView, false);
    Chat.e.viewConfig.querySelector('button.action-close').addEventListener('click', eToggleView, false);

    /** Internal acrobatics to allow certain settings toggles to access
        related toggles. */
    const namedOptions = {
      activeUsers:{
        label: "Show active users list",
        hint: "List users who have messages in the currently-loaded chat history.",
        boolValue: 'active-user-list'
      }
    };
    if(1){
      /* Per user request, toggle the list of users on and off if the
         legend element is tapped. */
      const optAu = namedOptions.activeUsers;
      optAu.theLegend = Chat.e.activeUserListWrapper.firstElementChild/*LEGEND*/;
      optAu.theList = optAu.theLegend.nextElementSibling/*user list container*/;
      optAu.theLegend.addEventListener('click',function(){
        D.toggleClass(Chat.e.activeUserListWrapper, 'collapsed');
        if(!Chat.e.activeUserListWrapper.classList.contains('collapsed')){
          Chat.animate(optAu.theList,'anim-flip-v');
        }
      }, false);
    }/*namedOptions.activeUsers additional setup*/
    /* Settings menu entries... they are presented in the order listed
       here, so the most frequently-needed ones "should" (arguably) be
       closer to the start of this list. */
    /**
       Settings ops structure:

       label: string for the UI

       boolValue: string (name of Chat.settings setting) or a function
       which returns true or false. If it is a string, it gets
       replaced by a function which returns
       Chat.settings.getBool(thatString) and the string gets assigned
       to the persistentSetting property of this object.

       select: SELECT element (instead of boolValue)

       callback: optional handler to call after setting is modified.
       Its "this" is the options object. If this object has a
       boolValue string or a persistentSetting property, the argument
       passed to the callback is a settings object in the form {key:K,
       value:V}. If this object does not have boolValue string or
       persistentSetting then the callback is passed an event object
       in response to the config option's UI widget being activated,
       normally a 'change' event.

       children: [array of settings objects]. These get listed under
       this element and indented slightly for visual grouping. Only
       one level of indention is supported.

       Elements which only have a label and maybe a hint and
       children can be used as headings.

       If a setting has a boolValue set, that gets rendered as a
       checkbox which toggles the given persistent setting (if
       boolValue is a string) AND listens for changes to that setting
       fired via Chat.settings.set() so that the checkbox can stay in
       sync with external changes to that setting. Various Chat UI
       elements stay in sync with the config UI via those settings
       events. The checkbox element gets added to the options object
       so that the callback() can reference it via this.checkbox.
    */
    const settingsOps = [{
      label: "Chat Configuration Options",
      hint: F.storage.isTransient()
        ? "Local store is unavailable. These settings are transient."
        : ["Most of these settings are persistent via ",
           F.storage.storageImplName(), ": ",
           F.storage.storageHelpDescription()].join('')
    },{
      label: "Editing Options...",
      children:[{
        label: "Chat-only mode",
        hint: "Toggle the page between normal fossil view and chat-only view.",
        boolValue: 'chat-only-mode'
      },{
        label: "Ctrl-enter to Send",
        hint: [
          "When on, only Ctrl-Enter will send messages and Enter adds ",
          "blank lines. When off, both Enter and Ctrl-Enter send. ",
          "When the input field has focus and is empty ",
          "then Ctrl-Enter toggles this setting."
        ].join(''),
        boolValue: 'edit-ctrl-send'
      },{
        label: "Compact mode",
        hint: [
          "Toggle between a space-saving or more spacious writing area. ",
          "When the input field has focus and is empty ",
          "then Shift-Enter may (depending on the current view) toggle this setting."
        ].join(''),
        boolValue: 'edit-compact-mode'
      },{
        label: "Use 'contenteditable' editing mode",
        boolValue: 'edit-widget-x',
        hint: [
          "When enabled, chat input uses a so-called 'contenteditable' ",
          "field. Though generally more comfortable and modern than ",
          "plain-text input fields, browser-specific quirks and bugs ",
          "may lead to frustration. Ideal for mobile devices."
        ].join('')
      },{
        label: "Shift-enter to preview",
        hint: ["Use shift-enter to preview being-edited messages. ",
               "This is normally desirable but some software-mode ",
               "keyboards misinteract with this, in which cases it can be ",
               "disabled."],
        boolValue: 'edit-shift-enter-preview'
      }]
    },{
      label: "Appearance Options...",
      children:[{
        label: "Left-align my posts",
        hint: "Default alignment of your own messages is selected "
          + "based on the window width/height ratio.",
        boolValue: ()=>!document.body.classList.contains('my-messages-right'),
        callback: function f(){
          document.body.classList[
            this.checkbox.checked ? 'remove' : 'add'
          ]('my-messages-right');
        }
      },{
        label: "Monospace message font",
        hint: "Use monospace font for message and input text.",
        boolValue: 'monospace-messages',
        callback: function(setting){
          document.body.classList[
            setting.value ? 'add' : 'remove'
          ]('monospace-messages');
        }
      },{
        label: "Show images inline",
        hint: "When enabled, attached images are shown inline, "+
          "else they appear as a download link.",
        boolValue: 'images-inline'
      }]
    }];

    /** Set up selection list of notification sounds. */
    if(1){
      const selectSound = D.select();
      D.option(selectSound, "", "(no audio)");
      const firstSoundIndex = selectSound.options.length;
      F.config.chat.alerts.forEach((a)=>D.option(selectSound, a));
      if(true===Chat.settings.getBool('audible-alert')){
        /* This setting used to be a plain bool. If we encounter
           such a setting, take the first sound in the list. */
        selectSound.selectedIndex = firstSoundIndex;
      }else{
        selectSound.value = Chat.settings.get('audible-alert','<none>');
        if(selectSound.selectedIndex<0){
          /* Missing file - removed after this setting was
            applied. Fall back to the first sound in the list. */
          selectSound.selectedIndex = firstSoundIndex;
        }
      }
      Chat.setNewMessageSound(selectSound.value);
      settingsOps.push({
        label: "Sound Options...",
        hint: "How to enable audio playback is browser-specific!",
        children:[{
          hint: "Audio alert",
          select: selectSound,
          callback: function(ev){
            const v = ev.target.value;
            Chat.setNewMessageSound(v);
            F.toast.message("Audio notifications "+(v ? "enabled" : "disabled")+".");
            if(v) setTimeout(()=>Chat.playNewMessageSound(), 0);
          }
        },{
          label: "Notify only once when away",
          hint: "Notify only for the first message received after chat is hidden from view.",
          boolValue: 'beep-once'
        },{
          label: "Play notification for your own messages",
          hint: "When enabled, the audio notification will be played for all messages, "+
            "including your own. When disabled only messages from other users "+
            "will trigger a notification.",
          boolValue: 'alert-own-messages'
        }]
      });
    }/*audio notification config*/
    settingsOps.push({
      label: "Active User List...",
      hint: [
        "/chat cannot track active connections, but it can tell ",
        "you who has posted recently..."].join(''),
      children:[
        namedOptions.activeUsers,{
          label: "Timestamps in active users list",
          indent: true,
          hint: "Show most recent message timestamps in the active user list.",
          boolValue: 'active-user-list-timestamps'
        }
      ]
    });
    /**
       Build UI for config options...
    */
    settingsOps.forEach(function f(op,indentOrIndex){
      const menuEntry = D.addClass(D.div(), 'menu-entry');
      if(true===indentOrIndex) D.addClass(menuEntry, 'child');
      const label = op.label
            ? D.append(D.label(),op.label) : undefined;
      const labelWrapper = D.addClass(D.div(), 'label-wrapper');
      var hint;
      if(op.hint){
        hint = D.append(D.addClass(D.label(),'hint'),op.hint);
      }
      if(op.hasOwnProperty('select')){
        const col0 = D.addClass(D.span(/*empty, but for spacing*/),
                                'toggle-wrapper');
        D.append(menuEntry, labelWrapper, col0);
        D.append(labelWrapper, op.select);
        if(hint) D.append(labelWrapper, hint);
        if(label) D.append(label);
        if(op.callback){
          op.select.addEventListener('change', (ev)=>op.callback(ev), false);
        }
      }else if(op.hasOwnProperty('boolValue')){
        if(undefined === f.$id) f.$id = 0;
        ++f.$id;
        if('string' ===typeof op.boolValue){
          const key = op.boolValue;
          op.boolValue = ()=>Chat.settings.getBool(key);
          op.persistentSetting = key;
        }
        const check = op.checkbox
              = D.attr(D.checkbox(1, op.boolValue()),
                       'aria-label', op.label);
        const id = 'cfgopt'+f.$id;
        const col0 = D.addClass(D.span(), 'toggle-wrapper');
        check.checked = op.boolValue();
        op.checkbox = check;
        D.attr(check, 'id', id);
        if(hint) D.attr(hint, 'for', id);
        D.append(menuEntry, labelWrapper, col0);
        D.append(col0, check);
        if(label){
          D.attr(label, 'for', id);
          D.append(labelWrapper, label);
        }
        if(hint) D.append(labelWrapper, hint);
      }else{
        if(op.callback){
          menuEntry.addEventListener('click', (ev)=>op.callback(ev));
        }
        D.append(menuEntry, labelWrapper);
        if(label) D.append(labelWrapper, label);
        if(hint) D.append(labelWrapper, hint);
      }
      D.append(optionsMenu, menuEntry);
      if(op.persistentSetting){
        Chat.settings.addListener(
          op.persistentSetting,
          function(setting){
            if(op.checkbox) op.checkbox.checked = !!setting.value;
            else if(op.select) op.select.value = setting.value;
            if(op.callback) op.callback(setting);
          }
        );
        if(op.checkbox){
          op.checkbox.addEventListener(
            'change', function(){
              Chat.settings.set(op.persistentSetting, op.checkbox.checked)
            }, false);
        }
      }else if(op.callback && op.checkbox){
        op.checkbox.addEventListener('change', (ev)=>op.callback(ev), false);
      }
      if(op.children){
        D.addClass(menuEntry, 'parent');
        op.children.forEach((x)=>f(x,true));
      }
    });
  })()/*#chat-button-settings setup*/;

  (function(){
    /* Install default settings... must come after
       chat-button-settings setup so that the listeners which that
       installs are notified via the properties getting initialized
       here. */
    Chat.settings.addListener('monospace-messages',function(s){
      document.body.classList[s.value ? 'add' : 'remove']('monospace-messages');
    })
    Chat.settings.addListener('active-user-list',function(s){
      Chat.showActiveUserList(s.value);
    });
    Chat.settings.addListener('active-user-list-timestamps',function(s){
      Chat.showActiveUserTimestamps(s.value);
    });
    Chat.settings.addListener('chat-only-mode',function(s){
      Chat.chatOnlyMode(s.value);
    });
    Chat.settings.addListener('edit-widget-x',function(s){
      let eSelected;
      if(s.value){
        if(Chat.e.inputX===Chat.inputElement()) return;
        eSelected = Chat.e.inputX;
      }else{
        eSelected = Chat.settings.getBool('edit-compact-mode')
          ? Chat.e.input1 : Chat.e.inputM;
      }
      const v = Chat.inputValue();
      Chat.inputValue('');
      Chat.e.inputFields.forEach(function(e,ndx){
        if(eSelected===e){
          Chat.e.inputFields.$currentIndex = ndx;
          D.removeClass(e, 'hidden');
        }
        else D.addClass(e,'hidden');
      });
      Chat.inputValue(v);
      eSelected.focus();
    });
    Chat.settings.addListener('edit-compact-mode',function(s){
      if(Chat.e.inputX!==Chat.inputElement()){
        /* Text field/textarea mode: swap them if needed.
           Compact mode of inputX is toggled via CSS. */
        const a = s.value
              ? [Chat.e.input1, Chat.e.inputM, 0]
              : [Chat.e.inputM, Chat.e.input1, 1];
        const v = Chat.inputValue();
        Chat.inputValue('');
        Chat.e.inputFields.$currentIndex = a[2];
        Chat.inputValue(v);
        D.removeClass(a[0], 'hidden');
        D.addClass(a[1], 'hidden');
      }
      Chat.e.inputLineWrapper.classList[
        s.value ? 'add' : 'remove'
      ]('compact');
      Chat.e.inputFields[Chat.e.inputFields.$currentIndex].focus();
    });
    Chat.settings.addListener('edit-ctrl-send',function(s){
      const label = (s.value ? "Ctrl-" : "")+"Enter submits message.";
      Chat.e.inputFields.forEach((e)=>{
        const v = e.dataset.placeholder0 + " " +label;
        if(e.isContentEditable) e.dataset.placeholder = v;
        else D.attr(e,'placeholder',v);
      });
      Chat.e.btnSubmit.title = label;
    });
    const valueKludges = {
      /* Convert certain string-format values to other types... */
      "false": false,
      "true": true
    };
    Object.keys(Chat.settings.defaults).forEach(function(k){
      var v = Chat.settings.get(k,Chat);
      if(Chat===v) v = Chat.settings.defaults[k];
      if(valueKludges.hasOwnProperty(v)) v = valueKludges[v];
      Chat.settings.set(k,v)
      /* fires event listeners so that the Config area checkboxes
         get in sync */;
    });
  })();

  (function(){/*set up message preview*/
    const btnPreview = Chat.e.btnPreview;
    Chat.setPreviewText = function(t){
      this.setCurrentView(this.e.viewPreview);
      this.e.previewContent.innerHTML = t;
      this.e.viewPreview.querySelectorAll('a').forEach(addAnchorTargetBlank);
      this.inputFocus();
    };
    Chat.e.viewPreview.querySelector('button.action-close').
      addEventListener('click', ()=>Chat.setCurrentView(Chat.e.viewMessages), false);
    let previewPending = false;
    const elemsToEnable = [btnPreview, Chat.e.btnSubmit, Chat.e.inputFields];
    const submit = function(ev){
      ev.preventDefault();
      ev.stopPropagation();
      if(previewPending) return false;
      const txt = Chat.inputValue();
      if(!txt){
        Chat.setPreviewText('');
        previewPending = false;
        return false;
      }
      const fd = new FormData();
      fd.append('content', txt);
      fd.append('filename','chat.md'
                /*filename needed for mimetype determination*/);
      fd.append('render_mode',F.page.previewModes.wiki);
      F.fetch('ajax/preview-text',{
        payload: fd,
        onload: function(html){
          reportConnectionOkay('ajax/preview-text');
          Chat.setPreviewText(html);
          F.pikchr.addSrcView(Chat.e.viewPreview.querySelectorAll('svg.pikchr'));
        },
        onerror: function(e){
          F.fetch.onerror(e);
          Chat.setPreviewText("ERROR: "+(
            e.message || 'Unknown error fetching preview!'
          ));
        },
        beforesend: function(){
          D.disable(elemsToEnable);
          Chat.ajaxStart();
          previewPending = true;
          Chat.setPreviewText("Loading preview...");
        },
        aftersend:function(){
          previewPending = false;
          Chat.ajaxEnd();
          D.enable(elemsToEnable);
        }
      });
      return false;
    };
    btnPreview.addEventListener('click', submit, false);
  })()/*message preview setup*/;

  (function(){/*Set up #chat-search and related bits */
    const btn = document.querySelector('#chat-button-search');
    D.attr(btn, 'role', 'button').addEventListener('click', function(ev){
      ev.preventDefault();
      ev.stopPropagation();
      const msg = Chat.inputValue();
      if( Chat.e.currentView===Chat.e.viewSearch ){
        if( msg ) Chat.submitSearch();
        else Chat.setCurrentView(Chat.e.viewMessages);
      }else{
        Chat.setCurrentView(Chat.e.viewSearch);
        if( msg ) Chat.submitSearch();
      }
      return false;
    }, false);
    Chat.e.viewSearch.querySelector('button.action-clear').addEventListener('click', function(ev){
      ev.preventDefault();
      ev.stopPropagation();
      Chat.clearSearch(true);
      Chat.setCurrentView(Chat.e.viewMessages);
      return false;
    }, false);
    Chat.e.viewSearch.querySelector('button.action-close').addEventListener('click', function(ev){
      ev.preventDefault();
      ev.stopPropagation();
      Chat.setCurrentView(Chat.e.viewMessages);
      return false;
    }, false);
  })()/*search view setup*/;

  /** Callback for poll() to inject new content into the page.  jx ==
      the response from /chat-poll. If atEnd is true, the message is
      appended to the end of the chat list (for loading older
      messages), else the beginning (the default). */
  const newcontent = function f(jx,atEnd){
    if(!f.processPost){
      /** Processes chat message m, placing it either the start (if atEnd
          is falsy) or end (if atEnd is truthy) of the chat history. atEnd
          should only be true when loading older messages. */
      f.processPost = function(m,atEnd){
        ++Chat.totalMessageCount;
        if( m.msgid>Chat.mxMsg ) Chat.mxMsg = m.msgid;
        if( !Chat.mnMsg || m.msgid<Chat.mnMsg) Chat.mnMsg = m.msgid;
        if(m.xfrom && m.mtime){
          const d = new Date(m.mtime);
          const uls = Chat.usersLastSeen[m.xfrom];
          if(!uls || uls<d){
            d.$uColor = m.uclr;
            Chat.usersLastSeen[m.xfrom] = d;
          }
        }
        if( m.mdel ){
          /* A record deletion notice. */
          Chat.deleteMessageElem(m.mdel);
          return;
        }
        if(!Chat._isBatchLoading
           && (Chat.me!==m.xfrom
               || Chat.settings.getBool('alert-own-messages'))){
          Chat.playNewMessageSound();
        }
        const row = new Chat.MessageWidget(m);
        Chat.injectMessageElem(row.e.body,atEnd);
        if(m.isError){
          Chat._gotServerError = m;
        }
      }/*processPost()*/;
    }/*end static init*/
    jx.msgs.forEach((m)=>f.processPost(m,atEnd));
    Chat.updateActiveUserList();
    if('visible'===document.visibilityState){
      if(Chat.changesSincePageHidden){
        Chat.changesSincePageHidden = 0;
        Chat.e.pageTitle.innerText = Chat.pageTitleOrig;
      }
    }else{
      Chat.changesSincePageHidden += jx.msgs.length;
      if(jx.msgs.length){
        Chat.e.pageTitle.innerText = '[*] '+Chat.pageTitleOrig;
      }
    }
  }/*newcontent()*/;
  Chat.newContent = newcontent;

  (function(){
    /** Add toolbar for loading older messages. We use a FIELDSET here
        because a fieldset is the only parent element type which can
        automatically enable/disable its children by
        enabling/disabling the parent element. */
    const loadLegend = D.legend("Load...");
    const toolbar = Chat.e.loadOlderToolbar = D.attr(
      D.fieldset(loadLegend), "id", "load-msg-toolbar"
    );
    Chat.disableDuringAjax.push(toolbar);
    /* Loads the next n oldest messages, or all previous history if n is negative. */
    const loadOldMessages = function(n){
      Chat.e.viewMessages.classList.add('loading');
      Chat._isBatchLoading = true;
      const scrollHt = Chat.e.viewMessages.scrollHeight,
            scrollTop = Chat.e.viewMessages.scrollTop;
      F.fetch("chat-poll",{
        urlParams:{
          before: Chat.mnMsg,
          n: n
        },
        responseType: 'json',
        onerror:function(err){
          Chat.reportErrorAsMessage(err);
          Chat._isBatchLoading = false;
        },
        onload:function(x){
          reportConnectionOkay('loadOldMessages()');
          let gotMessages = x.msgs.length;
          newcontent(x,true);
          Chat._isBatchLoading = false;
          Chat.updateActiveUserList();
          if(Chat._gotServerError){
            Chat._gotServerError = false;
            return;
          }
          if(n<0/*we asked for all history*/
             || 0===gotMessages/*we found no history*/
             || (n>0 && gotMessages<n /*we got fewer history entries than requested*/)
             || (n===0 && gotMessages<Chat.loadMessageCount
                 /*we asked for default amount and got fewer than that.*/)){
            /* We've loaded all history. Permanently disable the
               history-load toolbar and keep it from being re-enabled
               via the ajaxStart()/ajaxEnd() mechanism... */
            const div = Chat.e.loadOlderToolbar.querySelector('div');
            D.append(D.clearElement(div), "All history has been loaded.");
            D.addClass(Chat.e.loadOlderToolbar, 'all-done');
            const ndx = Chat.disableDuringAjax.indexOf(Chat.e.loadOlderToolbar);
            if(ndx>=0) Chat.disableDuringAjax.splice(ndx,1);
            Chat.e.loadOlderToolbar.disabled = true;
          }
          if(gotMessages > 0){
            F.toast.message("Loaded "+gotMessages+" older messages.");
            /* Return scroll position to where it was when the history load
               was requested, per user request */
            Chat.e.viewMessages.scrollTo(
              0, Chat.e.viewMessages.scrollHeight - scrollHt + scrollTop
            );
          }
        },
        aftersend:function(){
          Chat.e.viewMessages.classList.remove('loading');
          Chat.ajaxEnd();
        }
      });
    };
    const wrapper = D.div(); /* browsers don't all properly handle >1 child in a fieldset */;
    D.append(toolbar, wrapper);
    var btn = D.button("Previous "+Chat.loadMessageCount+" messages");
    D.append(wrapper, btn);
    btn.addEventListener('click',()=>loadOldMessages(Chat.loadMessageCount));
    btn = D.button("All previous messages");
    D.append(wrapper, btn);
    btn.addEventListener('click',()=>loadOldMessages(-1));
    D.append(Chat.e.viewMessages, toolbar);
    toolbar.disabled = true /*will be enabled when msg load finishes */;
  })()/*end history loading widget setup*/;

  /**
     Clears the search result view. If addInstructions is true it adds
     text to that view instructing the user to enter their query into
     the message-entry widget (noting that that widget has text
     implying that it's only for submitting a message, which isn't
     exactly true when the search view is active).

     Returns the DOM element which wraps all of the chat search
     result elements.
  */
  Chat.clearSearch = function(addInstructions=false){
    const e = D.clearElement( this.e.searchContent );
    if(addInstructions){
      D.append(e, "Enter search terms in the message field. "+
               "Use #NNNNN to search for the message with ID NNNNN.");
    }
    return e;
  };
  Chat.clearSearch(true);
  /**
     Submits a history search using the main input field's current
     text. It is assumed that Chat.e.viewSearch===Chat.e.currentView.
  */
  Chat.submitSearch = function(){
    const term = this.inputValue(true);
    const eMsgTgt = this.clearSearch(true);
    if( !term ) return;
    D.append( eMsgTgt, "Searching for ",term," ...");
    const fd = new FormData();
    fd.set('q', term);
    F.fetch(
      "chat-query", {
        payload: fd,
        responseType: 'json',
        onerror:function(err){
          Chat.setCurrentView(Chat.e.viewMessages);
          Chat.reportErrorAsMessage(err);
        },
        onload:function(jx){
          reportConnectionOkay('submitSearch()');
          let previd = 0;
          D.clearElement(eMsgTgt);
          jx.msgs.forEach((m)=>{
            m.isSearchResult = true;
            const mw = new Chat.MessageWidget(m);
            const spacer = new Chat.SearchCtxLoader({
              first: jx.first,
              last: jx.last,
              previd: previd,
              nextid: m.msgid
            });
            if( spacer.e ) D.append( eMsgTgt, spacer.e.body );
            D.append( eMsgTgt, mw.e.body );
            previd = m.msgid;
          });
          if( jx.msgs.length ){
            const spacer = new Chat.SearchCtxLoader({
              first: jx.first,
              last: jx.last,
              previd: previd,
              nextid: 0
            });
            if( spacer.e ) D.append( eMsgTgt, spacer.e.body );
          }else{
            D.append( D.clearElement(eMsgTgt),
                      'No search results found for: ',
                      term );
          }
        }
      }
    );
  }/*Chat.submitSearch()*/;

  /*
    To be called from F.fetch('chat-poll') beforesend() handler.  If
    we're currently in delayed-retry mode and a connection is
    started, try to reset the delay after N time waiting on that
    connection. The fact that the connection is waiting to respond,
    rather than outright failing, is a good hint that the outage is
    over and we can reset the back-off timer.

    Without this, recovery of a connection error won't be reported
    until after the long-poll completes by either receiving new
    messages or timing out. Once a long-poll is in progress, though,
    we "know" that it's up and running again, so can update the UI and
    connection timer to reflect that. That's the job this function
    does.

    Only one of these asynchronous checks will ever be active
    concurrently and only if Chat.timer.isDelayed() is true. i.e. if
    this timer is active or Chat.timer.isDelayed() is false, this is a
    no-op.
  */
  const chatPollBeforeSend = function(){
    //console.warn('chatPollBeforeSend outer', Chat.timer.tidClearPollErr, Chat.timer.currentDelay);
    if( !Chat.timer.tidClearPollErr && Chat.timer.isDelayed() ){
      Chat.timer.tidClearPollErr = setTimeout(()=>{
        //console.warn('chatPollBeforeSend inner');
        Chat.timer.tidClearPollErr = 0;
        if( poll.running ){
          /* This chat-poll F.fetch() is still underway, so let's
             assume the connection is back up until/unless it times
             out or breaks again. */
          reportConnectionOkay('chatPollBeforeSend', true);
        }
      }, Chat.timer.$initialDelay * 4/*kinda arbitrary: not too long for UI wait and
                                       not too short as to make connection unlikely. */ );
    }
  };

  /**
     Deal with the last poll() response and maybe re-start poll().
  */
  const afterPollFetch = function f(err){
    if(true===f.isFirstCall){
      f.isFirstCall = false;
      Chat.ajaxEnd();
      Chat.e.viewMessages.classList.remove('loading');
      setTimeout(function(){
        Chat.scrollMessagesTo(1);
      }, 250);
    }
    Chat.timer.cancelPendingPollTimer();
    if(Chat._gotServerError){
      Chat.reportErrorAsMessage(
        "Shutting down chat poller due to server-side error. ",
        "Reload this page to reactivate it."
      );
    } else {
      if( err && Chat.beVerbose ){
        console.error("afterPollFetch:",err.name,err.status,err.message);
      }
      if( !err || 'timeout'===err.name/*(probably) long-poll expired*/ ){
        /* Restart the poller immediately. */
        reportConnectionOkay('afterPollFetch '+err, false);
      }else{
        /* Delay a while before trying again, noting that other Chat
           APIs may try and succeed at connections before this timer
           resolves, in which case they'll clear this timeout and the
           UI message about the outage. */
        let delay;
        D.addClass(Chat.e.pollErrorMarker, 'connection-error');
        if( ++Chat.timer.errCount < Chat.timer.minErrForNotify ){
          delay = Chat.timer.resetDelay(
            (Chat.timer.minDelay * Chat.timer.errCount)
              + Chat.timer.randomInterval(Chat.timer.minDelay)
          );
          if(Chat.beVerbose){
            console.warn("Ignoring polling error #",Chat.timer.errCount,
                         "for another",delay,"ms" );
          }
        } else {
          delay = Chat.timer.incrDelay();
          //console.warn("afterPollFetch Chat.e.eMsgPollError",Chat.e.eMsgPollError);
          const msg = "Poller connection error. Retrying in "+delay+ " ms.";
          /* Replace the current/newest connection error widget. We could also
             just update its body with the new message, but then its timestamp
             never updates. OTOH, if we replace the message, we lose the
             start time of the outage in the log. It seems more useful to
             update the timestamp so that it doesn't look like it's hung. */
          if( Chat.e.eMsgPollError ){
            Chat.deleteMessageElem(Chat.e.eMsgPollError, false);
          }
          const theMsg = Chat.e.eMsgPollError = Chat.reportErrorAsMessage(msg);
          D.addClass(Chat.e.eMsgPollError.e.body,'poller-connection');
          /* Add a "retry now" button */
          const btnDel = D.addClass(D.button("Retry now"), 'retry-now');
          const eParent = Chat.e.eMsgPollError.e.content;
          D.append(eParent, " ", btnDel);
          btnDel.addEventListener('click', function(){
            D.remove(btnDel);
            D.append(eParent, D.text("retrying..."));
            Chat.timer.cancelPendingPollTimer().currentDelay =
              Chat.timer.resetDelay() +
              1  /*workaround for showing the "connection restored"
                   message, as the +1 will cause
                   Chat.timer.isDelayed() to be true.*/;
            poll();
          });
          //Chat.playNewMessageSound();// browser complains b/c this wasn't via human interaction
        }
        Chat.timer.startPendingPollTimer(delay);
      }
    }
  };
  afterPollFetch.isFirstCall = true;

  /**
     Initiates, if it's not already running, a single long-poll
     request to the /chat-poll endpoint. In the handling of that
     response, it end up will psuedo-recursively calling itself via
     the response-handling process. Despite being async, the implied
     returned Promise is meaningless.
  */
  const poll = Chat.poll = async function f(){
    if(f.running) return;
    f.running = true;
    Chat._isBatchLoading = f.isFirstCall;
    if(true===f.isFirstCall){
      f.isFirstCall = false;
      f.pendingOnError = undefined;
      Chat.ajaxStart();
      Chat.e.viewMessages.classList.add('loading');
      /*
        We manager onerror() results in poll() in a roundabout
        manner: when an onerror() arrives, we stash it aside
        for a moment before processing it.

        This level of indirection is necessary to be able to
        unambiguously identify client-timeout-specific polling errors
        from other errors. Timeouts are always announced in pairs of
        an HTTP 0 and something we can unambiguously identify as a
        timeout (in that order). When we receive an HTTP error we put
        it into this queue.  If an ontimeout() call arrives before
        this error is handled, this error is ignored.  If, however, an
        HTTP error is seen without an accompanying timeout, we handle
        it from here.

        It's kinda like in the curses C API, where you to match
        ALT-X by first getting an ESC event, then an X event, but
        this one is a lot less explicable. (It's almost certainly a
        mis-handling bug in F.fetch(), but it has so far eluded my
        eyes.)
      */
      f.delayPendingOnError = function(err){
        if( f.pendingOnError ){
          const x = f.pendingOnError;
          f.pendingOnError = undefined;
          afterPollFetch(x);
        }
      };
    }
    F.fetch("chat-poll",{
      timeout: Chat.timer.pollTimeout,
      urlParams:{
        name: Chat.mxMsg
      },
      responseType: "json",
      // Disable the ajax start/end handling for this long-polling op:
      beforesend: chatPollBeforeSend,
      aftersend: function(){
        poll.running = false;
      },
      ontimeout: function(err){
        f.pendingOnError = undefined /*strip preceeding non-timeout error, if any*/;
        afterPollFetch(err);
      },
      onerror:function(err){
        Chat._isBatchLoading = false;
        if(Chat.beVerbose){
          console.error("poll.onerror:",err.name,err.status,JSON.stringify(err));
        }
        f.pendingOnError = err;
        setTimeout(f.delayPendingOnError, 100);
      },
      onload:function(y){
        reportConnectionOkay('poll.onload', true);
        newcontent(y);
        if(Chat._isBatchLoading){
          Chat._isBatchLoading = false;
          Chat.updateActiveUserList();
        }
        afterPollFetch();
      }
    });
  }/*poll()*/;
  poll.isFirstCall = true;
  Chat._gotServerError = poll.running = false;
  if( window.fossil.config.chat.fromcli ){
    Chat.chatOnlyMode(true);
  }
  Chat.timer.startPendingPollTimer();
  delete ForceResizeKludge.$disabled;
  ForceResizeKludge();
  Chat.animate.$disabled = false;
  setTimeout( ()=>Chat.inputFocus(), 0 );
  F.page.chat = Chat/* enables testing the APIs via the dev tools */;
});
