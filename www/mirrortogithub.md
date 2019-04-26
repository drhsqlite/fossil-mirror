# How To Mirror A Fossil Repository On GitHub

Beginning with Fossil version 2.9, you can mirror a Fossil-based
project on GitHub by following these steps:

<ol>
<li><p>Create an account on GitHub if you do not have one already.  Log
    into that account.

<li><p>Create a new project.  GitHub will ask you if you want to prepopulate
    your project with various things like a README file.  Answer "no" to
    everything.  You want a completely blank project.  GitHub will then
    supply you with a URL for your project that will look something
    like this:

<blockquote>
https://github.com/username/project.git
</blockquote>

<li><p>Back on your workstation, move to a checkout for your Fossil
    project and type:

<blockquote>
<pre>$ fossil git export /path/to/git/repo --autopush \
  https://<font color="orange">username</font>:<font color="red">password</font>@github.com/username/project.git</pre>
</blockquote>

<p>   In place of the <code>/path/to...</code> argument above, put in
      some directory name that is <i>outside</i> of your Fossil checkout. If
      you keep multiple Fossil checkouts in a directory of their own,
      consider using <code>../git-mirror</code> to place the Git export
      mirror alongside them, for example.  Fossil will create this
      directory if necessary.  This directory will become a Git
      repository that holds a translation of your Fossil repository.

<p>   The <code>--autopush</code> option tells Fossil that you want to
      push the Git translation up to GitHub every time it is updated.
      
<p>   The URL parameter is the same as the one GitHub gave you, but with
      your GitHub <font color="orange">username</font> and <font
      color="red">password</font> added.
      
<p>   If your GitHub account uses two-factor authentication (2FA), you
      will have to <a href="https://github.com/settings/tokens">generate
      a personal access token</a> and use that in place of your actual
      password in the URL. This token should have “repo” scope enabled,
      only.

<p>   You can also run the command above outside of any open checkout of
      your project by supplying the “<code>-R&nbsp;repository</code>”
      option.

<li><p>Get some coffee.  Depending on the size of your project, the
       initial "<code>fossil git export</code>" command in the previous
       step might run for several minutes.

<li><p>And you are done!  Assuming everything worked, your project is now
    mirrored on GitHub.

<li><p>Whenever you update your project, simply run this command to update
    the mirror:

<blockquote>
<pre>$ fossil git export</pre>
</blockquote>


<p>   Unlike with the first time you ran that command, you don’t need
      the remaining arguments, because Fossil remembers those things.
      Subsequent mirror updates should usually happen in a fraction of
      a second.

<li><p>To see the status of your mirror, run:

<blockquote>
<pre>$ fossil git status</pre>
</blockquote>
</ol>

## Notes:

  *  The mirroring is one-way.  If you check in changes on GitHub, those
     changes will not be reabsorbed by Fossil.  There are technical problems
     that make a two-way mirror all but impossible.

     This also means that you cannot accept pull requests on GitHub.

  *  The "`fossil git export`" command creates subprocesses that run "`git`"
     commands, so you must have Git installed on your machine for any
     of this to work.

  *  The Git repository will have an extra unmanaged top-level directory named
     "`.mirror_state`" that contains one or more files.  Those files are
     used to store the intermediate state of the translation so that
     subsequent invocations of "`fossil git export`" will know where you
     left off the last time and what new content needs to be moved over into
     Git.  Be careful not to mess with the `.mirror_state` directory or
     any of its contents.  Do not put those files under Git management.  Do
     not edit or delete them.

  *  The name of the "trunk" branch is automatically translated into "master"
     in the Git mirror.

  *  Only check-ins and simple tags are translated to Git.  Git does not
     support wiki or tickets or unversioned content or any of the other
     features of Fossil that make it so convenient to use, so those other
     elements cannot be mirrored in Git.

  *  In Git, all tags must be unique.  If your Fossil repository has the
     same tag on two or more check-ins, the tag will only be preserved on
     the chronologically newest check-in.

  *  There is a 
     [long list of restrictions](https://git-scm.com/docs/git-check-ref-format)
     on tag and branch names in Git.  If any of your Fossil tag or branch names
     violate these rules, then the names are translated prior to being exported
     to Git.  The translation usually involves converting the offending characters
     into underscores.

## Example GitHub Mirrors

As of this writing (2019-03-16) Fossil’s own repository is mirrored
on GitHub at:

>
<https://github.com/drhsqlite/fossil-mirror>

In addition, an official Git mirror of SQLite is available:

>
<https://github.com/sqlite/sqlite>

The Fossil source repositories for these mirrors are at
<https://www2.fossil-scm.org/fossil> and <https://www2.sqlite.org/src>,
respectively.  Both repositories are hosted on the same VM at
[Linode](https://www.linode.com).  On that machine, there is a 
[cron job](https://linux.die.net/man/8/cron)
that runs at 17 minutes after the hour, every hour that does:

>
    /usr/bin/fossil sync -u -R /home/www/fossil/fossil.fossil
    /usr/bin/fossil sync -R /home/www/fossil/sqlite.fossil
    /usr/bin/fossil git export -R /home/www/fossil/fossil.fossil
    /usr/bin/fossil git export -R /home/www/fossil/sqlite.fossil

The initial two "sync" commands pull in changes from the primary
Fossil repositores for Fossil and SQLite.  The last two lines
export the changes to Git and push the results up to GitHub.
