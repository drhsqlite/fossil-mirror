/* Attach appropriate javascript to each ".accordion" button so that
** it expands and contracts when clicked.
*/
var a = document.getElementsByClassName("accordion");
for(var i=0; i<a.length; i++){
  var img = document.createElement("img");
  img.src =
"data:image/svg+xml,"+
"%3Csvg xmlns='http:"+"/"+"/www.w3.org/2000/svg' viewBox='0 0 16 16'%3E"+
"%3Cpath style='fill:black;opacity:0' d='M16,16H0V0h16v16z'/%3E"+
"%3Cpath style='fill:rgb(240,240,240)' d='M14,14H2V2h12v12z'/%3E"+
"%3Cpath style='fill:rgb(64,64,64)' d='M13,13H3V3h10v10z'/%3E"+
"%3Cpath style='fill:rgb(248,248,248)' d='M12,12H4V4h8v8z'/%3E"+
"%3Cpath style='fill:rgb(80,128,208)' d='M5,7h2v-2h2v2h2v2h-2v2h-2v-2h-2z'/%3E"+
"%3C/svg%3E";
  img.className = "accordion_btn accordion_btn_plus";
  a[i].insertBefore(img,a[i].firstChild);
  img = document.createElement("img");
  img.src =
"data:image/svg+xml,"+
"%3Csvg xmlns='http:"+"/"+"/www.w3.org/2000/svg' viewBox='0 0 16 16'%3E"+
"%3Cpath style='fill:black;opacity:0' d='M16,16H0V0h16v16z'/%3E"+
"%3Cpath style='fill:rgb(240,240,240)' d='M14,14H2V2h12v12z'/%3E"+
"%3Cpath style='fill:rgb(64,64,64)' d='M13,13H3V3h10v10z'/%3E"+
"%3Cpath style='fill:rgb(248,248,248)' d='M12,12H4V4h8v8z'/%3E"+
"%3Cpath style='fill:rgb(80,128,208)' d='M11,9H5V7h6v6z'/%3E"+
"%3C/svg%3E";
  img.className = "accordion_btn accordion_btn_minus";
  a[i].insertBefore(img,a[i].firstChild);
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
