# The Default Content Security Policy (CSP)

One of the most important things you have to know about the default
[Fossil-provided `<head>` text](./customskin.md#headfoot) is the
[Content Security Policy][csp] (CSP) it applies to your repository’s web
interface. The current version applies the following restrictions:


## default-src 'self' data

This policy means mixed-origin content isn’t allowed, so you can’t refer to
resources on other web domains, so the following Markdown for an inline
image hosted on another site will cause a CSP error:

         ![fancy 3D Fossil logotype](https://i.imgur.com/HalpMgt.png)

This policy allows inline `data:` URIs, which means you could
[data-encode][de] your image content and put it inline within the
document:

         ![small inline image](data:image/gif;base64,R0lGODlh...)

That method is best used for fairly small resources. Large `data:` URIs
are hard to read and edit. Keep in mind that if you put such a thing
into a Fossil forum post, anyone subscribed to email alerts will get a
copy of the raw URI text, which is really ugly.

For larger files, you could instead store the file in Fossil as:

*   **versioned content** retrieved via a [`/raw`](/help?cmd=/raw) URL
*   **[unversioned content](./unvers.wiki)** retrieved
    via a [`/uv`](/help?cmd=/uv) URL

Another path around this restriction is to [serve your
repo](./server.wiki) behind an HTTP proxy server, allowing mixed-mode
content serving, with static images and such served directly by the HTTP
server and the dynamic content by Fossil. That allows a URI scheme that
prevents the browser’s CSP enforcement from distinguishing content from
Fossil proper and that from the front-end proxy.


## style-src 'self' 'unsafe-inline'

This policy means CSS files can only come from the Fossil server or via
a front-end proxy as in the inline image workarounds above. It also says
that inline CSS is disallowed; this will give a CSP error:

        <p style="margin-left: 4em">Some bit of indented text</p>

In practice, this means you must put your CSS into [the “CSS” section of
a custom skin](./customskin.md), not inline within Markdown, Wiki, or
HTML tags. You can refer to specific tags in the document through “`id`”
and “`class`” attributes.

The reason for this restriction might not be obvious, but the risks boil
down to this: CSS is sufficiently powerful that if someone can apply
their CSS to your site, they can make it say things you don’t want it to
say, hide important information, and more. Thus, we restrict all CSS to
come from trusted channels only.

We do currently trust CSS checked into the repository as a file, but
that stance might be overly-trusting, so we might revoke it later, as we
do for JavaScript:


## script-src 'self' 'nonce-%s'

This policy means HTML `<script>` tags are only allowed to be emitted
into the output HTML by Fossil C or TH1 code, because only code running
in those contexts can correctly apply the random “nonce” attribute to
the tag that matches the one declared in the CSP, which changes on each
HTTP hit Fossil handles.

This means the workarounds given above will not work for JavaScript. In
effect, the only JavaScript that Fossil can serve is that which it
directly provided, such as that for the CSS section of the skin and that
behind the default [hamburger menu](./customskin.md#menu).

We’re so restrictive about how we treat JavaScript because it can lead
to [difficult-to-avoid cross-site scripting attacks][xssci].

[csp]:   https://developer.mozilla.org/en-US/docs/Web/HTTP/CSP
[de]:    https://dopiaza.org/tools/datauri/index.php
[xssci]: https://fossil-scm.org/forum/forumpost/e7c386b21f



