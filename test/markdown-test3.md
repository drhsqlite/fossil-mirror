
Markdown Footnotes Test Document
================================

**This document** should help with testing of footnotes support that
is introduced by the ["`markdown-footnotes`"][branch] branch.
It **might look pretty misformatted unless rendered by the proper Fossil
executable that incorporates the abovementioned branch.**[^1]

Developers are invited to add test cases here[^here].
It is suggested that the more simple is a test case the earlier it should
appear in this document.[^ if glitch occurs	]

[^lost3]: This note was defined at the begining of the document.

A footnote's label should be case insensitive[^ case INSENSITIVE ],
it is whitespace-savvy and can even contain newlines.[^ a
multiline
label]

A labeled footnote may be [referenced several times][^many-refs].

A footnote's text should support Markdown [markup][^].

Another reference[^many-refs] to the preveously used footnote.

[^lost2]: This note was defined in the middle of the document.
   It references [its previous][^lost3] 
   and [the forthcoming][^lost1] siblings.

[^i am unreferenced]: If this is rendered wihin footnotes,
  then there is a BUG!

Inline footnotes are supported.(^These may be usefull for adding
<s>small</s> comments.)

If [undefined label is used][^] then red "`misref`" is emited instead of
a numeric marker.[^ see it yourself ]
This can be overridden by the skin though.

The refenrence at the end of this sentence is the sole reason of
rendering of <s>`lost1` and</s> [lost2][^].

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
   Highlighted back-reference indicates a place from which navigation
   occurred[^lost1].

[^lost1]: This note was defined at the end of the document.
   It defines an inline note.
   
   (^This is inline note defined inside of [a labeled note][^lost1].)

[^markup]:   E.g. *emphasis*, and [so on](/md_rules).

[^undefined label is used]: For example due to a typo.

