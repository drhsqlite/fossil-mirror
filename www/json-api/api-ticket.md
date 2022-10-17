# JSON API: Tickets
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Ticket Reports](#reports)
  * [Fetch a Report](#report-get)
  * [List Reports](#report-list)
  * [Run a Report](#report-run)


---

# Tickets

This API is incomplete. It is missing at least the following features:

-   Content for a given ticket ID
-   History for a given ticket ID?
-   An option to enable/disable the generation of hyperlinks, as links
    won't be useful in most non-browser clients.


<a id="reports"></a>
# Ticket Reports

<a id="report-get"></a>
## Fetch a Report

**Status:** implemented 20111008

**Required privileges:** "t" (the thinking being that only those
permitted to create reports should be able to read their SQL code)

**Request:** `/json/report/get[/REPORT_NUMBER]`

**Request options:**

-   `report=number` The report number to fetch.\  
    CLI: `-report|-r` \  
    (Design note: `--number/-n` was not used because that parameter has a
    different meaning (limit response count) in several commands.) May
    optionally be defined via the 4th GET path element or CLI arg.

**Response payload example:**

```json
{
"report":1,
"owner":"drh",
"title":"All Tickets",
"timestamp":"112443570187200",
"columns":"#ffffff Key:\r\n#f2dcdc Active\r\n...",
"sqlCode":"..."
}
```

<a id="report-list"></a>
## List Reports

**Status:** implemented 20111008

**Required privileges:** "r" or "n"

**Request:** `/json/report/list`

**Response payload example:**

```json
[
 {
  "report":1,
  "title":"All Tickets",
  "owner":"drh"
  },
  …
]
```

<a id="report-run"></a>
## Run a Report

**Status:** implemented 20111008

**Required privileges:** "r" or "n"

**Request:** `/json/report/run[/REPORT_NUMBER]`

Request options:

-   `report|-r=int` Specifies which report to run. May optionally be
    supplied as the 4th CLI arg or URL path element.
-   `format|-f=string` (default="o") Specifies "array" or "object" output
    format.

**Response payload example:**

```json
{
  "report":1,
  "title":"All Tickets",
  "sqlcode": "only set if requester has 't' privileges.",
  "columnNames":[ … list of column names … ],
  "tickets":[
    {
      "bgcolor":"#cfe8bd",
      "#":"fc825dcf52",
      "timestamp":"112443570187200",
      "type":"Code_Defect",
      "status":"Fixed",
      "subsystem":null,
      "title":"\"config pull all\" asks to approve ssl cert"
    },
    …
  ]
}
```

Note that the column names of ticket reports are determined by the
reports themselves, and not C code. That means that we cannot return a
standard set of column names here. Fossil requires certain column naming
conventions for purposes of styling the HTML interface, e.g. the "\#"
column is always the uuid of the record and "bgcolor" (note the
different casing than bgColor used throughout the rest of this API!) has
a specific meaning to the HTML report browser. Fossil also allows the
tickets to be extended with client-specified fields, so we cannot
generically make these results fit into the API-wide naming scheme. Full
details are here:

[](/doc/trunk/www/custom_ticket.wiki)

and:

[](/rptsql?rn=1)

(That one may require non-default permission.)
