# The Default Content Security Policy (CSP)

When Fossil’s web interface generates an HTML page, it
normally includes a [Content Security Policy][csp] (CSP)
which applies to all content on that page. The CSP tells the browser
which types of content (HTML, image, CSS, JavaScript...) the repository
is expected to serve, and thus which types of content a compliant
browser is allowed to pay attention to. All of the major browsers
enforce CSP restrictions.

A CSP is an important security measure on any site where some of the
content it serves is user-provided. It defines a “white list” of content
types believed to be safe and desirable, so that if any content outside
of that definition gets inserted into the repository later, the browser
will treat that unwanted content as if it was not part of the document
at all.

A CSP is a fairly blunt tool. It cannot tell good and visually appealing
enhancements from vandalism or outright attacks. All it does is tell the
browser what types and sources for page content were anticipated by the
site’s creators. The browser treats everything else as “unwanted” even
if that’s not actually true. When the CSP prevents the browser from
displaying wanted content, you then have to understand the current rules
and what they’re trying to accomplish to decide on [an appropriate
workaround](#override).


## The Default Restrictions

The Fossil default CSP declares the following content restrictions:


### <a name="base"></a> default-src 'self' data

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
repo](./server/) behind an HTTP proxy server, allowing mixed-mode
content serving, with static images and such served directly by the HTTP
server and the dynamic content by Fossil. That allows a URI scheme that
prevents the browser’s CSP enforcement from distinguishing content from
Fossil proper and that from the front-end proxy.


### <a name="style"></a> style-src 'self' 'unsafe-inline'

This policy means CSS files can only come from the Fossil server or via
a front-end proxy as in the inline image workarounds above. It also says
that inline CSS is disallowed; this will give a CSP error:

        <p style="margin-left: 4em">Some bit of indented text</p>

In practice, this means you must put your CSS into [the “CSS” section of
a custom skin][cs], not inline within Markdown, Wiki, or
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


### <a name="script"></a> script-src 'self' 'nonce-%s'

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



## <a name="override"></a>Replacing the Default CSP

If you wish to relax the default CSP’s restrictions or to tighten them
further, there are two ways to accomplish that:


### <a name="th1"></a>TH1 Setup Hook

The stock CSP text is hard-coded in the Fossil C source code, but it’s
only used to set the default value of one of [the TH1 skinning
variables](./customskin.md#vars), `$default_csp`. That means you can
override the default CSP by giving this variable a value before Fossil
sees that it’s undefined and uses this default.

The best place to do that is from the [`th1-setup`
script](./th1-hooks.md), which runs before TH1 processing happens during
skin processing:

        $ fossil set th1-setup "set default_csp {default-src: 'self'}"

This is the cleanest method, allowing you to set a custom CSP without
recompiling Fossil or providing a hand-written `<head>` section in the
Header section of a custom skin.

You can’t remove the CSP entirely with this method, but you can get the
same effect by telling the browser there are no content restrictions:

        $ fossil set th1-setup 'set default_csp {default-src: *}'


### <a name="header"></a>Custom Skin Header

Fossil only inserts a CSP into the HTML pages it generates when the
[skin’s Header section](./customskin.md#headfoot) doesn’t contain a
`<head>` tag. None of the stock skins include a `<head>` tag,¹ so if you
haven’t [created a custom skin][cs], you should be getting Fossil’s
default CSP.

We say “should” because long-time Fossil users may be hanging onto a
legacy behavior from before Fossil 2.5, when Fossil added this automatic
`<head>` insertion feature. Repositories created before that release
where the admin either defined a custom skin *or chose one of the stock
skins* (!) will effectively override this automatic HTML `<head>`
insertion feature because the skins from before that time did include
these elements. Unless the admin for such a repository updated the skin
to track this switch to automatic `<head>` insertion, the default CSP
added to the generated header text in Fossil 2.7 is probably being
overridden by the skin.

If you want the protection of the default CSP in your custom skin, the
simplest method is to leave the `<html><head>...` elements out of the
skin’s Header section, starting it with the `<div class="head">` element
instead as described in the custom skinning guide. Alternately, you can
[make use of `$default_csp`](#th1).

This then tells you one way to override Fossil’s default CSP: provide
your own HTML header in a custom skin.

A useful combination is to entirely override the default CSP in the skin
but then provide a new CSP [in the front-end proxy layer](./server/)
using any of the many reverse proxy servers that can define custom HTTP
headers.


------------


**Asides and Digressions:**

1.  The stock Bootstrap skin does actually include a `<head>` tag, but
    from Fossil 2.7 through Fossil 2.9, it just repeated the same CSP
    text that Fossil’s C code inserts into the HTML header for all other
    stock skins. With Fossil 2.10, the stock Bootstrap skin uses
    `$default_csp` instead, so you can [override it as above](#th1).


[cs]:    ./customskin.md
[csp]:   https://developer.mozilla.org/en-US/docs/Web/HTTP/CSP
[de]:    https://dopiaza.org/tools/datauri/index.php
[xssci]: https://fossil-scm.org/forum/forumpost/e7c386b21f
