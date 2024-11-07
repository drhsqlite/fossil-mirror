# Glossary

There are several terms-of-art in Fossil that have specific meanings
which are either not immediately obvious to an outsider or which have
technical associations that can lead someone to either use the terms
incorrectly or to get the wrong idea from someone using those terms
correctly. We hope to teach users how to properly “speak Fossil” with
this glossary.

The bullet-point lists following each definition are meant to be
clarifying and illustrative. They are not part of the definitions
themselves.


## <a id="project"></a>Project

A collection of one or more computer files that serve some conceptually
unified purpose, which purpose changes and evolves over time, with the
history of that project being a valuable record.

*   We qualify the Fossil definition of this common word like this to
    set aside cases where a zip file or tarball would suffice. If you
    can pack your project up into such an archive once and be done with
    it, Fossil is overkill.

    And yet that is often just the beginning, since there is often a
    need for something to be changed, so now you have “version 2” of the
    archive file. If you can foresee yourself creating versioned archive
    files for your project, then you probably should be using Fossil for
    it instead, then using Fossil’s [zip] or [tarball] command to
    automatically produce archives of the latest version rather than
    manually produce and track versions of the archive. The web version
    of these commands ([`/zip`][zw] and [`/tarball`][tw]) are
    particularly useful for public distribution of the latest version of
    a project’s files.

*   Fossil was designed to host the SQLite software project, which is
    comprised of source code, makefiles, scripts, documentation files,
    and so forth. Fossil is also useful for many other purposes, such as
    a fiction book project where each chapter is held in a separate file
    and assembled into a finished whole deliverable.

*   We speak of projects being more than one file because even though
    Fossil can be made to track the history of a single file, it is far
    more often the case that when you get to something of a scale
    sufficient to be called a “project,” there is more than one
    version-tracked file involved, if not at the start, then certainly
    by the end of the project.

    To take the example of a fiction book above, instead of putting each
    chapter in a separate file, you could use a single AsciiDoc file for
    the entire book project rather than make use of its [include
    facility][AIF] to assemble it from chapter files, since that does at
    least solve the [key problems][IFRS] inherent in version-tracking
    something like Word’s DOCX format with Fossil instead.

    While Fossil will happily track the single file containing the prose
    of your book project for you, you’re still likely to want separate
    files for the cover artwork, a style sheet for use in converting the
    source document to HTML, scripts to convert that intermediate output
    to PDF and ePub in a reliably repeatable fashion, a `README` file
    containing instructions to the printing house, and so forth.

*   Fossil requires that all the files for a project be collected into a
    single directory hierarchy, owned by a single user with full rights
    to modify those files. Fossil is not a good choice for managing a
    project that has files scattered hither and yon all over the file
    system, nor of collections of files with complicated ownership and
    access rights.

    A project made of an operating system
    installation’s configuration file set is not a good use of Fossil,
    because you’ll have all of your OS’s *other* files intermixed.
    Worse, Fossil doesn’t track OS permissions, so even if you were to
    try to use Fossil as a system deployment tool by archiving versions
    of the OS configuration files and then unpacking them on a new
    system, the extracted project files would have read/write access by
    the user who did the extraction, which probably isn’t what you were
    wanting.

    Even with these problems aside, do you really want a `.fslckout`
    SQLite database at the root of your filesystem? Are you prepared for
    the consequences of saying `fossil clean --verily` on such a system?
    You can constrain that with [the `ignore-glob` setting][IGS], but
    are you prepared to write and maintain all the rules needed to keep
    Fossil from blowing away the untracked portions of the file system?
    We believe Fossil is a poor choice for a whole-system configuration
    backup utility.

    As a counterexample, a project tracking your [Vim] configuration
    history is a much better use of Fossil, because it’s all held within
    `~/.vim`, and your user has full rights to that subdirectory.

[AIF]:     https://docs.asciidoctor.org/asciidoc/latest/directives/include/
[IGS]:     /help?cmd=ignore-glob
[IFRS]:    ./image-format-vs-repo-size.md
[tarball]: /help?cmd=tarball
[tw]:      /help?cmd=/tarball
[Vim]:     https://www.vim.org/
[zip]:     /help?cmd=zip
[zw]:      /help?cmd=/zip


## Repository <a id="repository" name="repo"></a>

A single file that contains all historical versions of all files in a
project, which can be [cloned] to other machines and
[synchronized][sync] with them. Jargon: repo.

*   A Fossil repo is similar to an archive file in that it is a single
    file that stores compressed versions of one or more files.  Files can be
    extracted from the repo, and new files can be added to the repo,
    but a Fossil repo has other capabilities
    above and beyond what simple archive formats can do.

*   Fossil does not care what you name your repository files, though
    we do suggest “`.fossil`” as a standard extension. There is
    only one place in Fossil where that convention is required, being the
    [`fossil server DIRECTORY`][svrcmd] command, since it serves up
    `*.fossil` files from `DIRECTORY`. If you don’t use that feature,
    you can name your repo files anything you like.

*   Cloned and synced repos redundantly store all available information
    about that project, so if any one repo is lost, all of the cloned
    historical content of the project as of the last sync is preserved
    in each surviving repo.

    Each user generally clones the project repository down to each
    computer they use to participate in that project, and there is
    usually at least one central host for the project as well, so there
    is usually plenty of redundancy in any given Fossil-based project.

    That said, a Fossil repository clone is a backup only in a limited
    sense, because some information can’t be cloned, some doesn’t sync
    by default, and some data is neither clonable nor syncable. We cover
    these limitations and the workarounds for them in a separate
    document, [Backing Up a Remote Fossil Repository][backup].

*   Rather than be a backup, a Fossil repository clone is a
    communication method for coordinating work on the project among
    multiple machines and users: local work done against one repository
    is communicated up to its parent repository on the next sync, and
    work done on other repositories that were previously synced up to
    that parent get pulled down into the local repo clone.

    This bidirectional sync is automatic and on by default in Fossil.
    You can [disable it][asdis], or you can [push] or [pull]
    unidirectionally, if you wish.

    The Fossil philosophy is that all project information resides in
    each clone of the repository. In the ideal world, this would occur
    instantly and automatically, but in actual use, Fossil falls
    somewhat short of that mark. Some limitations are simply
    technological: a given clone may be temporarily out of communication
    with its parent repository, network delays exist, and so forth.
    Fossil is [an AP mode system][CAP]. (This is sometimes called
    “eventual consistency.”) Other cases come down to administrative
    necessity, as covered in [the backup doc][backup].

*   Fossil doesn’t require that you have redundant clones. Whether you
    do or not is a local decision based on usage needs, communication
    requirements, desire for backups, and so forth.

*   Fossil doesn’t care where the repositories are stored, but we
    recommend keeping them all in a single subdirectory such as
    "`~/fossils`" or "`%USERPROFILE%\Fossils`". A flat set of files
    suffices for simple purposes, but you may have use for something
    more complicated. This author uses a scheme like the following on
    mobile machines that shuttle between home and the office:

``` pikchr toggle indent
box "~/museum/" fit
move right 0.1
line right dotted
move right 0.05
box invis "where one stores valuable fossils" ljust

arrow down 50% from first box.s then right 50%
box "work/" fit
move right 0.1
line dotted
move right 0.05
box invis "projects from $DAYJOB" ljust

arrow down 50% from 2nd vertex of previous arrow then right 50%
box "home/" fit
move right 0.1
line dotted right until even with previous line.end
move right 0.05
box invis "personal at-home projects" ljust

arrow down 50% from 2nd vertex of previous arrow then right 50%
box "other/" fit
move right 0.1
line dotted right until even with previous line.end
move right 0.05
box invis "clones of Fossil itself, SQLite, etc." ljust
```

[asdis]:   /help?cmd=autosync
[backup]:  ./backup.md
[CAP]:     ./cap-theorem.md
[cloned]:  /help?cmd=clone
[pull]:    /help?cmd=pull
[push]:    /help?cmd=push
[svrcmd]:  /help?cmd=server
[sync]:    /help?cmd=sync

[repository]:   #repo
[repositories]: #repo


## Version / Revision / Hash / UUID <a id="version" name="hash"></a>

These terms all mean the same thing: a long hexadecimal
[SHA hash value](./hashes.md) that uniquely identifies a particular
[check-in](#ci).

We’ve listed the alternatives in decreasing preference order:

*   **Version** and **revision** are near-synonyms in common usage.
    Fossil’s code and documentation use both interchangeably because
    Fossil was created to manage the development of the SQLite project,
    which formerly used [CVS], the Concurrent Versions System. CVS in
    turn started out as a front-end to [RCS], the Revision Control
    System, but even though CVS uses “version” in its name, it numbers
    check-ins using a system derived from RCS’s scheme, which it calls
    “Revisions” in user-facing output. Fossil inherits this confusion
    honestly.

*   **Hash** refers to the [SHA1 or SHA3-256 hash](./hashes.md) of the
    content of the checked-in data, uniquely identifying that version of
    the managed files. It is a strictly correct synonym, used more often
    in low-level contexts than the term “version.”

*   **UUID** is a deprecated term still found in many parts of the
    Fossil internals and (decreasingly) its documentation. The problem
    with using this as a synonym for a Fossil-managed version of the
    managed files is that there are [standards][UUID] defining the
    format of a “UUID,” none of which Fossil follows, not even the
    [version 4][ruuid] (random) format, the type of UUID closest in
    meaning and usage to a Fossil hash.(^A pre-Fossil 2.0 style SHA1
    hash is 160 bits, not the 128 bits many people expect for a proper
    UUID, and even if you truncate it to 128 bits to create a “good
    enough” version prefix, the 6 bits reserved in the UUID format for
    the variant code cannot make a correct declaration except by a
    random 1:64 chance. The SHA3-256 option allowed in Fossil 2.0 and
    higher doesn’t help with this confusion, making a Fossil version
    hash twice as large as a proper UUID. Alas, the term will never be
    fully obliterated from use since there are columns in the Fossil
    repository format that use the obsolete term; we cannot change this
    without breaking backwards compatibility.)

You will find all of these synonyms used in the Fossil documentation.
Some day we may settle on a single term, but it doesn’t seem likely.

[CVS]:     https://en.wikipedia.org/wiki/Concurrent_Versions_System
[hash]:    #version
[RCS]:     https://en.wikipedia.org/wiki/Revision_Control_System
[ruuid]:   https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_(random)
[snfs]:    https://en.wikipedia.org/wiki/Snapshot_(computer_storage)#File_systems
[UUID]:    https://en.wikipedia.org/wiki/Universally_unique_identifier#Version_4_(random)
[version]: #version


## Check-in <a id="check-in" name="ci"></a>

A [version] of the project’s files that have been committed to the
[repository]; as such, it is sometimes called a “commit” instead.  A
check-in is a snapshot of the project at an instant in time, as seen from
a single [check-out’s](#co) perspective. It is sometimes styled
“`CHECKIN`”, especially in command documentation where any
[valid check-in name][ciname] can be used.

*   There is a harmless conflation of terms here: any of the various
    synonyms for [version] may be used where “check-in” is more accurate,
    and vice versa, because there is a 1:1 relation between them. A
    check-in *has* a version, but a version suffices to uniquely look up
    a particular commit.[^snapshot]

*   Combining both sets of synonyms results in a list of terms that is
    confusing to new Fossil users, but it’s easy
    enough to internalize the concepts. [Committing][commit] creates a
    *commit.*  It may also be said to create a checked-in *version* of a
    particular *revision* of the project’s files, thus creating an
    immutable *snapshot* of the project’s state at the time of the
    commit.  Fossil users find each of these different words for the
    same concept useful for expressive purposes among ourselves, but to
    Fossil proper, they all mean the same thing.

*   Check-ins are immutable.

*   Check-ins exist only inside the repository. Contrast a
    [check-out](#co).

*   Check-ins may have [one or more names][ciname], but only the
    [hash] is globally unique, across all time; we call it the check-in’s
    canonical name. The other names are either imprecise, contextual, or
    change their meaning over time and across [repositories].

[^snapshot]: You may sometimes see the term “snapshot” used as a synonym
  for a check-in or the version number identifying said check-in. We
  must warn against this usage because there is a potential confusion
  here: [the `stash` command][stash] uses the term “snapshot,” as does
  [the `undo` system][undo] to make a distinction with check-ins.
  Nevertheless, there is a conceptual overlap here between Fossil and
  systems that do use the term “snapshot,” the primary distinction being
  that Fossil will capture only changes to files you’ve [added][add] to
  the [repository], not to everything in [the check-out directory](#co)
  at the time of the snapshot. (Thus [the `extras` command][extras].)
  Contrast a snapshot taken by a virtual machine system or a
  [snapshotting file system][snfs], which captures changes to everything
  on the managed storage volume.

[add]:    /help?cmd=add
[ciname]: ./checkin_names.wiki
[extras]: /help?cmd=extras
[stash]:  /help?cmd=stash
[undo]:   /help?cmd=undo



## Check-out <a id="check-out" name="co"></a>

A set of files extracted from a [repository] that represent a
particular [check-in](#ci) of the [project](#project).

*   Unlike a check-in, a check-out is mutable. It may start out as a
    version of a particular check-in extracted from the repository, but
    the user is then free to make modifications to the checked-out
    files. Once those changes are formally [committed][commit], they
    become a new immutable check-in, derived from its parent check-in.

*   You can switch from one check-in to another within a check-out
    directory by passing those names to [the `fossil update`
    command][update].

*   Check-outs relate to repositories in a one-to-many fashion: it is
    common to have a single repo clone on a machine but to have it
    [open] in [multiple working directories][mwd]. Check-out directories
    are associated with the repos they were created from by settings
    stored in the check-out directory. This is in the `.fslckout` file on
    POSIX type systems, but for historical compatibility reasons, it’s
    called `_FOSSIL_` by native Windows builds of Fossil.

    (Contrast the Cygwin and WSL Fossil binaries, which use POSIX file
    naming rules.)

*   In the same way that one cannot extract files from a zip archive
    without having a copy of that zip file, one cannot make check-outs
    without access to the repository file or a clone thereof.

*   Because a Fossil repository is an SQLite database file, the same
    rules for avoiding data corruption apply to it. In particular, it is
    [nearly a hard requirement][h2cflp] that the repository clone be on
    the same machine as the one where you make check-outs and the
    subsequent check-ins.

    That said, the relative locations of the repo and the check-out
    within the local file system are arbitrary.  The repository may be
    located inside the folder holding the check-out, but it certainly
    does not have to be, and it usually is not. As an example, [the
    Fossil plugin for Visual Studio Code][fpvsc] defaults to storing the
    repo clone within the project directory as a file called `.fsl`, but
    this is because VSCode’s version control features assume it’s being
    used with Git, where the repository is the `.git` subdirectory
    contents. With Fossil, [different check-out workflows][cwork] are
    preferred.

[commit]: /help?cmd=commit
[cwork]:  ./ckout-workflows.md
[h2cflp]: https://www.sqlite.org/howtocorrupt.html#_file_locking_problems
[fpvsc]:  https://marketplace.visualstudio.com/items?itemName=koog1000.fossil
[open]:   /help?cmd=open
[mwd]:    ./ckout-workflows.md#mcw
[update]: /help?cmd=update


## <a id="docs"></a>Embedded Documentation

Serving as an alternative to Fossil’s built-in [wiki], the [embedded
documentation feature][edoc] stores the same type of marked-up text
files, but under Fossil’s powerful version control features.

*   The simple rule for determining whether to use the wiki or embedded
    docs for any given document is whether the content is considered
    “evergreen,” as with a Wikipedia article.

    While Fossil’s wiki feature does store the history of each
    document’s changes, Fossil always presents the current version of
    the document unless you manually go out of your way to dig back into
    the history.  Then, having done so, links from that historical
    version of the wiki document take you to the current versions of the
    target documents, not to the version contemporaneous with the source
    document.

    The consequence is that if you say something like…

          $ fossil up 2020-04-01
          $ fossil ui --page wcontent

    …you will **not** see the list of wiki articles as of April Fool’s Day in 2020, but
    instead the list of *current* wiki article versions, the same as if you ran it
    from a check-out of the tip-of-trunk.

    Contrast embedded docs, which are not only version-controlled as
    normal files are in Fossil, they participate in all the tagging,
    branching, and other versioning features. There are several
    consequences of this, such as that Fossil’s [special check-in
    names][ciname] work with embedded doc URLs:

    *   <p>If you visit an embedded doc as `/doc/release/file.md` and
        then click on a relative link from that document, you will remain on
        the release branch. This lets you see not only the release
        version of a software project but also the documentation as of
        that release.</p>

    *   <p>If you visit `/doc/2020-04-01/file.md`, you will not only
        pull up the version of `file.md` as of that date, relative links
        will take you to contemporaneous versions of those embedded docs
        as well.</p>

    *   <p>If you say `fossil up 2020-04-01 && fossil ui` and then visit
        `/doc/ckout/file.md`, you’ll not only see the checked-out
        version of the file as of that date, relative links will show
        you other files within that checkout.</p>

*   Fossil’s wiki presents a flat list of articles, while embedded docs
    are stored in the repository’s file hierarchy, a powerful
    organizational tool well-suited to complicated documentation.

*   Your repository’s Home page is a good candidate for the wiki, as is
    documentation meant for use only with the current version of the
    repository’s contents.

*   If you are at all uncertain whether to use the wiki or the embedded
    documentation feature, prefer the latter, since it is inherently
    more powerful, and when you use the [`/fileedit` feature][fef], the
    workflow is scarcely different from using the wiki.

    (This very file is embedded documentation: clone
    [Fossil’s self-hosting repository][fshr] and you will find it as
    `www/glossary.md`.)

[edoc]: ./embeddeddoc.wiki
[fef]:  ./fileedit-page.md
[fshr]: ./selfhost.wiki
[wiki]: ./wikitheory.wiki


## <a id="cap"></a>Capability

Fossil includes a powerful [role-based access control system][rbac]
which affects which users have permission to do certain things within a given
[repository]. You can read more about this complex topic
[here](./caps/).

Some people — and indeed certain parts of Fossil’s own code — use the
term “permissions” instead, but since [operating system file permissions
also play into this](./caps/#webonly), we prefer the term “capabilities”
(or “caps” for short) when talking about Fossil’s RBAC system to avoid a
confusion here.

[rbac]: https://en.wikipedia.org/wiki/Role-based_access_control

<div style="height:50em" id="this-space-intentionally-left-blank"></div>
