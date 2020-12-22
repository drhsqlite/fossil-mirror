# JSON API Introduction
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Why?](#why)
* [Building JSON Support](#builing)
* [Goals & Non-goals](#goals)
* [Potential Client-side Uses](#potential-uses)
* [Technical Problems and Considerations](#considerations)

---

<a id="why"></a>
# Why?

In September, 2011, Fossil contributor Stephan Beal had the great
pleasure of meeting D. Richard Hipp, Fossil's author, for lunch in
Munich, Germany. During the conversation Richard asked, "what does
Fossil need next?" Stephan's first answer was, "refactoring into a
library/client, as opposed to a monolithic app." We very quickly
agreed that the effort required would be "herculean," and second
choice was voiced, "a JSON API." They briefly discussed the idea and
Richard gave his blessing.  That night work began.

Why a JSON API? Because it is the next best thing to the
"librification" of Fossil, in that it makes Fossil's features
available to near-arbitrary applications using a simple, globally
available data format.

<a id="building"></a>
# Building JSON Support

In environments supported by fossil's `configure` script,
simply pass `--enable-json` to it:

```
$ ./configure --prefix=$HOME --enable-json ...
```

When built without that option, JSON support is disabled. **When
reconfiguring the source tree**, ***always be sure to do a "make
clean"*** (or equivalent for your platform) between builds (preferably
*before* reconfiguring), to ensure that everything is rebuilt properly.
If you fail to do that after enabling JSON on a tree which has already
been built, most of the sources will not be rebuilt properly. The reason
is that the JSON files are actually unconditionally compiled, but when
built without `--enable-json` they compile to empty object files. Thus
after a reconfigure the (empty) object files are still up-to-date
vis-a-vis the sources, and won't be rebuilt.

To build Fossil with JSON support on Windows using the Microsoft C
compiler:

```
cd win
nmake -f Makefile.msc FOSSIL_ENABLE_JSON=1
```

It has been seen to compile in VC versions 6 and higher.

<a id="goals"></a>
# Goals & Non-goals

The API described here is most certainly not
[*REST*](http://en.wikipedia.org/wiki/Representational_state_transfer)-conformant,
but is instead JSON over HTTP. The error reporting techniques of the
REST conventions (using HTTP error codes) "does not mesh" with my ideas
of separation of transport- vs. app-side errors. Additionally, REST
requires HTTP methods which are not specified by CGI (namely PUT and
DELETE), which means we can't possibly implement a REST-compatible
interface on top of fossil (which uses CGI mode even for its built-in
server).

The **overall goals** of this effort include:

-   A JSON-based API off of which clients can build customized Fossil
    UIs and special-purpose applications. e.g. a desktop notification
    applet which polls for new timeline data.
-   Arbitrary JSON-using clients should be able to use it. Though JSON
    originates from JavaScript, it is truly a cross-platform data format
    with a very high adoption rate. (There’s even a JSON implementation
    for Oracle PL/SQL.)
-   Fossil’s CGI and Server modes are the main targets and should be
    supported equally. CLI JSON mode is of secondary concern (but is in
    practice easier to test, so it’s generally implemented first).

The ***non-goals*** include:

-   We won’t be able to implement *every* feature of Fossil via a JSON
    interface, and we won’t try to.
-   Binary data (e.g. commits of binary files or downloading ZIP files)
    is not an initial goal, but "might be interesting" once the overall
    infrastructure is in place and working well. See below for more
    details about binary data in JSON.
-   A "pure REST" interface is seemingly not possible due to REST
    relying on HTTP methods not specified in the CGI standard (PUT and
    DELETE). Additionally, REST-style error reporting cannot be used by
    non-HTTP clients (which this code supports).

Adding JSON support also gives us a framework off of which to
build/enhance other features. Some examples include:

-   **Internationalization**. Errors are reported via standard codes and
    the raw artifact data is language-independent.
-   The ability to author **special-case clients**, e.g. a ticket
    poller.
-   Use **arbitrary HTTP-capable languages** to implement such tools.
    Programming languages which can execute programs and intercept their
    stdout output can use the JSON API via a local fossil binary.
-   **Automatable tests.** Many of fossil's test results currently have
    to be "visually reviewed" for correctness after changes (e.g.
    changes in the HTML interface). JSON structures can be
    programmatically checked for correctness. Artifacts are immutable,
    which allows us to be very specific in what data to expect as output
    (for artifact-specific requests the payload data will often (but not
    always) be the same across all requests and all time).

<a id="potential-uses"></a>
# Potential Client-side Uses

Some of the potential client-side uses of this API include...

-   Custom apps/applets to fetch timeline/ticket/etc. information from
    arbitrary repositories. There are many possibilities here, including
    "dashboard" sites which monitor several repositories.
-   Custom post-commit triggers, by polling for changes and reacting to
    them (e.g. sending mails).
-   A custom wiki front-end which uses fossil as the back-end storage,
    inheriting its versioning and user access support while providing a
    completely custom wiki-centric UI. Such a wiki need not have, on the
    surface, anything to do with fossil or source control, as fossil
    would just become a glorified wiki back-end. This approach also
    allows clients to serve wiki pages in a format of their choice -
    since all rendering would be done client-side, they could use
    whatever format they like.


<a id="considerations"></a>
# Technical Problems and Considerations

A random list of considerations which need to be made and potential
problem areas...

-   **Binary data:** JSON is a text serialization method, and it takes
    up the “payload” area of each HTTP request, so there is no
    reasonable way to include binary data in the JSON message without
    some sort of codec like Base64, for which there is no provision in
    the current JSON API. You will therefore find no JSON API for
    committing changes to a file in the repository, for example. Other
    Fossil APIs such as [`/raw`](/help?cmd=/raw) or
    [`/fileedit`](../fileedit-page.md) may serve you better.
-   **64-bit integers:** The JSON standard does not specify integer precision,
    because it targets many different platforms, and not all of
    them can support more than 32 bits. JavaScript (from which JSON
    derives) supports 53 bits of integer precision, which may affect how
    a given client-side JSON implementation sends large integers to Fossil’s JSON
    API. Our JSON parser can cope with integers larger than 32 bits on input, and it
    can emit them, but it requires platform support. If you’re running
    Fossil on a 64-bit host, you should not run into problems in
    this area, but if you’re on a legacy 32-bit only or a mixed 32/64-bit
    system, it’s possible that some integers in the API could be
    clipped. Realize however that this is a rare case: Fossil currently
    cannot store files large enough to exceed a 32-bit `size_t` value,
    and `time_t` won’t roll past 32-bit integers until 2038. We’re aware
    of no other uses of integers in this API that could even in
    principle exceed the range of a 32-bit integer.
-   **Timestamps:** For portability, this API uses UTC Unix epoch
    timestamps. (`time_t`) They are the most portable time representation out
    there, easily usable in most programming environments. (In
    hindsight, we might better have used a higher-precision time format,
    but changing that now would break API compatibility.)
