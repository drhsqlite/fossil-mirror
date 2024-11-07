# The fileedit Page

This document describes the limitations of, caveats for, and
disclaimers for the [](/fileedit) page, which provides users with
 basic editing features for files via the web interface when they
 have [checkin privileges](./caps/index.md).

# Important Caveats and Disclaimers

Predictably, the ability to edit files in a repository from a web
browser halfway around the world comes with several obligatory caveats
and disclaimers...

## <a id="cap"></a> `/fileedit` Does *Nothing* by Default.

In order to "activate" it, a user with [the "setup"
permission](./caps/index.md) must set the
[fileedit-glob](/help?cmd=fileedit-glob) repository setting to a
comma- or newline-delimited list of globs representing a whitelist of
files which may be edited online. Any user with commit access may then
edit files matching one of those globs. Certain pages within the UI
get an "edit" link added to them when the current user's permissions
and the whitelist both permit editing of that file.

## <a id="csrf"></a> CSRF & HTTP Referrer Headers

In order to protect against [Cross-site Request Forgery (CSRF)][csrf]
attacks, Fossil UI features which write to the database require that
the browser send the so-called [HTTP `Referer` header][referer]
(noting that the misspelling of "referrer" is a historical accident
which has long-since been standardized!). Modern browsers, by default,
include such information automatically for *interactive* actions which
lead to a request, e.g. clicking on a link back to the same
server. However, `/fileedit` uses asynchronous ["XHR"][xhr]
connections, which browsers *may* treat differently than strictly
interactive elements.

- **Firefox**: configuration option `network.http.sendRefererHeader`
  specifies whether the `Referer` header is sent. It must have a value
  of 2 (which is the default) for XHR requests to get the `Referer`
  header. Purely interactive Fossil features, in which users directly
  activate links or forms, work with a level of 1 or higher.
- **Chrome**: apparently requires an add-on in order to change this
  policy, so Chrome without such an add-on will not suppress this
  header.
- **Safari**: ???
- **Other browsers**: ???

If `/fileedit` shows an error message saying "CSRF violation," the
problem is that the browser is not sending a `Referer` header to XHR
connections. Fossil does not offer a way to disable its CSRF
protections.

[referer]: https://en.wikipedia.org/wiki/HTTP_referer
[csrf]: https://en.wikipedia.org/wiki/Cross-site_request_forgery
[xhr]: https://en.wikipedia.org/wiki/XMLHttpRequest

## <a id="commit"></a> `/fileedit` **Works by Creating Commits**

Thus any edits made via that page become a normal part of the
repository.

## <a id="intent"></a> `/fileedit` is *Intended* for use with Embedded Docs

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
style detected in the previous version of the file is used.

## <a id="checkout"></a> `/fileedit` **is Not a Replacement for a Checkout**

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
interface for several reasons.

Similarly, some *potential* features have significant downsides,
abuses, and/or implementation hurdles which make the decision of
whether or not to implement them subject to notable contributor
debate. e.g. the ability to add new files or remove/rename older
files.


## <a id="storage"></a> `/fileedit` **Stores Only Limited Local Edits While Working**

When changes are made to a given checkin/file combination,
`/fileedit` will, if possible, store them in [`window.localStorage`
or `window.sessionStorage`][html5storage], if available, but...

- Which storage is used is unspecified and may differ across
  environments.
- If neither of those is available, the storage is transient and
  will not survive a page reload. In this case, the UI issues a clear
  warning in the editor tab.
- It stores only the most recent checkin/file combinations which have
  been modified (exactly how many may differ - the number will be
  noted somewhere in the UI). Note that changing the "executable bit"
  is counted as a modification, but the checkin *comment* is *not*
  and is reset after a commit.
- If its internal limit on the number of modified files is exceeded,
  it silently discards the oldest edits to keep the list at its limit.

Edits are saved whenever the editor component fires its "change"
event, which essentially means as soon as it loses input focus. Thus
to force the browser to save any pending changes, simply click
somwhere on the page outside of the editor.

Exactly how long `localStorage` will survive, and how much it or
`sessionStorage` can hold, is environment-dependent. `sessionStorage`
will survive until the current browser tab is closed, but it survives
across reloads of the same tab.

If `/fileedit` determines that no persistent storage is available a
warning is displayed on the editor page.

[html5storage]: https://developer.mozilla.org/en-US/docs/Web/API/Web_Storage_API

## <a id="power"></a> The Power is Yours, but...

> "With great power comes great responsibility."

**Use this feature judiciously, *if at all*.**

Now, with those warnings and caveats out of the way...

-----

# <a id="tips"></a> Tips and Tricks

## <a id="global-js"></a> `fossil` Global-scope JS Object

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

## <a id="syn-hl"></a> Integrating Syntax Highlighting

Assuming a repository has integrated a 3rd-party syntax highlighting
solution, it can probably (depending on its API) be told how to
highlight `/fileedit`'s wiki/markdown-format previews. Here are
instructions for doing so with [highlightjs](https://highlightjs.org/):

At the very bottom of the [site skin's footer](customskin.md), add a
script tag similar to the following:

```javascript
<script nonce="$<nonce>">
if(window.fossil && fossil.page && fossil.page.name==='fileedit'){
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

The part in the `if` block adds an event listener to the `/fileedit`
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
but makes use of the other two. It fishes all `code` blocks out of
the preview which explicitly have a CSS class named
`language-`something, and then asks highlightjs to highlight them.

## <a id="editor"></a> Integrating a Custom Editor Widget

(These instructions also work for the `/wikiedit` page by replacing
"fileedit" with "wikiedit" in any strings or symbol names!)

It is possible to replace `/fileedit`'s basic text-editing widget (a
`textarea` element) with a fancy 3rd-party editor widget by following
these instructions...

All JavaScript code which follows is assumed to be in a script tag
similar to the one shown in the previous section:

```javascript
<script nonce="$<nonce>">
if(window.fossil && fossil.page && fossil.page.name==='fileedit'){
  // code specific to the fileedit page goes here
}
</script>
```

First, install proxy functions so that `fossil.page.fileContent()`
can get and set your content:

```
fossil.page.setContentMethods(
  function(){ return text-form content of your widget },
  function(content){ set text-form content of your widget }
};
```

Secondly, we need to alert the editor app when there are changes so
that it can do things like store edits locally so that they are not
lost on a page reload. How that is done is completely dependent on the
3rd-party editor widget, but it generically looks something like:

```
myCustomWidget.on('eventName', ()=>fossil.page.notifyOfChange());
```

Lastly, if the 3rd-party editor does *not* hide or remove the native
editor widget, and does not inject itself into the DOM on the caller's
behalf, we can replace the native widget with the 3rd-party one with:

```javascript
fossil.page.replaceEditorWidget(yourNewWidgetElement);
```

That method must be passed a DOM element and may only be called once:
it *removes itself* the first time it is called.

That should be all there is to it. When `fossil.page` needs to get the
being-edited content, it will call the installed content-getter
function with no arguments, and when it sets the content (immediately
after (re)loading a file or grabbing local edits), it will pass that
content to the installed content-setter method. Those, in turn will
trigger the installed proxies and fire any relevant events.

Below is an example of Fossil skin footer content which plugs in the
TinyMCE HTML editor into the `/wikiedit` page, but the process is
identical for `/fileedit` (noting that `/fileedit` may need to be able
to edit multiple types of files for which a special-purpose editor
like TinyMCE may not be suitable). Note that any paths to CSS and JS
resources of course need to be modified to suit one's own
installation.

```
<!-- TinyMCE CSS and JS: -->
<link href="$<home>/doc/ckout/skin.min.css" rel="stylesheet" type="text/css">
<link href="$<home>/doc/ckout/content.min.css" rel="stylesheet" type="text/css">
<script src='$<home>/doc/ckout/tinymce.min.js'></script>
<script src='$<home>/doc/ckout/theme.min.js'></script>
<script src='$<home>/doc/ckout/icons.min.js'></script>
<!-- Integrate TinyMCE into /wikiedit: -->
<script nonce="$<nonce>">
if(window.fossil && window.fossil.page.name==='wikiedit'){
  window.fossil.onPageLoad( function(){
    const elemId = 'wikiedit-content-editor';
    tinymce.init({selector: 'textarea#'+elemId});
    const widget = tinymce.get(elemId);
    fossil.page.setContentMethods(
      function(){return widget.getContent()},
      function(content){widget.setContent(content)}
    );
    widget.on('change', function(){
      if(widget.isDirty()) fossil.page.notifyOfChange();
    });
  });
}
</script>
```
