"""Saves the updater's test fixtures from a GitHub repository's public releases.

usage: python tools/update_fixtures.py <owner/name> <out folder> [tag]

Writes, into the folder (src/SB/Core/pc/tests/update/ for tssm_updatetest):

  release-<tag>.json   GET https://api.github.com/repos/<owner>/<name>/releases/latest
                       (or /releases/tags/<tag>: the same JSON for an older release)
  SHA256SUMS-<tag>     that release's SHA256SUMS file

Read-only and unauthenticated, with the updater's own User-Agent. The tests read
the structure, not who published it, so the account's name becomes
"example-owner", its numeric id 1 and its node id "U_example", and the release
notes become a short text with the escapes JSON allows. Everything else is kept
as GitHub sent it, field order and unknown fields included.
"""

import json
import os
import re
import sys
import urllib.request

UA = "TSSM-port-updater"

# Quotes, a backslash, CR LF, a tab, and characters outside ASCII (one outside
# the Basic Multilingual Plane, which JSON writes as a surrogate pair).
NOTES = ("Release notes (replaced in the fixture).\r\n\r\n- \"quoted\", a\\backslash, "
         "a tab\t, é, — and \U0001F9FD\n")


def get(url, accept):
    req = urllib.request.Request(url, headers={"User-Agent": UA, "Accept": accept})
    with urllib.request.urlopen(req, timeout=30) as r:
        return r.read()


def main():
    if len(sys.argv) not in (3, 4) or sys.argv[1].count("/") != 1:
        sys.exit(__doc__)
    repo, out = sys.argv[1], sys.argv[2]
    owner = repo.split("/")[0]
    os.makedirs(out, exist_ok=True)

    which = "tags/" + sys.argv[3] if len(sys.argv) == 4 else "latest"
    raw = get("https://api.github.com/repos/%s/releases/%s" % (repo, which),
              "application/vnd.github+json")
    release = json.loads(raw)
    tag = release["tag_name"]

    ids = set()
    for who in [release.get("author")] + [a.get("uploader") for a in release.get("assets", [])]:
        if isinstance(who, dict):
            ids.add((who.get("id"), who.get("node_id")))
    release["body"] = NOTES

    text = json.dumps(release, indent=2, ensure_ascii=True)
    for num, node in ids:
        if num is not None:
            # The whole number only (its "id" field and the avatar link's
            # "/u/<id>"), never digits inside a longer one.
            text = re.sub(r"(?<![0-9])%d(?![0-9])" % num, "1", text)
        if node:
            text = text.replace(node, "U_example")
    text = text.replace(owner, "example-owner").replace(owner.lower(), "example-owner")
    with open(os.path.join(out, "release-%s.json" % tag), "w", encoding="utf-8",
              newline="\n") as f:
        f.write(text + "\n")

    sums = [a for a in release.get("assets", []) if a.get("name") == "SHA256SUMS"]
    if sums:
        data = get(sums[0]["browser_download_url"], "application/octet-stream")
        with open(os.path.join(out, "SHA256SUMS-%s" % tag), "wb") as f:
            f.write(data)
    print("saved %s (%s, %d files)%s" % (out, tag, len(release.get("assets", [])),
                                         "" if sums else "; the release has no SHA256SUMS"))


if __name__ == "__main__":
    main()
