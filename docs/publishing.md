# Publishing a package

There is no registry and no account. A package is a git repository with an
`oak.json` at its root, or an archive at a URL. Publishing is tagging a release.

## A source package

```
oak-json/
  oak.json
  src/
    json.oak
    json/lexer.oak
  README.md
```

```json
{
  "name": "acme/oak-json",
  "version": "1.2.0",
  "module": "json",
  "src": "src",
  "oak": ">=1.0.0",
  "license": "MIT"
}
```

`module` is the namespace consumers import. It defaults to the tail of `name`,
so `acme/oak-json` would default to `oak-json` — which is not what you want
here, and is the usual reason to set it.

Tag the release with a `v` prefix:

```sh
git tag v1.2.0 && git push --tags
```

That is the whole process. Consumers write:

```json
"deps": { "json": "github:acme/oak-json@1.2.0" }
```

Two rules make this work for the people downstream:

- **Tags are immutable.** `oak-pkg install` resolves a tag to a commit once and
  locks the commit, so moving a tag does not change what an existing project
  builds — but it does change what the next person to add your package gets.
  Move a tag and the two disagree. Publish `1.2.1` instead.
- **Versions mean what semver says.** A breaking change is a major bump. Below
  `1.0.0`, a breaking change is a minor bump, because `^0.4.1` admits `0.4.x`
  and not `0.5.0`.

## An archive package

For code that is not on a git host, publish a `.tar.gz`, `.tar.xz`, `.tar.zst`
or `.zip` at a stable HTTPS URL. The archive is expected to wrap its contents in
a single directory, which is what `strip` defaults to removing.

```sh
tar czf oak-bits-0.9.1.tar.gz oak-bits-0.9.1/
sha256sum oak-bits-0.9.1.tar.gz
```

Publish the digest alongside the file. Consumers write:

```json
"deps": {
  "bits": {
    "url": "https://libs.example.org/oak-bits-0.9.1.tar.gz",
    "sha256": "9f86d081884c7d659a2feaa0c55ad015a3bf4f1b2b0b822cd15d6c15b0f00a08"
  }
}
```

A consumer who omits `sha256` gets the digest recorded on first fetch, and
verified from then on. Publishing it yourself is better: it means the first
fetch is checked too.

Never replace an archive at a URL that has already been published. The digest is
the identity; changing the bytes breaks every lockfile that names it, which is
the point.

## A native package

A native package ships a shared library plus the same bodyless `.oak` stubs the
standard library uses. The `.oak` file declares the interface, and the library
supplies the implementations.

```
acme-zlib/
  oak.json
  src/zlib.oak                        declarations, no bodies
  native/
    windows-x86_64/zlib.dll
    linux-x86_64/libzlib.so
    linux-aarch64/libzlib.so
    macos-aarch64/libzlib.dylib
```

```json
{
  "name": "acme/zlib",
  "version": "1.2.0",
  "module": "zlib",
  "src": "src",
  "native": { "abi": 1, "lib": "zlib", "dir": "native" }
}
```

The platform directory is `<os>-<arch>` — exactly what `oak --version` prints.
The filename comes from `lib` plus the platform's own convention (`lib` prefix,
`.so`/`.dylib`/`.dll` suffix), so you name the library once and every platform's
artifact is derived rather than listed.

Binaries ship **inside the package payload** — in the git repo, or in the
archive — so the commit or the archive digest already covers their integrity.
There is no second download and no separate hash to track.

### The stub

```oak
export fn compress(data : string, level : number) -> string;
export fn decompress(data : string) -> string;
```

No bodies. The compiler accepts this only for a module that has native bindings
registered under the same name, and checks that every declaration is actually
implemented — so a stub that has drifted from the library is a compile error
naming the function, not a crash at the call site.

### The library

One exported symbol, described by [`oak_plugin.h`](../include/oak_plugin.h):

```c
#include "oak_bind.h"
#include "oak_count_of.h"
#include "oak_plugin.h"
#include "oak_version.h"

static oak_fn_call_result_t compress_impl(oak_native_call_t* call,
                                          const oak_value_t* args,
                                          usize argc,
                                          oak_value_t* out_result)
{
  /* ... */
}

/* Static, because param_types is borrowed and must outlive compilation. */
static const oak_bind_type_ref_t compress_params[] = {
  OAK_BIND_SCALAR_INIT(OAK_TYPE_STRING),
  OAK_BIND_SCALAR_INIT(OAK_TYPE_NUMBER),
};

static int bind(oak_compile_options_t* opts)
{
  return oak_bind_fn_global(opts,
                            &(oak_bind_global_fn_t){
                                .module_name = "zlib",
                                .name = "compress",
                                .impl = compress_impl,
                                .return_type = OAK_BIND_SCALAR(OAK_TYPE_STRING),
                                .param_types = compress_params,
                                .param_count = OAK_COUNT_OF(compress_params),
                            });
}

static const oak_plugin_t plugin = {
  OAK_PLUGIN_ABI, "acme/zlib", "1.2.0", OAK_VERSION_STRING, bind,
};

OAK_PLUGIN_EXPORT const oak_plugin_t* oak_plugin_main(void)
{
  return &plugin;
}
```

`bind` is an ordinary binding function — the same code an embedder writes, moved
into a library that ships separately from the host. See
[Embedding](embedding-c.md) for the binding API itself.

Build it as a shared library against Oak's public headers and link `acorn`:

```sh
cc -shared -fPIC -I$(pkg-config --cflags-only-I oak) zlib_plugin.c \
   $(pkg-config --libs oak) -o native/linux-x86_64/libzlib.so
```

### What gets checked at load

- `abi` must equal the `OAK_PLUGIN_ABI` of the running Oak. A mismatch names
  both numbers and refuses to call anything, because the struct itself may not
  have the layout this build expects.
- `oak_version` must share a major with the running Oak — the binding structs
  are part of the ABI too.
- `name` must match the package that shipped the library. A mismatch means the
  wrong artifact was published.
- A missing binary for the running platform names the platform and lists the
  ones the package does ship.

`oak-pkg check` runs these against your cache before you publish, so a
stub/binding mismatch or a missing platform is caught by you rather than by a
consumer's `import`.

### Shipping both

A package may ship native binaries *and* work from source, but a consumer cannot
choose per-platform: if `native` is present, the library is required. If you
want a source fallback, publish it as a separate package.

## Checklist

```sh
oak-pkg check              # cache, versions, native libraries
oak examples/demo.oak      # it actually runs
git tag v1.2.0 && git push --tags
```

- `name` is `owner/package`, and `module` is what people will import.
- `version` matches the tag.
- `oak` states the runtime you actually tested against.
- The `src` directory contains a module named after `module`, so a bare
  `import <module>;` resolves.
