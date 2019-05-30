/* Create (if necessary) and initialize a "Copy Text" button <idButton> linked
** to the target element <idTarget>.
**
** HTML snippet for statically created buttons:
**
**    <span class="copy-button" id="idButton"
**      data-copytarget="idTarget" data-copylength="cchLength"></span>
**
** Note: Both <idTarget> and <cchLength> can be set statically or dynamically,
** i.e. the makeCopyButton() function does not overwrite the "data-copytarget"
** and "data-copylength" attributes with empty/zero values.
*/
function makeCopyButton(idButton,idTarget,cchLength){
  var elButton = document.getElementById(idButton);
  if( !elButton ){
    elButton = document.createElement("span");
    elButton.className = "copy-button";
    elButton.id = idButton;
  }
  elButton.style.transition = "";
  elButton.style.opacity = 1;
  if( idTarget ) elButton.setAttribute("data-copytarget",idTarget);
  if( cchLength ) elButton.setAttribute("data-copylength",cchLength);
  elButton.onclick = clickCopyButton;
  return elButton;
}
/* The onclick handler for the "Copy Text" button. */
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
    var text = elTarget.innerText;
    var cchLength = this.getAttribute("data-copylength");
    if( cchLength ) text = text.slice(0,cchLength); // Assume single-byte chars.
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
