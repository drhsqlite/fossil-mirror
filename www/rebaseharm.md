# Rebase Considered Harmful

Fossil deliberately does not implement "rebase", because the original
designer of Fossil (and author of this article) considers rebase to be 
an anti-pattern to be avoided. This article attempts to
explain that point of view.

## 1.0 Rebasing is dangerous

Everyone, even the most vocal advocates of rebase, agrees that rebase can
cause problems when misused.  All rebase documentation talks about the
[golden rule of rebase][golden], that it should never be used on a public
branch.  Horror stories of misused rebase abound, and the rebase 
documentation devotes considerable space to hints on how to recover from
rebase errors and/or misuse.

Sometimes sharp and dangerous tools are justified,
because they accomplish things that cannot be
(easily) done otherwise.  But rebase does not fall into that category.
It provides no new capabilities, as we shall see in the next section:

## 2.0 A rebase is just a merge with historical references omitted

A rebase is really nothing more than a merge (or a series of merges)
that deliberately forgets one of the parents of each merge step.
To help illustrate this fact,
consider the first rebase example from the 
[Git documentation][gitrebase].  The merge looks like this:

![merge case](./rebase01.svg)

And the rebase looks like this:

![rebase case](./rebase02.svg)

As the [Git documentation][gitrebase] points out, check-ins C4\' and C5
are identical.  The only difference between C4\' and C5 is that C5
records the fact that C4 is its merge parent but C4\' does not.

Thus, a rebase is just a merge that forgets where it came from.

The Git documentation acknowledges this fact (in so many words) and
justifies it by saying "rebas[e] makes for a cleaner history".  I read
that sentence as a tacit admission that the Git history display 
capabilities are weak and need active assistance from the user to 
keep things manageable.
Surely a better approach is to record
the complete ancestry of every check-in but then fix the tool to show
a "clean" history in those instances where a simplified display is
desirable and edifying, but retaining the option to show the real,
complete, messy history for cases where detail and accuracy are more
important.

So, another way of thinking about rebase is that it is a kind of
merge that intentionally forgets some details in order to
not overwhelm the weak history display mechanisms available in Git.

### 2.1 Rebase does not actually provide better feature-branch diffs

Another argument, often cited, is that rebasing a feature branch
allows one to see just the changes in the feature branch without
the concurrent changes in the main line of development. 
Consider a hypothetical case:

![unmerged feature branch](./rebase03.svg)

In the above, a feature branch consisting of check-ins C3 and C5 is
run concurrently with the main line in check-ins C4 and C6.  Advocates
for rebase say that you should rebase the feature branch to the tip
of main like the following (perhaps collapsing C3\' into C5\' to form
a single check-in, or not, depending on preferences):

![rebased feature branch](./rebase04.svg)

If only merge is available, one would do a merge from the concurrent
mainline changes into the feature branch as follows:

![merged feature branch](./rebase05.svg)

Check-ins C5\' and C7 check-ins hold identical code.  The only
difference is in their history.  

The argument from rebase advocates
is that with merge it is difficult to see only the changes associated
with the feature branch without the commingled mainline changes.
In other words, diff(C2,C7) shows changes associated both the feature
branch and from the mainline, whereas in the rebase case
diff(C6,C5\') should only the feature branch changes.

But that argument is comparing apples to oranges, since the two diffs
do not have the same baseline.  The correct way to see only the feature
branch changes in the merge case is not diff(C2,C7) but rather diff(C6,C7).

<center><table border="1" cellpadding="5" cellspacing="0">
<tr><th>Rebase<th>Merge<th>What You See
<tr><td>diff(C2,C5\')<td>diff(C2,C7)<td>Commingled branch and mainline changes
<tr><td>diff(C6,C5\')<td>diff(C6,C7)<td>Branch changes only
</table></center>

Remember: C7 and C5\' are bit-for-bit identical, so the output of the
diff is not determined by whether you select C7 or C5\' as the target
of the diff, but rather by your choice of the diff source, C2 or C6.

So, to help with the problem of viewing changes associated with a feature
branch, perhaps what is needed is not rebase but rather better tools to 
help users identify an appropriate baseline for their diffs.

## 3.0 Rebase encourages siloed development

The [golden rule of rebase][golden] is that you should never do it
on public branches, so if you are using rebase as intended, that means
you are keeping private branches.  Or, to put it another way, you are
doing siloed development.  You are not sharing your intermediate work
with collaborators.  This is not good for product quality.

[Nagappan, et. al][nagappan] studied bugs in Windows Vista and found
that best predictor of bugs is the distance on the org-chart between
the stake-holders.  Or, bugs are reduced when the engineers talk to
one another.  Similar finds arise in other disciplines.  Keeping
private branches is a key symptom of siloing.

[Weinberg][weinberg] argues programming should be "egoless".  That
is to say, programmers should avoid linking their code with their sense of
self, as that makes it more difficult for them to find and respond
to bugs, and hence makes them less productive.  Many developers are
drawn to private branches out of sense of ego.  "I want to get the
code right before I publish it".  I sympathize with this sentiment,
and am frequently guilty of it myself.  It is humbling to display
your stupid mistake to the whole world on an internet that
never forgets.  And yet, humble programmers generate better code.

## 4.0 Rebase commits untested check-ins to the blockchain

Rebase adds new check-ins to the blockchain without giving the operator
an opportunity to test and verify those check-ins.  Just because the
underlying three-way merge had no conflict does not mean that the resulting
code actually works.  Thus, rebase runs the very real risk of adding
non-functional check-ins to the permanent record.

Of course, a user can also commit untested or broken check-ins without
the help of rebase.  But at least with an ordinary commit or merge
(in Fossil at least), the operator
has the *opportunity* to test and verify the merge before it is committed,
and a chance to back out or fix the change if it is broken, without leaving
busted check-ins on the blockchain to complicate future bisects.

With rebase, pre-commit testing is not an option.

## 5.0 Rebase causes timestamp confusion

Consider the earlier example of rebasing a feature branch:

![rebased feature branch, again](./rebase04.svg)

What timestamps go on the C3\' and C5\' check-ins?  If you choose
the same timestamps as the original C3 and C5, then you have the
odd situation C3' is older than its parent C6.  We call that a
"timewarp" in Fossil.  Timewarps can also happen due to misconfigured
system clocks, so they are not unique to rebase.  But they are very
confusing and best avoided.  The other option is to provide new
unique timestamps for C3' and C5'.  But then you lose the information
about when those check-ins were originally created, which can make
historical analysis of changes more difficult, and might also
complicate prior art claims.

## 6.0 Rebasing is the same as lying

By discarding parentage information, rebase attempts to deceive the
reader about how the code actually came together.

The [Git rebase documentation][gitrebase] admits as much.  They acknowledge
that when you view a repository as record of what actually happened,
doing a rebase is "blasphemous" and "you're _lying_ about what
actually happened", but then goes on to justify rebase as follows:

>
_"The opposing point of view is that the commit history is the **story of 
how your project was made.** You wouldn\'t publish the first draft of a 
book, and the manual for how to maintain your software deserves careful
editing. This is the camp that uses tools like rebase and filter-branch 
to tell the story in the way that’s best for future readers."_

I reject this argument utterly.
Unless you project is a work of fiction, it is not a "story" but a "history".
Honorable writers adjust their narrative to fit
history.  Rebase adjusts history to fit the narrative.

Truthful texts can be redrafted for clarity and accuracy.
Fossil supports this by providing mechanisms to fix
typos in check-in comments, attach supplemental notes,
and make other editorial changes.
The corrections are accomplished by adding
new modification records to the blockchain.  The original incorrect
inputs are preserved in the blockchain and are easily accessible.
But for routine display purposes, the more readable edited
presentation is provided.  A repository can be a true and accurate
representation of history even without getting everything perfect
on the first draft.

Unfortunately, Git does not provide the ability to add corrections
or clarifications to historical check-ins in its blockchain.  Hence,
once again, rebase can be seen as an attempt to work around limitations
of Git.  Wouldn't it be better to fix the tool rather than to lie about
the project history?

## 7.0 Cherry-pick merges work better then rebase

Perhaps there some cases where a rebase-like transformation
is actually helpful.  But those cases are rare.  And when they do
come up, running a series of cherry-pick merges achieve the same
topology, but with advantages:

  1.  Cherry-pick merges preserve an honest record of history.
      (They do in Fossil at least.  Git's file format does not have
      a slot to record cherry-pick merge history, unfortunately.)

  2.  Cherry-picks provide an opportunity to test each new check-in
      before it is committed to the blockchain

  3.  Cherry-pick merges are "safe" in the sense that they do not
      cause problems for collaborators if you do them on public branches.

  4.  Cherry-picks preserve both the original and the revised check-ins,
      so both timestamps are preserved.

## 8.0 Summary and conclusion

Rebasing is an anti-pattern.  It is dishonest.  It deliberately
omits historical information.  It causes problems for collaboration.
And it has no offsetting benefits.

For these reasons, rebase is intentionally and deliberately omitted
from the design of Fossil.


[golden]: https://www.atlassian.com/git/tutorials/merging-vs-rebasing#the-golden-rule-of-rebasing
[gitrebase]: https://git-scm.com/book/en/v2/Git-Branching-Rebasing
[nagappan]: https://www.microsoft.com/en-us/research/wp-content/uploads/2016/02/tr-2008-11.pdf
[weinberg]: https://books.google.com/books?id=76dIAAAAMAAJ
