/* Javascript code to handle button actions on the login page */
var autofillButton = document.getElementById('autofillButton');
autofillButton.onclick = function(){
  document.getElementById('u').value = 'anonymous';
  document.getElementById('p').value = autofillButton.getAttribute('data-af');
};
