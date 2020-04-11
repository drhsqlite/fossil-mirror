# Fossil CSS Tips and Tricks

Many aspects of Fossil's appearance can be customized by
[customizing the site skin](customskin.md). This document
details certain specific CSS tweaks which users have asked
about via the forums.

This is a "living document" - please feel free to suggest
additions via [the Fossil forum](https://fossil-scm.org/forum/).

This document is *not* an introduction to CSS - the web is
full of tutorials on that topic. It covers only the specifics
of customizing certain CSS-based behaviors in a Fossil UI. That said...

# Overriding Default Rules

One behavior of the skinning system works considerably differently
from the cascading nature of CSS: if a skin applies a CSS selector for
which Fossil has a built-in default value, Fossil elides the entire
default definition for that rule. i.e., the skin's definition is the
only one which is applied, rather than cascading the definition from
the default value.

For example, if Fossil has a default CSS rule which looks like:

```css
div.foo {
  font-size: 120%;
  margin-left: 1em;
}
```

And a skin has:

```css
div.foo {}
```

Then Fossil will *not* emit its default rule and the user's copy will
become the only definition of that CSS rule. This is different from
normal CSS cascading rules, in which the above sequence would result
in, effectively, the top set of rules being applied because the second
(empty) one does not override anything from the first.

If a skin applies a given selector more than once, or imports external
style sheets which do, those cascade following CSS's normal rules.

## Is it Really `!important`?

By and large, CSS's `!important` qualifier is not needed when
customzing Fossil's CSS. On occasion, however, particular styles may
be set directly on DOM elements when Fossil generates its HTML, and
such cases require the user of `!important` to override them.


<!-- ============================================================ -->
# Main UI CSS

## Number of Columns in `/dir` View

The width of columns on the [`/dir` page](/dir) is calculated
dynamically as the page is generated, to attempt to fit the widest
name in a given directory. The number of columns is determined
automatically by CSS. To modify the number of columns and/or the entry width:

```css
div.columns {
  columns: WIDTH COLUMN_COUNT !important;
  /* Examples:
    columns: 20ex 3 !important
    columns: auto auto !important
  */
}
/* The default rule uses div.columns, but it can also be selected using: */
div.columns.files { ... }
```

The `!important` qualifier is required here because the style values are dynamically
calculated and applied when the HTML is emitted.

The file list itself can be further customized via:

```css
div.columns > ul {
 ...
}
ul.browser {
 ...
}
```


<!-- ============================================================ -->
# Forum-specific CSS

## Limiting Display Length of Long Posts

Excessively long posts can make scrolling through threads problematic,
especially on mobile devices. The amount of a post which is visible can
be configured using:

```css
div.forumPostBody {
  max-height: 25em; /* change to the preferred maximum effective height */
  overflow: auto; /* tells the browser to add scrollbars as needed */
}
```

