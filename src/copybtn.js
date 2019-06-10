/* Manage "Copy Buttons" linked to target elements, to copy the text (or, parts
** thereof) of the target elements to the clipboard.
**
** Newly created buttons are <span> elements with an SVG background icon,
** defined by the "copy-button" class in the default CSS style sheet, and are
** assigned the element ID "copy-<idTarget>".
**
** To simplify customization, the only properties modified for HTML-defined
** buttons are the "onclick" handler, and the "transition" and "opacity" styles
** (used for animation).
**
** For HTML-defined buttons, either initCopyButtonById(), or initCopyButton(),
** needs to be called to attach the "onclick" handler (done automatically from
** a handler attached to the "DOMContentLoaded" event).
**
** The initialization functions do not overwrite the "data-copytarget" and
** "data-copylength" attributes with empty or null values for <idTarget> and
** <cchLength>, respectively. Set <cchLength> to "-1" to explicitly remove the
** previous copy length limit.
**
** HTML snippet for statically created buttons:
**
**    <span class="copy-button" id="copy-<idTarget>"
**      data-copytarget="<idTarget>" data-copylength="<cchLength>"></span>
*/
function makeCopyButton(idTarget,bFlipped,cchLength){
  var elButton = document.createElement("span");
  elButton.className = "copy-button";
  if( bFlipped ) elButton.className += " copy-button-flipped";
  elButton.id = "copy-" + idTarget;
  initCopyButton(elButton,idTarget,cchLength);
  return elButton;
}
function initCopyButtonById(idButton,idTarget,cchLength){
  idButton = idButton || "copy-" + idTarget;
  var elButton = document.getElementById(idButton);
  if( elButton ) initCopyButton(elButton,idTarget,cchLength);
  return elButton;
}
function initCopyButton(elButton,idTarget,cchLength){
  elButton.style.transition = "";
  elButton.style.opacity = 1;
  if( idTarget ) elButton.setAttribute("data-copytarget",idTarget);
  if( cchLength ) elButton.setAttribute("data-copylength",cchLength);
  elButton.onclick = clickCopyButton;
  return elButton;
}
setTimeout(function(){
  var aButtons = document.getElementsByClassName("copy-button");
  for ( var i=0; i<aButtons.length; i++ ){
    initCopyButton(aButtons[i],0,0);
  }
},1);
/* The onclick handler for the "Copy Button". */
var lockCopyText = false;
function clickCopyButton(e){
  e.preventDefault();   /* Mandatory for <a> and <button>. */
  e.stopPropagation();
  if( lockCopyText ) return;
  lockCopyText = true;
  this.style.transition = "opacity 400ms ease-in-out";
  this.style.opacity = 0;
  var idTarget = this.getAttribute("data-copytarget");
  var elTarget = document.getElementById(idTarget);
  if( elTarget ){
    var text = elTarget.innerText.replace(/^\s+|\s+$/g,'');
    var cchLength = parseInt(this.getAttribute("data-copylength"));
    if( !isNaN(cchLength) && cchLength>0 ){
      text = text.slice(0,cchLength);   // Assume single-byte chars.
    }
    copyTextToClipboard(text);
  }
  setTimeout(function(id){
    var elButton = document.getElementById(id);
    if( elButton ){
      elButton.style.transition = "";
      elButton.style.opacity = 1;
    }
    lockCopyText = false;
  }.bind(null,this.id),400);
}
/* Create a temporary <textarea> element and copy the contents to clipboard. */
function copyTextToClipboard(text){
  var x = document.createElement("textarea");
  x.style.position = 'absolute';
  x.style.left = '-9999px';
  x.value = text;
  document.body.appendChild(x);
  x.select();
  try{
    document.execCommand('copy');
  }catch(err){}
  document.body.removeChild(x);
}
