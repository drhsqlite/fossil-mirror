
Markdown Footnotes Test Document
================================

**This document** should help with testing of footnotes support that
is introduced by the ["`markdown-footnotes`"][branch] branch.
It **might look pretty misformatted unless rendered by the proper Fossil
executable** that incorporates the abovementioned branch.[^1]  
That is also a humble attempt to explore the robustness of the Markdown parser.
So please excuse for the mess in the [source code of this document][src].
By no means the normal use of footnotes should look that scarry.

Developers are invited to add test cases here[^here].
It is suggested that the more simple is a test case the earlier it should
appear in this document.[^ if glitch occurs	]


[^lost3]: This note was defined at the begining of the document.

[^duplicate]: This came from the begining of the document.

A footnote's label should be case insensitive[^ case INSENSITIVE ],
it is whitespace-savvy and can even contain newlines.[^ a
multiline
label]

A labeled footnote may be [referenced several times][^many-refs].

A footnote's text should support Markdown [markup][^].  
Markup within [a [text fragment](https://en.wikipedia.org/wiki/Lorem_ipsum)
of a *span-bounded footnote*][^markup] should also be rendered.

Another reference[^many-refs] to the preveously used footnote.

[^lost2]: This note was defined in the middle of the document.
   It references [its previous][^lost3] 
   and [the forthcoming][^lost1] siblings.

[^i am strayed]:
  This should be presented **verbatim** (without any [markup][^])
  in the end of the footnotes.
  
  Default skin renders label in red font and the main text in gray.
  Other styling may also apply.

Inline footnotes are supported.(^These may be usefull for adding
<s>small</s> comments.)

This is a corner case that is rendered as [an empty footnote](^  []  ()).

If [undefined label is used][^] then red "`misref`" is emited instead of
a numeric marker.[^ see it yourself ]
This can be overridden by the skin though.

The refenrence at the end of this sentence is the sole reason of
rendering of <s>`lost1` and</s> [lost2][^].

If several labeled footnote definitions have the same equal label then texts
from all these definitions are joined.[^duplicate]

Several references should be recognized as several distinct numbers.
(^There should be an interval between numbers.) [^many-refs]

If markup is ambigous between a span-bounded footnote and
a "free-standing" footnote followed by another footnote
then interpret as the later case.
This facilitates the usage in the usual case
when several footnotes are refenrenced at the end
of a phrase.[^scipub][^many-refs](^All these four should
be parsed as "free-standing" footnotes)[^Coelurosauria]

An ambiguity between a link to an image and a *free-standing referenced
footnote* should be resolved as a footnote![^not-image]

A footnote may not be empty(^)
or consist just of blank characters.(^        
              )

The same holds for labeled footnotes. If definition of a labeled footnote
is blank then it is not accepted by the first pass of the parser and
is recognized during the second pass as misreference.
[^ This definition consists of just blanks ]:     
     
     
<style>
  li.fn-upc-example span.fn-upc {
    border: solid 2px lightgreen;
    border-radius: 0.25em;
    padding-left: 2px;
    padding-right: 2px;
    margin-bottom: 0.2em;
  }
  li.fn-upc-example span.fn-upcDot:first-child {
    font-weight: bold;
  }
  sup.noteref.fn-upc-example,
  span.notescope.fn-upc-example sup.noteref {
    border: solid 2px lightgreen;
[^duplicate]:
      Labeled footnote definition may appear anywhere.
      That part came from inside of an inline style definition.
    border-radius: 0.4em;
    padding: 2px;
  }
  sup.noteref.fn-upc-example::after,
  span.notescope.fn-upc-example sup.noteref::after {
    content: " ⛄";
  }
  sup.noteref.fn-upc-example:hover::after,
  span.notescope.fn-upc-example sup.noteref:hover::after {
    content: " 👻";
  }
  li.fn-upc-l span.fn-upc  {
    font-size: 60%;
    color: orange;
  }
  li.fn-upc-l span.fn-upc span.fn-upcDot {
    display: none;
  }
</style>

It is possible to provide a list of classes for a particular footnote and
all its references. This is achieved by prepending a footnote's text with
a special token that starts with dot and ends with colon.
(^
   .alpha-Numeric123.EXAMPLE:
   This token defines a dot-separated list of CSS classes
   which are added to that particular footnote and also to the
   corresponding reference(s). Hypens ('-') are also allowed.
   Classes from the token are tranformed to lowercase and are prepended
   with `"fn-upc-"` to avoid collisions.
)  
This feature is "*opt-in*": there is nothing wrong in starting a footnote's
text with a token of that form while not defining any corresponding classes
in the stylesheet.[^nostyle]
If a footnote consists just of a valid userclass token then this token
is not interpreted as such, instead it is emitted as plain text.
(^  
   .bare.classlist.inside.inline.footnote:  
)[^bare1]
[^bare2]

[^duplicate]: .with.UPC.token:   
   When duplicates are joined their UPC tokens are treated as plain-text.
   Blank characters between token and main text must be preserved.

<html>
  Click
  <a href="?a=B&quote='&nonASCII=😂&script=<script>alert('Broken!');</script>">
  here</a> and
  <a href='?a=B&quote="&nonASCII=😂&script=<script>alert("Broken!");</script>'>
  here</a>
  to test escaping of REQUEST_URI in the generated footnote markers.
</html>

A depth of nesting must be limited.
(^
 .L.1: A long chain of nested inline footnotes...
 (^
  .L.2: is a rather unusual thing...
  (^
   .L.3: and requires extra CPU cycles for processing.
   (^
    .L.4: Theoretically speaking O(n<sup>2</sup>).
    (^
     .L.5: Thus it is worth dismissing those footnotes...
     (^
      .L.6: that are nested deeper than on a certain level.
      (^
       .L.7: A particular value for that limit...
       (^
        is hard-coded in src/markdown.c ...
        (^
         in function `markdown()` ...
         (^
          in variable named `maxDepth`.
          (^
           For the time being, its value is **5**
          )
         )
        )
       )
      )
     )
    )
   )
  )
 )
)

## Footnotes

[branch]: /timeline?r=markdown-footnotes&nowiki

[^ 1]:  Footnotes is a Fossil extension of
        Markdown. Your other tools may have limited support for these.

[^here]: [History of test/markdown-test3.md](/finfo/test/markdown-test3.md)

[src]: /file/test/markdown-test3.md?ci=markdown-footnotes&txt&ln

[^if glitch occurs]:
        So that simple cases are processed even if
        a glitch happens for more tricky cases.

[^	CASE	 insensitive  	]: And also tolerate whitespaces.

[^ a multiline label ]: But at a footnote's definition it should still
    be written within square brackets
             on a single line.

[^duplicate]: And that came from the end of the document.

[^many-refs]:
   Each letter on the left is a back-reference to the place of use.
   Highlighted back-reference indicates a place from which navigation
   occurred[^lost1].

[^lost1]: This note was defined at the end of the document.
   It defines an inline note.
   
   (^This is inline note defined inside of [a labeled note][^lost1].)

[^markup]:   E.g. *emphasis*, and [so on](/md_rules).
   BTW, this note may not have a backreference to the "stray".

[^undefined label is used]: For example due to a typo.

[^not-image]: The rationale is that URLs do not start with **^**
  while a footnote may follow *immediately* after an exclamation mark
  at the end of a sentence.

[^another stray]: Just to verify the correctness of ordering and styling.

[^scipub]: Which is common in the scientific publications.

[^bare1]:  .at.the.1st.line.of.labeled.footnote.definition:
     

[^bare2]:  
           .at.the.2nd.line.of.labeled.footnote.definition:
           
[^stray with UPC]: .UPC-token:
    A token of user-provided classes must be rendered within strays.
    Aslo: this and the previous line may not have extra indentation.

[^nostyle]:
  .unused.classes:
  In that case text of the footnote just looks like as if
  no special processing occured.


[^ <script>alert("You have been pwned!");</script> ]: Labels are escaped

[^ <textarea>"Last words here...' ]:
  <textarea>Content is also escaped</textarea>
