"use strict";
/* Bootstrapping bits for the window.fossil object. Must be
   loaded after style.c:style_emit_script_tag() has initialized
   that object.
*/

/*
** By default fossil.message() sends its arguments console.debug(). If
** fossil.message.targetElement is set, it is assumed to be a DOM
** element, its innerText gets assigned to the concatenation of all
** arguments (with a space between each), and the CSS 'error' class is
** removed from the object. Pass it a falsy value to clear the target
** element.
**
** Returns this object.
*/
window.fossil.message = function f(msg){
  const args = Array.prototype.slice.call(arguments,0);
  const tgt = f.targetElement;
  if(tgt){
    tgt.classList.remove('error');
    tgt.innerText = args.join(' ');
  }
  else{
    args.unshift('Fossil status:');
    console.debug.apply(console,args);
  }
  return this;
};
/*
** Set message.targetElement to #fossil-status-bar, if found.
*/
window.fossil.message.targetElement =
  document.querySelector('#fossil-status-bar');
/*
** By default fossil.error() sends its first argument to
** console.error(). If fossil.message.targetElement (yes,
** fossil.message) is set, it adds the 'error' CSS class to
** that element and sets its content as defined for message().
**
** Returns this object.
*/
window.fossil.error = function f(msg){
  const args = Array.prototype.slice.call(arguments,0);
  const tgt = window.fossil.message.targetElement;
  if(tgt){
    tgt.classList.add('error');
    tgt.innerText = args.join(' ');
  }
  else{
    args.unshift('Fossil error:');
    console.error.apply(console,args);
  }
  return this;
};
