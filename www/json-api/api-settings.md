# JSON API: /settings
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Fetch Settings](#get)
* [Set Settings](#set)

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


<a id="set"></a>
# Set Settings

**Status:** Implemented 20230120

**Required permissions:** "s"

**Request:** `/json/settings/set`

This call requires that the input payload be an object containing a
mapping of fossil-known configuration keys (case-sensitive) to
values. For example:

```json
{
  "editor": "emacs",
  "admin-log": true,
  "auto-captcha": false
}
```

It iterates through each property, which must have a data type of
`null`, boolean, number, or string. A value of `null` _unsets_
(deletes) the setting.  Boolean values are stored as integer 0
or 1. All other types are stored as-is. It only updates the
`repository.config` database and never updates a checkout or global
config database, nor is it capable of updating versioned settings
(^Updating versioned settings requires creating a full check-in.).

It has no result payload but this may be changed in the future it
practice shows that it should return something specific.

Error responses include:

- `FOSSIL-2002`: called without "setup" permissions.
- `FOSSIL-3002`: called without a payload object.
- `FOSSIL-3001`: passed an unknown config option.
- `FOSSIL-3000`: a value has an unsupported data type.

If an error is triggered, any settings made by this call up until that
point are discarded.
