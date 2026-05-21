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

warning_args = is_msvc ? ['/wd4200'] : ['-Wno-unused-function']
debug_defines = is_debug ? ['-DOAK_TRACK_MEMORY', '-DOAK_DEBUG_LOGGING'] : []
platform_defines = is_msvc ? ['-D_CRT_SECURE_NO_WARNINGS'] : ['-D_XOPEN_SOURCE=700']
common_c_args = warning_args + debug_defines + platform_defines
common_c_args += ['-DOAK_ATOMIC_REFCOUNT']

yyjson_dep = dependency('yyjson')
math_dep = cc.find_library('m', required: false)

stdlib_dir_define = '-DOAK_STDLIB_DIR="' + meson.current_source_dir() / 'stdlib' + '"'
common_c_args += [stdlib_dir_define]

src_inc = include_directories(
  'src',
  'src/common',
  'src/runtime',
  'src/lexer',
  'src/parser',
  'src/compiler',
  'src/vm',
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
  'src/common/oak_run.c',
  'src/compiler/oak_compiler.c',
  'src/compiler/oak_compiler_builtins.c',
  'src/compiler/oak_compiler_call_arg.c',
  'src/compiler/oak_compiler_call_method.c',
  'src/compiler/oak_compiler_calls.c',
  'src/compiler/oak_compiler_method_builtins.c',
  'src/compiler/oak_compiler_method_table.c',
  'src/compiler/oak_compiler_emit.c',
  'src/compiler/oak_compiler_attrs.c',
  'src/compiler/oak_compiler_enums.c',
  'src/compiler/oak_compiler_expr.c',
  'src/compiler/oak_compiler_expr_assign.c',
  'src/compiler/oak_compiler_expr_binary.c',
  'src/compiler/oak_compiler_expr_collection.c',
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
  'src/compiler/oak_compiler_imports.c',
  'src/compiler/oak_compiler_stmt.c',
  'src/compiler/oak_compiler_traits.c',
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
  'src/runtime/oak_type.c',
  'src/runtime/oak_value.c',
  'src/runtime/oak_value_json.c',
  'src/runtime/oak_value_repr.c',
  'src/stdlib/oak_stdlib.c',
  'src/stdlib/oak_stdlib_file.c',
  'src/stdlib/oak_stdlib_math.c',
  'src/vm/oak_vm.c',
  'src/vm/oak_vm_arith.c',
  'src/vm/oak_vm_call.c',
  'src/vm/oak_vm_error.c',
  'src/vm/oak_vm_object.c',
)

if is_emscripten
  acorn = static_library(
    'acorn',
    oak_core_sources,
    include_directories: [public_inc, src_inc],
    c_args: common_c_args + ['-DOAK_STATIC'],
    dependencies: [yyjson_dep, math_dep],
  )
else
  acorn = shared_library(
    'acorn',
    oak_core_sources,
    include_directories: [public_inc, src_inc],
    c_args: common_c_args + ['-DOAK_BUILDING_ACORN'],
    dependencies: [yyjson_dep, math_dep],
    gnu_symbol_visibility: 'hidden',
  )
endif

acorn_dep = declare_dependency(
  link_with: acorn,
  include_directories: public_inc,
  dependencies: [yyjson_dep, math_dep],
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
  oak = executable(
    'oak',
    files('oak.c', 'oak_cli.c'),
    include_directories: [include_directories('.'), src_inc],
    c_args: common_c_args,
    dependencies: acorn_dep,
  )

  test_sources = {
    'compiler_const_mut_binding': 'tests/compiler/compiler_const_mut_binding.c',
    'compiler_enum_access': 'tests/compiler/compiler_enum_access.c',
    'compiler_native_fn_binding': 'tests/compiler/compiler_native_fn_binding.c',
    'compiler_native_type_binding': 'tests/compiler/compiler_native_type_binding.c',
    'compiler_semantic_errors': 'tests/compiler/compiler_semantic_errors.c',
    'compiler_traits': 'tests/compiler/compiler_traits.c',
    'compiler_type_binding': 'tests/compiler/compiler_type_binding.c',
    'compiler_type_field_binding': 'tests/compiler/compiler_type_field_binding.c',
    'compiler_void_fn': 'tests/compiler/compiler_void_fn.c',
    'lexer_language': 'tests/lexer/lexer_language.c',
    'parser_language': 'tests/parser/parser_language.c',
    'stdlib_file': 'tests/stdlib/stdlib_file.c',
    'stdlib_file_missing': 'tests/stdlib/stdlib_file_missing.c',
    'vm_language_runtime': 'tests/vm/vm_language_runtime.c',
    'vm_runtime_errors': 'tests/vm/vm_runtime_errors.c',
    'vm_short_circuit': 'tests/vm/vm_short_circuit.c',
    'vm_value_boxing': 'tests/vm/vm_value_boxing.c',
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

  example_sources = {
    '01_values': 'examples/01_values/01_values.oak',
    '02_control_flow': 'examples/02_control_flow/02_control_flow.oak',
    '03_collections': 'examples/03_collections/03_collections.oak',
    '04_functions': 'examples/04_functions/04_functions.oak',
    '05_records_enums': 'examples/05_records_enums/05_records_enums.oak',
    '06_modules': 'examples/06_modules/06_modules.oak',
    '07_algorithms': 'examples/07_algorithms/07_algorithms.oak',
    '08_file_io': 'examples/08_file_io/08_file_io.oak',
    '10_traits': 'examples/10_traits/10_traits.oak',
    '11_weak_refs': 'examples/11_weak_refs/11_weak_refs.oak',
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
