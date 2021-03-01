# Checkout vs Update

Fossil has two commands that look like they do the same thing on initial
examination, [`fossil update`][up] and [`fossil checkout`][co], but
there are several key differences:

1.  `fossil checkout` aborts if there are changed files in the local
    directory unless you give the `--force` option, whereas
    `fossil update` merges upstream changes with your local changes.
    Since Fossil tends to follow the CVS command design, and CVS
    popularized the [merge on update][cvsmu] workflow, we expect that
    Fossil’s update behavior is more likely to be what you want.

2.  Update triggers an autosync attempt; checkout does not.

3.  Several features in `fossil update` do not exist in
    `fossil checkout`, so developing a habit to type `fossil up` 
    means you’re more likely to have the features you want at hand.

4.  Inversely, the `fossil checkout --keep` feature doesn’t exist in
    `fossil update`, but it’s a rarely-needed operation, so it doesn’t
    provide a good reason to develop a habit of using `fossil checkout`
    instead.

In summary, these are two separate commands; neither is an alias for the
other. They overlap enough that they can be used interchangeably for
some use cases, but `update` is more powerful and more broadly useful.

[co]:    /help?cmd=checkout
[cvsmu]: http://web.mit.edu/gnu/doc/html/cvs_7.html#SEC37
[up]:    /help?cmd=update
