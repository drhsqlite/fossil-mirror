# The fileedit Page

This document describes some tips and tricks for the [](/fileedit)
page, which provides, for users with [checkin
privileges](./caps/index.md), basic editing features for files via the
web interface.

# Important Caveats and Disclaimers

Predictably, the ability to edit files in a repository from a web
browser halfway around the world comes with several obligatory caveats
and disclaimers...

### `/fileedit` Does *Nothing* by Default.

In order to "activate" it, a user with [the "setup"
permission](./caps/index.md) must set the
[fileedit-glob](/help?cmd=fileedit-glob) repository setting to a
comma- or newline-delimited list of globs representing a whitelist of
files which may be edited online. Any user with commit access may then
edit files matching one of those globs.

### `/fileedit` **Works by Creating Commits**

Thus any edits made via that page become a normal part of the
repository's blockchain.

### `/fileedit` is **Intended for use with Embedded Docs**

... and similar text files, and is most certainly
**not intended for editing code**.

Editing files with unusual syntax requirements, e.g. hard tabs in
makefiles, may break them. *You Have Been Warned.*

Similarly, though every effort is made to retain the end-of-line
style used by being-edited files, the round-trip through an HTML
textarea element may change the EOLs. The Commit section of the page
offers three different options for how to treat newlines when saving
changes. **Files with mixed EOL styles** *will be normalized to a single
EOL style* when modified using `/fileedit`. When "inheriting" the EOL
style from a previous version which has mixed styles, the first EOL
style detected in the file is used.

### `/fileedit` **is Not a Replacement for a Checkout**

A full-featured checkout allows far more possibilities than this basic
online editor permits, and the feature scope of `/fileedit` is
intentionally kept small, implementing only the bare necessities
needed for performing basic edits online. It *is not, and will never
be, a replacement for a checkout.*

It is to be expected that users will want to do "more" with this
page, and we generally encourage feature requests, but be aware that
certain types of ostensibly sensible feature requests *will be
rejected* for `/fileedit`. These include, but are not limited to:

- Features which are already provided by other pages, e.g.
the ability to create a new named branch or add tags.
- Features which would require re-implementing significant
capabilities provided only within a checkout (e.g. merging files).
- The ability to edit/manipulate files which are in a local
checkout. (If you have a checkout, use your local editor, not
`/fileedit`.)
- Editing of non-text files, e.g. images. Use a checkout and your
preferred graphics editor.
- Support for syncing/pulling/pushing of a repository before and/or
after edits. Those features cannot be *reliably* provided via a web
interface for several reasons, not the least of which is that some
backend servers simply do not permit outbound network connections to
arbitrary hosts.

Similarly, some *potential* features have significant downsides,
abuses, and/or implementation hurdles which make the decision of
whether or not to implement them subject to notable contributor
debate. e.g. the ability to add new files or remove/rename older
files.


### `/fileedit` **Does Not Store Draft Versions While Working**

When using `/fileedit`, if the browser's tab is closed, a link is
clicked, or the user otherwise leaves the page, any current edits
*will be lost*. See the note above about this feature *not* being a
replacement for a full-fledged checkout.

### The Power is Yours, but...

> "With great power comes great responsibility."

**Use this feature judiciously, *if at all*.**

Now, with those warnings and caveats out of the way...

-----

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
  by Fossil (by its file extension).

The event listener callback shown above doesn't use the `mimetype`,
but makes used of the other two. It fishes all `code` blocks out of
the preview which explicitly have a CSS class named
`language-`something, and then asks highlightjs to highlight them.

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
