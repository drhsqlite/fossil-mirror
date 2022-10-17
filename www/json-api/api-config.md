# JSON API: /config
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Get Config](#get)
* [Set Config](#set)

---

<a id="get"></a>
# Fetch Config

**Status:** Implemented 20120217

**Required permissions:** "s"

**Request:** `/json/config/get/Area[/Area2/...AreaN]`

Where "Area" can be any combination of: *skin*, *ticket*, *project*,
*all*, or *skin-backup* (which is not included in "all" by default).

**Response payload example:**

```json
{
"ignore-glob":"*~",
"project-description":"For testing Fossil's JSON API.",
"project-name":"fossil-json-tests"
}
```

<a id="set"></a>
# Set/Modify Config

Not implemented.
