add_executable(oak_wasm ${OAK_WASM_DIR}/oak_wasm_entry.c)

target_link_libraries(oak_wasm PRIVATE acorn)
target_include_directories(oak_wasm PRIVATE ${OAK_SRC_INCLUDE_DIRS})

target_link_options(oak_wasm PRIVATE
    "-sWASM=1"
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=OakModule"
    "-sSTACK_SIZE=1048576"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sEXPORTED_FUNCTIONS=['_oak_run_wrapper','_oak_run_file_wrapper']"
    "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','FS']"
)

set_target_properties(oak_wasm PROPERTIES SUFFIX ".js")
