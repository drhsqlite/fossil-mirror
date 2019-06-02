/* Manage "Copy Buttons" linked to target elements, to copy the text (or, parts
** thereof) of the target elements to the clipboard.
**
** Newly created buttons are <span> elements with an SVG background icon,
** defined by the "copy-button" class in the default CSS style sheet.
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
**    <span class="copy-button" id="idButton"
**      data-copytarget="idTarget" data-copylength="cchLength"></span>
*/
function makeCopyButton(idButton,idTarget,cchLength){
  var elButton = document.createElement("span");
  elButton.className = "copy-button";
  elButton.id = idButton;
  initCopyButton(elButton,idTarget,cchLength);
  return elButton;
}
function initCopyButtonById(idButton,idTarget,cchLength){
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
onContentLoaded(function(){
  var aButtons = document.getElementsByClassName("copy-button");
  for ( var i=0; i<aButtons.length; i++ ){
    initCopyButton(aButtons[i],0,0);
  }
});
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
  var textArea = document.createElement("textarea");
  textArea.style.position = 'fixed';
  textArea.style.top = 0;
  textArea.style.left = 0;
  textArea.style.width = '2em';
  textArea.style.height = '2em';
  textArea.style.padding = 0;
  textArea.style.border = 'none';
  textArea.style.outline = 'none';
  textArea.style.boxShadow = 'none';
  textArea.style.background = 'transparent';
  textArea.value = text;
  document.body.appendChild(textArea);
  textArea.focus();
  textArea.select();
  try{
    document.execCommand('copy');
  }catch(err){
  }
  document.body.removeChild(textArea);
}
/* Execute a function as soon as the HTML document has been completely loaded.
** The idea for this code is based on the contentLoaded() function presented
** here:
**
**    Cross-browser wrapper for DOMContentLoaded
**    http://javascript.nwbox.com/ContentLoaded/
*/
function onContentLoaded(fnready) {
  var fninit = function() {
    if (document.addEventListener ||
        event.type === 'load' ||
        document.readyState === 'complete') {
      if (document.addEventListener) {
        document.removeEventListener('DOMContentLoaded',fninit,false);
        window.removeEventListener('load',fninit,false);
      }
      else {
        document.detachEvent('onreadystatechange',fninit);
        window.detachEvent('onload',fninit);
      }
    }
    fnready.call(window);
  };
  if (document.readyState === 'complete')
    fnready.call(window);
  else if (document.addEventListener) {
    document.addEventListener('DOMContentLoaded',fninit,false);
    window.addEventListener('load',fninit,false);
  }
  else {
    document.attachEvent('onreadystatechange',fninit);
    window.attachEvent('onload',fninit);
  }
}
