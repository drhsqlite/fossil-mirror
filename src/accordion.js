# Attach appropriate javascript to each ".accordion" button so that
# it expands and contracts when clicked.
#
var a = document.getElementsByClassName("accordion");
for(var i=0; i<a.length; i++){
  var p = a[i].nextElementSibling;
  p.style.maxHeight = p.scrollHeight + "px";
  a[i].addEventListener("click",function(){
    var x = this.nextElementSibling;
    if( this.classList.contains("accordion_closed") ){
      x.style.maxHeight = x.scrollHeight + "px";
    }else{
      x.style.maxHeight = "0";
    }
    this.classList.toggle("accordion_closed");
  });
}
