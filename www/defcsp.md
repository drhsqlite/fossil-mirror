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
in those contexts can insert the correct “nonce” tag attribute, matching
the one declared in the CSP.¹ Since the nonce is a very large random
number that changes on each HTTP hit Fossil handles, it is effectively
unguessable, which prevents attackers from inserting `<script>` tags
statically.

This means the workarounds given above will not work for JavaScript.
Under this policy, the only JavaScript that Fossil can serve is that
which it directly provided.

Users with the all-powerful Setup capability can insert arbitrary
JavaScript by [defining a custom skin][cs], adding it to the skin’s
“JavaScript” section, which has the random nonce automatically inserted
by Fossil when it serves the page. This is how the JS backing the
default skin’s [hamburger menu](./customskin.md#menu) works.

We’re so restrictive about how we treat JavaScript because it can lead
to difficult-to-avoid scripting attacks. If we used the same CSP for
`<script>` tags [as for `<style>` tags](#style), anyone with check-in
rights on your repository could add a JavaScript file to your repository
and then refer to it from other content added to the site.  Since
JavaScript code can access any data from any URI served under its same
Internet domain, and many Fossil users host multiple Fossil repositories
under a single Internet domain, such a CSP would only be safe if all of
those repositories are trusted equally.

Consider [the Chisel hosting service](http://chiselapp.com/), which
offers free Fossil repository hosting to anyone on the Internet, all
served under the same `http://chiselapp.com/user/$NAME/$REPO` URL
scheme. Any one of those hundreds of repositories could trick you into
visiting their repository home page, set to [an HTML-formatted embedded
doc page][hfed] via Admin → Configuration → Index&nbsp;Page, with this
content:

         <script src="/doc/trunk/bad.js"></script>

That script can then do anything allowed in JavaScript to *any other*
Chisel repository your browser can access.The possibilities for mischief
are *vast*. For just one example, if you have login cookies on four
different Chisel repositories, your attacker could harvest the login
cookies for all of them through this path if we allowed Fossil to serve
JavaScript files under the same CSP policy as we do for CSS files.

This is why the default configuration of Fossil has no way for [embedded
docs][ed], [wiki articles][wiki], [tickets][tkt], [forum posts][fp], or
[tech notes][tn] to automatically insert a nonce into the page content.
This is all user-provided content, which could link to user-provided
JavaScript via check-in rights, effectively giving all such users a
capability that is usually reserved to the repository’s administrator.

The default-disabled [TH1 documents feature][edtf] is the only known
path around this restriction.  If you are serving a Fossil repository
that has any user you do not implicitly trust to a level that you would
willingly run any JavaScript code they’ve provided, blind, you **must
not** give the `--with-th1-docs` option when configuring Fossil, because
that allows substitution of the [pre-defined `$nonce` TH1
variable](./th1.md#nonce) into [HTML-formatted embedded docs][hfed]:

         <script src="/doc/trunk/bad.js" nonce="$nonce"></script>

Even with this feature enabled, you cannot put `<script>` tags into
Fossil Wiki or Markdown-formatted content, because our HTML generators
for those formats purposely strip or disable such tags in the output.
Therefore, if you trust those users with check-in rights to provide
JavaScript but not those allowed to file tickets, append to wiki
articles, etc., you might justify enabling TH1 docs on your repository,
since the only way to create or modify HTML-formatted embedded docs is
through check-ins.

[ed]:   ./embeddeddoc.wiki
[edtf]: ./embeddeddoc.wiki#th1
[fp]:   ./forum.wiki
[hfed]: ./embeddeddoc.wiki#html
[tkt]:  ./tickets.wiki
[tn]:   ./event.wiki
[wiki]: ./wikitheory.wiki


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
`<head>` tag. None of the stock skins include a `<head>` tag,² so if you
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

1.  There is actually a third context that can correctly insert this
    nonce attribute: [a CGI server extension](./serverext.wiki), by use of
    the `FOSSIL_NONCE` variable sent to the CGI by Fossil.

2.  The stock Bootstrap skin does actually include a `<head>` tag, but
    from Fossil 2.7 through Fossil 2.9, it just repeated the same CSP
    text that Fossil’s C code inserts into the HTML header for all other
    stock skins. With Fossil 2.10, the stock Bootstrap skin uses
    `$default_csp` instead, so you can [override it as above](#th1).


[cs]:    ./customskin.md
[csp]:   https://developer.mozilla.org/en-US/docs/Web/HTTP/CSP
[de]:    https://dopiaza.org/tools/datauri/index.php
