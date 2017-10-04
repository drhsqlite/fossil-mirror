# Markdown Overview #

## Paragraphs ##

> Paragraphs are divided by blank lines.  
> End a line with two or more spaces to force a mid-paragraph line break.

## Headings ##

>
    # Top Level Heading                 Alternative Top Level Heading
    # Top Level Heading Variant #       =============================
>
    ## 2nd Level Heading                Alternative 2nd Level Heading
    ## 2nd Level Heading Variant ##     -----------------------------
>
    ### 3rd Level Heading               ### 3rd Level Heading Variant ###
    #### 4th Level Heading              #### 4th Level Heading Variant ####
    ##### 5th Level Heading             ##### 5th Level Heading Variant #####
    ###### 6th Level Heading            ###### 6th Level Heading Variant ######

## Links ##

> 1.  **\[display text\]\(URL\)**
> 2.  **\[display text\]\(URL "Title"\)**
> 3.  **\[display text\]\(URL 'Title'\)**
> 4.  **\<URL\>**
> 5.  **\[display text\]\[label\]**
> 6.  **\[display text\]\[\]**

> **URL** may optionally be written **\<URL\>**.  With link formats 5 and 6
> ("reference links"), the URL is supplied elsewhere in the document, as shown
> below.  Link format 6 reuses the display text as the label.  Labels are
> case-insensitive.  The title may be split onto the next line with optional
> indenting.

> * **\[label\]:&nbsp;URL**
> * **\[label\]:&nbsp;URL&nbsp;"Title"**
> * **\[label\]:&nbsp;URL&nbsp;'Title'**
> * **\[label\]:&nbsp;URL&nbsp;(Title)**

## Fonts ##

> *   _\*italic\*_
> *   *\_italic\_*
> *   __\*\*bold\*\*__
> *   **\_\_bold\_\_**
> *   ___\*\*\*italic+bold\*\*\*___
> *   ***\_\_\_italic+bold\_\_\_***
> *   \``code`\`

> The **\`code\`** construct disables HTML markup, so one can write, for
> example, **\`\<html\>\`** to yield **`<html>`**.

## Lists ##

>
     *   bullet item
     +   bullet item
     -   bullet item
     1.  numbered item

> A two-level list is created by placing additional whitespace before the
> **\***/**+**/**-**/**1.** of the secondary items.

>
     *   top-level item
       * secondary item

## Block Quotes ##

> Begin each line of a paragraph with **>** to block quote that paragraph.

> >
    > This paragraph is indented
> >
    > > Double-indented paragraph

> Begin each line with at least four spaces or one tab to produce a verbatim
> code block.

## Tables ##

>
    | Header 1     | Header 2    | Header 3      |
    ----------------------------------------------
    | Row 1 Col 1  | Row 1 Col 2 | Row 1 Col 3   |
    |:Left-aligned |:Centered   :| Right-aligned:|
    |              | ← Blank   → |               |
    | Row 4 Col 1  | Row 4 Col 2 | Row 4 Col 3   |

> The first row is a header if followed by a horizontal rule or a blank line.

> Placing **:** at the left, both, or right sides of a cell gives left-aligned,
> centered, or right-aligned text, respectively.  By default, header cells are
> centered, and body cells are left-aligned.

> The leftmost **\|** is required if the first column contains at least one
> blank cell.  The rightmost **\|** is optional.

## Miscellaneous ##

> *   In-line images are made using **\!\[alt-text\]\(image-URL\)**.
> *   Use HTML for advanced formatting such as forms.
> *   **\<!--** HTML-style comments **-->** are supported.
> *   Escape special characters (ex: **\[** **\(** **\|** **\***)
>     using backslash (ex: **\\\[** **\\\(** **\\\|** **\\\***).
> *   A line consisting of **---**, **\*\*\***, or **\_\_\_** is a horizontal
>     rule.  Spaces and extra **-**/**\***/**_** are allowed.
> *   See [daringfireball.net][] for additional information.
> *   See this page's [Markdown source](/md_rules?txt=1) for complex examples.

## Special Features For Fossil ##

> *  In hyperlinks, if the URL begins with **/** then the root of the Fossil
>    repository is prepended.  This allows for repository-relative hyperlinks.
> *  For documents that begin with a top-level heading (ex: **# heading #**),
>    the heading is omitted from the body of the document and becomes the
>    document title displayed at the top of the Fossil page.

[daringfireball.net]: http://daringfireball.net/projects/markdown/syntax
