# Markdown Overview #

## Paragraphs ##

> Paragraphs are divided by blank lines.

## Headings ##

>
    # Top-level Heading                         Alternative Top Level Heading
                                                =============================
>
    ## Second-level Heading                     Alternative 2nd Level Heading
                                                -----------------------------

## Links ##

> 1.  **\[display text\]\(URL\)**
> 2.  **\[display text\]\(URL "Title"\)**
> 3.  **\<URL\>**
> 4.  **\[display text\]\[label\]**

> With link format 4 ("reference links") the label must be resolved by
> including a line of the form **\[label\]:&nbsp;URL** or
> **\[label\]:&nbsp;URL&nbsp;"Title"** somewhere else
> in the document.

## Fonts ##

> *   _\*italic\*_
> *   *\_italic\_*
> *   __\*\*bold\*\*__
> *   **\_\_bold\_\_**
> *   `` `code` ``

> Note that the \`...\` construct disables HTML markup, so one can write,
> for example, **\``<html>`\`** to yield **`<html>`**.

## Lists ##

>
     *   bullet item
     +   bullet item
     -   bullet item
     1.  numbered item

## Block Quotes ##

> Begin each line of a paragraph with ">" to block quote that paragraph.

> >
    > This paragraph is indented
> >
    > > Double-indented paragraph

## Miscellaneous ##

> *   In-line images using **\!\[alt-text\]\(image-URL\)**
> *   Use HTML for complex formatting issues.
> *   Escape special characters (ex: "\[", "\(", "\*")
>     using backslash (ex: "\\\[", "\\\(", "\\\*").
> *   See [daringfireball.net](http://daringfireball.net/projects/markdown/syntax)
>     for additional information.

## Special Features For Fossil ##

> *  In hyperlinks, if the URL begins with "/" then the root of the Fossil
>    repository is prepended.  This allows for repository-relative hyperlinks.
> *  For documents that begin with top-level heading (ex: "# heading #"), the
>    heading is omitted from the body of the document and becomes the document
>    title displayed at the top of the Fossil page.
