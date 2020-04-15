# JSON API: /user
([&#x2b11;JSON API Index](index.md))

Jump to:

* [Get User Info](#get)
* [List Users](#list)
* [Save User](#save)

---

<a id="get"></a>
# Get User Info

**Status:** implemented 20110927.

**Required privileges:** "a" or "s"

**Request:**

-   POST `/json/user/get`\  
    with `POST.payload.name=USERNAME`
-   `/json/user/get?name=USERNAME`

**Response payload example:**

```json
{
  "uid":1,
  "name":"stephan",
  "capabilities":"abcdefhgijkmnopqrstuvwxz",
  "info":"https://wanderinghorse.net/home/stephan/",
  "timestamp":1316122562
}
```

(What does that timestamp field represent, anyway?)

<a id="list"></a>
# List Users

**Status:** implemented 20110927.

**Required privileges:** "a" or "s"

**Request:** `/json/user/list`

**Response payload example:**

```json
[
 {
  "uid":1,
  "name":"stephan",
  "capabilities":"abcdefhgijkmnoprstuvwxz",
  "info":"",
  "timestamp":1316122562
 },
 ... more users...
]
```


<a id="save"></a>
# Save User

Only admin/setup users may modify accounts other than their own.

**Status:** implemented 20111021 *but* it is missing "login group"
support, so changes do not yet propagate to other repos within a group.

**Required privileges:** 'p' or 'a' or 's', depending on the context.

**Request:** `/json/user/save`

All request options must come from the `POST.payload` and/or GET/CLI
parameters (exception: "name" must come from POST.payload or CLI).
GET/CLI parameters take precedence over those in `POST.payload`, the
intention being to use an input file as a template and overriding the
template's defaults via the CLI. The options include:

-   `name=string` Specifies the user name to change. When changing a
    user's name, the current uid and the new name must be specified.\  
    **Achtung:** due to fossil-internal ambiguity in the handling of the
    "name" parameter, this parameter must come from the `POST.payload`
    data or it will not be recognized. In CLI mode it may be specified
    with the `--name` flag.
-   `uid=int` Specifies the uid to change. At least one of uid or name are
    required. A uid of -1 means to create a new user, in which case the
    name must be provided.
-   `password=string` Optionally changes the user's password. When
    renaming existing or creating new users, be sure to always provide a
    new password because any old password hash is invalidated by the
    name change.
-   `info=string` Optionally changes the user's info field.
-   `capabilities=string` Optionally changes the user's capabilities
    field.
-   `forceLogout=bool` (=false, or true when renaming) Optionally clears
    any current login info for the current user, which will invalidate
    any active session. Requires 'a' or 's' privileges. Intended to be
    used when disabling a user account, to ensure that any open session
    is invalidated. When a user is renamed this option is implied (and
    cannot be disabled) because renaming invalidates any currently
    stored auth token (because the old name is part of the hash
    equation).

Fields which are not provided in the request will not be modified.
Non-admin/setup users cannot edit other users and may only change their
own data if they have the 'p' (password) privilege.

As of 20120217, users who do not have the setup privilege may neither
change the setup privilege for any user nor edit another user who has
that privilege. That is, only users with setup access may propagate or
remove setup status and accounts with the setup privilege may only be
edited by themselves and other setup users.

**Response payload:** Same as user/get, using the new/saved state of the
modified user.

Example usage from the command line:

```console
$ fossil json user save --name drh --password sqlite3 \
 --capabilities "as" --info "DRH"
$ fossil json user save --uid 1 --name richard \
 --password fossil \
 --info "Previously known as drh"
```

**Warnings:**

-   When creating a new user or renaming a user, if no (new) password is
    specified in the save request then the user will not be able to log
    in because the previous password (for existing users) is hashed
    against the old name.
-   Renaming a user invalidates any active login token because his old
    name is a part of the hash. i.e. the user must log back in with the
    new name after being renamed.

**TODOs:**

-   Login group support.
