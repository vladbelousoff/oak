set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    set(OAK_WARN_FLAGS /W3 /wd4200)
else()
    set(OAK_WARN_FLAGS -Wall -Wextra -Wno-unused-function)
endif()

set(OAK_DEBUG_DEFINES
    $<$<CONFIG:Debug>:OAK_TRACK_MEMORY>
    $<$<CONFIG:Debug>:OAK_DEBUG_LOGGING>
)
