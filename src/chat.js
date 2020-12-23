(function(){
  const form = document.querySelector('#chat-form');
  let mxMsg = 0;
  const F = window.fossil;
  const _me = F.user.name;
  form.addEventListener('submit',(e)=>{
    e.preventDefault();
    if( form.msg.value.length>0 || form.file.value.length>0 ){
      fetch("chat-send",{
        method: 'POST',
        body: new FormData(form)
      });
    }
    form.msg.value = "";
    form.file.value = "";
    form.msg.focus();
  });
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
  /* Timestampt popup widget */
  const tsPopup = new F.PopupWidget({
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
  const hidePopup = ()=>tsPopup.hide();
  tsPopup.e.addEventListener('click', hidePopup, false);
  document.body.addEventListener('click', hidePopup, true);
  document.body.addEventListener('keydown', function(ev){
    if(tsPopup.isShown() && 27===ev.which) tsPopup.hide();
  }, true);
  /* Event handler for clicking .message-user elements to show their
     timestamps. */
  const handleLegendClicked = function(ev){
    const rect = ev.target.getBoundingClientRect();
    tsPopup._timestamp = ev.target.dataset.timestamp;
    let x = rect.left, y = rect.top - 10;
    tsPopup.show(ev.target)/*so we can get its computed size*/;
    // Shift to the left for right-aligned messages
    if('right'===ev.target.getAttribute('align')){
      const pRect = tsPopup.e.getBoundingClientRect();
      x -= pRect.width/3*2;
    }
    tsPopup.show(x, y);
  };

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
          a.href = "%string($downloadurl)/" + m.msgid;
          a.appendChild(document.createTextNode(txt));
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
    .finally(()=>poll.running=false)
  }
  poll();
  setInterval(poll, 1000);
})();
