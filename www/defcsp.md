# The Default Content Security Policy (CSP)

When Fossil’s web interface generates an HTML page, it normally includes
a [Content Security Policy][csp] (CSP) in the `<head>`.  The CSP defines
a “white list” to tell the browser what types of content (HTML, images,
CSS, JavaScript...) the document may reference and the sources the
browser is allowed to pull such content from. The aim is to prevent
certain classes of [cross-site scripting][xss] (XSS) and code injection
attacks.  The browser will not pull content types disallowed by the CSP.

Fossil has built-in server-side content filtering logic. For example, it
purposely breaks `<script>` tags when it finds them in Markdown and
Fossil Wiki documents. (But not in [HTML-formatted embedded
docs][hfed]!) We also back that with multiple levels of analysis and
checks to find and fix content security problems: compile-time static
analysis, run-time dynamic analysis, and manual code inspection. Fossil
is open source software, so it benefits from the “[many
eyeballs][llaw],” limited by the size of its developer community.

However, there is a practical limit to the power of server-side
filtering and code quality practices.

First, there is an endless battle between those looking for clever paths
around such barriers and those erecting the barriers. The developers of
Fossil are committed to holding up our end of that fight, but this is,
to some extent, a reactive posture. It is cold comfort if Fossil’s
developers react quickly to a report of code injection — as we do! — if
the bad guys learn of it and start exploiting it first.

Second, Fossil has purposefully powerful features that are inherently
difficult to police from the server side: HTML [in wiki](/wiki_rules)
and [in Markdown](/md_rules) docs, [TH1 docs](./th1.md), etc.

Fossil’s strong default CSP adds client-side filtering to backstop our
server-side measures.

Fossil site administrators can [modify the default CSP](#override), perhaps
to add trusted external sources for auxiliary content.  But for maximum
safety, site developers are encouraged to work within the restrictions
imposed by the default CSP and avoid the temptation to relax the CSP
unless they fully understand the security implications of what they are
doing.

[llaw]: https://en.wikipedia.org/wiki/Linus%27s_Law


## The Default Restrictions

The Fossil default CSP declares the following content restrictions:


### <a name="base"></a> default-src 'self' data:

This policy means mixed-origin content isn’t allowed, so you can’t refer
to resources on other web domains. Browsers will ignore a link like the
one in the following Markdown under our default CSP:

         ![fancy 3D Fossil logotype](https://i.imgur.com/HalpMgt.png)

If you look in the browser’s developer console, you should see a CSP
error when attempting to render such a page.

The default policy does allows inline `data:` URIs, which means you could
[data-encode][de] your image content and put it inline within the
document:

         ![small inline image](data:image/gif;base64,R0lGODlh...)

That method is best used for fairly small resources. Large `data:` URIs
are hard to read and edit. There are secondary problems as well: if you
put a large image into a Fossil forum post this way, anyone subscribed
to email alerts will get a copy of the raw URI text, which can amount to
pages and pages of [ugly Base64-encoded text][b64].

Fossil offers several alternatives for serving large content resources
from within the repository:

*   **versioned content** via [`/raw`](/help?cmd=/raw)
*   **[unversioned content](./unvers.wiki)** via [`/uv`](/help?cmd=/uv)
*   **relative links**

Only the first two options work in [wiki articles][wiki],
[tickets][tkt], [forum posts][fp], and [tech notes][tn].  The last
option is a much simpler alternative, but it only works within [embedded
documentation][ed]:

         ![large inline image](./inlineimage.jpg)

Because all of these methods pull content from within the Fossil
repository, they all count as “self” for the purposes of the CSP.

This rule also works when the Fossil repository is but one path in a
larger website. The browser can’t distinguish Fossil-served content from
that served by the rest of the same web domain, so your repository can
refer to other resources within that same web site, whether they are
static files served by [an HTTP proxy in front of Fossil][svr], by
another Fossil repository served under that same domain, or dynamic
content served by, say, a PHP app on that same site.

Beware that there are a number of problems that come up with using such
out-of-repository resources, which all stem from the fact that they
aren’t included in a [sync](/help?cmd=sync):

1.  Relative links break in `fossil ui` when run on a clone.

2.  Absolute links break under certain types of failover and
    load-balancing schemes.

3.  Absolute links fail when one’s purpose in using a clone is to
    recover from the loss of a project web site by standing that clone
    up [as a server][svr] elsewhere.

You can avoid all of these problems by referring to in-repo resources
exclusively.

[b64]: https://en.wikipedia.org/wiki/Base64
[svr]: ./server/


### <a name="style"></a> style-src 'self' 'unsafe-inline'

This policy allows CSS information to come from separate files hosted
under the Fossil repo server’s Internet domain, or for CSS to be
embedded within `<style>` tags within the document text.

The `'unsafe-inline'` declaration excludes CSS within individual HTML
elements:

        <p style="margin-left: 4em">Indented text.</p>

Because this policy is weaker than [our default for script
elements](#script), there is the potential for an atacker to modify a
Fossil-generated page via CSS. While such page modifications are not as
dangerous as injected JavaScript, the real reason we allow it is that
Fossil still emits in-page `<style>` blocks in a few places. Over time,
we may work out ways to avoid each of these, which will eventually allow
us to tighten this CSP rule down to match the `script` rule. We
recommend that you do your own CSS modifications [via the skin][cs]
rather than depend on the ability to insert `<script>` blocks into
individual pages.


### <a name="script"></a> script-src 'self' 'nonce-%s'

This policy disables in-line javascript and only allows `<script>`
elements if the `<script>` includes a `nonce=` attribute the
matches the %s section of the CSP.  Fossil provides a different
random nonce for every page it generates, and since an attacker has
no way of predicting what that nonce will be, the attacker is unable
to inject working javascript.

For documents generated by the [CGI extensions][ext], the
value of the nonce is accessible in the FOSSIL_NONCE environment variable.
TH1 scripts that run while generating the header or footer can access
the nonce in the $nonce variable.  The JavaScript section of a
[custom skin][cs] automatically includes the appropriate nonce.

#### <a name="xss"></a>Cross-Site Scripting via Ordinary User Capabilities

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
[ext]:  ./serverext.wiki
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
but then provide a new CSP [in the front-end proxy layer][svr]
using any of the many reverse proxy servers that can define custom HTTP
headers.


------------


**Asides and Digressions:**

1.  There is actually a third context that can correctly insert this
    nonce attribute: [a CGI server extension][ext], by use of
    the `FOSSIL_NONCE` variable sent to the CGI by Fossil.

2.  The stock Bootstrap skin does actually include a `<head>` tag, but
    from Fossil 2.7 through Fossil 2.9, it just repeated the same CSP
    text that Fossil’s C code inserts into the HTML header for all other
    stock skins. With Fossil 2.10, the stock Bootstrap skin uses
    `$default_csp` instead, so you can [override it as above](#th1).


[cs]:    ./customskin.md
[csp]:   https://developer.mozilla.org/en-US/docs/Web/HTTP/CSP
[de]:    https://dopiaza.org/tools/datauri/index.php
[xss]:   https://en.wikipedia.org/wiki/Cross-site_scripting
[xssci]: https://fossil-scm.org/forum/forumpost/e7c386b21f
