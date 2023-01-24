# Implementation Details of User Capabilities

## <a id="choices"></a>Capability Letter Choices

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


## <a id="bitfield"></a>Why Not Bitfields?

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


## <a id="filter"></a>Why Doesn’t Fossil Filter “Bad” Artifacts on Sync?

Fossil is more trusting about the content it receives from a remote
clone during sync than you might expect. Common manifestations of this
design choice are:

1.  A user may be able to impersonate other users. This can be
    [accidental](./index.md#defuser) as well as purposeful.

2.  If your local system clock is out-of-sync with absolute time,
    artifacts committed to that repo will appear with the “wrong” time
    when sync’d. If the time sync error is big enough, it can make
    check-ins appear to go back in time and other bad effects.

3.  You can purposely overwrite good timestamps with bad ones and push
    those changes up to the remote with no interference, even though
    Fossil tries to make that a Setup-only operation.

All of this falls out of two of Fossil’s design choices: sync is
all-or-nothing, and [the Fossil hash tree][bc] is immutable. Fossil
would have to violate one or both of these principles to filter such
problems out of incoming syncs.

We have considered auto-[shunning][shun] “bad” content on sync, but this
is [difficult][asd] due to [the design of the sync protocol][dsp]. This
is not an impossible set of circumstances, but implementing a robust
filter on this input path would be roughly as difficult as writing a
basic [inter-frame video codec][ifvc]: do-able, but still a lot of
work. Patches to do this will be thoughtfully considered.

We can’t simply change content as it arrives. Such manipulations would
change the artifact manifests, which would change the hashes, which
would require rewriting all parts of the block chain from that point out
to the tips of those branches. The local Fossil repo must then go
through the same process as the remote one on subsequent syncs in order
to build up a sync sequence that the remote can understand.  Even if
you’re willing to accept all of that, this would break all references to
the old artifact IDs in forum posts, wiki articles, check-in comments,
tickets, etc.

The bottom line here is that [**Clone**](./ref.html#g) and
[**Write**](./ref.html#i) are a potent combination of user capabilities.
Be careful who you give that pair to!


-----

*[Back to Administering User Capabilities](./)*

<!-- add padding so anchor links always scroll ref’d section to top -->
<div style="height: 75em"></div>

[asd]:  https://fossil-scm.org/forum/forumpost/ce4a3b5f3e
[bc]:   ../blockchain.md
[dsp]:  https://fossil-scm.org/fossil/doc/trunk/www/sync.wiki
[for]:  ./forum.wiki
[ifvc]: https://en.wikipedia.org/wiki/Inter_frame
[mn]:   https://en.wikipedia.org/wiki/Mnemonic
[ref]:  ./ref.html
[sexp]: /artifact?ln=1223-1298&name=889d6724
[sff]:  /artifact?ln=80-117&name=52d2860f
[sc]:   https://en.cppreference.com/w/c/string/byte/strchr
[shun]: ../shunning.wiki
[ucap]: ./index.md#ucap
