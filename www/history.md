# The History And Purpose Of Fossil

Fossil is a [distributed version control system (DVCS)][100] written
beginning in [2007][105] by the [architect of SQLite][110] for the
purpose of managing the [SQLite project][115].

[100]: https://en.wikipedia.org/wiki/Distributed_version_control
[105]: /timeline?a=1970-01-01&n1=10
[110]: https://sqlite.org/crew.html
[115]: https://sqlite.org/

Though Fossil was originally written specifically to support SQLite,
it is now also used by countless other projects.  The SQLite architect (drh)
is still the top committer to Fossil, but there are also
[many other contributors][120].

[120]: /reports?type=ci&view=byuser

## History

The SQLite project started out using [CVS][300], as CVS was the most
commonly used version control system in that era (circa 2000).  CVS
was an amazing version control system for its day in that it allowed
multiple developers to be editing the same file at the same time.

[300]: https://en.wikipedia.org/wiki/Concurrent_Versions_System

Though innovative and much loved in its time, CVS was not without problems.
Among those was a lack of visibility into the project history and the
lack of integrated bug tracking.  To address these deficiencies,
the SQLite author developed the [CVSTrac][305] wrapper for CVS beginning
in [2002][310].

[305]: http://cvstrac.org/
[310]: http://cvstrac.org/fossil/timeline?a=19700101&n1=10

CVSTrac greatly improved the usability of CVS and was adopted by
other projects.  CVSTrac also [inspired the design][315] of [Trac][320],
which was a similar system that was (and is) far more widely used.

[315]: https://trac.edgewall.org/wiki/TracHistory
[320]: https://trac.edgewall.org/

Historians can see the influence of CVSTrac on the development of
SQLite.  [Early SQLite check-ins][325] that happened before CVSTrac
often had a check-in comment that was just a "smiley".
That was not an unreasonable check-in comment, as check-in comments
were scarcely seen and of questionable utility in raw CVS.  CVSTrac
changed that, making check-in comments more visible and more useful.
The SQLite developers reacted by creating [better check-in comments][330].

[325]: https://sqlite.org/src/timeline?a=19700101&n1=10
[330]: https://sqlite.org/src/timeline?c=20030101&n1=10&nd

At about this same time, the [Monotone][335] system appeared.
Monotone was one of the first distributed version control systems. As far as
this author is aware, Monotone was the first VCS to make use of
SHA1 to identify artifacts.  Monotone stored its content in an SQLite
database, which is what brought it to the attention of the SQLite architect.
These design choices were a major source of inspiration for Fossil.

[335]: https://www.monotone.ca/

Beginning around 2005, the need for a better version control system
for SQLite began to become evident.  The SQLite architect looked
around for a suitable replacement.  Monotone, Git, and Mercurial were
all considered.  But at that time, none of these supported sync
over ordinary HTTP, none could be run from an inexpensive shell
account on a leased server (this was before the widespread availability
of affordable virtual private servers), and none of them supported anything 
resembling the wiki and ticket features of CVSTrac that had been 
found to be so useful.  And so, the SQLite architect began writing
his own DVCS.

Early prototypes were done in [TCL][340].  As experiments proceeded,
however, it was found that the low-level byte manipulates needed for
things like delta compression and computing diffs
were better implemented in plain old C.
Experiments continued.  Finally, a prototype capable of self-hosting
was devised on [2007-07-16][345].

[340]: https://www.tcl.tk/
[345]: https://fossil-scm.org/fossil/timeline?c=200707211410&n1=10

The first project hosted by Fossil was Fossil itself.  After a
few months of development work, the code was considered stable enough
to begin hosting the [SQLite documentation repository][350] which was
split off from the main SQLite CVS repository on [2007-11-12][355].
After two years of development work on Fossil, the
SQLite source code itself was transferred to Fossil on
[2009-08-11][360].

[350]: https://www.sqlite.org/docsrc/doc/trunk/README.md
[355]: https://www.sqlite.org/docsrc/timeline?c=200711120345&n1=10
[360]: https://sqlite.org/src/timeline?c=b0848925babde524&n1=12&y=ci
