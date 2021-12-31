# Glossary

There are several terms-of-art in Fossil that have specific meanings
which are either not immediately obvious to an outsider or which have
technical associations that can lead someone to either use the terms
incorrectly or to get the wrong idea from someone using those terms
correctly. We hope to teach users how to properly “speak Fossil” with
this glossary.

Each definition is followed by a bullet-point list of clarifying
details. These are not part of the definition itself.


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

    We used the example of a fiction book above, with one chapter per
    file.  That implies scripts for combining those chapters into the
    finished book and converting that into PDF and ePub outputs, each of
    which benefit from being version-tracked.

    You could instead use a Word DOCX file for the entire book project,
    with these implicit scripts replaced by Word menu commands. Fossil
    will happily track that single file’s evolution for you, though
    there are [good reasons](./image-format-vs-repo-size.md) to *not* do
    that.

    Let us say you choose to solve the primary problems brought up in
    that document by using a format like AsciiDoc instead. You could
    still use a single file for the entire book’s prose content, but
    even then you’re still likely to want separate files for a style
    sheet, a script to convert the HTML to PDF and ePub in a reliably
    repeatable fashion, cover artwork files, instructions to the
    printing house, and so forth.

*   Fossil requires that all the files for a project be collected into a
    single directory hierarchy, owned by a single user with full rights
    to modify those files. Fossil is not a good choice for managing a
    project that has files scattered hither and yon all over the file
    system, nor of collections of files with complicated ownership and
    access rights.

    A good mental model for a Fossil project is a versioned zip file or
    tarball. If you cannot easily conceive of creating or extracting
    such an archive for your project, Fossil is probably not a good fit.

    As a counterexample, a project made of an operating system
    installation’s configuration file set is not a good use of Fossil,
    because you’ll have all of your OS’s *other* files intermixed.
    Worse, Fossil doesn’t track OS permissions, so even if you were to
    try to use Fossil as a system deployment tool by archiving versions
    of the OS configuration files and then unpacking them on a new
    system, the extracted project files would have read/write access by
    the user who did the extraction, which probably isn’t want you were
    wanting.

    Even with these problems aside, do you really want a `.fslckout`
    SQLite database at the root of your filesystem? Are you prepared for
    the consequences of saying `fossil clean --verily` on such a system?
    We believe Fossil is a poor choice for a whole-system configuration
    backup utility.

    And as a counter-counterexample, a project made of your user’s [Vim]
    configuration is a much better use of Fossil, because it’s all held
    within `~/.vim`, and your user has full rights to that subdirectory.

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
scale=0.8
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
[backup]: ./backup.md
[CAP]:    ./cap-theorem.md
[cloned]: /help?cmd=clone
[pull]:   /help?cmd=pull
[push]:   /help?cmd=push
[svrcmd]: /help?cmd=server
[sync]:   /help?cmd=sync


## Check-in <a id="check-in" name="ci"></a>

A version of the project’s files that have been committed to the
repository. It is a snapshot of the project at an instant in time, as
seen from a single [check-out’s](#co) perspective. Synonyms: version,
snapshot, revision, commit. Sometimes styled “`CHECKIN`”, especially in
command documentation where any [valid checkin name][ciname] can be
used.

*   The long list of synonyms is confusing to new Fossil users,
    particularly the noun sense of the word “commit,” but it’s easy
    enough to internalize the concepts. [Committing][commit] creates a
    *commit.*  It may also be said to create a checked-in *version* of a
    particular *revision* of the project’s files, thus creating an
    immutable *snapshot* of the project’s state at the time of the
    commit.  Fossil users find each of these different words for the
    same concept useful for expressive purposes among ourselves, but to
    Fossil proper, they all mean the same thing.

    You will find all of these synonyms used in the Fossil documentation.
    Some day we may settle on a single term, but it doesn’t seem likely.

*   Check-ins are immutable.

*   Check-ins exist only inside the repository. Contrast a
    [check-out](#co).

*   Check-ins may have [one or more names][ciname], but only the SHA
    hash is globally unique, across all time; we call it the check-in’s
    canonical name. The other names are either imprecise, contextual, or
    change their meaning over time and across [repositories](#repo).

[ciname]: ./checkin_names.wiki



## Check-out <a id="check-out" name="co"></a>

A set of files extracted from a [repository](#repo) that represent a
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
    stored in the check-out drectory. This is in the `.fslckout` file on
    POSIX type systems, but for historical compatibility reasons, it’s
    called `_FOSSIL_` by native Windows builds of Fossil.

    (Contrast the Cygwin and WSL Fossil binaries, which use POSIX file
    naming rules.)

*   In the same way that one cannot extract files from a zip archive
    without having a copy of that zip file, one cannot make check-outs
    without access to the repository file or a clone thereof.

*   Because a Fossil repository is a SQLite database file, the same
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

<div style="height:50em" id="this-space-intentionally-left-blank"></div>
