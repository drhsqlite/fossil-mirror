# Implementation Details of User Capabilities

## <a name="choices"></a>Capability Letter Choices

We assigned user capability characters using only lowercase ASCII
letters at first, so those are the most important within Fossil: they
control the functions most core to Fossil’s operation. Once we used up
most of the lowercase letters, we started using uppercase, and then
during the development of the [forum feature][for] we assigned most of
the decimal numerals.  Eventually, we might have to start using
punctuation. We expect to run out of reasons to define new caps before
we’re forced to switch to Unicode, though the possibilities for [mnemonic][mn]
assignments with emoji are intriguing. <span style="vertical-align:
bottom">😉</span>

The existing caps are usually mnemonic, especially among the
earliest and therefore most central assignments, made when we still had
lots of letters to choose from.  There is still hope for good future
mnemonic assignments among the uppercase letters, which are mostly still
unused.


## <a name="bitfield"></a>Why Not Bitfields?

When Fossil is deciding whether a user has a given capability, it simply
searches the cap string for a given character. This is slower than
checking bits in a bitfield, but it’s fast enough in the context where
it runs: at the front end of an HTTP request handler, where the
nanosecond differences in such implementation details are completely
swamped by the millisecond scale ping time of that repo’s network
connection, followed by the requires I/O to satisfy the request. A
[`strchr()` call](https://en.cppreference.com/w/c/string/byte/strchr) is
plenty fast in that context.

-----

*[Back to Administering User Capabilities](./)*

[for]:  ./forum.wiki
[mn]:   https://en.wikipedia.org/wiki/Mnemonic
