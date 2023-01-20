# JSON API: /settings
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Fetch Settings](#get)
* Set Settings is TODO

---

<a id="get"></a>
# Fetch Settings

**Status:** Implemented 20230120

**Required permissions:** "o"

**Request:** `/json/settings/get[?version=X]`

**Response payload example:**

```json
{
    "access-log":{
      "versionable":false,
      "sensitive":false,
      "defaultValue":"off",
      "valueSource":null,
      "value":null
    },
...
    "binary-glob":{
      "versionable":true,
      "sensitive":false,
      "defaultValue":null,
      "valueSource":"versioned",
      "value":"*.gif\n*.ico\n*.jpg\n*.odp\n*.dia\n*.pdf\n*.png\n*.wav..."
    },
...
    "web-browser":{
      "versionable":false,
      "sensitive":true,
      "defaultValue":null,
      "valueSource":"repo",
      "value":"firefox"
    }
}
```

Each key in the payload is the name of a fossil-recognized setting,
modeled as an object. The keys of that are:


- `defaultValue`: the setting's default value, or `null` if no default
  is defined.
- `value`: The current value of that setting.
- `valueSource`: one of (`"repo"`, `"checkout"`, `"versioned"`, or
  `null`), specifying the data source where the setting was found. The
  settings sources are checked in this order and the first one found
  is the result:
    - If `version=X` is provided, check-in `X` is searched for a
      versionable-settings file. If found, its value is used and
      `valueSource` will be `"versioned"`. If `X` is not a checkin, an
      error response is produced with code `FOSSIL-3006`.
    - If a versionable setting is found in the current checkout, its
      value is used and `valueSource` will be `"versioned"`
    - If the setting is found in checkout database's `vvar` table, its
      value is used and `valueSource` will be `"checkout"`.
    - If the setting is found in repository's `config` table, its
      value is used and `valueSource` will be `"repo"`.
    - If no value is found, `null` is used for both the `value` and
      `valueSource` results. Note that _global settings are never
      checked_ because they can leak information which have nothing
      specifically to do with the given repository.
- `sensitive`: a value which fossil has flagged as sensitive can only
  be fetched by a Setup user.  For other users, they will always have
  a `value` and `valueSource` of `null`.
- `versionable`: `true` if the setting is tagged as versionable, else
  `false`.

Note that settings are internally stored as strings, even if they're
semantically treated as numbers. The way settings are stored and
handled does not give us enough information to recognize their exact
data type here so they are passed on as-is.
