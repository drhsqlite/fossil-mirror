(function callee(arg){
  /*
    JS counterpart of info.c:output_text_with_line_numbers()
    which ties an event handler to the line numbers to allow
    selection of individual lines or ranges.

    Requires: fossil.bootstrap, fossil.dom, fossil.popupwidget,
    fossil.copybutton
  */
  var tbl = arg || document.querySelectorAll('table.numbered-lines');
  if(tbl && !arg){
    if(tbl.length>1){ /* multiple query results: recurse */
      tbl.forEach( (t)=>callee(t) );
      return;
    }else{/* single query result */
      tbl = tbl[0];
    }
  }
  if(!tbl) return /* no matching elements */;
  const F = window.fossil, D = F.dom;
  const tdLn = tbl.querySelector('td.line-numbers');
  const urlArgsRaw = (window.location.search||'?')
      .replace(/&?\budc=[^&]*/,'') /* "update display prefs cookie" */
      .replace(/&?\bln=[^&]*/,'') /* inbound line number/range */
      .replace('?&','?');
  var urlArgsDecoded = urlArgsRaw;
  try{urlArgsDecoded = decodeURIComponent(urlArgsRaw);}
  catch{}
  const lineState = { urlArgs: urlArgsDecoded, start: 0, end: 0 };
  const lineTip = new F.PopupWidget({
    style: {
      cursor: 'pointer'
    },
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
          ' Copy link to '+(
            ls.length===1 ? 'line ' : 'lines '
          )+ls.join('-')
        );
      }else{
        D.append(link, "No lines selected.");
      }
    },
    init: function(){
      const e = this.e;
      const btnCopy = D.span(),
            link = D.span();
      this.state = {link};
      F.copyButton(btnCopy,{
        copyFromElement: link,
        extractText: ()=>link.dataset.url,
        oncopy: (ev)=>{
          D.flashOnce(ev.target, undefined, ()=>lineTip.hide());
          // arguably too snazzy: F.toast.message("Copied link to clipboard.");
        }
      });
      this.e.addEventListener('click', ()=>btnCopy.click(), false);
      D.append(this.e, btnCopy, link)
    }
  });

  tbl.addEventListener('click', ()=>lineTip.hide(), true);
  
  tdLn.addEventListener('click', function f(ev){
    if('SPAN'!==ev.target.tagName) return;
    else if('number' !== typeof f.mode){
      f.mode = 0 /*0=none selected, 1=1 selected, 2=2 selected*/;
      f.spans = tdLn.querySelectorAll('span');
      f.selected = tdLn.querySelectorAll('span.selected-line');
      f.unselect = (e)=>D.removeClass(e, 'selected-line','start','end');
    }
    ev.stopPropagation();
    const ln = +ev.target.innerText;
    if(2===f.mode){/*Reset selection*/
      f.mode = 0;
    }
    if(0===f.mode){/*Select single line*/
      lineState.end = 0;
      lineState.start = ln;
      f.mode = 1;
    }else if(1===f.mode){
      if(ln === lineState.start){/*Unselect line*/
        lineState.start = 0;
        f.mode = 0;
      }else{/*Select range*/
        if(ln<lineState.start){
          lineState.end = lineState.start;
          lineState.start = ln;
        }else{
          lineState.end = ln;
        }
        f.mode = 2;
      }
    }
    if(f.selected){/*Unmark previously-selected lines.*/
      f.selected.forEach(f.unselect);
      f.selected = undefined;
    }
    if(0===f.mode){
      lineTip.hide();
    }else{/*Mark selected lines*/
      const rect = ev.target.getBoundingClientRect();
      f.selected = [];
      if(f.spans.length>=lineState.start){
        let i = lineState.start, end = lineState.end || lineState.start, span = f.spans[i-1];
        for( ; i<=end && span; span = f.spans[i++] ){
          span.classList.add('selected-line');
          f.selected.push(span);
          if(i===lineState.start) span.classList.add('start');
          if(i===end) span.classList.add('end');
        }
      }
      lineTip.refresh().show(rect.right+3, rect.top-4);
    }
  }, false);
  
})();
