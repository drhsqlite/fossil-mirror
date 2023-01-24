Notes On Diff Formatting
========================

There are two main kinds of diff display for the web interface:
unified and side-by-side.  Both displays are implemented using
a &lt;table&gt;.  The unified diff is a 4-column table, and the
side-by-side diff is a 5-column table.  In a page like /info that
might show multiple file diffs, each file diff is in a separate
&lt;table&gt;.  For side-by-side diffs, a small amount of Javascript
code is used to resize the text columns so that they fill the screen
width and to keep horizontal scrollbars in sync.

For the unified diff, the basic structure
is like this:

> ~~~~
<table class='diff udiff'>
<tr>
  <td class='diffln difflnl'><pre>
     Line numbers for the left-hand file
  </pre></td>
  <td class='diffln difflnr'><pre>
     Line numbers for the right-hand file
  </pre></td>
  <td class='diffsep'><pre>
     Change marks.  "+" or "=" or nothing
  </pre></td>
  <td class='difftxt difftxtu'><pre>
     The text
  </pre></td>
</tr>
</table>
~~~~

The structure for a side-by-side diff follows the
same basic pattern, though with 5 columns instead of
4, and slightly different class names:

> ~~~~
<table class='diff splitdiff'>
<tr>
  <td class='diffln difflnl'><pre>
     Line numbers for the left-hand file
  </pre></td>
  <td class='difftxt difftxtl'><pre>
     The text for the left side
  </pre></td>
  <td class='diffsep'><pre>
     Change marks.  "+" or "=" or nothing
  </pre></td>
  <td class='diffln difflnr'><pre>
     Line numbers for the right-hand file
  </pre></td>
  <td class='difftxt difftxtr'><pre>
     The text on the right-hand side
  </pre></td>
</tr>
</table>
~~~~

The outer &lt;table&gt; always has class "diff".  The "diffu" class
is added for unified diffs and the "splitdiff" class is added for
side-by-side diffs.

All line-number columns have the "diffln" class.  They also always
have one of "difflnl" or "difflnr" depending on whether they hold
line numbers for the left or right files, respectively.

Text is always kept in a separate column so that it can be scraped
and copied by the user.  All text columns have the "difftxt" class.
One additional class "difftxtu", "difftxtl", or "difftxtr" is added
depending on if the text is for a unified diff, the left column of
a side-by-side diff, or the right column of a side-by-side diff.

The content of all columns is a single &lt;pre&gt; that contains the
appropriate diff-text for that column.  Scrolling is done on the
&lt;pre&gt; element.

Within text columns, highlighting is done with &lt;del&gt; and
&lt;ins&gt; markup.  All text on a line that contains an isert or
delete is surrounded by &lt;ins&gt;...&lt;/ins&gt; or
&lt;del&gt;..&lt;/del&gt;.  Within that line, specific characters
of text that specifically inserted deleted have an additional
layer of &lt;ins&gt; or &lt;del&gt; markup.  Thus CSS like the
following is appropriate:

> ~~~~
td.difftxt ins {
  background-color: #dafbe1;  /* Light green for the whole line */
  text-decoration: none;
}
td.difftxt ins > ins {
  background-color: #a0e4b2;  /* Dark green for specific characters that change */
  text-decoration: none;
}
~~~~

In a side-by-side diff, if an interior &lt;ins&gt; or &lt;del&gt; that mark
specific characters that change correspond to a delete/insert on the other
side, they they have the "edit" class tag.  (ex:  &lt;ins&nbsp;class='edit'&gt;
or &lt;del&nbsp;class='edit'&gt;).  Some skins choose to paint these "modified"
regions blue:

> ~~~~
td.difftxt ins > ins.edit {
  background-color: #c0c0ff;  /* Blue for "modified" text region */
  text-decoration: none;
}
~~~~

Line number text also has &lt;ins&gt; and &lt;del&gt; tags for lines which
are pure insert or pure delete.  But the tags do not nest for line numbers.
