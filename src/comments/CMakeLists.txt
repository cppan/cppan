#
# cppan
#

########################################
# extract_comments
########################################

if (FLEX_FOUND AND BISON_FOUND)
    BISON_TARGET(grammar
        "${CMAKE_CURRENT_SOURCE_DIR}/comments.yy"
        "${CMAKE_CURRENT_BINARY_DIR}/comments.yy.cpp"
        COMPILE_FLAGS "--defines=${CMAKE_CURRENT_BINARY_DIR}/comments.yy.hpp"
    )
    FLEX_TARGET(lexer
        "${CMAKE_CURRENT_SOURCE_DIR}/comments.ll"
        "${CMAKE_CURRENT_BINARY_DIR}/comments.ll.cpp"
        COMPILE_FLAGS "--header-file=${CMAKE_CURRENT_BINARY_DIR}/lexer.h -L"
    )
    ADD_FLEX_BISON_DEPENDENCY(lexer grammar)

    set(parser_src
        ${BISON_grammar_OUTPUTS}
        ${FLEX_lexer_OUTPUTS}
        ${CMAKE_CURRENT_BINARY_DIR}/lexer.h
    )
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/lexer.h PROPERTIES GENERATED TRUE)

    set(builder_src
        driver.cpp
        driver.h
        comments.yy
        comments.ll

        ${parser_src}
    )
else()
    file(GLOB builder_src "*")
    list(FILTER builder_src EXCLUDE REGEX ".*CMakeLists.txt")
endif()

add_library(extract_comments ${builder_src})
target_include_directories(extract_comments PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
set_property(TARGET extract_comments PROPERTY FOLDER parse)

################################################################################
