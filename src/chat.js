(function(){
  const form = document.querySelector('#chat-form');
  let mxMsg = 0;
  // let _me = "%string($me)";
  form.addEventListener('submit',(e)=>{
    e.preventDefault();
    if( form.msg.value.length>0 || form.file.value.length>0 ){
      fetch("%string($submiturl)",{
        method: 'POST',
        body: new FormData(form)
      });
    }
    form.msg.value = "";
    form.file.value = "";
    form.msg.focus();
  });
  const rxUrl = /\b(?:https?|ftp):\/\/\[a-z0-9-+&@\#\/%?=~_|!:,.;]*\[a-z0-9-+&@\#\/%=~_|]/gim;
  const rxAtName = /@\w+/gmi;
  // ^^^ achtung, extra backslashes needed for the outer TCL.
  const textNode = (T)=>document.createTextNode(T);

  // Converts a message string to a message-containing DOM element
  // and returns that element, which may contain child elements.
  // If 2nd arg is passed, it must be a DOM element to which all
  // child elements are appended.
  const messageToDOM = function f(str, tgtElem){
    "use strict";
    if(!f.rxUrl){
      f.rxUrl = rxUrl;
      f.rxAt = rxAtName;
      f.rxNS = /\S/;
      f.ce = (T)=>document.createElement(T);
      f.ct = (T)=>document.createTextNode(T);
      f.replaceUrls = function ff(sub, offset, whole){
        if(offset > ff.prevStart){
          f.accum.push((ff.prevStart?' ':'')+whole.substring(ff.prevStart, offset-1)+' ');
        }
        const a = f.ce('a');
        a.setAttribute('href',sub);
        a.setAttribute('target','_blank');
        a.appendChild(f.ct(sub));
        f.accum.push(a);
        ff.prevStart = offset + sub.length + 1;
      };
      f.replaceAtName = function ff(sub, offset,whole){
        if(offset > ff.prevStart){
          ff.accum.push((ff.prevStart?' ':'')+whole.substring(ff.prevStart, offset-1)+' ');
        }else if(offset && f.rxNS.test(whole[offset-1])){
          // Sigh: https://stackoverflow.com/questions/52655367
          ff.accum.push(sub);
          return;
        }
        const e = f.ce('span');
        e.classList.add('at-name');
        e.appendChild(f.ct(sub));
        ff.accum.push(e);
        ff.prevStart = offset + sub.length + 1;
      };
    }
    f.accum = []; // accumulate strings and DOM elements here.
    f.rxUrl.lastIndex = f.replaceUrls.prevStart = 0; // reset regex cursor
    str.replace(f.rxUrl, f.replaceUrls);
    // Push remaining non-URL part of the string to the queue...
    if(f.replaceUrls.prevStart < str.length){
      f.accum.push((f.replaceUrls.prevStart?' ':'')+str.substring(f.replaceUrls.prevStart));
    }
    // Pass 2: process @NAME references...
    // TODO: only match NAME if it's the name of a currently participating
    // user. Add a second class if NAME == current user, and style that one
    // differently so that people can more easily see when they're spoken to.
    const accum2 = f.replaceAtName.accum = [];
    //console.debug("f.accum =",f.accum);
    f.accum.forEach(function(v){
      //console.debug("v =",v);
      if('string'===typeof v){
        f.rxAt.lastIndex = f.replaceAtName.prevStart = 0;
        v.replace(f.rxAt, f.replaceAtName);
        if(f.replaceAtName.prevStart < v.length){
          accum2.push((f.replaceAtName.prevStart?' ':'')+v.substring(f.replaceAtName.prevStart));
        }
      }else{
        accum2.push(v);
      }
      //console.debug("accum2 =",accum2);
    });
    delete f.accum;
    //console.debug("accum2 =",accum2);
    const span = tgtElem || f.ce('span');
    accum2.forEach(function(e){
      if('string'===typeof e) e = f.ct(e);
      span.appendChild(e);
    });
    //console.debug("span =",span.innerHTML);
    return span;
  }/*end messageToDOM()*/;
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
  function newcontent(jx){
    var i;
    for(i=0; i<jx.msgs.length; ++i){
      let m = jx.msgs[i];
      let row = document.createElement("fieldset");
      if( m.msgid>mxMsg ) mxMsg = m.msgid;
      row.classList.add('message-row');
      injectMessage(row);
      const eWho = document.createElement('legend');
      eWho.setAttribute('align', (m.xfrom===_me ? 'right' : 'left'));
      row.appendChild(eWho);
      eWho.classList.add('message-user');
      let whoName;
      if( m.xfrom===_me ){
        whoName = 'me';
        row.classList.add('user-is-me');
      }else{
        whoName = m.xfrom;
      }
      eWho.append(textNode(
                  whoName+' @ '+
                  localTimeString(new Date(Date.parse(m.mtime+".000Z"))))
      );
      let span = document.createElement("div");
      span.classList.add('message-content');
      row.appendChild(span);
      if( m.fsize>0 ){
        if( m.fmime && m.fmime.startsWith("image/") ){
          let img = document.createElement("img");
          img.src = "%string($downloadurl)/" + m.msgid;
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
        messageToDOM(m.xmsg, span);
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
    fetch("%string($pollurl)/" + mxMsg)
    .then(x=>x.json())
    .then(y=>newcontent(y))
    .finally(()=>poll.running=false)
  }
  setInterval(poll, 1000);
})();</script>
