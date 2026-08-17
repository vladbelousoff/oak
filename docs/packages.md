# Packages

A package is a directory with an `oak.json` in it. Point a dependency at one and
its modules become importable by name, from anywhere in your program, without
copying files into your tree.

Two commands do the work. `oak-pkg` resolves and fetches; `oak` reads the
lockfile and runs. That split is the whole design: running a program never
touches the network and never decides a version.

```sh
oak-pkg init --name me/demo
oak-pkg add github:acme/oak-json@1.2.0
oak src/app.oak
```

## A project

```json
{
  "name": "me/demo",
  "version": "1.0.0",
  "module": "app",
  "src": "src",
  "oak": ">=1.0.0",
  "deps": {
    "greet": { "path": "vendor/greet" },
    "json": "github:acme/oak-json@1.2.0"
  }
}
```

| Field | Meaning |
|---|---|
| `name` | `owner/package`. Required. |
| `version` | Full `MAJOR.MINOR.PATCH`. Required — `1.2` is rejected rather than guessed at. |
| `module` | The import namespace this package claims. Defaults to `name` without its owner, so `acme/json` is imported as `json`. |
| `src` | Directory that module names resolve against, relative to the manifest. Defaults to `.`. |
| `oak` | Runtime this package needs: `*`, `1.2.3`, `^1.2.3` or `>=1.2.3`. |
| `license` | Free text. Optional. |
| `deps` | Import alias → source. |
| `native` | Only for packages shipping a compiled library. See [Publishing](publishing.md). |

Unknown fields are ignored, and `oak-pkg add` preserves them when it rewrites
the file — so a manifest written for a newer oak-pkg does not lose configuration
when someone on an older one edits it.

## How a name becomes a file

Exactly the rule the standard library already follows: the package contributes a
directory, and the whole dotted name resolves under it.

```
vendor/greet/            oak.json  ("name": "example/greet")
vendor/greet/src/
  greet.oak              import greet;
  greet/polite.oak       import greet.polite;
```

The first segment of an import is looked up in the importing package's own
dependency list. If it matches, resolution continues inside that dependency; if
it does not, the import resolves relative to the importing file, as it always
has. So adding a dependency cannot change the meaning of an import that already
worked.

A dependency's own dependencies are **not** visible to you. Two packages can
depend on different things under the same name without colliding.

Renaming is just the alias:

```json
"deps": { "j": "github:acme/oak-json@1.2.0" }
```

```oak
import { parse } from j.lexer;
```

The exception is a package that ships a native library. Its bindings are
registered once, under the name the library was built with, so it has to be
imported under its own module name. Renaming one is refused with a message
saying so rather than failing later as a stub with no implementations.

## Dependency sources

```json
"deps": {
  "json":  "github:acme/oak-json@1.2.0",
  "utf8":  { "git": "https://example.org/utf8.git", "tag": "v0.4.0" },
  "pinned":{ "git": "https://example.org/x.git", "rev": "a1b2c3d4..." },
  "bits":  { "url": "https://example.org/bits-0.9.1.tar.gz", "sha256": "9f86…" },
  "local": { "path": "../mylib" }
}
```

- `github:owner/repo@1.2.0` expands to that repository at tag `v1.2.0`, and asks
  for `^1.2.0` so a transitive dependent can raise the version without editing
  your manifest. `gitlab:` works the same way.
- A git dependency must pin a `tag` or a `rev`. A bare branch is rejected: it is
  not reproducible. URLs may be `https`, `ssh`, or `file` — the last for local
  mirrors and air-gapped checkouts.
- An archive URL must be `https`, and its `sha256` is verified before anything
  is unpacked. `strip` (default `1`) drops leading directory components, which
  is what release tarballs need.
- A `path` dependency is never fetched, hashed, or locked. Its own dependencies
  still join the graph; it is whatever is on disk.

Any dependency may also carry `"version"` — `*`, `1.2.3`, `^1.2.3` or `>=1.2.3`
— which is what the shorthand sets for you.

## The commands

| Command | What it does |
|---|---|
| `oak-pkg init [--name <owner/pkg>] [--src <dir>]` | Write a new `oak.json` here |
| `oak-pkg add <spec> [--as <alias>] [--tag/--rev/--sha256/--strip]` | Fetch it, add it, re-lock |
| `oak-pkg remove <alias>` | Drop it and re-lock |
| `oak-pkg install` | Fetch what the manifest asks for; honour the existing lock |
| `oak-pkg update` | Re-resolve, taking tags as they stand today |
| `oak-pkg tree` | Print the resolved graph |
| `oak-pkg check` | Verify the lock, the cache, and any native libraries |
| `oak-pkg cache path` / `cache clean` | Where downloads live; remove them |

`install` and `update` differ in exactly one way. `install` reuses the commit
already recorded in `oak.lock`, so a moved tag cannot change what you build.
`update` ignores it and re-reads each tag. Everything that edits the manifest
re-locks immediately, so the two files never drift apart.

`oak-pkg add` on an archive URL with no `--sha256` records the digest it
received and writes it into both files — trust on first use, but written down,
so every later fetch is verified.

## The lockfile

`oak.lock` records the exact commit or digest chosen for every fetched
dependency. Commit it. `oak` reads only the lockfile — never the network, never
a resolver — so running a program is deterministic and works offline.

```json
{
  "lock": 1,
  "packages": [
    {
      "name": "acme/json",
      "version": "1.2.0",
      "module": "json",
      "src": "src",
      "source": { "git": "https://github.com/acme/oak-json.git", "rev": "a1b2…" }
    }
  ]
}
```

A project with only `path` dependencies needs no lockfile at all.

## The cache

Fetched packages are shared across every project on the machine, addressed by
what they contain:

```
~/.oak/packages/git/<name>-<commit>/
~/.oak/packages/archive/<name>-<sha256>/
```

A directory that exists is by construction the right bytes, so nothing is ever
invalidated and two projects on the same version share one copy. A fetch stages
in a temporary directory and is renamed into place only once it is complete, so
an interrupted download never leaves a half-populated entry. Nothing is written
into your project. Set `OAK_PACKAGE_CACHE` to move it.

## Version selection

One version per package name, chosen as the highest any dependent asks for.
There is no backtracking search, and a dependency publishing a release cannot
change what you build — only the lockfile decides that.

Two incompatible majors of the same package is an error rather than a
double-load. Oak's export namespaces and per-module type identities make two
copies of a type genuinely unsafe, so it is refused rather than papered over.
Below 1.0.0 the minor plays the major's role, matching how `^0.4.1` reads.

## Environment

| Variable | Effect |
|---|---|
| `OAK_PACKAGE_CACHE` | Where fetched packages are kept |
| `OAK_GIT` | The git binary `oak-pkg` runs |
| `OAK_OFFLINE` | Refuse to use the network; a cache miss becomes an error |

## Running

Nothing to configure. `oak` looks for `oak.json` beside your program and in each
parent directory, up to 32 levels:

```sh
oak src/app.oak
```

A script with no manifest above it is unaffected — imports resolve exactly as
they did before packages existed.

`oak --no-plugins` refuses to load any package's native library. A graph that
needs one then fails rather than running without it, because a native package's
declarations have no bodies without its library.

## Builds without HTTPS

Archive dependencies need libcurl and libarchive. A build configured with
`-Dhttps=disabled`, or one where neither library was found, still handles git
and path dependencies completely; an archive dependency reports that this
`oak-pkg` cannot fetch it. `-Dpkg=false` skips the tool entirely — `oak` still
reads `oak.lock` either way.

See [`examples/14_packages`](../examples/14_packages) for a complete working
project, [Publishing](publishing.md) for shipping one, and
[Embedding](embedding-c.md) for the C API (`oak_package.h`) behind all of this.
