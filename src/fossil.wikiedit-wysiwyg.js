/**
   A slight adaptation of fossil's legacy wysiwyg wiki editor which
   makes it usable with the newer editor's edit widget replacement
   API.

   Requires: window.fossil, fossil.dom, and that the current page is
   /wikiedit. If called from another page it returns without effect.

   Caveat: this is an all-or-nothing solution. That is, once plugged
   in to /wikiedit, it cannot be removed without reloading the page.
   That is a limitation of the current editor-widget-swapping API.
*/
(function(F/*fossil object*/){
  'use strict';
  if(!F || !F.page || F.page.name!=='wikiedit') return;

  const D = F.dom;

  ////////////////////////////////////////////////////////////////////////
  // Install an app-specific stylesheet...
  (function(){
    const head = document.head || document.querySelector('head'),
          styleTag = document.createElement('style'),
          styleCSS = `
.intLink { cursor: pointer; }
img.intLink { border: 0; }
#wysiwyg-container {
  display: flex;
  flex-direction: column;
  max-width: 100% /* w/o this, toolbars don't wrap properly! */
}
#wysiwygBox {
  border: 1px solid rgba(127,127,127,0.3);
  border-radius: 0.25em;
  padding: 0.25em 1em;
  margin: 0;
  overflow: auto;
  min-height: 20em;
  resize: vertical;
}
#wysiwygEditMode { /* wrapper for radio buttons */
  border: 1px solid rgba(127,127,127,0.3);
  border-radius: 0.25em;
  padding: 0 0.35em 0 0.35em
}
#wysiwygEditMode > * {
  vertical-align: text-top;
}
#wysiwygEditMode label { cursor: pointer; }
#wysiwyg-toolbars {
  margin: 0 0 0.25em 0;
  display: flex;
  flex-wrap: wrap;
  flex-direction: column;
  align-items: flex-start;
}
#wysiwyg-toolbars > * {
  margin: 0 0.5em 0.25em 0;
}
#wysiwyg-toolBar1, #wysiwyg-toolBar2 {
  margin: 0 0.2em 0.2em 0;
  display: flex;
  flex-flow: row wrap;
}
#wysiwyg-toolBar1 > * { /* formatting buttons */
  vertical-align: middle;
  margin: 0 0.25em 0.25em 0;
}
#wysiwyg-toolBar2 > * { /* icons */
  border: 1px solid rgba(127,127,127,0.3);
  vertical-align: baseline;
  margin: 0.1em;
}
`;
    head.appendChild(styleTag);
    styleTag.type = 'text/css';
    D.append(styleTag, styleCSS);
  })();

  const outerContainer = D.attr(D.div(), 'id', 'wysiwyg-container'),
        toolbars = D.attr(D.div(), 'id', 'wysiwyg-toolbars'),
        toolbar1 = D.attr(D.div(), 'id', 'wysiwyg-toolBar1'),
        // ^^^ formatting options
        toolbar2 = D.attr(D.div(), 'id', 'wysiwyg-toolBar2')
        // ^^^^ action icon buttons
  ;
  D.append(outerContainer, D.append(toolbars, toolbar1, toolbar2));

  /** Returns a function which simplifies adding a list of options
      to the given select element. See below for example usage. */
  const addOptions = function(select){
    return function ff(value, label){
      D.option(select, value, label || value);
      return ff;
    };
  };

  ////////////////////////////////////////////////////////////////////////
  // Edit mode selection (radio buttons).
  const radio0 =
        D.attr(
          D.input('radio'),
          'name','wysiwyg-mode',
          'id', 'wysiwyg-mode-0',
          'value',0,
          'checked',true),
        radio1 = D.attr(
          D.input('radio'),
          'id','wysiwyg-mode-1',
          'name','wysiwyg-mode',
          'value',1),
        radios = D.append(
          D.attr(D.span(), 'id', 'wysiwygEditMode'),
          radio0, D.append(
            D.attr(D.label(), 'for', 'wysiwyg-mode-0'),
            "WYSIWYG"
          ),
          radio1, D.append(
            D.attr(D.label(), 'for', 'wysiwyg-mode-1'),
            "Raw HTML"
          )
        );
  D.append(toolbar1, radios);
  const radioHandler = function(){setDocMode(+this.value)};
  radio0.addEventListener('change',radioHandler, false);
  radio1.addEventListener('change',radioHandler, false);


  ////////////////////////////////////////////////////////////////////////
  // Text formatting options...
  var select;
  select = D.addClass(D.select(), 'format');
  select.dataset.format = "formatblock";
  D.append(toolbar1, select);
  addOptions(select)(
    '', '- formatting -')(
    "h1", "Title 1 <h1>")(
    "h2", "Title 2 <h2>")(
    "h3", "Title 3 <h3>")(
    "h4", "Title 4 <h4>")(
    "h5", "Title 5 <h5>")(
    "h6", "Subtitle <h6>")(
    "p", "Paragraph <p>")(
    "pre", "Preformatted <pre>");

  select = D.addClass(D.select(), 'format');
  select.dataset.format = "fontname";
  D.append(toolbar1, select);
  D.addClass(
    D.option(select, '', '- font -'),
    "heading"
  );
  addOptions(select)(
    'Arial')(
    'Arial Black')(
    'Courier New')(
    'Times New Roman');

  select = D.addClass(D.select(), 'format');
  D.append(toolbar1, select);
  select.dataset.format = "fontsize";
  D.addClass(
    D.option(select, '', '- size -'),
    "heading"
  );
  addOptions(select)(
    "1", "Very small")(
    "2", "A bit small")(
    "3", "Normal")(
    "4", "Medium-large")(
    "5", "Big")(
    "6", "Very big")(
    "7", "Maximum");

  select = D.addClass(D.select(), 'format');
  D.append(toolbar1, select);
  select.dataset.format = 'forecolor';
  D.addClass(
    D.option(select, '', '- color -'),
    "heading"
  );
  addOptions(select)(
    "red", "Red")(
    "blue", "Blue")(
    "green", "Green")(
    "black", "Black")(
    "grey", "Grey")(
    "yellow", "Yellow")(
    "cyan", "Cyan")(
    "magenta", "Magenta");


  ////////////////////////////////////////////////////////////////////////
  // Icon-based toolbar...
  /**
     Inject the icons...

     mkbuiltins strips anything which looks like a C++-style comment,
     even if it's in a string literal, and thus the runs of "/"
     characters in the DOM element data attributes have been mangled
     to work around that: we simply use \x2f for every 2nd slash.
  */
  (function f(title,format,src){
    const img = D.img();
    D.append(toolbar2, img);
    D.addClass(img, 'intLink');
    D.attr(img, 'title', title);
    img.dataset.format = format;
    D.attr(img, 'src', 'string'===typeof src ? src : src.join(''));
    return f;
  })(
    'Undo', 'undo',
    ["data:image/gif;base64,R0lGODlhFgAWAOMKADljwliE33mOrpGjuYKl8aezxqPD+7",
     "/I19DV3NHa7P/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "/\x2f/\x2f/\x2f/yH5BAEKAA8ALAAAAAAWABYAAARR8MlJq704680",
     "7TkaYeJJBnES4EeUJvIGapWYAC0CsocQ7SDlWJkAkCA6ToMYWIARGQF3mRQVIEjkkSVLIbSfE",
     "whdRIH4fh/DZMICe3/C4nBQBADs="]
  )(
    'Redo','redo',
    ["data:image/gif;base64,R0lGODlhFgAWAMIHAB1ChDljwl9vj1iE34Kl8aPD+7/I1/",
     "/\x2f/yH5BAEKAAcALAAAAAAWABYAAANKeLrc/jDKSesyphi7SiEgsVXZEATDICqBVJjpqWZt9Na",
     "EDNbQK1wCQsxlYnxMAImhyDoFAElJasRRvAZVRqqQXUy7Cgx4TC6bswkAOw=="]
  )(
    "Remove formatting",
    "removeFormat",
    ["data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAABYAAAAWCAYAAADEtGw7AA",
     "AABGdBTUEAALGPC/xhBQAAAAZiS0dEAP8A/wD/oL2nkwAAAAlwSFlzAAAOxAAADsQBlSsOGwA",
     "AAAd0SU1FB9oECQMCKPI8CIIAAAAIdEVYdENvbW1lbnQA9syWvwAAAuhJREFUOMtjYBgFxAB5",
     "01ZWBvVaL2nHnlmk6mXCJbF69zU+Hz/9fB5O1lx+bg45qhl8/fYr5it3XrP/YWTUvvvk3VeqG",
     "Xz70TvbJy8+Wv39+2/Hz19/mGwjZzuTYjALuoBv9jImaXHeyD3H7kU8fPj2ICML8z92dlbtMz",
     "deiG3fco7J08foH1kurkm3E9iw54YvKwuTuom+LPt/BgbWf3/\x2fsf37/1/c02cCG1lB8f/\x2ff95",
     "DZx74MTMzshhoSm6szrQ/a6Ir/Z2RkfEjBxuLYFpDiDi6Af/\x2f/2ckaHBp7+7wmavP5n76+P2C",
     "lrLIYl8H9W36auJCbCxM4szMTJac7Kza/\x2f/\x2fR3H1w2cfWAgafPbqs5g7D95++/P1B4+ECK8tA",
     "wMDw/1H7159+/7r7ZcvPz4fOHbzEwMDwx8GBgaGnNatfHZx8zqrJ+4VJBh5CQEGOySEua/v3n",
     "7hXmqI8WUGBgYGL3vVG7fuPK3i5GD9/fja7ZsMDAzMG/Ze52mZeSj4yu1XEq/ff7W5dvfVAS1",
     "lsXc4Db7z8C3r8p7Qjf/\x2f/2dnZGxlqJuyr3rPqQd/Hhyu7oSpYWScylDQsd3kzvnH738wMDzj",
     "5GBN1VIWW4c3KDon7VOvm7S3paB9u5qsU5/x5KUnlY+eexQbkLNsErK61+++VnAJcfkyMTIwf",
     "fj0QwZbJDKjcETs1Y8evyd48toz8y/ffzv/\x2fvPP4veffxpX77z6l5JewHPu8MqTDAwMDLzyrj",
     "b/mZm0JcT5Lj+89+Ybm6zz95oMh7s4XbygN3Sluq4Mj5K8iKMgP4f0/\x2f/\x2ffv77/\x2f8nLy+7MCc",
     "XmyYDAwODS9jM9tcvPypd35pne3ljdjvj26+H2dhYpuENikgfvQeXNmSl3tqepxXsqhXPyc66",
     "6s+fv1fMdKR3TK72zpix8nTc7bdfhfkEeVbC9KhbK/9iYWHiErbu6MWbY/7/\x2f8/4/\x2f9/pgOnH",
     "6jGVazvFDRtq2VgiBIZrUTIBgCk+ivHvuEKwAAAAABJRU5ErkJggg=="]
  )(
    "Bold",
    "bold",
    ["data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB",
     "YAQAInhI+pa+H9mJy0LhdgtrxzDG5WGFVk6aXqyk6Y9kXvKKNuLbb6zgMFADs="]
  )(
    "Italic",
    "italic",
    ["data:image/gif;base64,R0lGODlhFgAWAKEDAAAAAF9vj5WIbf/\x2f/yH5BAEAAAMALA",
     "AAAAAWABYAAAIjnI+py+0Po5x0gXvruEKHrF2BB1YiCWgbMFIYpsbyTNd2UwAAOw=="]
  )(
    "Underline",
    "underline",
    ["data:image/gif;base64,R0lGODlhFgAWAKECAAAAAF9vj/\x2f/\x2f/\x2f/\x2fyH5BAEAAAIALA",
     "AAAAAWABYAAAIrlI+py+0Po5zUgAsEzvEeL4Ea15EiJJ5PSqJmuwKBEKgxVuXWtun+DwxCCgA",
     "7"]
  )(
    "Left align",
    "justifyleft",
    ["data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB",
     "YAQAIghI+py+0Po5y02ouz3jL4D4JMGELkGYxo+qzl4nKyXAAAOw=="]
  )(
    "Center align",
    "justifycenter",
    ["data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB",
     "YAQAIfhI+py+0Po5y02ouz3jL4D4JOGI7kaZ5Bqn4sycVbAQA7"]
  )(
    "Right align",
    "justifyright",
    ["data:image/gif;base64,R0lGODlhFgAWAID/AMDAwAAAACH5BAEAAAAALAAAAAAWAB",
     "YAQAIghI+py+0Po5y02ouz3jL4D4JQGDLkGYxouqzl43JyVgAAOw=="]
  )(
    "Numbered list",
    "insertorderedlist",
    ["data:image/gif;base64,R0lGODlhFgAWAMIGAAAAADljwliE35GjuaezxtHa7P/\x2f/\x2f",
     "/\x2f/yH5BAEAAAcALAAAAAAWABYAAAM2eLrc/jDKSespwjoRFvggCBUBoTFBeq6QIAysQnRHaEO",
     "zyaZ07Lu9lUBnC0UGQU1K52s6n5oEADs="]
  )(
    "Dotted list",
    "insertunorderedlist",
    ["data:image/gif;base64,R0lGODlhFgAWAMIGAAAAAB1ChF9vj1iE33mOrqezxv/\x2f/\x2f",
     "/\x2f/yH5BAEAAAcALAAAAAAWABYAAAMyeLrc/jDKSesppNhGRlBAKIZRERBbqm6YtnbfMY7lud6",
     "4UwiuKnigGQliQuWOyKQykgAAOw=="]
  )(
    "Quote",
    "formatblock",
    ["data:image/gif;base64,R0lGODlhFgAWAIQXAC1NqjFRjkBgmT9nqUJnsk9xrFJ7u2",
     "R9qmKBt1iGzHmOrm6Sz4OXw3Odz4Cl2ZSnw6KxyqO306K63bG70bTB0rDI3bvI4P",
     "/\x2f/\x2f/\x2f/\x2f/",
     "/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "/\x2f/\x2f/\x2fyH5BAEKAB8ALAAAAAAWABYAAAVP4CeOZGmeaKqubEs2Cekk",
     "ErvEI1zZuOgYFlakECEZFi0GgTGKEBATFmJAVXweVOoKEQgABB9IQDCmrLpjETrQQlhHjINrT",
     "q/b7/i8fp8PAQA7"]
  )(
    "Delete indentation",
    "outdent",
    ["data:image/gif;base64,R0lGODlhFgAWAMIHAAAAADljwliE35GjuaezxtDV3NHa7P",
     "/\x2f/yH5BAEAAAcALAAAAAAWABYAAAM2eLrc/jDKCQG9F2i7u8agQgyK1z2EIBil+TWqEMxhMcz",
     "sYVJ3e4ahk+sFnAgtxSQDqWw6n5cEADs="]
  )(
    "Add indentation",
    "indent",
    ["data:image/gif;base64,R0lGODlhFgAWAOMIAAAAADljwl9vj1iE35GjuaezxtDV3N",
     "Ha7P/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "/\x2f/\x2f/yH5BAEAAAgALAAAAAAWABYAAAQ7EMlJq704650",
     "B/x8gemMpgugwHJNZXodKsO5oqUOgo5KhBwWESyMQsCRDHu9VOyk5TM9zSpFSr9gsJwIAOw=="
    ]
  )(
    "Hyperlink",
    "createlink",
    ["data:image/gif;base64,R0lGODlhFgAWAOMKAB1ChDRLY19vj3mOrpGjuaezxrCztb",
     "/I19Ha7Pv8/f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "/yH5BAEKAA8ALAAAAAAWABYAAARY8MlJq704682",
     "7/2BYIQVhHg9pEgVGIklyDEUBy/RlE4FQF4dCj2AQXAiJQDCWQCAEBwIioEMQBgSAFhDAGghG",
     "i9XgHAhMNoSZgJkJei33UESv2+/4vD4TAQA7"]
  )(
    "Cut",
    "cut",
    ["data:image/gif;base64,R0lGODlhFgAWAIQSAB1ChBFNsRJTySJYwjljwkxwl19vj1",
     "dusYODhl6MnHmOrpqbmpGjuaezxrCztcDCxL/I18rL1P/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/",
     "/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "yH5BAEAAB8ALAAAAAAWABYAAAVu4CeOZGmeaKqubDs6TNnE",
     "bGNApNG0kbGMi5trwcA9GArXh+FAfBAw5UexUDAQESkRsfhJPwaH4YsEGAAJGisRGAQY7UCC9",
     "ZAXBB+74LGCRxIEHwAHdWooDgGJcwpxDisQBQRjIgkDCVlfmZqbmiEAOw=="]
  )(
    "Copy",
    "copy",
    ["data:image/gif;base64,R0lGODlhFgAWAIQcAB1ChBFNsTRLYyJYwjljwl9vj1iE31",
     "iGzF6MnHWX9HOdz5GjuYCl2YKl8ZOt4qezxqK63aK/9KPD+7DI3b/I17LM/MrL1MLY9NHa7OP",
     "s++bx/Pv8/f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "/yH5BAEAAB8ALAAAAAAWABYAAAWG4CeOZGmeaKqubOum1SQ/",
     "kPVOW749BeVSus2CgrCxHptLBbOQxCSNCCaF1GUqwQbBd0JGJAyGJJiobE+LnCaDcXAaEoxhQ",
     "ACgNw0FQx9kP+wmaRgYFBQNeAoGihCAJQsCkJAKOhgXEw8BLQYciooHf5o7EA+kC40qBKkAAA",
     "Grpy+wsbKzIiEAOw=="]
  )(
    /* Paste, when activated via JS, has no effect in some (maybe all)
       environments. Activated externally, e.g. keyboard, it works. */
    "Paste (does not work in all environments)",
    "paste",
    ["data:image/gif;base64,R0lGODlhFgAWAIQUAD04KTRLY2tXQF9vj414WZWIbXmOrp",
     "qbmpGjudClFaezxsa0cb/I1+3YitHa7PrkIPHvbuPs+/fvrvv8/f/\x2f/\x2f/\x2f",
     "/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/",
     "/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f/\x2f",
     "yH5BAEAAB8ALAAAAAAWABYAAAWN4CeOZGmeaKqubGsusPvB",
     "SyFJjVDs6nJLB0khR4AkBCmfsCGBQAoCwjF5gwquVykSFbwZE+AwIBV0GhFog2EwIDchjwRiQ",
     "o9E2Fx4XD5R+B0DDAEnBXBhBhN2DgwDAQFjJYVhCQYRfgoIDGiQJAWTCQMRiwwMfgicnVcAAA",
     "MOaK+bLAOrtLUyt7i5uiUhADs="]
  );

  ////////////////////////////////////////////////////////////////////////
  // The main editor area...
  const oDoc = D.attr(D.div(), 'id', "wysiwygBox");
  D.attr(oDoc, 'contenteditable', 'true');
  D.append(outerContainer, oDoc);
  
  /* Initialize the document editor */
  function initDoc() {
    initEventHandlers();
    if (!isWysiwyg()) { setDocMode(true); }
  }

  function initEventHandlers() {
    //console.debug("initEventHandlers()");
    const handleDropDown = function() {
      formatDoc(this.dataset.format,this[this.selectedIndex].value);
      this.selectedIndex = 0;
    };
    
    const handleFormatButton = function() {
      var extra;
      switch (this.dataset.format) {
      case 'createlink':
        const sLnk = prompt('Target URL:','');
        if(sLnk) extra = sLnk;
        break;
      case 'formatblock':
        extra = 'blockquote';
        break;
      }
      formatDoc(this.dataset.format, extra);
    };

    var i, controls = outerContainer.querySelectorAll('select.format');
    for(i = 0; i < controls.length; i++) {
      controls[i].addEventListener('change', handleDropDown, false);;
    }
    controls = outerContainer.querySelectorAll('.intLink');
    for(i = 0; i < controls.length; i++) {
      controls[i].addEventListener('click', handleFormatButton, false);
    }
  }

  /* Return true if the document editor is in WYSIWYG mode.  Return
  ** false if it is in Markup mode */
  function isWysiwyg() {
    return radio0.checked;
  }

  /* Run the editing command if in WYSIWYG mode */
  function formatDoc(sCmd, sValue) {
    if (isWysiwyg()){
      try {
        // First, try the W3C draft standard way, which has
        // been working on all non-IE browsers for a while.
        // It is also supported by IE11 and higher.
        document.execCommand("styleWithCSS", false, false);
      } catch (e) {
        try {
          // For IE9 or IE10, this should work.
          document.execCommand("useCSS", 0, true);
        } catch (e) {
          // OK, that apparently did not work, do nothing.
        }
      }
      document.execCommand(sCmd, false, sValue);
      oDoc.focus();
    }
  }

  /* Change the editing mode.  Convert to markup if the argument
  ** is true and wysiwyg if the argument is false. */
  function setDocMode(bToMarkup, content) {
    if(undefined===content){
      content = bToMarkup ? oDoc.innerHTML : oDoc.innerText;
    }
    if(!setDocMode.linebreak){
      setDocMode.linebreak = new RegExp("</p><p>","ig");
    }
    if(!setDocMode.toHide){
      setDocMode.toHide = toolbars.querySelectorAll(
        '#wysiwyg-toolBar1 > *:not(#wysiwygEditMode), '
          +'#wysiwyg-toolBar2');
    }
    if (bToMarkup) {
      /* WYSIWYG -> Markup */
      // Legacy did this: content=content.replace(setDocMode.linebreak,"</p>\n\n<p>")
      D.append(D.clearElement(oDoc), content)
      oDoc.style.whiteSpace = "pre-wrap";
      D.addClass(setDocMode.toHide, 'hidden');
    } else {
      /* Markup -> WYSIWYG */
      D.parseHtml(D.clearElement(oDoc), content);
      oDoc.style.whiteSpace = "normal";
      D.removeClass(setDocMode.toHide, 'hidden');
    }
    oDoc.focus();
  }

  ////////////////////////////////////////////////////////////////////////
  // A hook which can be activated via a site skin to plug this editor
  // in to the wikiedit page.
  F.page.wysiwyg = {
    // only for debugging: oDoc: oDoc,
    /*
      Replaces wikiedit's default editor widget with this wysiwyg
      editor.

      Must either be called via an onPageLoad handler via the site
      skin's footer or else it can be called manually from the dev
      tools console. Calling it too early (e.g. in the page footer
      outside of an an onPageLoad handler) will crash because wikiedit
      has not been initialized.
    */
    init: function(){
      initDoc();
      const content = F.page.wikiContent() || '';
      var isDirty = false /* keep from stashing too often */;
      F.page.setContentMethods(
        function(){
          const rc = isWysiwyg() ? oDoc.innerHTML : oDoc.innerText;
          return rc;
        },
        function(content){
          isDirty = false;
          setDocMode(radio0.checked ? 0 : 1, content);
        }
      );
      oDoc.addEventListener('blur', function(){
        if(isDirty) F.page.notifyOfChange();
      }, false);
      oDoc.addEventListener('input', function(){isDirty = true}, false);
      F.page.wikiContent(content)/*feed it back in to our widget*/;
      F.page.replaceEditorElement(outerContainer);
      F.message("Replaced wiki editor widget with legacy wysiwyg editor.");
    }
  };
})(window.fossil);
