cmake_minimum_required(VERSION 3.16)
project(telefonema C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# O KIT Tools SDK vive no repositório principal (jcrvlh/kit), em tools-sdk/.
# Aponte KIT_SDK_PATH para esse diretório — por -D ou variável de ambiente.
#   cmake -B build -S . -DKIT_SDK_PATH=/caminho/para/kit/tools-sdk
if(NOT DEFINED KIT_SDK_PATH)
    set(KIT_SDK_PATH "$ENV{KIT_SDK_PATH}")
endif()
if(NOT KIT_SDK_PATH OR NOT EXISTS "${KIT_SDK_PATH}/include/kit_tool_api.h")
    message(FATAL_ERROR
        "KIT_SDK_PATH não encontrado. Passe -DKIT_SDK_PATH=<checkout de jcrvlh/kit>/tools-sdk")
endif()

# Biblioteca de stubs para build nativo (desktop / CI).
add_subdirectory("${KIT_SDK_PATH}/stubs" "${CMAKE_BINARY_DIR}/kit_stubs")

# --- Tool em modo nativo (stubs): só compila e exercita a lógica sem UI ------
# O binário de entrega (`tool.so`, Xtensa) sai de `kit-cli build --target
# xtensa`, que compila src/*.c direto — não passa por este CMake.
add_executable(tool.elf
    src/main.c
    "${KIT_SDK_PATH}/stubs/stub_main.c"
)
target_include_directories(tool.elf PRIVATE src)
target_link_libraries(tool.elf PRIVATE kit_stubs)
