# Markdown Emphasis Test Cases

<style>
div.markdown table {
  border: 2px solid black;
  border-spacing: 0;
}
div.markdown th {
  border-left: 1px solid black;
  border-right: 1px solid black;
  border-bottom: 1px solid black;
  padding: 4px 1em 4px;
  text-align: left;
}
div.markdown td {
  border-left: 1px solid black;
  border-right: 1px solid black;
  padding: 4px 1em 4px;
  text-align: left;
}
</style>

See <https://spec.commonmark.org/0.29/#emphasis-and-strong-emphasis>

| Source Text    | Actual Rendering     | Correct Rendering          |
----------------------------------------------------------------------
| `*foo bar*`    | *foo bar*            | <em>foo bar</em>           |
| `a * foo bar*` | a * foo bar*         | a &#42; foo bar&#42;       |
| `a*"foo"*`     | a*"foo"*             | a&#42;&quot;foo&quot;&#42; |
| `* a *`        | * a *                | &#42; a &#42;              |
| `foo*bar*`     | foo*bar*             | foo<em>bar</em>            |
| `5*6*78`       | 5*6*78               | 5<em>6</em>78              |
| `_foo bar_`    | _foo bar_            | <em>foo bar</em>           |
| `_ foo bar_`   | _ foo bar_           | &#95; foo bar&#95;         |
| `a_"foo"_`     | a_"foo"_             | a&#95;&quot;foo&quot;&#95; |
| foo_bar_       | foo_bar_             | foo&#95;bar&#95;           |
| 5_6_78         | 5_6_78               | 5&#95;6&#95;78             |
| `пристаням_стремятся_` | пристаням_стремятся_ | пристаням&#95;стремятся&#95; |
| `aa_"bb"_cc`   | aa_"bb"_cc           | aa&#95;&quot;bb&quot;&#95;cc |
| `foo-_(bar)_`  | foo-_(bar)_          | foo-<em>(bar)</em>         |
| `*(*foo`       | *(*foo               | &#42;(&#42;foo             |
| `*(*foo*)*`    | *(*foo*)*            | <em>(</em>foo<em>)</em>    |
| `*foo*bar`     | *foo*bar             | <em>foo</em>bar            |
| `_foo bar _`   | _foo bar _           | &#95;foo bar &#95;         |
| `_(_foo)`      | _(_foo)              | &#95;(&#95;foo)            |
| `_(_foo_)_`    | _(_foo_)_            | <em>(</em>foo<em>)</em>    |
| `_foo_bar`     | _foo_bar             | &#95;foo&#95;bar           |
| `_пристаням_стремятся` | _пристаням_стремятся | \_пристаням\_стремятся |
| `_foo_bar_baz_` | _foo_bar_baz_       | <em>foo&#95;bar&#95;baz</em> |
| `_(bar)_`      | _(bar)_              | <em>(bar)</em>             |
