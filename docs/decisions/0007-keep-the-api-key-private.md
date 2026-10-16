# 0007: Keep the API key private

Date: 2026-10-16
Status: accepted

## Context
The CurseForge key is personal and tied to the user's account. It was stored
in a world-readable `config.json`, sent on redirects to whatever host they
named, and the CLI only took it as a command-line argument, which shells keep
in their history file.

## Decision
- `config.json` is written atomically with mode `0600` in a `0700` folder,
  and loading an older, looser one tightens both.
- All HTTP is HTTPS only. Requests that carry the key (`HttpClient::get` and
  `post`) do not follow redirects; curl would forward the `x-api-key` header to
  the new host. Downloads follow redirects and never get the key.
- `wam config set-key` with no argument reads the key from stdin without echo.
- `tools/git-hooks/pre-commit` refuses a commit that adds the user's key.

## Consequences
If CurseForge ever answers an API call with a redirect, that call fails with
the 3xx status instead of following it. Do not turn redirects back on for
requests with the key; send the request to the new URL explicitly instead.
Tests check the file modes, the http refusal, and that downloads never carry
the key.
