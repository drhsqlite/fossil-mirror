(function callee(arg){
  /* JS counterpart of info.c:output_text_with_line_numbers()
     which ties an event handler to the line numbers to allow
     selection of individual lines or ranges. */
  var tbl = arg || document.querySelectorAll('table.numbered-lines');
  if(!tbl) return /* no matching elements */;
  else if(!arg){
    if(tbl.length>1){ /* multiple query results: recurse */
      tbl.forEach( (t)=>callee(t) );
      return;
    }else{/* single query result */
      tbl = tbl[0];
    }
  }
  const tdLn = tbl.querySelector('td'),
        urlArgs = (window.location.search||'').replace(/&?\bln=[^&]*/,'');
  console.debug("urlArgs =",urlArgs);
  tdLn.addEventListener('click', function f(ev){
    if(!f.selectedRange){
      f.selectedRange = [0,0];
      f.mode = 0 /*0=none selected, 1=1 selected, 2=2 selected*/;
    }
    if('SPAN'===ev.target.tagName){
      const rng = f.selectedRange;
      const ln = +ev.target.innerText;
      if(2===f.mode){/*reset selection*/
        f.mode = 0;
        //rng[0] = rng[1] = 0;
      }
      if(0===f.mode){
        rng[1] = 0;
        rng[0] = ln;
        //console.debug("Selected line #"+ln);
        history.pushState(undefined,'',urlArgs+'&ln='+ln);
        f.mode = 1;
      }else if(1===f.mode){
        if(ln === rng[0]){/*unselect line*/
          //console.debug("Unselected line #"+ln);
          history.pushState(undefined,'',urlArgs+'&ln=on');
          rng[0] = 0;
          f.mode = 0;
        }else{
          if(ln<rng[0]){
            rng[1] = rng[0];
            rng[0] = ln;
          }else{
            rng[1] = ln;
          }
          //console.debug("Selected range: ",rng);
          history.pushState(undefined,'',urlArgs+'&ln='+rng.join('-'));
          f.mode = 2;
        }
      }                
    }
  }, false);
})();
