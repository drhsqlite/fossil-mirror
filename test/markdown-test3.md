
Markdown Footnotes Test Document
================================

**This document** should help with testing of footnotes support that
is introduced by the ["`markdown-footnotes`"][branch] branch.
It **might look pretty misformatted unless rendered by the proper Fossil
executable that incorporates the abovementioned branch.**[^1]

Developers are invited to add test cases here[^here].
It is suggested that the more simple is a test case the earlier it should
appear in this document.[^ if glitch occurs	]

A footnote's label should be case insensitive[^ case INSENSITIVE ],
it is whitespace-savvy and can even contain newlines.[^ a
multiline
label]

A labeled footnote may be [referenced several times][^many-refs].

A footnote's text should support Markdown [markup][^].

Another reference[^many-refs] to the preveously used footnote.

Inline footnotes are supported.(^These may be usefull for adding
<s>small</s> comments.)

If [undefined label is used][^] then red "`misreference`" is emited instead of
a numeric marker.[^ see it yourself ]
This can be overridden by the skin though.


## Footnotes

[branch]: /timeline?r=markdown-footnotes&nowiki

[^ 1]:  Footnotes is a Fossil' extention of
        Markdown. Your other tools may have limited support for these.

[^here]: [History of test/markdown-test3.md](/finfo/test/markdown-test3.md)

[^if glitch occurs]:
        So that simple cases are processed even if
        a glitch happens for more tricky cases.

[^	CASE	 insensitive  	]: And also tolerate whitespaces.

[^ a multiline label ]: But at a footnote's definition it should still
    be written within square brackets
             on a single line.


[^many-refs]:
   Each letter on the left is a back-reference to the place of use.
   Highlighted back-reference indicates a place from which navigation occurred.

[^markup]:   E.g. *emphasis*, and [so on](/md_rules).

[^undefined label is used]: For example due to a typo.

