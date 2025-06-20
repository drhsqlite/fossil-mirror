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
  const eCurrentUrl = E('#currentUrl');

  /*
    Prepend the fossil instance's URL to each link. We have to guess
    which part of the URL is the fossil CGI/server instance.  The
    following works when run (A) from under /uv or /ext and (B) from
    /doc/branchname/test/link-tester.html.
  */
  let urlTop;
  let loc = (''+window.location);
  let aLoc = loc.split('/')
  aLoc.pop(); /* file name */
  const thisDir = aLoc.join('/');
  const rxDoc = /.*\/doc\/[^/]+\/.*/;
  //console.log(rxDoc, loc, aLoc);
  if( loc.match(rxDoc) ){
    /* We're hopefully now at the top-most fossil-served
       URL. */
    aLoc.pop(); aLoc.pop(); /* /doc/foo */
    aLoc.pop(); /* current dir name */
  }else{
    aLoc.pop(); /* current dir name */
  }
  urlTop = aLoc.join('/');
  //console.log(urlTop, aLoc);
  for( const o of eSelect.options ){
    o.value = urlTop + (o.value || o.innerText);
  }

  const eBtnPrev = E('#btn-prev');
  const eBtnNext = E('#btn-next');

  const updateUrl = function(opt){
    if( opt ){
      let url = (opt.value || opt.innerText);
      eCurrentUrl.innerText = url.replace(urlTop,'');
      eCurrentUrl.setAttribute('href', url);
    }else{
      eCurrentUrl.innerText = '';
    }
  };

  eSelect.addEventListener('change',function(ev){
    const so = ev.target.options[ev.target.selectedIndex];
    if( so ){
      eIframe.setAttribute('src', so.value || so.innerText);
      updateUrl(so);
    }
  });

  const selectEntry = function(ndx){
    if( ndx>=0 ){
      eSelect.selectedIndex = ndx;
      eSelect.dispatchEvent(new Event('change',{target:eSelect}));
    }
  };

  const cycleLink = function(dir){
    let n = eSelect.selectedIndex + dir;
    if( n < 0 ) n = eSelect.options.length-1;
    else if( n>=eSelect.options.length ){
      n = 0;
    }
    const opt = eSelect.options[n];
    if( opt && opt.disabled ){
      /* If that OPTION element is disabled, skip over it. */
      eSelect.selectedIndex = n;
      cycleLink(dir);
    }else{
      selectEntry(n);
    }
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
      eConstrained.style.top = 'calc('+extra+'px + 1.5em)';
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

  selectEntry(0);

  /**
     Read link-tester.json, which should live in the same directory
     as this file. It's expected to be an array with entries
     in one of the following forms:

     - "string"   = Separator label (disabled)
     - ["/path"]  = path with itself as a label
     - ["label", "/path"] = path with the given label

     All paths are expected to have a "/" prefix and this script
     accounts for mapping that to the fossil part of this script's
     URL.
  */
  window.fetch(thisDir+'/link-tester.json').then((r)=>r.json()).then(j=>{
    //console.log("fetched",j);
    eSelect.innerHTML = '';
    const opt = function(arg){
      const o = document.createElement('option');
      //console.warn(arguments);
      let rc = true;
      if( 'string' === typeof arg ){
        /* Grouping separator */
        o.innerText = "--- " + arg + " ---";
        o.setAttribute('disabled','');
        rc = false;
      }else if( 1===arg.length ){
        o.innerText = arg[0];
        o.value = urlTop + arg[0];
      }else if( 2==arg.length ){
        o.innerText = arg[0];
        o.value = urlTop + arg[1];
      }
      eSelect.appendChild(o);
      return rc;
    };
    let ndx = -1/*index of first non-disabled entry*/, i = 0;
    for(const e of j){
      if( opt(e) && ndx<0 ){
        ndx = i;
      }
      ++i;
    }
    selectEntry(ndx);
  });
});
