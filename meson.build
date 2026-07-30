project(
  'oak',
  'c',
  version: '1.0.0',
  default_options: [
    'c_std=c17',
    'default_library=static',
    'warning_level=2',
  ],
)

cc = meson.get_compiler('c')
python = import('python').find_installation('python3')

is_msvc = cc.get_id() == 'msvc'
is_emscripten = cc.get_id() == 'emscripten'
is_debug = get_option('buildtype') == 'debug'
install_oak = get_option('install')

warning_args = is_msvc ? ['/wd4200'] : ['-Wno-unused-function']
debug_defines = is_debug ? ['-DOAK_TRACK_MEMORY', '-DOAK_DEBUG_LOGGING'] : []
platform_defines = is_msvc ? ['-D_CRT_SECURE_NO_WARNINGS'] : ['-D_XOPEN_SOURCE=700']
common_c_args = warning_args + debug_defines + platform_defines
common_c_args += ['-DOAK_ATOMIC_REFCOUNT']

yyjson_dep = dependency('yyjson')
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
  'src/vm',
  'src/debugger',
)
public_inc = include_directories('include')
test_inc = include_directories('tests/common')

oak_core_sources = files(
  'src/common/oak_allocator.c',
  'src/common/oak_arena.c',
  'src/common/oak_dynarr.c',
  'src/common/oak_file_map.c',
  'src/common/oak_htable.c',
  'src/common/oak_log.c',
  'src/common/oak_module_loader.c',
  'src/common/oak_module_loader_compile.c',
  'src/common/oak_module_loader_native.c',
  'src/common/oak_module_loader_path.c',
  'src/common/oak_net.c',
  'src/common/oak_run.c',
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

acorn_dep = declare_dependency(
  link_with: acorn,
  include_directories: public_inc,
  dependencies: [yyjson_dep, math_dep, socket_dep],
)

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
      '-sEXPORTED_FUNCTIONS=["_oak_run_wrapper","_oak_run_file_wrapper"]',
      '-sEXPORTED_RUNTIME_METHODS=["ccall","cwrap","FS"]',
    ],
  )
else
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
    dependencies: acorn_dep,
    install: install_oak,
    install_rpath: oak_install_rpath,
  )

  if install_oak
    install_subdir(
      'stdlib',
      install_dir: get_option('bindir'),
    )
  endif

  cli_parse_test = executable(
    'cli_parse',
    files('tests/cli/cli_parse.c', 'oak_cli.c'),
    include_directories: include_directories('.'),
    c_args: common_c_args,
  )
  test('cli_parse', cli_parse_test)

  test(
    'dap_integration',
    python,
    args: [
      files('tests/debugger/dap_test.py'),
      oak.full_path(),
      meson.project_source_root() / 'examples/04_functions/04_functions.oak',
    ],
  )

  test_sources = {
    'dynarr': 'tests/common/dynarr.c',
    'compiler_anon_fn': 'tests/compiler/compiler_anon_fn.c',
    'compiler_const_mut_binding': 'tests/compiler/compiler_const_mut_binding.c',
    'compiler_cycles': 'tests/compiler/compiler_cycles.c',
    'compiler_enum_access': 'tests/compiler/compiler_enum_access.c',
    'compiler_imports': 'tests/compiler/compiler_imports.c',
    'compiler_native_fn_binding': 'tests/compiler/compiler_native_fn_binding.c',
    'compiler_native_value_types': 'tests/compiler/compiler_native_value_types.c',
    'compiler_native_type_binding': 'tests/compiler/compiler_native_type_binding.c',
    'compiler_semantic_errors': 'tests/compiler/compiler_semantic_errors.c',
    'compiler_interfaces': 'tests/compiler/compiler_interfaces.c',
    'compiler_type_binding': 'tests/compiler/compiler_type_binding.c',
    'compiler_type_field_binding': 'tests/compiler/compiler_type_field_binding.c',
    'compiler_void_fn': 'tests/compiler/compiler_void_fn.c',
    'lexer_language': 'tests/lexer/lexer_language.c',
    'parser_language': 'tests/parser/parser_language.c',
    'stdlib_file': 'tests/stdlib/stdlib_file.c',
    'stdlib_file_missing': 'tests/stdlib/stdlib_file_missing.c',
    'vm_language_runtime': 'tests/vm/vm_language_runtime.c',
    'vm_math_builtins': 'tests/vm/vm_math_builtins.c',
    'vm_string_methods': 'tests/vm/vm_string_methods.c',
    'vm_map_table': 'tests/vm/vm_map_table.c',
    'vm_runtime_errors': 'tests/vm/vm_runtime_errors.c',
    'vm_short_circuit': 'tests/vm/vm_short_circuit.c',
    'vm_value_boxing': 'tests/vm/vm_value_boxing.c',
    'debugger_test': 'tests/debugger/debugger_test.c',
  }

  foreach test_name, test_src : test_sources
    test_exe = executable(
      test_name,
      files(test_src),
      include_directories: [src_inc, test_inc],
      c_args: common_c_args,
      dependencies: acorn_dep,
    )
    test(test_name, test_exe)
  endforeach

  # C++ wrapper test
  add_languages('cpp', native: false, required: true)
  cxx = meson.get_compiler('cpp')
  is_msvc_cxx = cxx.get_id() == 'msvc'
  cpp_warning_args = is_msvc_cxx ? ['/wd4200'] : ['-Wno-unused-function']
  cpp_std_args = is_msvc_cxx ? ['/std:c++20', '/EHs-c-', '/GR-'] : ['-std=c++20', '-fno-exceptions', '-fno-rtti']
  cpp_args = cpp_warning_args + cpp_std_args + debug_defines + platform_defines + ['-DOAK_ATOMIC_REFCOUNT', stdlib_dir_define]

  test_oak_hpp = executable(
    'test_oak_hpp',
    files('tests/cpp/test_oak_hpp.cpp'),
    include_directories: [src_inc, public_inc],
    cpp_args: cpp_args,
    dependencies: acorn_dep,
  )
  test('test_oak_hpp', test_oak_hpp)

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
