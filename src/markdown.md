# Markdown formatting rules

In addition to its native Wiki formatting syntax, Fossil supports Markdown syntax as specified by
[John Gruber's original Markdown implementation](http://daringfireball.net/projects/markdown/).
For lots of examples - not repeated here - please refer to its
[syntax description](http://daringfireball.net/projects/markdown/syntax), of which the page you
are reading is an extract.

This page itself uses Markdown formatting.

## Summary

  - Block elements

      * A **paragraph** is a group of consecutive lines. Paragraphs are separated by blank lines.

      * A **Header** is a line of text underlined with equal signs or hyphens, or prefixed by a
        number of hash marks.

      * **Block quotes** are blocks of text prefixed by '>'.

      * **Ordered list** items are prefixed by a number and a period. **Unordered list** items
        are prefixed by a hyphen, asterisk or plus sign. Prefix and item text are separated by
        whitespace.

      * **Code blocks** are formed by lines of text (possibly including empty lines) prefixed by
        at least 4 spaces or a tab.

      * A **horizontal rule** is a line consisting of 3 or more asterisks, hyphens or underscores,
        with optional whitespace between them.

  - Span elements

      * 3 types of **links** exist:

        - **automatic links** are URLs or email addresses enclosed in angle brackets
          ('<' and '>'), and are displayed as such.

        - **inline links** consist of the displayed link text in square brackets ('[' and ']'),
          followed by the link target in parentheses.

        - **reference links** separate _link instance_ from _link definition_. A link instance
          consists of the displayed link text in square brackets, followed by a link definition name
          in square brackets.
          The corresponding link definition can occur anywhere on the page, and consists
          of the link definition name in square brackets followed by a colon, whitespace and the
          link target.

      * **Emphasis** can be given by wrapping text in one or two asterisks or underscores - use
        one for HTML `<em>`, and two for `<strong>` emphasis.

      * A **code span** is text wrapped in backticks ('`').

      * **Images** use a syntax much like inline or reference links, but with alt attribute text
        ('img alt=...') instead of link text, and the first pair of square
        brackets in an image instance prefixed by an exclamation mark.

  - **Inline HTML** is mostly interpreted automatically.

  - **Escaping** Markdown punctuation characters is done by prefixing them by a backslash ('\\').

## Details

### Paragraphs

To cause an explicit line break within a paragraph, use 2 or more spaces at the end of a line.

Any line containing only whitespace (space or tab characters) is considered a blank line.

### Headers

#### 'Setext' style headers (underlined)

The number of underlining equal signs or hyphens used has no impact on the resulting header.

Underlining using equal sign(s) creates a top level header (corresponding to HTML `<h1>`),
while hyphen(s) create a second level header (HTML `<h2>`). Thus, only 2 levels of headers
can be made this way.

#### 'Atx' style headers (hash prefixed)

1 to 6 hash characters can be used to indicate header levels 1 (HTML `<h1>`) to 6 (`<h6>`).

Headers may optionally be 'closed' for cosmetic reasons, by appending a whitespace and hash
characters to the header. The number of trailing hash characters has no impact on the header
level.

### Block quotes

Not every line in a paragraph needs to be prefixed by '>' in order to make it a block quote,
only the first line.

Block quoted paragraphs can be nested by using multiple '>' characters as prefix.

Within a block quote, Markdown formatting (e.g. lists, emphasis) still works as normal.

### Lists

A list item prefix need not occur first on its line; up to 3 leading spaces are allowed
(4 spaces would make a code block out of the following text).

For unordered lists, asterisks, hyphens and plus signs can be used interchangeably.

For ordered lists, arbitrary numbers can be used as part of an item prefix; the items will be
renumbered during rendering. However, future implementations may demand that the number used
for the first item in a list indicates an offset to be used for subsequent items.

For list items spanning multiple lines, subsequent lines can be indented using an arbitrary amount
of whitespace.

List items will be wrapped in HTML `<p>` tags if they are separated by blank lines.

A list item may span multiple paragraphs. At least the first line of each such paragraph must
be indented using at least 4 spaces or a tab character.

Block quotes within list items must have their '>' delimiters indented using 4 up to 7 spaces.

Code blocks within list items need to be indented _twice_, that is, using 8 spaces or 2 tab
characters.

### Code blocks

Lines within a code block are rendered verbatim using HTML `<pre>` and `<code>` tags, except that
HTML punctuation characters like '<' and '&' are automatically converted to HTML entities. Thus,
there is no need to explicitly escape HTML syntax within a code block.

A code block runs until the first non blank line with indent less than 4 spaces or 1 tab character.


Regular Markdown syntax is not processed within code blocks.

### Links

#### Automatic links

When rendering automatic links to email addresses, HTML encoding obfuscation is used to
prevent some spambots from harvesting.

#### Inline links

Links to resources on the same server can use relative paths (i.e. can start with a '/').

An optional title for the link (e.g. to have mouseover text in the browser) may be given behind
the link target but within the parentheses, in single and double quotes, and separated from the
link target by whitespace.

#### Reference links

> Each reference link consists of
>
>   - one or more _link instances_ at appropriate locations in the page text
>   - a single _link definition_ at an arbitrary location on the page
>
> During rendering, each link instance is resolved, and the corresponding definition is
> filled in. No separate link definition clauses occur in the rendered output.
>
> There are 3 fields involved in link instances and definitions:
>
>   - link text (i.e. the text that is displayed at the resulting link)
>   - link definition name (i.e. an unique ID binding link instances to link definition)
>   - link target (a target URL for the link)

Multiple link instances may reference the same link definition using its link definition
name.

Link definition names are case insensitive, and may contain letters, numbers, spaces and
punctuation.

##### Link instance

A space may be inserted between the bracket pairs for link text and link definition name.

A link instance can use an _implicit link definition name_ shortcut, in which case the link
text is used as the link definition name. The second set of brackets then remains empty, e.g.
'[Google][]' ('Google' being used as both link text and link definition name).

##### Link definition

The first bracket pair containing the link definition name may be indented using up to 3 spaces.

The link target may optionally be surrounded by angle brackets ('<' and '>').

A link target may be followed by an optional title (e.g. to have mouseover text in the browser).
This title may be enclosed in parentheses, single or double quotes.

Link definitions may be split into 2 lines, with the title on the second line, arbitrarily
indented. This may be more visually pleasing when using long link targets.

### Emphasis

The same character(s) used for starting the emphasis must be used to end it; don't mix
asterisks and underscores.

Emphasis can be used in the middle of a word. That is, there need not be whitespace on either
side of emphasis start or end punctuation characters.

### Code spans

To include a literal backtick character in a code span, use multiple backticks as opening and
closing delimiters.

Whitespace may exist immediately after the opening delimiter and before the closing delimiter
of a code span, to allow for code fragments starting or ending with a backtick.

Within a code span - like within a code block - angle brackets and ampersands are automatically encoded to make including
HTML fragments easier.

### Images

If necessary, HTML must be used to specify image dimensions. Markdown has no provision for this.

### Inline HTML

Start and end tags of
a HTML block level construct (`<div>`, `<table>` etc) must be separated from surrounding
context using blank lines, and must both occur at the start of a line.

No extra unwanted `<p>` HTML tags are added around HTML block level tags.

Markdown formatting within HTML block level tags is not processed; however, formatting within
span level tags (e.g. `<mark>`) is processed normally.

### Escaping Markdown punctuation

The following punctuation characters can be escaped using backslash:

  - \\   backslash
  - `   backtick
  - *   asterisk
  - _   underscore
  - {}  curly braces
  - []  square brackets
  - ()  parentheses
  - #   hash mark
  - +   plus sign
  - -   minus sign (hyphen)
  - .   dot
  - !   exclamation mark

To render a literal backslash, use 2 backslashes ('\\\\').

