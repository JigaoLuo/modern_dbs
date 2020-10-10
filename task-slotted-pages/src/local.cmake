# ---------------------------------------------------------------------------
# MODERNDBS
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Includes
# ---------------------------------------------------------------------------

set(
    SRC_CC_LINTER_IGNORE
)
# include("${CMAKE_SOURCE_DIR}/src/this-could-be-your-folder/local.cmake")

# ---------------------------------------------------------------------------
# Files
# ---------------------------------------------------------------------------

set(
    SRC_CC
    src/buffer/buffer_manager.cc src/buffer/2Q_replacer.cc # buffer
    src/disk/disk_manager.cc # disk
    src/slotted_page/database.cc src/slotted_page/fsi_segment.cc src/slotted_page/hex_dump.cc src/slotted_page/schema.cc src/slotted_page/schema_segment.cc src/slotted_page/slotted_page.cc src/slotted_page/sp_segment.cc # slotted_page
)
if(UNIX)
    set(SRC_CC ${SRC_CC} src/common/posix_file.cc)
elseif(WIN32)
    message(SEND_ERROR "Windows is not supported")
else()
    message(SEND_ERROR "unsupported platform")
endif()

# Gather lintable files
set(SRC_CC_LINTING "")
foreach(SRC_FILE ${SRC_CC})
    list(FIND SRC_CC_LINTER_IGNORE "${SRC_FILE}" SRC_FILE_IDX)
    if (${SRC_FILE_IDX} EQUAL -1)
        list(APPEND SRC_CC_LINTING "${SRC_FILE}")
    endif()
endforeach()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------

add_library(moderndbs STATIC ${SRC_CC} ${INCLUDE_H})
target_link_libraries(
    moderndbs
    gflags
    rapidjson
    Threads::Threads
    ${LLVM_LIBS}
    ${LLVM_LDFLAGS}
)

# ---------------------------------------------------------------------------
# Linting
# ---------------------------------------------------------------------------

add_clang_tidy_target(lint_src "${SRC_CC_LINTING}")
list(APPEND lint_targets lint_src)
