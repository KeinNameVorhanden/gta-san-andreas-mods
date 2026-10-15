# {{NAME}}: built as a SACore mod DLL (C++20, x86).
add_library({{ID}} SHARED src/{{ID}}.cpp mod.toml)
sacore_target({{ID}})
target_link_libraries({{ID}} PRIVATE sacore_api)
