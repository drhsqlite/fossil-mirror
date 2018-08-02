/*
** Copyright (c) 2012 D. Richard Hipp
**
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
**
** Author contact information:
**   drh@hwaci.com
**   http://www.hwaci.com/drh/
**
*******************************************************************************
**
** This file contains code that generates WYSIWYG text editors on
** web pages.
*/
#include "config.h"
#include <assert.h>
#include <ctype.h>
#include "wysiwyg.h"


/*
** Output code for a WYSIWYG editor.  The caller must have already generated
** the <form> that will contain the editor, and the call must generate the
** corresponding </form> after this routine returns.  The caller must include
** an onsubmit= attribute on the <form> element that invokes the
** wysiwygSubmit() function.
**
** There can only be a single WYSIWYG editor per frame.
*/
void wysiwygEditor(
  const char *zId,        /* ID for this editor */
  const char *zContent,   /* Initial content (HTML) */
  int w, int h            /* Initial width and height */
){

  @ <style type="text/css">
  @ .intLink { cursor: pointer; }
  @ img.intLink { border: 0; }
  @ #wysiwygBox {
  @   border: 1px #000000 solid;
  @   padding: 12px;
  @ }
  @ #editMode label { cursor: pointer; }
  @ </style>

  @ <input id="wysiwygValue" type="hidden" name="%s(zId)">
  @ <div id="editModeDiv">Edit mode:
  @   <select id="editMode" size=1 onchange="setDocMode(this.selectedIndex)">
  @ <option value="0">WYSIWYG</option>
  @ <option value="1">Raw HTML</option>
  @ </select></div>
  @ <div id="toolBar1">
  @ <select onchange="formatDoc('formatblock',this[this.selectedIndex].value);
  @                   this.selectedIndex=0;">
  @ <option selected>- formatting -</option>
  @ <option value="h1">Title 1 &lt;h1&gt;</option>
  @ <option value="h2">Title 2 &lt;h2&gt;</option>
  @ <option value="h3">Title 3 &lt;h3&gt;</option>
  @ <option value="h4">Title 4 &lt;h4&gt;</option>
  @ <option value="h5">Title 5 &lt;h5&gt;</option>
  @ <option value="h6">Subtitle &lt;h6&gt;</option>
  @ <option value="p">Paragraph &lt;p&gt;</option>
  @ <option value="pre">Preformatted &lt;pre&gt;</option>
  @ </select>
  @ <select onchange="formatDoc('fontname',this[this.selectedIndex].value);
  @                   this.selectedIndex=0;">
  @ <option class="heading" selected>- font -</option>
  @ <option>Arial</option>
  @ <option>Arial Black</option>
  @ <option>Courier New</option>
  @ <option>Times New Roman</option>
  @ </select>
  @ <select onchange="formatDoc('fontsize',this[this.selectedIndex].value);
  @                   this.selectedIndex=0;">
  @ <option class="heading" selected>- size -</option>
  @ <option value="1">Very small</option>
  @ <option value="2">A bit small</option>
  @ <option value="3">Normal</option>
  @ <option value="4">Medium-large</option>
  @ <option value="5">Big</option>
  @ <option value="6">Very big</option>
  @ <option value="7">Maximum</option>
  @ </select>
  @ <select onchange="formatDoc('forecolor',this[this.selectedIndex].value);
  @                   this.selectedIndex=0;">
  @ <option class="heading" selected>- color -</option>
  @ <option value="red">Red</option>
  @ <option value="blue">Blue</option>
  @ <option value="green">Green</option>
  @ <option value="black">Black</option>
  @ </select>
  @ </div>
  @ <div id="toolBar2">
  @ <img class="intLink" title="Undo" onclick="formatDoc('undo');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAOMKADljwliE33mOrpGjuYKl8aezxqPD+7
  @ /I19DV3NHa7P///////////////////////yH5BAEKAA8ALAAAAAAWABYAAARR8MlJq704680
  @ 7TkaYeJJBnES4EeUJvIGapWYAC0CsocQ7SDlWJkAkCA6ToMYWIARGQF3mRQVIEjkkSVLIbSfE
  @ whdRIH4fh/DZMICe3/C4nBQBADs=">

  @ <img class="intLink" title="Redo" onclick="formatDoc('redo');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAMIHAB1ChDljwl9vj1iE34Kl8aPD+7/I1/
  @ ///yH5BAEKAAcALAAAAAAWABYAAANKeLrc/jDKSesyphi7SiEgsVXZEATDICqBVJjpqWZt9Na
  @ EDNbQK1wCQsxlYnxMAImhyDoFAElJasRRvAZVRqqQXUy7Cgx4TC6bswkAOw==">

  @ <img class="intLink" title="Remove formatting"
  @ onclick="formatDoc('removeFormat')"
  @ src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABYAAAAWCAYAAADEtGw7AA
  @ AABGdBTUEAALGPC/xhBQAAAAZiS0dEAP8A/wD/oL2nkwAAAAlwSFlzAAAOxAAADsQBlSsOGwA
  @ AAAd0SU1FB9oECQMCKPI8CIIAAAAIdEVYdENvbW1lbnQA9syWvwAAAuhJREFUOMtjYBgFxAB5
  @ 01ZWBvVaL2nHnlmk6mXCJbF69zU+Hz/9fB5O1lx+bg45qhl8/fYr5it3XrP/YWTUvvvk3VeqG
  @ Xz70TvbJy8+Wv39+2/Hz19/mGwjZzuTYjALuoBv9jImaXHeyD3H7kU8fPj2ICML8z92dlbtMz
  @ deiG3fco7J08foH1kurkm3E9iw54YvKwuTuom+LPt/BgbWf3//sf37/1/c02cCG1lB8f//f95
  @ DZx74MTMzshhoSm6szrQ/a6Ir/Z2RkfEjBxuLYFpDiDi6Af///2ckaHBp7+7wmavP5n76+P2C
  @ lrLIYl8H9W36auJCbCxM4szMTJac7Kza////R3H1w2cfWAgafPbqs5g7D95++/P1B4+ECK8tA
  @ wMDw/1H7159+/7r7ZcvPz4fOHbzEwMDwx8GBgaGnNatfHZx8zqrJ+4VJBh5CQEGOySEua/v3n
  @ 7hXmqI8WUGBgYGL3vVG7fuPK3i5GD9/fja7ZsMDAzMG/Ze52mZeSj4yu1XEq/ff7W5dvfVAS1
  @ lsXc4Db7z8C3r8p7Qjf///2dnZGxlqJuyr3rPqQd/Hhyu7oSpYWScylDQsd3kzvnH738wMDzj
  @ 5GBN1VIWW4c3KDon7VOvm7S3paB9u5qsU5/x5KUnlY+eexQbkLNsErK61+++VnAJcfkyMTIwf
  @ fj0QwZbJDKjcETs1Y8evyd48toz8y/ffzv//vPP4veffxpX77z6l5JewHPu8MqTDAwMDLzyrj
  @ b/mZm0JcT5Lj+89+Ybm6zz95oMh7s4XbygN3Sluq4Mj5K8iKMgP4f0////fv77//8nLy+7MCc
  @ XmyYDAwODS9jM9tcvPypd35pne3ljdjvj26+H2dhYpuENikgfvQeXNmSl3tqepxXsqhXPyc66
  @ 6s+fv1fMdKR3TK72zpix8nTc7bdfhfkEeVbC9KhbK/9iYWHiErbu6MWbY/7//8/4//9/pgOnH
  @ 6jGVazvFDRtq2VgiBIZrUTIBgCk+ivHvuEKwAAAAABJRU5ErkJggg==">

  @ <img class="intLink" title="Bold" onclick="formatDoc('bold');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB
  @ YAQAInhI+pa+H9mJy0LhdgtrxzDG5WGFVk6aXqyk6Y9kXvKKNuLbb6zgMFADs=" />

  @ <img class="intLink" title="Italic" onclick="formatDoc('italic');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAKEDAAAAAF9vj5WIbf///yH5BAEAAAMALA
  @ AAAAAWABYAAAIjnI+py+0Po5x0gXvruEKHrF2BB1YiCWgbMFIYpsbyTNd2UwAAOw==" />

  @ <img class="intLink" title="Underline" onclick="formatDoc('underline');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAKECAAAAAF9vj////////yH5BAEAAAIALA
  @ AAAAAWABYAAAIrlI+py+0Po5zUgAsEzvEeL4Ea15EiJJ5PSqJmuwKBEKgxVuXWtun+DwxCCgA
  @ 7" />

  @ <img class="intLink" title="Left align"
  @ onclick="formatDoc('justifyleft');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB
  @ YAQAIghI+py+0Po5y02ouz3jL4D4JMGELkGYxo+qzl4nKyXAAAOw==" />

  @ <img class="intLink" title="Center align"
  @ onclick="formatDoc('justifycenter');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB
  @ YAQAIfhI+py+0Po5y02ouz3jL4D4JOGI7kaZ5Bqn4sycVbAQA7" />

  @ <img class="intLink" title="Right align"
  @ onclick="formatDoc('justifyright');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB
  @ YAQAIghI+py+0Po5y02ouz3jL4D4JQGDLkGYxouqzl43JyVgAAOw==" />
  @ <img class="intLink" title="Numbered list"
  @ onclick="formatDoc('insertorderedlist');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAMIGAAAAADljwliE35GjuaezxtHa7P////
  @ ///yH5BAEAAAcALAAAAAAWABYAAAM2eLrc/jDKSespwjoRFvggCBUBoTFBeq6QIAysQnRHaEO
  @ zyaZ07Lu9lUBnC0UGQU1K52s6n5oEADs=" />

  @ <img class="intLink" title="Dotted list"
  @ onclick="formatDoc('insertunorderedlist');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAMIGAAAAAB1ChF9vj1iE33mOrqezxv////
  @ ///yH5BAEAAAcALAAAAAAWABYAAAMyeLrc/jDKSesppNhGRlBAKIZRERBbqm6YtnbfMY7lud6
  @ 4UwiuKnigGQliQuWOyKQykgAAOw==" />

  @ <img class="intLink" title="Quote"
  @ onclick="formatDoc('formatblock','blockquote');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAIQXAC1NqjFRjkBgmT9nqUJnsk9xrFJ7u2
  @ R9qmKBt1iGzHmOrm6Sz4OXw3Odz4Cl2ZSnw6KxyqO306K63bG70bTB0rDI3bvI4P/////////
  @ //////////////////////////yH5BAEKAB8ALAAAAAAWABYAAAVP4CeOZGmeaKqubEs2Cekk
  @ ErvEI1zZuOgYFlakECEZFi0GgTGKEBATFmJAVXweVOoKEQgABB9IQDCmrLpjETrQQlhHjINrT
  @ q/b7/i8fp8PAQA7" />

  @ <img class="intLink" title="Delete indentation"
  @ onclick="formatDoc('outdent');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAMIHAAAAADljwliE35GjuaezxtDV3NHa7P
  @ ///yH5BAEAAAcALAAAAAAWABYAAAM2eLrc/jDKCQG9F2i7u8agQgyK1z2EIBil+TWqEMxhMcz
  @ sYVJ3e4ahk+sFnAgtxSQDqWw6n5cEADs=" />

  @ <img class="intLink" title="Add indentation"
  @ onclick="formatDoc('indent');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAOMIAAAAADljwl9vj1iE35GjuaezxtDV3N
  @ Ha7P///////////////////////////////yH5BAEAAAgALAAAAAAWABYAAAQ7EMlJq704650
  @ B/x8gemMpgugwHJNZXodKsO5oqUOgo5KhBwWESyMQsCRDHu9VOyk5TM9zSpFSr9gsJwIAOw==">

  @ <img class="intLink" title="Hyperlink"
  @ onclick="var sLnk=prompt('Target URL:','');
  @          if(sLnk&&sLnk!=''){formatDoc('createlink',sLnk)}"
  @ src="data:image/gif;base64,R0lGODlhFgAWAOMKAB1ChDRLY19vj3mOrpGjuaezxrCztb
  @ /I19Ha7Pv8/f///////////////////////yH5BAEKAA8ALAAAAAAWABYAAARY8MlJq704682
  @ 7/2BYIQVhHg9pEgVGIklyDEUBy/RlE4FQF4dCj2AQXAiJQDCWQCAEBwIioEMQBgSAFhDAGghG
  @ i9XgHAhMNoSZgJkJei33UESv2+/4vD4TAQA7" />

#if 0  /* Cut/Copy/Paste requires special browser permissions for security
       ** reasons.  So omit these buttons */
  @ <img class="intLink" title="Cut"
  @ onclick="formatDoc('cut');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAIQSAB1ChBFNsRJTySJYwjljwkxwl19vj1
  @ dusYODhl6MnHmOrpqbmpGjuaezxrCztcDCxL/I18rL1P/////////////////////////////
  @ //////////////////////////yH5BAEAAB8ALAAAAAAWABYAAAVu4CeOZGmeaKqubDs6TNnE
  @ bGNApNG0kbGMi5trwcA9GArXh+FAfBAw5UexUDAQESkRsfhJPwaH4YsEGAAJGisRGAQY7UCC9
  @ ZAXBB+74LGCRxIEHwAHdWooDgGJcwpxDisQBQRjIgkDCVlfmZqbmiEAOw==" />

  @ <img class="intLink" title="Copy"
  @ onclick="formatDoc('copy');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAIQcAB1ChBFNsTRLYyJYwjljwl9vj1iE31
  @ iGzF6MnHWX9HOdz5GjuYCl2YKl8ZOt4qezxqK63aK/9KPD+7DI3b/I17LM/MrL1MLY9NHa7OP
  @ s++bx/Pv8/f///////////////yH5BAEAAB8ALAAAAAAWABYAAAWG4CeOZGmeaKqubOum1SQ/
  @ kPVOW749BeVSus2CgrCxHptLBbOQxCSNCCaF1GUqwQbBd0JGJAyGJJiobE+LnCaDcXAaEoxhQ
  @ ACgNw0FQx9kP+wmaRgYFBQNeAoGihCAJQsCkJAKOhgXEw8BLQYciooHf5o7EA+kC40qBKkAAA
  @ Grpy+wsbKzIiEAOw==" />

  @ <img class="intLink" title="Paste"
  @ onclick="formatDoc('paste');"
  @ src="data:image/gif;base64,R0lGODlhFgAWAIQUAD04KTRLY2tXQF9vj414WZWIbXmOrp
  @ qbmpGjudClFaezxsa0cb/I1+3YitHa7PrkIPHvbuPs+/fvrvv8/f/////////////////////
  @ //////////////////////////yH5BAEAAB8ALAAAAAAWABYAAAWN4CeOZGmeaKqubGsusPvB
  @ SyFJjVDs6nJLB0khR4AkBCmfsCGBQAoCwjF5gwquVykSFbwZE+AwIBV0GhFog2EwIDchjwRiQ
  @ o9E2Fx4XD5R+B0DDAEnBXBhBhN2DgwDAQFjJYVhCQYRfgoIDGiQJAWTCQMRiwwMfgicnVcAAA
  @ MOaK+bLAOrtLUyt7i5uiUhADs=" />
#endif

  @ </div>
  @ <div id="wysiwygBox"
  @  style="resize:both; overflow:auto; width: %d(w)em; height: %d(h)em;"
  @  contenteditable="true">%s(zContent)</div>
  @ <script>
  @ var oDoc;
  @
  @ /* Initialize the document editor */
  @ function initDoc() {
  @   oDoc = document.getElementById("wysiwygBox");
  @   if (!isWysiwyg()) { setDocMode(true); }
  @ }
  @
  @ /* Return true if the document editor is in WYSIWYG mode.  Return
  @ ** false if it is in Markup mode */
  @ function isWysiwyg() {
  @   return document.getElementById("editMode").selectedIndex==0;
  @ }
  @
  @ /* Invoke this routine prior to submitting the HTML content back
  @ ** to the server */
  @ function wysiwygSubmit() {
  @   if(oDoc.style.whiteSpace=="pre-wrap"){setDocMode(0);}
  @   document.getElementById("wysiwygValue").value=oDoc.innerHTML;
  @ }
  @
  @ /* Run the editing command if in WYSIWYG mode */
  @ function formatDoc(sCmd, sValue) {
  @   if (isWysiwyg()){
  @     try {
  @       // First, try the W3C draft standard way, which has
  @       // been working on all non-IE browsers for a while.
  @       // It is also supported by IE11 and higher.
  @       document.execCommand("styleWithCSS", false, false);
  @     } catch (e) {
  @       try {
  @         // For IE9 or IE10, this should work.
  @         document.execCommand("useCSS", 0, true);
  @       } catch (e) {
  @         // Ok, that apparently did not work, do nothing.
  @       }
  @     }
  @     document.execCommand(sCmd, false, sValue);
  @     oDoc.focus();
  @   }
  @ }
  @
  @ /* Change the editing mode.  Convert to markup if the argument
  @ ** is true and wysiwyg if the argument is false. */
  @ function setDocMode(bToMarkup) {
  @   var oContent;
  @   if (bToMarkup) {
  @     /* WYSIWYG -> Markup */
  @     var linebreak = new RegExp("</p><p>","ig");
  @     oContent = document.createTextNode(
  @                  oDoc.innerHTML.replace(linebreak,"</p>\n\n<p>"));
  @     oDoc.innerHTML = "";
  @     oDoc.style.whiteSpace = "pre-wrap";
  @     oDoc.appendChild(oContent);
  @     document.getElementById("toolBar1").style.visibility="hidden";
  @     document.getElementById("toolBar2").style.visibility="hidden";
  @   } else {
  @     /* Markup -> WYSIWYG */
  @     if (document.all) {
  @       oDoc.innerHTML = oDoc.innerText;
  @     } else {
  @       oContent = document.createRange();
  @       oContent.selectNodeContents(oDoc.firstChild);
  @       oDoc.innerHTML = oContent.toString();
  @     }
  @     oDoc.style.whiteSpace = "normal";
  @     document.getElementById("toolBar1").style.visibility="visible";
  @     document.getElementById("toolBar2").style.visibility="visible";
  @   }
  @   oDoc.focus();
  @ }
  @ initDoc();
  @ </script>

}
