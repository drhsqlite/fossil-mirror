/*
** Attach appropriate javascript to each ".accordion" button so that it expands
** and contracts when clicked.
**
** The uncompressed source code for the SVG icons can be found on the wiki page
** "branch/accordion-experiments" in the Fossil repository.
**
** Implementation notes:
**
** The `maxHeight' CSS property is quite restrictive for vertical resizing of
** elements, especially for dynamic-content areas like the diff panels. That's
** why `maxHeight' is set only during animation, to prevent truncated elements.
** (The diff panels may get truncated right after page loading, and other
** elements may get truncated when resizing the browser window to a smaller
** width, causing vertical growth.)
**
** Another problem is that `scrollHeight' used to calculate the expanded height
** while still in the contracted state may return values with small errors on
** some browsers, especially for large elements, presumably due to omitting the
** space required by the vertical scrollbar that may become necessary, causing
** additional horizontal shrinking and consequently more vertical growth than
** calculated. That's why setting `maxHeight' to `scrollHeight' is considered
** "good enough" only during animation, but cleared afterwards.
**
** https://fossil-scm.org/forum/forumpost/66d7075f40
** https://fossil-scm.org/home/timeline?r=accordion-fix
*/
var acc_svgdata = ["data:image/svg+xml,"+
  "%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 16 16'%3E"+
  "%3Cpath style='fill:black;opacity:0' d='M16,16H0V0h16v16z'/%3E"+
  "%3Cpath style='fill:rgb(240,240,240)' d='M14,14H2V2h12v12z'/%3E"+
  "%3Cpath style='fill:rgb(64,64,64)' d='M13,13H3V3h10v10z'/%3E"+
  "%3Cpath style='fill:rgb(248,248,248)' d='M12,12H4V4h8v8z'/%3E"+
  "%3Cpath style='fill:rgb(80,128,208)' d='", "'/%3E%3C/svg%3E",
  "M5,7h2v-2h2v2h2v2h-2v2h-2v-2h-2z", "M11,9H5V7h6v6z"];
var a = document.getElementsByClassName("accordion");
for(var i=0; i<a.length; i++){
  var img = document.createElement("img");
  img.src = acc_svgdata[0]+acc_svgdata[2]+acc_svgdata[1];
  img.className = "accordion_btn accordion_btn_plus";
  a[i].insertBefore(img,a[i].firstChild);
  img = document.createElement("img");
  img.src = acc_svgdata[0]+acc_svgdata[3]+acc_svgdata[1];
  img.className = "accordion_btn accordion_btn_minus";
  a[i].insertBefore(img,a[i].firstChild);
  a[i].addEventListener("click",function(){
    var x = this.nextElementSibling;
    if( this.classList.contains("accordion_closed") ){
      x.style.maxHeight = x.scrollHeight + "px";
      setTimeout(function(){
        x.style.maxHeight = "";
      },250); // default.css: .accordion_panel { transition-duration }
    }else{
      x.style.maxHeight = x.scrollHeight + "px";
      setTimeout(function(){
        x.style.maxHeight = "0";
      },1);
    }
    this.classList.toggle("accordion_closed");
  });
}
