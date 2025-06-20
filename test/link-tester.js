/**
   JS code for link-tester.html. We cannot host this JS inline in that
   file because fossil's default Content Security Policy won't let it
   run that way.
*/
window.addEventListener("DOMContentLoaded", function(){
  const E = function(s){
    const e = document.querySelector(s);
    if( !e ) throw new Error("Missing element: "+s);
    return e;
  };
  const EAll = function(s){
    const e = document.querySelectorAll(s);
    if( !e || !e.length ) throw new Error("Missing elements: "+s);
    return e;
  };
  const eIframe = E('#iframe');
  const eSelect = E('#selectPage');
  eSelect.addEventListener('change',function(ev){
    const so = ev.target.options[ev.target.selectedIndex];
    if( so ){
      eIframe.setAttribute('src', so.value || so.innerText);
    }
  });

  /*
    Prepend the fossil instance's URL to each link so that this script
    works when run from a non-localhost "fossil ui/server" instance.
    We _assume_ that this script is run from /uv or /ext, with no
    subsequent dirs after /uv or /ext. To run from deeper levels the
    following logic needs to be adjusted to guess where the fossil
    instance's CGI script is.
  */
  let top = (''+window.location).split('/');
  top.pop(); // this file name
  top.pop(); // parent dir
  /* We're hopefully now at the top-most fossil-served
     URL. */
  top = top.join('/');
  for( const o of eSelect.options ){
    o.value = top + (o.value || o.innerText);
  }

  const eBtnPrev = E('#btn-prev');
  const eBtnNext = E('#btn-next');

  const cycleLink = function(dir){
    let n = eSelect.selectedIndex + dir;
    if( n < 0 ) n = eSelect.options.length-1;
    else if( n>=eSelect.options.length ){
      n = 0;
    }
    if( n>=0 ){
      eSelect.selectedIndex = n;
    }
    eSelect.dispatchEvent(new Event('change',{target:eSelect}));
  };

  eBtnPrev.addEventListener('click', ()=>cycleLink(-1), false);
  eBtnNext.addEventListener('click', ()=>cycleLink(1), false);

  /**
     We have to adjust the iframe's size dynamically to account for
     other widgets around it. iframes don't simply like to fill up all
     available space without some help. If #controls only contained
     the one SELECT element, CSS would be sufficient, but once we add
     text around it, #controls's size becomes unpredictable and we
     need JS to calculate it. We do this every time the window size
     changes.
  */
  // Copied from fossil.dom.js
  const effectiveHeight = function f(e){
    if(!e) return 0;
    if(!f.measure){
      f.measure = function callee(e, depth){
        if(!e) return;
        const m = e.getBoundingClientRect();
        if(0===depth){
          callee.top = m.top;
          callee.bottom = m.bottom;
        }else{
          callee.top = m.top ? Math.min(callee.top, m.top) : callee.top;
          callee.bottom = Math.max(callee.bottom, m.bottom);
        }
        Array.prototype.forEach.call(e.children,(e)=>callee(e,depth+1));
        if(0===depth){
          //console.debug("measure() height:",e.className, callee.top, callee.bottom, (callee.bottom - callee.top));
          f.extra += callee.bottom - callee.top;
        }
        return f.extra;
      };
    }
    f.extra = 0;
    f.measure(e,0);
    return f.extra;
  };

  // Copied from fossil.bootstrap.js
  const debounce = function f(func, waitMs, immediate) {
    var timeoutId;
    if(!waitMs) waitMs = f.$defaultDelay;
    return function() {
      const context = this, args = Array.prototype.slice.call(arguments);
      const later = function() {
        timeoutId = undefined;
        if(!immediate) func.apply(context, args);
      };
      const callNow = immediate && !timeoutId;
      clearTimeout(timeoutId);
      timeoutId = setTimeout(later, waitMs);
      if(callNow) func.apply(context, args);
    };
  };

  const ForceResizeKludge = (function(eToAvoid, eConstrained){
    const resized = function f(){
      if( f.$disabled ) return;
      const wh = window.innerHeight;
      let ht;
      let extra = 0;
      eToAvoid.forEach((e)=>e ? extra += effectiveHeight(e) : false);
      ht = wh - extra;
      if( ht < 100 ) ht = 100;
      eConstrained.style.top = 'calc('+extra+'px + 0.25em)';
      eConstrained.style.height =
        eConstrained.style.maxHeight = [
          "calc(", ht, "px",
          " - 0.65em"/*fudge value*/,")"
          /* ^^^^ hypothetically not needed, but both Chrome/FF on
             Linux will force scrollbars on the body if this value is
             too small; current value is empirically selected. */
        ].join('');
    };
    resized.$disabled = true/* gets deleted later */;
    window.addEventListener('resize', debounce(resized, 250), false);
    return resized;
  })(
    EAll('body > *:not(iframe)'),
    eIframe
  );

  delete ForceResizeKludge.$disabled;
  ForceResizeKludge();

  eSelect.dispatchEvent(new Event('change',{target:eSelect}));
});
