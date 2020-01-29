# JSON API: Tips and Tricks
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Beware of Content-Type and Encoding...](#content-type)
* [Example JavaScript](#javascript)
* [Demo Apps](#demo-apps)

---

<a id="content-type"></a>
# Beware of Content-Type and Encoding...

When posting data to fossil, make sure that the request sends:

-   **Content-Type** of application/json. Fossil also (currently)
    accepts `application/javascript` and `text/plain` as JSON input,
    but `application/json` is preferred. The client may optionally
    send `;charset=utf-8` with the Content-Type, but any other
    encoding produces undefined results. Behaviour without the charset
    or with `;charset=utf-8` suffix is identical.
-   **POST data must be an non-form-encoded JSON string**
    (ASCII or UTF-8). jQuery, by default, form-urlencodes it, which the
    fossil json bits cannot read. e.g. post the result of
    `JSON.stringify(requestObject)`, without any additional encoding on
    top of it.
-   **When POSTing via jQuery**, set these AJAX options:
    -   `contentType:'application/json'`
    -   `dataType:'text'`
    -   `data:JSON.stringify(requestObject)`
-   **When POSTing via XMLHttpRequest** (XHR), be sure to:
    -   `xhr.open( … )`
    -   `xhr.setRequestHeader("Content-Type", "application/json")`
    -   `xhr.send( JSON.stringify( requestObject ) )`

The response will be (except in the case of an HTTP 500 error or
similar) a JSON or JSONP string, ready to be parsed by your favourite
`JSON.parse()` implementation or `eval()`'d directly.

<a href="javascript"></a>
# Example JavaScript (Browser and Shell)

In the fossil source tree, [in the ajax directory](/dir/ajax), is test/demo code
implemented in HTML+JavaScript. While it is still quite experimental, it
demonstrates one approach to creating client-side wrapper APIs for
remote Fossil/JSON repositories.

There is some additional JS test code, which uses the Rhino JS engine
(i.e. from the console, not the browser), under
[`ajax/i-test`](/dir/ajax/-itest). That adds a Rhino-based connection
back-end to the AJAJ API and uses it for running integration-style
tests against an arbitrary JSON-capable repository.


<a id="demo-apps"></a>
# Demo Apps

Known in-the-wild apps using this API:

-   The wiki browsers/editors at [](https://fossil.wanderinghorse.net/wikis/)

