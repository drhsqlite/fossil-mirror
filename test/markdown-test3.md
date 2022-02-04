
Markdown Footnotes Test Document
================================

This document should help with testing of footnotes support that
is introduced by the ["`markdown-footnotes`"][branch] branch.[^1]

Developers are invited to add test cases here[^here].
It is suggested that the more simple is a test case the earlier it should
appear in this document.[^ if glitch occurs	]

A footnote's label should be case insensitive[^ case INSENSITIVE ],
but may not contain newlines.[^broken
label]

A labeled footnote may be [referenced several times][^many-refs].

A footnote's text should support Markdown [markup][^].

Another reference[^many-refs] to the preveously used footnote.

Inline footnotes are supported.(^These may be usefull for adding
<s>small</s> comments.)



## Footnotes

[branch]: /timeline?t=markdown-footnotes

[^ 1]:  Footnotes is a Fossil' extention of
        Markdown. Your other tools may have limited support for these.

[^here]: [](/finfo/test/markdown-test3.md)

[^if glitch occurs]:
        So that simple cases are processed even if
        a glitch happens for more tricky cases.

[^	CASE	 insensitive  	]: And also tolerate whitespaces.

^[broken label]: This text should not render within a list of footnotes.


[^many-refs]:
   Each letter on the left is a back-reference to the place of use.
   Highlighted back-reference indicates a place from which navigation occurred.

[^markup]:   E.g. *emphasis*, and [so on](/md_rules).
