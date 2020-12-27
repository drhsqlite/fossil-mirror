/**
   This file contains the client-side implementation of fossil's /chat
   application. 
*/
(function(){
  const F = window.fossil, D = F.dom;
  const E1 = function(selector){
    const e = document.querySelector(selector);
    if(!e) throw new Error("missing required DOM element: "+selector);
    return e;
  };
  const isInViewport = function(e) {
    const rect = e.getBoundingClientRect();
    return (
      rect.top >= 0 &&
      rect.left >= 0 &&
      rect.bottom <= (window.innerHeight || document.documentElement.clientHeight) &&
      rect.right <= (window.innerWidth || document.documentElement.clientWidth)
    );
  };

  (function(){
    let dbg = document.querySelector('#debugMsg');
    if(dbg){
      /* This can inadvertently influence our flexbox layouts, so move
         it out of the way. */
      D.append(document.body,dbg);
    }
  })();
  const ForceResizeKludge = 0 ? function(){} : (function f(){
    /* Workaround for Safari mayhem regarding use of vh CSS units....
       We tried to use vh units to set the content area size for the
       chat layout, but Safari chokes on that, so we calculate that
       height here: 85% when in "normal" mode and 95% in chat-only
       mode. Larger than ~95% is too big for Firefox on Android,
       causing the input area to move off-screen. */
    if(!f.eHead){
      f.eHead = document.querySelector('body > div.header');
      f.eMenu = document.querySelector('body > div.mainmenu');
      f.eFoot = document.querySelector('body > div.footer');
      f.contentArea = E1('div.content');
      f.extra = 0;
      f.measure = function(e){
        if(e){
          const m = window.getComputedStyle(e);
          f.extra += parseFloat(m.height);
        }
      };
    }
    const bcl = document.body.classList;
    const resized = function(){
      const wh = window.innerHeight,
            com = bcl.contains('chat-only-mode');
      var ht;
      if(com){
        ht = wh - 10/*fudge value*/;
      }else{
        f.extra = 0;
        [f.eHead, f.eMenu, f.eFoot].forEach(f.measure);
        ht = wh - f.extra - 10/*fudge value*/;
      }
      f.contentArea.style.height =
        f.contentArea.style.maxHeight = (ht>=100 ? ht : 100)+"px";
      if(false){
        console.debug("resized.",wh, f.extra, ht,
                      window.getComputedStyle(f.contentArea).maxHeight,
                      f.contentArea);
      }
    };
    var doit;
    window.addEventListener('resize',function(ev){
      clearTimeout(doit);
      doit = setTimeout(resized, 100);
    }, false);
    resized();
    return resized;
  })();

  const Chat = (function(){
    const cs = {
      e:{/*map of certain DOM elements.*/
        messageInjectPoint: E1('#message-inject-point'),
        pageTitle: E1('head title'),
        loadOlderToolbar: undefined /* the load-posts toolbar (dynamically created) */,
        inputWrapper: E1("#chat-input-area"),
        fileSelectWrapper: E1('#chat-input-file-area'),
        messagesWrapper: E1('#chat-messages-wrapper'),
        inputForm: E1('#chat-form'),
        btnSubmit: E1('#chat-message-submit'),
        inputSingle: E1('#chat-input-single'),
        inputMulti: E1('#chat-input-multi'),
        inputCurrent: undefined/*one of inputSingle or inputMulti*/,
        inputFile: E1('#chat-input-file'),
        contentDiv: E1('div.content')
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
      /** Gets (no args) or sets (1 arg) the current input text field value,
          taking into account single- vs multi-line input. The getter returns
          a string and the setter returns this object. */
      inputValue: function(){
        const e = this.inputElement();
        if(arguments.length){
          e.value = arguments[0];
          return this;
        }
        return e.value;
      },
      /** Asks the current user input field to take focus. Returns this. */
      inputFocus: function(){
        this.inputElement().focus();
        return this;
      },
      /** Returns the current message input element. */
      inputElement: function(){
        return this.e.inputCurrent;
      },
      /** Toggles between single- and multi-line edit modes. Returns this. */
      inputToggleSingleMulti: function(){
        const old = this.e.inputCurrent;
        if(this.e.inputCurrent === this.e.inputSingle){
          this.e.inputCurrent = this.e.inputMulti;
        }else{
          this.e.inputCurrent = this.e.inputSingle;
        }
        D.addClass(old, 'hidden');
        D.removeClass(this.e.inputCurrent, 'hidden');
        this.e.inputCurrent.value = old.value;
        old.value = '';
        return this;
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
      /* Injects element e as a new row in the chat, at the top of the
         list if atEnd is falsy, else at the end of the list, before
         the load-history widget. */
      injectMessageElem: function f(e, atEnd){
        const mip = atEnd ? this.e.loadOlderToolbar : this.e.messageInjectPoint,
              holder = this.e.messagesWrapper,
              prevMessage = this.e.newestMessage;
        if(atEnd){
          const fe = mip.nextElementSibling;
          if(fe) mip.parentNode.insertBefore(e, fe);
          else D.append(mip.parentNode, e);
        }else{
          D.append(holder,e);
          this.e.newestMessage = e;
        }
        if(!atEnd && !this.isBatchLoading
           && e.dataset.xfrom!==this.me && !isInViewport(e)){
          /* If a new non-history message arrives while the user is
             scrolled elsewhere, do not scroll to the latest
             message, but gently alert the user that a new message
             has arrived. */
          F.toast.message("New message has arrived.");
        }else if(!this.isBatchLoading && e.dataset.xfrom===Chat.me){
          this.scheduleScrollOfMsg(e);
        }else if(!this.isBatchLoading){
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
          }else if(!prevMessage || (prevMessage && isInViewport(prevMessage))){
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
          document.querySelectorAll(
            ["body > div.header",
             "body > div.mainmenu",
             "body > div.footer",
             "#debugMsg"
            ].join(',')
          ).forEach((e)=>f.elemsToToggle.push(e));
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
          <0 = top of the message list, >1 = bottom of the message list,
          0 == the newest message (normally the same position as >1).
      */
      scrollMessagesTo: function(where){
        if(where<0){
          Chat.e.messagesWrapper.scrollTop = 0;
        }else if(where>0){
          Chat.e.messagesWrapper.scrollTop = Chat.e.messagesWrapper.scrollHeight;
        }else if(Chat.e.newestMessage){
          Chat.e.newestMessage.scrollIntoView(false);
        }
      },
      toggleChatOnlyMode: function(){
        return this.chatOnlyMode(!this.isChatOnlyMode());
      },
      settings:{
        get: (k,dflt)=>F.storage.get(k,dflt),
        getBool: (k,dflt)=>F.storage.getBool(k,dflt),
        set: (k,v)=>F.storage.set(k,v),
        defaults:{
          "images-inline": !!F.config.chat.imagesInline,
          "monospace-messages": false
        }
      }
    };
    /* Install default settings... */
    Object.keys(cs.settings.defaults).forEach(function(k){
      const v = cs.settings.get(k,cs);
      if(cs===v) cs.settings.set(k,cs.settings.defaults[k]);
    });
    if(window.innerWidth<window.innerHeight){
      /* Alignment of 'my' messages: right alignment is conventional
         for mobile chat apps but can be difficult to read in wide
         windows (desktop/tablet landscape mode), so we default to a
         layout based on the apparently "orientation" of the window:
         tall vs wide. Can be toggled via settings popup. */
      document.body.classList.add('my-messages-right');
    }
    if(cs.settings.getBool('monospace-messages',false)){
      document.body.classList.add('monospace-messages');
    }
    cs.e.inputCurrent = cs.e.inputSingle;
    cs.pageTitleOrig = cs.e.pageTitle.innerText;

    if(true){
      /* In order to make the input area opaque, such that the message
         list scrolls under it without being visible, we have to
         ensure that the input area has a non-transparent background
         color. Ideally we'd select the color of div.content, but that
         is not necessarily set, so we fall back to using the body's
         background color and hope it's been explicitly set
         somewhere. If we rely on the input area having its own color
         specified in CSS then all skins would have to define an
         appropriate color. Thus our selection of the body color,
         while slightly unfortunate, is in the interest of keeping
         skins from being forced to define an opaque bg color.
      */
      const bodyStyle = window.getComputedStyle(document.body);
      cs.e.inputWrapper.style.backgroundColor = bodyStyle.backgroundColor;
    }

    const qs = (e)=>document.querySelector(e);
    const argsToArray = function(args){
      return Array.prototype.slice.call(args,0);
    };
    cs.reportError = function(/*msg args*/){
      const args = argsToArray(arguments);
      console.error("chat error:",args);
      F.toast.error.apply(F.toast, args);
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
    cs.deleteMessageElem = function(id){
      var e;
      if(id instanceof HTMLElement){
        e = id;
        id = e.dataset.msgid;
      }else{
        e = this.getMessageElemById(id);
      }
      if(e && id){
        D.remove(e);
        if(e===this.e.newestMessage){
          this.fetchLastMessageElem();
        }
        F.toast.message("Deleted message "+id+".");
      }
      return !!e;
    };

    /** Given a .message-row element, this function returns whethe the
        current user may, at least hypothetically, delete the message
        globally.  A user may always delete a local copy of a
        post. The server may trump this, e.g. if the login has been
        cancelled after this page was loaded.
    */
    cs.userMayDelete = function(eMsg){
      return this.me === eMsg.dataset.xfrom
        || F.user.isAdmin/*will be confirmed server-side*/;
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
        this.ajaxStart();
        fetch("chat-delete?name=" + id)
          .then(()=>this.deleteMessageElem(e))
          .catch(err=>this.reportError(err))
          .finally(()=>this.ajaxEnd());
      }else{
        this.deleteMessageElem(id);
      }
    };
    document.addEventListener('visibilitychange', function(ev){
      cs.pageIsActive = !document.hidden;
      if(cs.pageIsActive){
        cs.e.pageTitle.innerText = cs.pageTitleOrig;
      }
    }, true);
    return cs;
  })()/*Chat initialization*/;

  /**
     Custom widget type for rendering messages (one message per
     instance). These are modelled after FIELDSET elements but we
     don't use FIELDSET because of cross-browser inconsistencies in
     features of the FIELDSET/LEGEND combination, e.g. inability to
     align legends via CSS in Firefox and clicking-related
     deficiencies in Safari.
  */
  const MessageWidget = (function(){
    const cf = function(){
      this.e = {
        body: D.addClass(D.div(), 'message-widget'),
        tab: D.addClass(D.span(), 'message-widget-tab'),
        content: D.addClass(D.div(), 'message-widget-content')
      };
      D.append(this.e.body, this.e.tab);
      D.append(this.e.body, this.e.content);
      this.e.tab.setAttribute('role', 'button');
    };
    cf.prototype = {
      setLabel: function(label){
        return this;
      },
      setPopupCallback: function(callback){
        this.e.tab.addEventListener('click', callback, false);
        return this;
      },
      setMessage: function(m){
        const ds = this.e.body.dataset;
        ds.timestamp = m.mtime;
        ds.lmtime = m.lmtime;
        ds.msgid = m.msgid;
        ds.xfrom = m.xfrom;
        if(m.xfrom === Chat.me){
          D.addClass(this.e.body, 'mine');
        }
        this.e.content.style.backgroundColor = m.uclr;
        this.e.tab.style.backgroundColor = m.uclr;
          
        const d = new Date(m.mtime);
        D.append(
          D.clearElement(this.e.tab),
          D.text(
            m.xfrom," #",m.msgid,' @ ',d.getHours(),":",
            (d.getMinutes()+100).toString().slice(1,3)            
          )
        );
        var contentTarget = this.e.content;
        if( m.fsize>0 ){
          if( m.fmime
              && m.fmime.startsWith("image/")
              && Chat.settings.getBool('images-inline',true)
            ){
            contentTarget.appendChild(D.img("chat-download/" + m.msgid));
            ds.hasImage = 1;
          }else{
            const a = D.a(
              window.fossil.rootPath+
                'chat-download/' + m.msgid+'/'+encodeURIComponent(m.fname),
              // ^^^ add m.fname to URL to cause downloaded file to have that name.
              "(" + m.fname + " " + m.fsize + " bytes)"
            )
            D.attr(a,'target','_blank');
            contentTarget.appendChild(a);
          }
          ;
        }
        if(m.xmsg){
          if(m.fsize>0){
            /* We have file/image content, so need another element for
               the message text. */
            contentTarget = D.div();
            D.append(this.e.content, contentTarget);
          }
          // The m.xmsg text comes from the same server as this script and
          // is guaranteed by that server to be "safe" HTML - safe in the
          // sense that it is not possible for a malefactor to inject HTML
          // or javascript or CSS.  The m.xmsg content might contain
          // hyperlinks, but otherwise it will be markup-free.  See the
          // chat_format_to_html() routine in the server for details.
          //
          // Hence, even though innerHTML is normally frowned upon, it is
          // perfectly safe to use in this context.
          contentTarget.innerHTML = m.xmsg;
        }
        return this;
      }
    };
    return cf;
  })()/*MessageWidget*/;

  const BlobXferState = (function(){/*drag/drop bits...*/
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
    const updateDropZoneContent = function(blob){
      const dd = bxs.dropDetails;
      bxs.blob = blob;
      D.clearElement(dd);
      if(!blob){
        Chat.e.inputFile.value = '';
        return;
      }
      D.append(dd, "Name: ", blob.name,
               D.br(), "Size: ",blob.size);
      if(blob.type && blob.type.startsWith("image/")){
        const img = D.img();
        D.append(dd, D.br(), img);
        const reader = new FileReader();
        reader.onload = (e)=>img.setAttribute('src', e.target.result);
        reader.readAsDataURL(blob);
      }
      const btn = D.button("Cancel");
      D.append(dd, D.br(), btn);
      btn.addEventListener('click', ()=>updateDropZoneContent(), false);
    };
    Chat.e.inputFile.addEventListener('change', function(ev){
      updateDropZoneContent(this.files && this.files[0] ? this.files[0] : undefined)
    });
    /* Handle image paste from clipboard. TODO: figure out how we can
       paste non-image binary data as if it had been selected via the
       file selection element. */
    document.addEventListener('paste', function(event){
      const items = event.clipboardData.items,
            item = items[0];
      if(!item || !item.type) return;
      else if('file'===item.kind){
        updateDropZoneContent(false/*clear prev state*/);
        updateDropZoneContent(items[0].getAsFile());
      }
    }, false);
    /* Add help button for drag/drop/paste zone */
    Chat.e.inputFile.parentNode.insertBefore(
      F.helpButtonlets.create(
        Chat.e.fileSelectWrapper.querySelector('.help-buttonlet')
      ), Chat.e.inputFile
    );
    ////////////////////////////////////////////////////////////
    // File drag/drop visual notification.
    const dropHighlight = Chat.e.inputFile /* target zone */;
    const dropEvents = {
      drop: function(ev){
        D.removeClass(dropHighlight, 'dragover');
      },
      dragenter: function(ev){
        ev.preventDefault();
        ev.dataTransfer.dropEffect = "copy";
        D.addClass(dropHighlight, 'dragover');
      },
      dragleave: function(ev){
        D.removeClass(dropHighlight, 'dragover');
      },
      dragend: function(ev){
        D.removeClass(dropHighlight, 'dragover');
      }
    };
    Object.keys(dropEvents).forEach(
      (k)=>Chat.e.inputFile.addEventListener(k, dropEvents[k], true)
    );
    return bxs;
  })()/*drag/drop*/;

  const tzOffsetToString = function(off){
    const hours = Math.round(off/60), min = Math.round(off % 30);
    return ''+(hours + (min ? '.5' : ''));
  };
  const pad2 = (x)=>('0'+x).substr(-2);
  const localTime8601 = function(d){
    return [
      d.getYear()+1900, '-', pad2(d.getMonth()+1), '-', pad2(d.getDate()+1),
      'T', pad2(d.getHours()),':', pad2(d.getMinutes()),':',pad2(d.getSeconds())
    ].join('');
  };
    
  Chat.submitMessage = function(){
    const fd = new FormData(this.e.inputForm)
    /* ^^^^ we don't really want/need the FORM element, but when
       FormData() is default-constructed here then the server
       segfaults, and i have no clue why! */;
    const msg = this.inputValue();
    if(msg) fd.set('msg',msg);
    const file = BlobXferState.blob || this.e.inputFile.files[0];
    if(file) fd.set("file", file);
    if( msg || file ){
      fd.set("lmtime", localTime8601(new Date()));
      fetch("chat-send",{
        method: 'POST',
        body: fd
      });
    }
    BlobXferState.clear();
    Chat.inputValue("").inputFocus();
  };

  Chat.e.inputSingle.addEventListener('keydown',function(ev){
    if(13===ev.keyCode/*ENTER*/){
      ev.preventDefault();
      ev.stopPropagation();
      Chat.submitMessage();
      return false;
    }
  }, false);
  Chat.e.inputMulti.addEventListener('keydown',function(ev){
    if(ev.ctrlKey && 13 === ev.keyCode){
      ev.preventDefault();
      ev.stopPropagation();
      Chat.submitMessage();
      return false;
    }
  }, false);
  Chat.e.btnSubmit.addEventListener('click',(e)=>{
    e.preventDefault();
    Chat.submitMessage();
    return false;
  });

  /* Returns a new TEXT node with the given text content. */
  /** Returns the local time string of Date object d, defaulting
      to the current time. */
  const localTimeString = function ff(d){
    if(!ff.pad){
      ff.pad = (x)=>(''+x).length>1 ? x : '0'+x;
    }
    d || (d = new Date());
    return [
      d.getFullYear(),'-',ff.pad(d.getMonth()+1/*sigh*/),
      '-',ff.pad(d.getDate()),
      ' ',ff.pad(d.getHours()),':',ff.pad(d.getMinutes()),
      ':',ff.pad(d.getSeconds())
    ].join('');
  };
  /* Returns an almost-ISO8601 form of Date object d. */
  const iso8601ish = function(d){
    return d.toISOString()
      .replace('T',' ').replace(/\.\d+/,'').replace('Z', ' zulu');
  };
  /* Event handler for clicking .message-user elements to show their
     timestamps. */
  const handleLegendClicked = function f(ev){
    if(!f.popup){
      /* Timestamp popup widget */
      f.popup = new F.PopupWidget({
        cssClass: ['fossil-tooltip', 'chat-message-popup'],
        refresh:function(){
          const eMsg = this._eMsg;
          if(!eMsg) return;
          D.clearElement(this.e);
          const d = new Date(eMsg.dataset.timestamp);
          if(d.getMinutes().toString()!=="NaN"){
            // Date works, render informative timestamps
            const xfrom = eMsg.dataset.xfrom;
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
            Chat.deleteMessageElem(eMsg);
          });
          if(Chat.userMayDelete(eMsg)){
            const btnDeleteGlobal = D.button("Delete globally");
            D.append(toolbar, btnDeleteGlobal);
            btnDeleteGlobal.addEventListener('click', function(){
              self.hide();
              Chat.deleteMessage(eMsg);
            });
          }
        }/*refresh()*/
      });
      f.popup.installHideHandlers();
      f.popup.hide = function(){
        delete this._eMsg;
        D.clearElement(this.e);
        return this.show(false);
      };
    }/*end static init*/
    const rect = ev.target.getBoundingClientRect();
    const eMsg = ev.target.parentNode/*the owning .message-widget element*/;
    f.popup._eMsg = eMsg;
    let x = rect.left, y = rect.topm;
    f.popup.show(ev.target)/*so we can get its computed size*/;
    if(eMsg.dataset.xfrom===Chat.me
       && document.body.classList.contains('my-messages-right')){
      // Shift popup to the left for right-aligned messages to avoid
      // truncation off the right edge of the page.
      const pRect = f.popup.e.getBoundingClientRect();
      x = rect.right - pRect.width;
    }
    f.popup.show(x, y);
  }/*handleLegendClicked()*/;

  (function(){/*Set up #chat-settings-button */
    const settingsButton = document.querySelector('#chat-settings-button');
    var popupSize = undefined/*placement workaround*/;
    const settingsPopup = new F.PopupWidget({
      cssClass: ['fossil-tooltip', 'chat-settings-popup']
    });
    /* Settings menu entries... */
    const settingsOps = [{
      label: "Multi-line input",
      boolValue: ()=>Chat.inputElement()===Chat.e.inputMulti,
      callback: function(){
        Chat.inputToggleSingleMulti();
      }
    },{
      label: "Monospace message font",
      boolValue: ()=>document.body.classList.contains('monospace-messages'),
      callback: function(){
        document.body.classList.toggle('monospace-messages');
        Chat.settings.set('monospace-messages',
                          document.body.classList.contains('monospace-messages'));
      }
    },{
      label: "Chat-only mode",
      boolValue: ()=>Chat.isChatOnlyMode(),
      callback: function(){
        Chat.toggleChatOnlyMode();
      }
    },{
      label: "Left-align my posts",
      boolValue: ()=>!document.body.classList.contains('my-messages-right'),
      callback: function f(){
        document.body.classList.toggle('my-messages-right');
      }
    },{
      label: "Images inline",
      boolValue: ()=>Chat.settings.getBool('images-inline'),
      callback: function(){
        const v = Chat.settings.getBool('images-inline',true);
        Chat.settings.set('images-inline', !v);
        F.toast.message("Image mode set to "+(v ? "hyperlink" : "inline")+".");
      }
    }];

    /**
       Rebuild the menu each time it's shown so that the toggles can
       show their current values.
    */
    settingsPopup.options.refresh = function(){
      D.clearElement(this.e);
      settingsOps.forEach(function(op){
        const line = D.addClass(D.span(), 'menu-entry');
        const btn = D.append(D.addClass(D.span(), 'button'), op.label);
        const callback = function(ev){
          settingsPopup.hide();
          op.callback.call(this,ev);
        };
        D.append(line, btn);
        if(op.hasOwnProperty('boolValue')){
          const check = D.attr(D.checkbox(1, op.boolValue()),
                                          'aria-label', op.label);
          D.append(line, check);
          check.addEventListener('click', callback);
        }
        D.append(settingsPopup.e, line);
        btn.addEventListener('click', callback);
      });
    };
    settingsPopup.installHideHandlers(false, true, true)
    /** Reminder: click-to-hide interferes with "?" embedded within
        the popup, so cannot be used together with those. Enabling
        this means, however, that tapping the menu button to toggle
        the menu cannot work because tapping the menu button while the
        menu is opened will, because of the click-to-hide handler,
        hide the menu before the button gets an event saying to toggle
        it.*/;
    D.attr(settingsButton, 'role', 'button');
    settingsButton.addEventListener('click',function(ev){
      //ev.preventDefault();
      if(settingsPopup.isShown()) settingsPopup.hide();
      else settingsPopup.show(settingsButton);
      /* Reminder: we cannot toggle the visibility from her
       */
    }, false);

    /* Find an ideal X/Y position for the popup, directly above the settings
       button, based on the size of the popup... */
    settingsPopup.show(document.body);
    popupSize = settingsPopup.e.getBoundingClientRect();
    settingsPopup.hide();
    settingsPopup.options.adjustX = function(x){
      const rect = settingsButton.getBoundingClientRect();
      return rect.right - popupSize.width;
    };
    settingsPopup.options.adjustY = function(y){
      const rect = settingsButton.getBoundingClientRect();
      return rect.top - popupSize.height -2;
    };
  })()/*#chat-settings-button setup*/;

  (function(){ /* buttons to scroll to the begin/end of the messages. */
    E1('#chat-scroll-bottom').addEventListener('click',function(ev){
      ev.preventDefault();
      Chat.scrollMessagesTo(1);
      return false;
    });
    E1('#chat-scroll-top').addEventListener('click',function(ev){
      ev.preventDefault();
      Chat.scrollMessagesTo(-1);
      return false;
    });
  })();
  
  /** Callback for poll() to inject new content into the page.  jx ==
      the response from /chat-poll. If atEnd is true, the message is
      appended to the end of the chat list, else the beginning (the
      default). */
  const newcontent = function f(jx,atEnd){
    if(!f.processPost){
      /** Processes chat message m, placing it either the start (if atEnd
          is falsy) or end (if atEnd is truthy) of the chat history. atEnd
          should only be true when loading older messages. */
      f.processPost = function(m,atEnd){
        ++Chat.totalMessageCount;
        if( m.msgid>Chat.mxMsg ) Chat.mxMsg = m.msgid;
        if( !Chat.mnMsg || m.msgid<Chat.mnMsg) Chat.mnMsg = m.msgid;
        if( m.mdel ){
          /* A record deletion notice. */
          Chat.deleteMessageElem(m.mdel);
          return;
        }
        const row = new MessageWidget()
        row.setMessage(m);
        row.setPopupCallback(handleLegendClicked);
        Chat.injectMessageElem(row.e.body,atEnd);
      }/*processPost()*/;
    }/*end static init*/
    jx.msgs.forEach((m)=>f.processPost(m,atEnd));
    if('visible'===document.visibilityState){
      if(Chat.changesSincePageHidden){
        Chat.changesSincePageHidden = 0;
        Chat.e.pageTitle.innerText = Chat.pageTitleOrig;
      }
    }else{
      Chat.changesSincePageHidden += jx.msgs.length;
      if(jx.msgs.length){
        Chat.e.pageTitle.innerText = '('+Chat.changesSincePageHidden+') '+
          Chat.pageTitleOrig;
      }
    }
    if(jx.msgs.length && F.config.chat.pingTcp){
      fetch("http:/"+"/localhost:"+F.config.chat.pingTcp+"/chat-ping");
    }
  }/*newcontent()*/;

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
      Chat.ajaxStart();
      Chat.e.messagesWrapper.classList.add('loading');
      Chat.isBatchLoading = true;
      var gotMessages = false;
      fetch("chat-poll?before="+Chat.mnMsg+"&n="+n)
        .then(x=>x.json())
        .then(function(x){
          gotMessages = x.msgs.length;
          newcontent(x,true);
        })
        .catch(e=>Chat.reportError(e))
        .finally(function(){
          Chat.isBatchLoading = false;
          Chat.e.messagesWrapper.classList.remove('loading');
          if(n<0/*we asked for all history*/
             || 0===gotMessages/*we found no history*/
             || (n>0 && gotMessages<n /*we got fewer history entries than requested*/)
             || (false!==gotMessages && n===0 && gotMessages<Chat.loadMessageCount
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
          }
          Chat.ajaxEnd();
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
    D.append(Chat.e.messagesWrapper, toolbar);
    toolbar.disabled = true /*will be enabled when msg load finishes */;
  })()/*end history loading widget setup*/;

  async function poll(isFirstCall){
    if(poll.running) return;
    poll.running = true;
    if(isFirstCall){
      Chat.ajaxStart();
      Chat.e.messagesWrapper.classList.add('loading');
    }
    Chat.isBatchLoading = isFirstCall;
    var p = fetch("chat-poll?name=" + Chat.mxMsg);
    p.then(x=>x.json())
      .then(y=>newcontent(y))
      .catch(e=>console.error(e))
    /* ^^^ we don't use Chat.reportError(e) here b/c the polling
       fails exepectedly when it times out, but is then immediately
       resumed, and reportError() produces a loud error message. */
      .finally(function(){
        if(isFirstCall){
          Chat.isBatchLoading = false;
          Chat.ajaxEnd();
          setTimeout(function(){
            Chat.scrollMessagesTo(1);
            Chat.e.messagesWrapper.classList.remove('loading');
          }, 250);
        }
        poll.running=false;
      });
  }
  poll.running = false;
  poll(true);
  setInterval(poll, 1000);
  F.page.chat = Chat/* enables testing the APIs via the dev tools */;
})();
