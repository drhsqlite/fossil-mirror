# Implementation Details of User Capabilities

## <a name="choices"></a>Capability Letter Choices

We [assigned][ref] user capability characters using only lowercase ASCII
letters at first, so those are the most important within Fossil: they
control the functions most core to Fossil’s operation. Once we used up
most of the lowercase letters, we started using uppercase, and then
during the development of the [forum feature][for] we assigned most of
the decimal numerals. All of the lowercase ASCII letters are now
assigned. Eventually, we might have to start using ASCII
punctuation and symbols. We expect to run out of reasons to define new caps before
we’re forced to switch to Unicode, though the possibilities for [mnemonic][mn]
assignments with emoji are intriguing. <span style="vertical-align:
bottom">😉</span>

The existing caps are usually mnemonic, especially among the
earliest and therefore most central assignments, made when we still had
lots of letters to choose from.  There is still hope for good future
mnemonic assignments among the uppercase letters, which are mostly still
unused.


## <a name="bitfield"></a>Why Not Bitfields?

Some may question the use of ASCII character strings for [capability
sets][ucap] instead of bitfields, which are more efficient, both in
terms of storage and processing time.

Fossil handles these character strings in one of two ways. For most HTTP
hits, Fossil [expands][sexp] the string into a [`struct` full of
flags][sff] so that later code can just do simple Boolean tests. In a
minority of cases, where Fossil only needs to check for the presence of
a single flag, it just does a [`strchr()` call][sc] on the string
instead.

Both methods are slower than bit testing in a bitfield, but keep the
execution context in mind: at the front end of an HTTP request handler,
where the nanosecond differences in such implementation details are
completely swamped by the millisecond scale ping time of that repo’s
network connection, followed by the required I/O to satisfy the request.
Either method is plenty fast in that context.

In exchange for this immeasurable cost per hit, we get human-readable
capability sets.

-----

*[Back to Administering User Capabilities](./)*

[for]:  ./forum.wiki
[mn]:   https://en.wikipedia.org/wiki/Mnemonic
[ref]:  ./ref.html
[sexp]: http://fossil-scm.org/fossil/artifact?udc=1&ln=1223-1298&name=889d6724
[sff]:  http://fossil-scm.org/fossil/artifact?udc=1&ln=80-117&name=52d2860f
[sc]:   https://en.cppreference.com/w/c/string/byte/strchr
[ucap]: ./index.md#ucap
