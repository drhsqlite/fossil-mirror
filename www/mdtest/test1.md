# Markdown Link-test

This document exists solely as a test for some of the hyperlinking
capabilities of Markdown as implemented by Fossil.

## Relative-Path Links

  *   The index: [](../index.wiki)

  *   Load management: [](../loadmgmt.md)

  *   Site-map:  [](../../../../sitemap)

  *   Windows CGI: [](../server/windows/cgi.md)

## The Magic $ROOT Path Prefix

In text of the form `href="$ROOT/..."` in the HTML that markdown
generates, the $ROOT is replaced by the complete URI for the root 
of the document tree.
Note that the $ROOT translation only occurs within the `<a href="...">`
element, not within the text of the hyperlink.  So you should see the
$ROOT text on this page, but if you mouse-over the hyperlink the $ROOT
value should have been expanded to the actual document root.

  *   Timeline: []($ROOT/timeline)

  *   Site-map:  []($ROOT/sitemap)

The $ROOT prefix on markdown links is superfluous.  The same link
works without the $ROOT prefix.  (Though: the $ROOT prefix is required
for HTML documents.)

  *   Timeline:  [](/timeline)

  *   Help: [](/help?cmd=help)

  *   Site-map:  [](/sitemap)

## The Magic $CURRENT Document Version Translation

In URI text of the form `.../doc/$CURRENT/...` the
$CURRENT value is converted to the version number of the document
currently being displayed.  This conversion happens after translation
into HTML and only occurs on href='...' attributes so it does not occur
for plain text.

  *   Document index:  [](/doc/$CURRENT/www/index.wiki)

Both the $ROOT and the $CURRENT conversions can occur on the same link.

  *   Document index:  []($ROOT/doc/$CURRENT/www/index.wiki)

The translations must be contained within HTML markup in order to work.
They do not work for ordinary text that appears to be an href= attribute.

  *   `x href='$ROOT/timeline'`
  *   `x action="$ROOT/whatever"`
  *   `x href="https://some-other-site.com/doc/$CURRENT/tail"`
