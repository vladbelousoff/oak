project(
  'oak',
  'c',
  version: '1.0.0',
  default_options: [
    'c_std=c17',
    'default_library=static',
    'prefix=~/.local',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')
python = import('python').find_installation('python3')

is_msvc = cc.get_id() == 'msvc'
is_emscripten = cc.get_id() == 'emscripten'
is_debug = get_option('buildtype') == 'debug'
is_release = get_option('buildtype') == 'release'
install_oak = get_option('install')

# Promote two diagnostics to errors. Both are silent-corruption bugs in C, not
# style issues: an implicitly declared function links against whatever symbol
# happens to match, and an incompatible pointer assignment reinterprets memory.
# Passing the wrong pointer type to a void*-taking API (or calling a function
# whose header is no longer included) otherwise compiles and fails at runtime.
strict_args = is_msvc ? ['/we4013', '/we4133'] : [
  '-Werror=implicit-function-declaration',
  '-Werror=incompatible-pointer-types',
]
warning_args = (is_msvc ? ['/wd4200'] : ['-Wno-unused-function']) + strict_args
debug_defines = is_debug ? ['-DOAK_TRACK_MEMORY', '-DOAK_DEBUG_LOGGING'] : []
platform_defines = is_msvc ? ['-D_CRT_SECURE_NO_WARNINGS'] : ['-D_XOPEN_SOURCE=700']
release_symbol_args = []
if is_release
  release_symbol_args = is_msvc ? ['/Z7'] : ['-g']
endif
common_c_args = warning_args + debug_defines + platform_defines + release_symbol_args
if get_option('atomic_refcount')
  common_c_args += ['-DOAK_ATOMIC_REFCOUNT']
endif

if is_release and is_msvc
  add_project_link_arguments('/DEBUG', language: 'c')
endif

if is_msvc
  # MSVC reports these level-1 diagnostics in mimalloc even at
  # warning_level=0. Keep them quiet in bundled C subprojects.
  add_global_arguments('/wd4024', '/wd4047', language: 'c')
endif

# Keep bundled third-party code quiet without lowering the warning level for
# Oak itself. Meson's CMake dependency probe also emits spurious
# generator-expression warnings with recent MSVC toolchains, so use the bundled
# yyjson dependency directly there.
subproject_default_options = [
  'warning_level=0',
]
if is_msvc
  yyjson_subproject = subproject(
    'yyjson',
    default_options: subproject_default_options,
  )
  yyjson_dep = yyjson_subproject.get_variable('yyjson_dep')
else
  yyjson_dep = dependency(
    'yyjson',
    default_options: subproject_default_options,
  )
endif
math_dep = cc.find_library('m', required: false)
socket_dep = host_machine.system() == 'windows' ? cc.find_library('ws2_32') : declare_dependency()

# Source-tree stdlib path, baked in as a development fallback.
stdlib_dir_define = '-DOAK_STDLIB_SOURCE_DIR="' + meson.current_source_dir() / 'stdlib' + '"'

# Executable-relative install paths. The CLI is in <prefix>/<bindir>; the
# stdlib is installed next to it, and the library is under <prefix>/<libdir>.
# The library rpath is resolved relative to the executable, which needs the
# climb from bindir back to the prefix. bindir may be multi-component (e.g.
# 'libexec/oak'), so count its segments rather than assuming a single '..'. An
# absolute bindir has no executable-relative meaning, so reject it when
# installing.
if install_oak and get_option('bindir').startswith('/')
  error('install requires a relative bindir for executable-relative library lookup; got "' + get_option('bindir') + '"')
endif
# Climb (sequence of '..') from bindir back to the prefix, one per real path
# segment. Empty when bindir is '.' (the executable sits directly in the prefix).
bindir_climb = ''
foreach part : get_option('bindir').split('/')
  if part != '' and part != '.'
    bindir_climb = bindir_climb == '' ? '..' : bindir_climb + '/..'
  endif
endforeach
# Prefix used to turn a prefix-relative subdir into an executable-relative one.
# '' when the executable is already at the prefix root, else '<climb>/'.
exe_to_prefix = bindir_climb == '' ? '' : bindir_climb + '/'

# Installed stdlib path RELATIVE to the executable's directory. The loader
# resolves it against the running executable's
# location, so an installed tree is relocatable and a build-tree binary (which
# has no co-located stdlib) never picks up an install prefix. These defines must
# reach the module loader, which is compiled into the acorn library (not the oak
# CLI), so they belong in common_c_args rather than on the executable alone.
stdlib_install_relpath = 'stdlib'
stdlib_install_define = '-DOAK_STDLIB_INSTALL_RELPATH="' + stdlib_install_relpath + '"'
common_c_args += [stdlib_dir_define, stdlib_install_define]

src_inc = include_directories(
  'src',
  'src/common',
  'src/runtime',
  'src/lexer',
  'src/parser',
  'src/compiler',
  'src/stdlib',
  'src/vm',
  'src/debugger',
)
public_inc = include_directories('include')
test_inc = include_directories('tests/support')

# The machine-readable definition of "public". Listed explicitly rather than
# globbed so that adding a header to include/ without publishing it here is a
# visible omission in review. Every header below must be self-contained under
# -Iinclude alone; the public_api test target at the bottom of this file is the
# guard that keeps it that way.
oak_public_headers = files(
  'include/oak_allocator.h',
  'include/oak_bind.h',
  'include/oak_chunk.h',
  'include/oak_compiler.h',
  'include/oak_container.h',
  'include/oak_count_of.h',
  'include/oak_diagnostic.h',
  'include/oak_export.h',
  'include/oak_hash_map.h',
  'include/oak_hash_set.h',
  'include/oak_lexer.h',
  'include/oak_list.h',
  'include/oak_module.h',
  'include/oak_module_loader.h',
  'include/oak_native.h',
  'include/oak_object.h',
  'include/oak_parser.h',
  'include/oak_program.h',
  'include/oak_refcount.h',
  'include/oak_stdlib.h',
  'include/oak_stdlib_file.h',
  'include/oak_token.h',
  'include/oak_type_id.h',
  'include/oak_type_kind.h',
  'include/oak_types.h',
  'include/oak_value.h',
  'include/oak_vector.h',
  'include/oak_vm.h',
)

oak_core_sources = files(
  'src/common/oak_allocator.c',
  'src/common/oak_arena.c',
  'src/common/oak_container.c',
  'src/common/oak_diagnostic.c',
  'src/common/oak_program.c',
  'src/common/oak_file_map.c',
  'src/common/oak_hash_map.c',
  'src/common/oak_hash_set.c',
  'src/common/oak_hash_table.c',
  'src/common/oak_log.c',
  'src/common/oak_module_loader.c',
  'src/common/oak_module_loader_compile.c',
  'src/common/oak_module_loader_native.c',
  'src/common/oak_module_loader_path.c',
  'src/common/oak_net.c',
  'src/common/oak_object.c',
  'src/common/oak_vector.c',
  'src/compiler/oak_compiler.c',
  'src/compiler/oak_compiler_builtins.c',
  'src/compiler/oak_compiler_call_arg.c',
  'src/compiler/oak_compiler_call_method.c',
  'src/compiler/oak_compiler_calls.c',
  'src/compiler/oak_compiler_diagnostics.c',
  'src/compiler/oak_compiler_method_builtins.c',
  'src/compiler/oak_compiler_method_table.c',
  'src/compiler/oak_compiler_emit.c',
  'src/compiler/oak_compiler_attrs.c',
  'src/compiler/oak_compiler_cycles.c',
  'src/compiler/oak_compiler_enums.c',
  'src/compiler/oak_compiler_expr.c',
  'src/compiler/oak_compiler_expr_assign.c',
  'src/compiler/oak_compiler_expr_binary.c',
  'src/compiler/oak_compiler_expr_collection.c',
  'src/compiler/oak_compiler_expr_fn.c',
  'src/compiler/oak_compiler_expr_record.c',
  'src/compiler/oak_compiler_expr_member.c',
  'src/compiler/oak_compiler_fn_argcheck.c',
  'src/compiler/oak_compiler_fn_body.c',
  'src/compiler/oak_compiler_fn_decl.c',
  'src/compiler/oak_compiler_fn_register.c',
  'src/compiler/oak_compiler_modules.c',
  'src/compiler/oak_compiler_record_registry.c',
  'src/compiler/oak_compiler_records.c',
  'src/compiler/oak_compiler_records_native.c',
  'src/compiler/oak_compiler_scope.c',
  'src/compiler/oak_compiler_symbols.c',
  'src/compiler/oak_compiler_imports.c',
  'src/compiler/oak_compiler_lifecycle.c',
  'src/compiler/oak_compiler_pipeline.c',
  'src/compiler/oak_compiler_stmt.c',
  'src/compiler/oak_compiler_interfaces.c',
  'src/compiler/oak_compiler_stmt_for.c',
  'src/compiler/oak_compiler_type_infer.c',
  'src/compiler/oak_compiler_type_infer_call.c',
  'src/compiler/oak_compiler_types.c',
  'src/compiler/oak_compiler_void_diag.c',
  'src/lexer/oak_lexer.c',
  'src/lexer/oak_lexer_buffer.c',
  'src/lexer/oak_token.c',
  'src/parser/oak_parser.c',
  'src/parser/oak_parser_ast.c',
  'src/parser/oak_parser_grammar.c',
  'src/parser/oak_parser_pratt.c',
  'src/parser/oak_parser_rules.c',
  'src/runtime/oak_bind.c',
  'src/runtime/oak_chunk.c',
  'src/runtime/oak_module.c',
  'src/runtime/oak_symbol.c',
  'src/runtime/oak_type.c',
  'src/runtime/oak_value.c',
  'src/runtime/oak_value_json.c',
  'src/runtime/oak_value_repr.c',
  'src/stdlib/oak_stdlib.c',
  'src/stdlib/oak_stdlib_file.c',
  'src/stdlib/oak_stdlib_math.c',
  'src/stdlib/oak_stdlib_string.c',
  'src/vm/oak_vm.c',
  'src/vm/oak_vm_arith.c',
  'src/vm/oak_vm_call.c',
  'src/vm/oak_vm_error.c',
  'src/vm/oak_vm_native.c',
  'src/vm/oak_vm_object.c',
  'src/debugger/oak_debugger.c',
  'src/debugger/oak_dap.c',
)

if is_emscripten
  acorn = static_library(
    'acorn',
    oak_core_sources,
    include_directories: [public_inc, src_inc],
    c_args: common_c_args + ['-DOAK_STATIC'],
    dependencies: [yyjson_dep, math_dep, socket_dep],
  )
else
  acorn = shared_library(
    'acorn',
    oak_core_sources,
    include_directories: [public_inc, src_inc],
    c_args: common_c_args + ['-DOAK_BUILDING_ACORN'],
    dependencies: [yyjson_dep, math_dep, socket_dep],
    gnu_symbol_visibility: 'hidden',
    version: meson.project_version(),
    install: install_oak,
  )
endif

# include_directories is public_inc ONLY, deliberately. Adding src_inc here
# would let in-repo targets compile against private headers without saying so,
# and would silently break every installed consumer -- which is exactly how the
# public headers came to depend on src/ in the first place. The public_api test
# target below is the guard.
acorn_dep = declare_dependency(
  link_with: acorn,
  include_directories: public_inc,
  dependencies: [yyjson_dep, math_dep, socket_dep],
)

if install_oak
  install_headers(oak_public_headers, subdir: 'oak')
endif

if is_emscripten
  executable(
    'oak_wasm',
    files('wasm/oak_wasm_entry.c'),
    name_suffix: 'js',
    include_directories: src_inc,
    c_args: common_c_args,
    dependencies: acorn_dep,
    link_args: [
      '-sWASM=1',
      '-sMODULARIZE=1',
      '-sEXPORT_NAME=OakModule',
      '-sSTACK_SIZE=1048576',
      '-sALLOW_MEMORY_GROWTH=1',
      '-sEXPORTED_FUNCTIONS=["_oak_run_file_wrapper"]',
      '-sEXPORTED_RUNTIME_METHODS=["ccall","cwrap","FS"]',
    ],
  )
else
  if is_msvc
    mimalloc_subproject = subproject(
      'mimalloc',
      default_options: subproject_default_options,
    )
    mimalloc_dep = mimalloc_subproject.get_variable('mi_dep')
  else
    mimalloc_dep = dependency(
      'mimalloc',
      default_options: subproject_default_options,
    )
  endif

  # The installed CLI links the shared acorn library, which installs to
  # <prefix>/libdir. For a non-system prefix the dynamic loader will not find it
  # via PATH alone, so bake a relocatable install rpath pointing from the
  # executable (in <prefix>/bindir) to the library, using the same bindir climb
  # as the stdlib path so non-default bindir/libdir layouts stay correct. Windows
  # has no rpath and finds acorn.dll next to oak.exe in bindir, so leave it empty.
  oak_install_rpath = ''
  if host_machine.system() == 'darwin'
    oak_install_rpath = '@loader_path/' + exe_to_prefix + get_option('libdir')
  elif host_machine.system() != 'windows'
    oak_install_rpath = '$ORIGIN/' + exe_to_prefix + get_option('libdir')
  endif

  oak = executable(
    'oak',
    files('oak.c', 'oak_cli.c'),
    include_directories: [include_directories('.'), src_inc],
    c_args: common_c_args,
    dependencies: [acorn_dep, mimalloc_dep],
    install: install_oak,
    install_rpath: oak_install_rpath,
  )

  if install_oak
    install_subdir(
      'stdlib',
      install_dir: get_option('bindir'),
    )

    # subdirs: 'oak' emits -I${includedir}/oak to match install_headers above,
    # so `#include "oak_bind.h"` resolves identically in-tree and installed.
    #
    # No -DOAK_STATIC here: acorn is unconditionally a shared_library in this
    # branch (the static build is the emscripten one, which installs nothing),
    # so a consumer needs the default __declspec(dllimport) path on Windows.
    # Do NOT key this on default_library -- that option is 'static' project-wide
    # for the bundled subprojects and says nothing about how acorn was built.
    # yyjson is absorbed statically and so needs no Requires.
    import('pkgconfig').generate(
      acorn,
      name: 'oak',
      filebase: 'oak',
      description: 'Oak language runtime (acorn)',
      subdirs: 'oak',
      version: meson.project_version(),
      libraries_private: [math_dep, socket_dep],
    )
  endif

  # The guard for the public API. Two things make it real rather than
  # ceremonial: it takes no include_directories, so -Iinclude (from acorn_dep)
  # is the entire header search path; and it uses warning_args rather than
  # common_c_args, so it compiles with zero -DOAK_* flags while the library
  # itself is built with them. That makes it a live check that no public header
  # keys on a build define -- a layout mismatch in oak_obj_t would show up here
  # as corruption or a leak report at shutdown.
  public_api_smoke = executable(
    'oak_public_api_smoke',
    files('tests/public_api/oak_embed_smoke.c'),
    c_args: warning_args,
    dependencies: acorn_dep,
  )
  test('public_api', public_api_smoke)

  test(
    'dap_integration',
    python,
    args: [
      files('tests/dap_test.py'),
      oak.full_path(),
      meson.project_source_root() / 'examples/04_functions/04_functions.oak',
    ],
  )

  # One suite per file; the utest.h suite name inside each file matches its
  # basename minus the _test suffix, which is what the --filter below selects.
  test_suites = [
    'lexer',
    'utf8',
    'compiler_semantics',
    'compiler_types',
    'compiler_mut',
    'compiler_cycles',
    'compiler_interfaces',
    'vm_errors',
    'vm_exec',
    'vm_value',
    'vm_map',
    'cli',
    'allocator',
    'containers',
    'parser',
    'debugger',
    'bind_fn',
    'bind_type',
    'module_loader',
  ]

  test_suite_sources = []
  foreach suite : test_suites
    test_suite_sources += files('tests/suites/' + suite + '_test.c')
  endforeach

  # utest.h registers every test through a constructor, so all suites live in
  # one binary with a single main(). oak_cli.c is linked in for the cli suite;
  # it declares no main() of its own (that is in oak.c).
  oak_tests = executable(
    'oak_tests',
    files('tests/main_test.c', 'tests/support/oak_test_support.c') +
    test_suite_sources + files('oak_cli.c'),
    include_directories: [
      src_inc,
      test_inc,
      include_directories('tests/third_party'),
      include_directories('.'),
    ],
    # The module loader is the one subsystem that cannot be exercised without
    # real files on disk, so its suite reads the multi-module example from the
    # source tree. Same pattern as OAK_STDLIB_SOURCE_DIR above; nothing is
    # written, so the "tests do no filesystem I/O" rule still holds for output.
    c_args: common_c_args +
      ['-DOAK_TEST_EXAMPLES_DIR="' + meson.current_source_dir() / 'examples' + '"',
       '-DOAK_TEST_FIXTURES_DIR="' + meson.current_source_dir() / 'tests/suites/fixtures' + '"'],
    dependencies: acorn_dep,
  )

  # Register each suite separately so meson reports them individually and a
  # crash in one suite cannot take the rest of the run down with it.
  foreach suite : test_suites
    test(suite, oak_tests, args: ['--filter=' + suite + '.*'])
  endforeach

  example_sources = {
    '01_values': 'examples/01_values/01_values.oak',
    '02_control_flow': 'examples/02_control_flow/02_control_flow.oak',
    '03_collections': 'examples/03_collections/03_collections.oak',
    '04_functions': 'examples/04_functions/04_functions.oak',
    '05_records_enums': 'examples/05_records_enums/05_records_enums.oak',
    '06_modules': 'examples/06_modules/06_modules.oak',
    '07_algorithms': 'examples/07_algorithms/07_algorithms.oak',
    '08_file_io': 'examples/08_file_io/08_file_io.oak',
    '10_interfaces': 'examples/10_interfaces/10_interfaces.oak',
    '11_weak_refs': 'examples/11_weak_refs/11_weak_refs.oak',
    '12_strings': 'examples/12_strings/12_strings.oak',
    '13_anonymous_functions': 'examples/13_anonymous_functions/13_anonymous_functions.oak',
  }

  foreach example_name, script : example_sources
    test(
      'example_' + example_name,
      oak,
      args: ['--track-memory', files(script)],
      workdir: meson.project_source_root(),
    )
  endforeach

  test(
    'example_09_diagnostics',
    python,
    args: [
      files('tests/check_expected_error.py'),
      oak.full_path(),
      meson.project_source_root() / 'examples/09_diagnostics/09_diagnostics.oak',
      meson.project_source_root() / 'examples/09_diagnostics/09_diagnostics.expected_error',
      meson.project_source_root(),
    ],
  )
endif
