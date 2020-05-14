# The fileedit Page

This document describes some tips and tricks for the [](/fileedit)
page, which provides basic editing features for files via the web
interface.

# First and Foremost...

Notes, caveats, and disclaimers:

- **`/fileedit` does nothing by default.** In order to "activate" it,
  a user with [the "setup" permission](./caps/index.md) must set the
  [fileedit-glob](/help?cmd=fileedit-glob) repository setting to a
  comma- or newline-delimited list of globs representing a whitelist
  of files which may be edited online. Any user with commit access may
  then edit files matching one of those globs.
- `/fileedit` **works by creating commits** (a.k.a. checkins), thus
  any edits made via that page become a normal part of the repository's
  blockchain.
- `/fileedit` is **intended to facilitate online edits of
  embedded docs and similar text files**, and is most certainly
  **not intended for editing code**. Editing files with unusual
  syntax requirements, e.g. hard tabs in makefiles, may break
  them. *You Have Been Warned.*
    - Similarly, though every effort is made to retain the end-of-line
    style used by being-edited files, the round-trip through an HTML
    textarea element may change the EOLs. The Commit section of the
    page offers 3 different options for how to treat newlines when
    saving changes.
- `/fileedit` **is not a replacement for a checkout**. A full-featured
  checkout allows far more possibilities than this basic online editor
  permits, and the feature scope of `/fileedit` is intentionally kept
  small, implementing only the bare necessities needed for performing
  basic edits online.
- `/fileedit` **does not store draft versions while working**. i.e. if
  the browser's tab is closed or a link is clicked, taking the user to
  a different page, any current edits *will be lost*. See the note
  above about this feature *not* being a replacement for a
  full-fledged checkout.
- "With great power comes great responsibility." **Use this feature
  judiciously, if at all.**


# Tips and Tricks

## `fossil` Global-scope JS Object

`/fileedit` is largely implemented in JavaScript, and makes heavy use
of the global-scope `fossil` object, which provides
infrastructure-level features intended for use by Fossil UI pages.
(That said, that infrastructure was introduced with `/fileedit`, and
most pages do not use it.)

The `fossil.page` object represents the UI's current page (on pages
which make use of this API - most do not). That object supports
listening to page-specific events so that JS code installed via
[client-side edits to the site skin's footer](customskin.md) may react
to those changes somehow. The next section describes one such use for
such events...

## Integrating Syntax Highlighting

Assuming a repository has integrated a 3rd-party syntax highlighting
solution, it can probably (depending on its API) be told how to
highlight `/fileedit`'s wiki/markdown-format previews. Here are
instructions for doing so with highlightjs:

At the very bottom of the [site skin's footer](customskin.md), add a
script tag similar to the following:

```javascript
<script nonce="$<nonce>">
if(fossil && fossil.page && fossil.page.name==='fileedit'){
  fossil.page.addEventListener(
    'fileedit-preview-updated',
    (ev)=>{
     if(ev.detail.previewMode==='wiki'){
       ev.detail.element.querySelectorAll(
         'code[class^=language-]'
        ).forEach((e)=>hljs.highlightBlock(e));
     }
    }
  );
}
</script>
```

Note that the `nonce="$<nonce>"` part is intended to be entered
literally as shown above. It will be expanded to contain the current
request's nonce value when the page is rendered.

The first line of the script just ensures that the expected JS-level
infrastructure is loaded. It's only loaded in the `/fileedit` page and
possibly pages added or "upgraded" since `/fileedit`'s introduction.

The part in the `if` block adds an event listener to the `/filepage`
app which gets called when the preview is refreshed. That event
contains 3 properties:

- `previewMode`: a string describing the current preview mode: `wiki`
  (which includes Fossil-native wiki and markdown), `text`,
  `htmlInline`, `htmlIframe`. We should "probably" only highlight wiki
  text, and thus the example above limits its work to that type of
  preview. It won't work with `htmlIframe`, as that represents an
  iframe element which contains a complete HTML document.
- `element`: the DOM element in which the preview is rendered.
- `mimetype`: the mimetype of the being-previewed content, as determined
  by fossil (by its file extension).

The event listener callback doesn't use the `mimetype`, but makes used
of the other two. It fishes all `code` blocks out of the preview which
explicitly have a CSS class named `language-`something, and then asks
highlightjs to highlight them.

## Integrating a Custom Editor Widget

*Hypothetically*, though this is currently unproven "in the wild," it
is possible to replace `/filepage`'s basic text-editing widget (a
`textarea` element) with a fancy 3rd-party editor widget by doing the
following:

First, replace the `fossil.page.value()` method with a custom
implementation which can get and set the being-edited text from/to the
custom editor widget:

```
fossil.page.value = function(){
  if(0===arguments.length){//call as a "getter"
    return the text-form content of your custom widget
  }
  else{// called as a setter
    set the content of your custom widget to arguments[0]
    and then:
    return this; // required by the interface!
  }
};
```

Secondly, inject the custom editor widget into the UI, replacing
the default editor widget:

```javascript
fossil.page.replaceEditorWidget(yourNewWidgetElement);
```

That method must be passed a DOM element and may only be called once:
it *removes itself* the first time it is called.

That "should" be all there is to it. When `fossil.page` needs to get
the being-edited content, it will call `fossil.page.value()` with no
arguments, and when it sets the content (immediately after (re)loading
a file), it will pass that content to `fossil.page.value()`.
