# Every folder below mods/ with a CMakeLists.txt is a mod project (tools/new_mod.py creates them).
file(GLOB mod_dirs LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/*)
foreach(dir ${mod_dirs})
    if(IS_DIRECTORY ${dir} AND EXISTS ${dir}/CMakeLists.txt)
        add_subdirectory(${dir})
    endif()
endforeach()
