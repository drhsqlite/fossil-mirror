/* This script accompanies the /setup_uedit web page.  Its job is to keep
** the check-boxes with user capabilities up-to-date with the capability
** string.
**
** The capability string is stored in #usetupEditCapability
*/
function updateCapabilityString(){
  try {
    var inputs = document.getElementsByTagName('input');
    if( inputs && inputs.length ){
      var output = document.getElementById('usetupEditCapability');
      if( output ){
        var permsIds = [], x = 0;
        for(var i = 0; i < inputs.length; i++){
          var e = inputs[i];
          if( !e.name || !e.type ) continue;
          if( e.type.toLowerCase()!=='checkbox' ) continue;
          if( e.name.length===2 && e.name[0]==='a' ){
            // looks like a capability checkbox
            e.onchange = updateCapabilityString;
            if( e.checked ){
              // grab the second character of the element
              // name, which is the textual flag for this
              // capability, and then add it to the result
              // array.
              permsIds[x++] = e.name[1];
            }
          }
        }
        permsIds.sort();
        output.innerHTML = permsIds.join('');
      }
    }
  } catch (e) {
    /* ignore errors */
  }
}
updateCapabilityString();
