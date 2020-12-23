(function(){
  const form = document.querySelector('#chat-form');
  let mxMsg = 0;
  const F = window.fossil;
  const _me = F.user.name;
  /* State for pasting an image from the clipboard */
  const ImagePasteState = {
    imgTag: document.querySelector('#chat-pasted-image img'),
    blob: undefined
  };
  form.addEventListener('submit',(e)=>{
    e.preventDefault();
    const fd = new FormData(form);
    if(ImagePasteState.blob/*replace file content with this*/){
      fd.set("file", ImagePasteState.blob);
    }
    if( form.msg.value.length>0 || form.file.value.length>0 || ImagePasteState.blob ){
      fetch("chat-send",{
        method: 'POST',
        body: fd
      });
    }
    ImagePasteState.blob = undefined;
    ImagePasteState.imgTag.removeAttribute('src');
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
    if('file'===item.kind && item.type.startsWith("image/")){
      ImagePasteState.blob = items[0].getAsFile();
      //console.debug("pasted blob =",ImagePasteState.blob);
      const reader = new FileReader();
      reader.onload = function(event){
        ImagePasteState.imgTag.setAttribute('src', event.target.result);
      };
      reader.readAsDataURL(ImagePasteState.blob);
    }else if('string'===item.kind){
      item.getAsString((v)=>form.msg.value = v);
    }
  };
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
  const textNode = (T)=>document.createTextNode(T);
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
        cssClass: ['fossil-tooltip', 'chat-timestamp'],
        refresh:function(){
          const D = F.dom;
          D.clearElement(this.e);
          const d = new Date(this._timestamp+"Z");
          if(d.getMinutes().toString()!=="NaN"){
            // Date works, render informative timestamps
            D.append(this.e, localTimeString(d)," client-local", D.br(),
                     iso8601ish(d));
          }else{
            // Date doesn't work, so dumb it down...
            D.append(this.e, this._timestamp," GMT");
          }
        }
      });
      f.popup.installClickToHide();
    }
    const rect = ev.target.getBoundingClientRect();
    f.popup._timestamp = ev.target.dataset.timestamp;
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
      let m = jx.msgs[i];
      let row = document.createElement("fieldset");
      if( m.msgid>mxMsg ) mxMsg = m.msgid;
      row.classList.add('message-row');
      injectMessage(row);
      const eWho = document.createElement('legend');
      eWho.dataset.timestamp = m.mtime;
      eWho.addEventListener('click', handleLegendClicked, false);
      eWho.setAttribute('align', (m.xfrom===_me ? 'right' : 'left'));
      eWho.style.backgroundColor = m.uclr;
      row.appendChild(eWho);
      eWho.classList.add('message-user');
      let whoName;
      if( m.xfrom===_me ){
        whoName = 'me';
        row.classList.add('user-is-me');
      }else{
        whoName = m.xfrom;
      }
      var d = new Date(m.mtime + "Z");
      if( d.getMinutes().toString()!="NaN" ){
        /* Show local time when we can compute it */
        eWho.append(textNode(whoName+' @ '+
          d.getHours()+":"+(d.getMinutes()+100).toString().slice(1,3)
        ))
      }else{
        /* Show UTC on systems where Date() does not work */
        eWho.append(textNode(whoName+' @ '+m.mtime.slice(11,16)))
      }
      let span = document.createElement("div");
      span.classList.add('message-content');
      span.style.backgroundColor = m.uclr;
      row.appendChild(span);
      if( m.fsize>0 ){
        if( m.fmime && m.fmime.startsWith("image/") ){
          let img = document.createElement("img");
          img.src = "chat-download/" + m.msgid;
          span.appendChild(img);
        }else{
          let a = document.createElement("a");
          let txt = "(" + m.fname + " " + m.fsize + " bytes)";
          a.href = window.fossil.rootPath+
            'chat-download/' + m.msgid+'/'+encodeURIComponent(m.fname);
          // ^^^ add m.fname to URL to cause downloaded file to have that name.
          a.appendChild(textNode(txt));
          span.appendChild(a);
        }
        let br = document.createElement("br");
        br.style.clear = "both";
        span.appendChild(br);
      }
      if(m.xmsg){
        span.innerHTML += m.xmsg;
      }
      span.classList.add('chat-message');
      if( m.xfrom!=_me ){
        span.classList.add('chat-mx');
      }else{
        span.classList.add('chat-ms');
      }
    }
  }
  async function poll(){
    if(poll.running) return;
    poll.running = true;
    fetch("chat-poll/" + mxMsg)
    .then(x=>x.json())
    .then(y=>newcontent(y))
    .catch(e=>console.error(e))
    .finally(()=>poll.running=false)
  }
  poll();
  setInterval(poll, 1000);
})();
