# Interwiki Links

Interwiki links are a short-hand notation for links that target
external wikis or websites.  For example, the following two
hyperlinks mean the same thing (assuming an appropriate [intermap](#intermap)
configuration):

  * [](wikipedia:MediaWiki#Interwiki_links)
  * [](https://en.wikipedia.org/wiki/MediaWiki#Interwiki_links)

Another example:  The Fossil Forum is hosted in a separate repository
from the Fossil source code.  This page is part of the
source code repository.  Interwiki links can be used to more easily
refer to the forum repository:

  * [](forum:d5508c3bf44c6393df09c)
  * [](https://fossil-scm.org/forum/info/d5508c3bf44c6393df09c)

## Advantages Over Full URL Targets

  *  Interwiki links are easier to write.  There is less typing,
     and fewer opportunities to make mistakes.

  *  Interwiki links are easier to read.  With well-chosen
     intermap tags, the links are easier to understand.

  *  Interwiki links continue to work after a domain change on the
     target.  If the target of a link moves to a different domain,
     an interwiki link will continue to work, if the intermap is adjusted,
     but a hard-coded link will be permanently broken.

  *  Interwiki links allow clones to use a different target domain from the
     original repository.

## Details

Fossil supports interwiki links in both the 
[Fossil Wiki](/wiki_rules) and [Markdown](/md_rules) markup
styles. An interwiki link consists of a tag followed by a colon
and the link target:

> <i>Tag</i><b>:</b><i>PageName</i>

The Tag must consist of ASCII alphanumeric characters only - no
punctuation or whitespace or characters greater than U+007A.
The PageName is the link notation on the target wiki.
Three different classes of PageNames are recognized by Fossil:

  1.  <b>Path Links</b> &rarr; the PageName begins with the "/" character
      or is an empty string.

  2.  <b>Hash Links</b> &rarr; the PageName is a hexadecimal number with
      at least four digits.

  3.  <b>Wiki Links</b> &rarr; A PageName that is not a Path or Hash.

The Intermap defines a base URL for each Tag.  Path links are appended
directly to the URL contained in the Intermap.  The Intermap can define
additional text to put in between the base URL and the PageName for
Hash and Wiki links, respectively.

<a id="intermap"></a>
## Intermap

The intermap defines a mapping from interwiki Tags to full URLs.  The
Intermap can be viewed and managed using the [fossil interwiki][iwiki]
command or the [/intermap][imap] webpage.

[iwiki]: /help?cmd=interwiki
[imap]: /intermap

The current intermap for a server is seen on the [/intermap][imap] page
(which is read-only for non-Setup users) and at the bottom of the built-in
[Fossil Wiki rules](/wiki_rules) and [Markdown rules](/md_rules)
documentation pages.

Each intermap entry stores, at a minimum, the base URL for the remote
wiki.  The intermap entry might also store additional path text that
is used for Hash and Wiki links.  If only the base URL is provided,
then the intermap will only allow Path style interwiki links.  The
Hash and Wiki style interwiki links are only allowed if the necessary
extensions are provided in the intermap.


## Disadvantages and Limitations

  *  Configuration is required.  The intermap must be set up correctly
     before interwiki links will work.  This contrasts with ordinary
     links that just work without any configuration.  Cloning a repository
     copies the intermap, but normal syncs do not keep the intermap in
     sync.  Use the "[fossil config pull interwiki][fcfg]" command to
     synchronize the intermap.

  *  The is no backlink tracking.  For ordinary intrawiki links, Fossil keeps
     track of both the source and target, and when displaying targets it
     commonly shows links to that target.  For example, if you mention a
     check-in as part of a comment of another check-in, that new check-in
     shows up in the "References" section of the target check-in.
     ([example](31af805348690958).  In other words, Fossil tracks not just
     "_source&rarr;target_", but it also tracks "_target&rarr;source_".
     But backtracking does not work for interwiki links, since the Fossil
     running on the target has no way of scanning the source text and
     hence has no way of knowing that it is a target of a link from the source.

[fcfg]: /help?cmd=config

## Intermap Storage Details

The intermap is stored in the CONFIG table of the repository database,
in entries with names of the form "<tt>interwiki:</tt><i>Tag</i>".  The
value for each such entry is a JSON string that defines the base URL
and extensions for Hash and Wiki links.

## See Also

  1. [](https://www.mediawiki.org/wiki/Manual:Interwiki)
  2. [](https://duckduckgo.com/?q=interwiki+links&ia=web)
