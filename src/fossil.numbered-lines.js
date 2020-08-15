(function callee(arg){
  /*
    JS counterpart of info.c:output_text_with_line_numbers()
    which ties an event handler to the line numbers to allow
    selection of individual lines or ranges.

    Requires: fossil.bootstrap, fossil.dom, fossil.tooltip
  */
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
  const F = window.fossil, D = F.dom;
  const tdLn = tbl.querySelector('td.line-numbers');
  const lineState = {
    urlArgs: (window.location.search||'').replace(/&?\bln=[^&]*/,''),
    start: 0, end: 0
  };

  const lineTip = new fossil.TooltipWidget({
    refresh: function(){
      const link = this.state.link;
      D.clearElement(link);
      if(lineState.start){
        const ls = [lineState.start];
        if(lineState.end) ls.push(lineState.end);
        link.dataset.url = (
          window.location.toString().split('?')[0]
            + lineState.urlArgs + '&ln='+ls.join('-')
        );
        D.append(
          D.clearElement(link),
          ' ',
          (ls.length===1 ? 'line ' : 'lines ')+ls.join('-')
        );
      }else{
        D.append(link, "No lines selected.");
      }
    },
    adjustX: function(x){
      return x + 20;
    },
    adjustY: function(y){
      return y - this.e.clientHeight/2;
    },
    init: function(){
      const e = this.e;
      const btnCopy = D.addClass(D.span(), 'copy-button');
      const link = D.attr(D.span(), 'id', 'fossil-ln-link');
      this.state = {link};
      F.copyButton(btnCopy,{
        copyFromElement: link,
        extractText: ()=>link.dataset.url
      });
      D.append(this.e, btnCopy, link)
    }
  });

  tbl.addEventListener('click', function f(ev){
    lineTip.show(false);
  }, false);
  
  tdLn.addEventListener('click', function f(ev){
    if('SPAN'!==ev.target.tagName) return;
    else if('number' !== typeof f.mode){
      f.mode = 0 /*0=none selected, 1=1 selected, 2=2 selected*/;
    }
    ev.stopPropagation();
    const ln = +ev.target.innerText;
    if(2===f.mode){/*reset selection*/
      f.mode = 0;
    }
    if(0===f.mode){
      lineState.end = 0;
      lineState.start = ln;
      f.mode = 1;
    }else if(1===f.mode){
      if(ln === lineState.start){/*unselect line*/
        //console.debug("Unselected line #"+ln);
        lineState.start = 0;
        f.mode = 0;
      }else{
        if(ln<lineState.start){
          lineState.end = lineState.start;
          lineState.start = ln;
        }else{
          lineState.end = ln;
        }
        //console.debug("Selected range: ",rng);
        f.mode = 2;
      }
    }
    tdLn.querySelectorAll('span.selected-line').forEach(
      (e)=>D.removeClass(e, 'selected-line','start','end'));
    if(f.mode>0){
      lineTip.show(ev.clientX, ev.clientY);
      const spans = tdLn.querySelectorAll('span');
      if(spans.length>=lineState.start){
        let i = lineState.start, end = lineState.end || lineState.start, span = spans[i-1];
        for( ; i<=end && span; span = spans[i++] ){
          span.classList.add('selected-line');
          if(i===lineState.start) span.classList.add('start');
          if(i===end) span.classList.add('end');
        }
      }
      lineTip.refresh();
    }else{
      lineTip.show(false);
    }
  }, false);
  
})();
