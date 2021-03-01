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

| Id | Source Text    | Actual Rendering     | Correct Rendering            |
-----------------------------------------------------------------------------
|  1:| `*foo bar*`    | *foo bar*            | <em>foo bar</em>             |
|  2:| `a * foo bar*` | a * foo bar*         | a &#42; foo bar&#42;         |
|  3:| `a*"foo"*`     | a*"foo"*             | a&#42;&quot;foo&quot;&#42;   |
|  4:| `* a *`        | * a *                | &#42; a &#42;                |
|  5:| `foo*bar*`     | foo*bar*             | foo<em>bar</em>              |
|  6:| `5*6*78`       | 5*6*78               | 5<em>6</em>78                |
|  7:| `_foo bar_`    | _foo bar_            | <em>foo bar</em>             |
|  8:| `_ foo bar_`   | _ foo bar_           | &#95; foo bar&#95;           |
|  9:| `a_"foo"_`     | a_"foo"_             | a&#95;&quot;foo&quot;&#95;   |
| 10:| `foo_bar_`     | foo_bar_             | foo&#95;bar&#95;             |
| 11:| `5_6_78`       | 5_6_78               | 5&#95;6&#95;78               |
| 12:| `aa_"bb"_cc`   | aa_"bb"_cc           | aa&#95;&quot;bb&quot;&#95;cc |
| 13:| `foo-_(bar)_`  | foo-_(bar)_          | foo-<em>(bar)</em>           |
| 14:| `*(*foo`       | *(*foo               | &#42;(&#42;foo               |
| 15:| `*(*foo*)*`    | *(*foo*)*            | <em>(<em>foo</em>)</em>      |
| 16:| `*foo*bar`     | *foo*bar             | <em>foo</em>bar              |
| 17:| `_foo bar _`   | _foo bar _           | &#95;foo bar &#95;           |
| 18:| `_(_foo)`      | _(_foo)              | &#95;(&#95;foo)              |
| 19:| `_(_foo_)_`    | _(_foo_)_            | <em>(</em>foo<em>)</em>      |
| 20:| `_foo_bar`     | _foo_bar             | &#95;foo&#95;bar             |
| 21:| `_foo_bar_baz_` | _foo_bar_baz_       | <em>foo&#95;bar&#95;baz</em> |
| 22:| `foo_bar_baz`  | foo_bar_baz          | foo&#95;bar&#95;baz          |
| 23:| `_(bar)_`      | _(bar)_              | <em>(bar)</em>               |


# Strong emphasis


| Id | Source Text      | Actual Rendering       | Correct Rendering                      |
-------------------------------------------------------------------------------------------
|  1:| `**foo bar**`    | **foo bar**            | <strong>foo bar</strong>               |
|  2:| `a ** foo bar**` | a ** foo bar**         | a &#42;&#42; foo bar&#42;&#42;         |
|  3:| `a**"foo"**`     | a**"foo"**             | a&#42;&#42;&quot;foo&quot;&#42;&#42;   |
|  4:| `** a **`        | ** a **                | &#42;&#42; a &#42;&#42;                |
|  5:| `foo**bar**`     | foo**bar**             | foo<strong>bar</strong>                |
|  6:| `5**6**78`       | 5**6**78               | 5<strong>6</strong>78                  |
|  7:| `__foo bar__`    | __foo bar__            | <strong>foo bar</strong>               |
|  8:| `__ foo bar__`   | __ foo bar__           | &#95;&#95; foo bar&#95;&#95;           |
|  9:| `a__"foo"__`     | a__"foo"__             | a&#95;&#95;&quot;foo&quot;&#95;&#95;   |
| 10:| `foo__bar__`     | foo__bar__             | foo&#95;&#95;bar&#95;&#95;             |
| 11:| `5__6__78`       | 5__6__78               | 5&#95;&#95;6&#95;&#95;78               |
| 12:| `aa__"bb"__cc`   | aa__"bb"__cc           | aa&#95;&#95;&quot;bb&quot;&#95;&#95;cc |
| 13:| `foo-__(bar)__`  | foo-__(bar)__          | foo-<strong>(bar)</strong>             |
| 14:| `**(**foo`       | **(**foo               | &#42;&#42;(&#42;&#42;foo               |
| 15:| `**(**foo**)**`  | **(**foo**)**          | <strong>(<strong>foo</strong>)</strong> |
| 16:| `**foo**bar`     | **foo**bar             | <strong>foo</strong>bar                |
| 17:| `__foo bar __`   | __foo bar __           | &#95;&#95;foo bar &#95;&#95;           |
| 18:| `__(__foo)`      | __(__foo)              | &#95;&#95;(&#95;&#95;foo)              |
| 19:| `__(__foo__)__`  | __(__foo__)__          | <strong>(</strong>foo<strong>)</strong> |
| 20:| `__foo__bar`     | __foo__bar             | &#95;&#95;foo&#95;&#95;bar             |
| 21:| `__foo__bar__baz__` | __foo__bar__baz__   | <strong>foo&#95;&#95;bar&#95;&#95;baz</strong> |
| 22:| `foo__bar__baz`  | foo__bar__baz          | foo&#95;&#95;bar&#95;&#95;baz          |
| 23:| `__(bar)__`      | __(bar)__              | <strong>(bar)</strong>                 |
