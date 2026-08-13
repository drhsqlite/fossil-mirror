/*
 * This script adds a checkbox to reverse the sorting on any body.tkt
 * pages which contain a .tktCommentArea element.
 */
window.addEventListener( 'load', function() {
  const tgt = document.querySelectorAll('.tktCommentArea');
  if( !tgt ) return;
  const F = globalThis.fossil, D = F.dom;
  let i = 0;
  for(const e of tgt) {
    ++i;
    const childs = e.querySelectorAll('.tktCommentEntry');
    if( !childs || 1===childs.length ) continue;
    const cbReverseKey = 'tktCommentArea:reverse';
    const cbReverse = D.checkbox();
    const cbId = cbReverseKey+':'+i;
    cbReverse.setAttribute('id',cbId);
    const widget = D.append(
      D.div(),
      cbReverse,
      D.label(cbReverse, " Show newest first? ")
    );
    widget.classList.add('newest-first-controls');
    e.parentElement.insertBefore(widget,e);
    const cbReverseIt = ()=>{
      e.classList[cbReverse.checked ? 'add' : 'remove']('reverse');
      F.storage.set(cbReverseKey, cbReverse.checked ? 1 : 0);
    };
    cbReverse.addEventListener('change', cbReverseIt, true);
    cbReverse.checked = !!(+F.storage.get(cbReverseKey, 0));
  };
}); // window.addEventListener( 'load' ...
