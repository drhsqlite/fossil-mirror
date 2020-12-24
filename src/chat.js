(function(){
  const form = document.querySelector('#chat-form');
  const F = window.fossil, D = F.dom;
  const Chat = (function(){
    const cs = {
      me: F.user.name,
      mxMsg: F.config.chatInitSize ? -F.config.chatInitSize : -50,
      pageIsActive: !document.hidden,
      onPageActive: function(){console.debug("Page active.")}, //override below
      onPageInactive: function(){console.debug("Page inactive.")} //override below
    };
    document.addEventListener('visibilitychange', function(ev){
      cs.pageIsActive = !document.hidden;
      if(cs.pageIsActive) cs.onPageActive();
      else cs.onPageInactive();
    }, true);

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
        fetch("chat-delete?name=" + id)
          .then(()=>this.deleteMessageElem(e))
          .catch(err=>this.reportError(err))
      }else{
        this.deleteMessageElem(id);
      }
    };

    return cs;
  })();
  /* State for paste and drag/drop */
  const BlobXferState = {
    dropDetails: document.querySelector('#chat-drop-details'),
    blob: undefined
  };
  /** Updates the paste/drop zone with details of the pasted/dropped
      data. The argument must be a Blob or Blob-like object (File) or
      it can be falsy to reset/clear that state.*/
  const updateDropZoneContent = function(blob){
    const bx = BlobXferState, dd = bx.dropDetails;
    bx.blob = blob;
    D.clearElement(dd);
    if(!blob){
      form.file.value = '';
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
  form.file.addEventListener('change', function(ev){
    //console.debug("this =",this);
    updateDropZoneContent(this.files && this.files[0] ? this.files[0] : undefined)
  });

  form.addEventListener('submit',(e)=>{
    e.preventDefault();
    const fd = new FormData(form);
    if(BlobXferState.blob/*replace file content with this*/){
      fd.set("file", BlobXferState.blob);
    }
    if( form.msg.value.length>0 || form.file.value.length>0 || BlobXferState.blob ){
      fetch("chat-send",{
        method: 'POST',
        body: fd
      });
    }
    BlobXferState.blob = undefined;
    D.clearElement(BlobXferState.dropDetails);
    form.msg.value = "";
    form.file.value = "";
    form.msg.focus();
  });
  /* Handle image paste from clipboard. TODO: figure out how we can
     paste non-image binary data as if it had been selected via the
     file selection element. */
  document.onpaste = function(event){
    const items = event.clipboardData.items,
          item = items[0];
    if(!item || !item.type) return;
    //console.debug("pasted item =",item);
    if('file'===item.kind){
      updateDropZoneContent(false/*clear prev state*/);
      updateDropZoneContent(items[0].getAsFile());
    }else if(false && 'string'===item.kind){
      /* ----^^^^^ disabled for now: the intent here is that if
         form.msg is not active, populate it with this text, but
         whether populating it from ctrl-v when it does not have focus
         is a feature or a bug is debatable.  It seems useful but may
         violate the Principle of Least Surprise. */
      if(document.activeElement !== form.msg){
        /* Overwrite input field if it DOES NOT have focus,
           otherwise let it do its own paste handling. */
        item.getAsString((v)=>form.msg.value = v);
      }
    }
  };
  if(true){/* Add help button for drag/drop/paste zone */
    form.file.parentNode.insertBefore(
      F.helpButtonlets.create(
        document.querySelector('#chat-input-file-area .help-buttonlet')
      ), form.file
    );
  }
  ////////////////////////////////////////////////////////////
  // File drag/drop visual notification.
  const dropHighlight = form.file /* target zone */;
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
    (k)=>form.file.addEventListener(k, dropEvents[k], true)
  );

  /* Injects element e as a new row in the chat, at the top of the list */
  const injectMessage = function f(e){
    if(!f.injectPoint){
      f.injectPoint = document.querySelector('#message-inject-point');
    }
    if(f.injectPoint.nextSibling){
      f.injectPoint.parentNode.insertBefore(e, f.injectPoint.nextSibling);
    }else{
      f.injectPoint.parentNode.appendChild(e);
    }
  };
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
      .replace('T',' ').replace(/\.\d+/,'').replace('Z', ' GMT');
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
          const d = new Date(eMsg.dataset.timestamp+"Z");
          if(d.getMinutes().toString()!=="NaN"){
            // Date works, render informative timestamps
            D.append(this.e,
                     D.append(D.span(), localTimeString(d)," client-local"),
                     D.append(D.span(), iso8601ish(d)));
          }else{
            // Date doesn't work, so dumb it down...
            D.append(this.e, D.append(D.span(), eMsg.dataset.timestamp," GMT"));
          }
          const toolbar = D.addClass(D.div(), 'toolbar');
          const btnDelete = D.button("Delete "+
                                     (Chat.userMayDelete(eMsg)
                                      ? "globally" : "locally"));
          const self = this;
          btnDelete.addEventListener('click', function(){
            self.hide();
            Chat.deleteMessage(eMsg);
          });
          D.append(this.e, toolbar);
          D.append(toolbar, btnDelete);
        }
      });
      f.popup.installClickToHide();
      f.popup.hide = function(){
        delete this._eMsg;
        D.clearElement(this.e);
        return this.show(false);
      };
    }
    const rect = ev.target.getBoundingClientRect();
    const eMsg = ev.target.parentNode/*the owning fieldset element*/;
    f.popup._eMsg = eMsg;
    let x = rect.left, y = rect.top - 10;
    f.popup.show(ev.target)/*so we can get its computed size*/;
    if('right'===ev.target.getAttribute('align')){
      // Shift popup to the left for right-aligned messages to avoid
      // truncation off the right edge of the page.
      const pRect = f.popup.e.getBoundingClientRect();
      x -= pRect.width/3*2;
    }
    f.popup.show(x, y);
  };
  /** Callback for poll() to inject new content into the page. */
  function newcontent(jx){
    var i;
    for(i=0; i<jx.msgs.length; ++i){
      const m = jx.msgs[i];
      if( m.msgid>Chat.mxMsg ) Chat.mxMsg = m.msgid;
      if( m.mdel ){
        /* A record deletion notice. */
        Chat.deleteMessageElem(m.mdel);
        continue;
      }
      const eWho = D.create('legend'),
            row = D.addClass(D.fieldset(eWho), 'message-row');
      row.dataset.msgid = m.msgid;
      row.dataset.xfrom = m.xfrom;
      row.dataset.timestamp = m.mtime;
      injectMessage(row);
      eWho.addEventListener('click', handleLegendClicked, false);
      if( m.xfrom==Chat.me && window.outerWidth<1000 ){
        eWho.setAttribute('align', 'right');
        row.style.justifyContent = "flex-end";
      }else{
        eWho.setAttribute('align', 'left');
      }
      eWho.style.backgroundColor = m.uclr;
      eWho.classList.add('message-user');
      let whoName = m.xfrom;
      var d = new Date(m.mtime + "Z");
      if( d.getMinutes().toString()!="NaN" ){
        /* Show local time when we can compute it */
        eWho.append(D.text(whoName+' @ '+
          d.getHours()+":"+(d.getMinutes()+100).toString().slice(1,3)
        ))
      }else{
        /* Show UTC on systems where Date() does not work */
        eWho.append(D.text(whoName+' @ '+m.mtime.slice(11,16)))
      }
      let eContent = D.addClass(D.div(),'message-content','chat-message');
      eContent.style.backgroundColor = m.uclr;
      row.appendChild(eContent);
      if( m.fsize>0 ){
        if( m.fmime && m.fmime.startsWith("image/") ){
          eContent.appendChild(D.img("chat-download/" + m.msgid));
        }else{
          eContent.appendChild(D.a(
            window.fossil.rootPath+
              'chat-download/' + m.msgid+'/'+encodeURIComponent(m.fname),
            // ^^^ add m.fname to URL to cause downloaded file to have that name.
            "(" + m.fname + " " + m.fsize + " bytes)"
          ));
        }
        const br = D.br();
        br.style.clear = "both";
        eContent.appendChild(br);
      }
      if(m.xmsg){
        try{D.moveChildrenTo(eContent, D.parseHtml(m.xmsg))}
        catch(e){console.error(e)}
      }
      eContent.classList.add('chat-message');
    }
  }
  async function poll(){
    if(poll.running) return;
    poll.running = true;
    fetch("chat-poll?name=" + Chat.mxMsg)
    .then(x=>x.json())
    .then(y=>newcontent(y))
    .catch(e=>console.error(e))
    .finally(()=>poll.running=false)
  }
  poll();
  setInterval(poll, 1000);
  F.page.chat = Chat;
})();
