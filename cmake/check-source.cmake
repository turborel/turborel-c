# The source rules a tool can check with CMake alone (R-PROC-1), run by
# CTest as `rules.source`:
#
#   R-SIZE-2   no source file longer than 600 lines;
#   R-DOC-5    no TODO, FIXME or XXX, and so no citation of TODO.md;
#   R-DOC-6    every source file opens with a doc comment naming it;
#   R-DOC-1    every macro, type and function of include/ documented;
#   R-ERR-1    no call in the library that ends the process, prints, or
#              is unsafe by design;
#   R-TRACE-1  every test case cites what it verifies, beginning
#              `Verifies:` and a tag of RULES.md D6;
#   R-TRACE-2  every citation, in code, tests, the build and the
#              documents, names a heading, decision or rule that exists
#              (cmake/CheckCitations.cmake);
#   R-DOC-7    every page of the manual opens with its title and its
#              `[Contents](README.md)` line, each volume's README lists its
#              pages, every relative link resolves, and none leaves `doc/`
#              (cmake/CheckManual.cmake).
#
# Takes: SOURCE_DIR.

set(TRL_MAX_FILE_LINES 600)

# The lines of `path` as a list, each with \ ; [ ] spelled <b> <s> <l> <r>,
# so CMake's lists keep them whole.
function(trl_read_lines path out)
  file(READ "${path}" text)
  string(REPLACE "\\" "<b>" text "${text}")
  string(REPLACE ";" "<s>" text "${text}")
  string(REPLACE "[" "<l>" text "${text}")
  string(REPLACE "]" "<r>" text "${text}")
  string(REPLACE "\n" ";" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

include("${CMAKE_CURRENT_LIST_DIR}/CheckCitations.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/CheckManual.cmake")

# Calls the library never makes: they end the process, print, keep
# global state, or cannot be used safely (R-ERR-1, R-SAFE-5, R-MEM-1).
set(TRL_BANNED_CALLS
    abort exit _Exit quick_exit assert printf fprintf puts fputs perror putchar sprintf vsprintf strcpy
    strcat gets strtok atoi atol atof rand srand setjmp longjmp system signal alloca)

set(trl_failures "")

# Reports `message` about `file`, and goes on.
macro(trl_fail file message)
  list(APPEND trl_failures "${file}: ${message}")
endmacro()


# The doc comment that ends on the line before `index` of `lines`, its
# lines joined by spaces with their leading stars gone; empty if the line
# before is not the end of a comment that opened with /**.
function(trl_doc_before lines index out)
  set(${out} "" PARENT_SCOPE)
  math(EXPR at "${index} - 1")
  if(at LESS 0)
    return()
  endif()
  list(GET lines ${at} line)
  if(NOT line MATCHES "\\*/[ \t]*$")
    return()
  endif()
  set(text "")
  while(at GREATER_EQUAL 0)
    list(GET lines ${at} line)
    string(REGEX REPLACE "^[ \t]*(/\\*\\*|\\*/|\\*)?[ \t]?" "" stripped "${line}")
    string(REGEX REPLACE "\\*/[ \t]*$" "" stripped "${stripped}")
    set(text "${stripped} ${text}")
    if(line MATCHES "^[ \t]*/\\*\\*")
      set(${out} "${text}" PARENT_SCOPE)
      return()
    endif()
    if(line MATCHES "^[ \t]*/\\*")
      return()
    endif()
    math(EXPR at "${at} - 1")
  endwhile()
endfunction()

# R-SIZE-2, R-DOC-5, R-DOC-6, for every source file.
function(trl_check_file file lines)
  list(LENGTH lines count)
  # A file ending in a newline ends in an empty last element.
  math(EXPR count "${count} - 1")
  if(count GREATER TRL_MAX_FILE_LINES)
    trl_fail("${file}" "${count} lines, more than ${TRL_MAX_FILE_LINES} (R-SIZE-2)")
  endif()
  list(GET lines 0 first)
  if(NOT first MATCHES "^/\\*\\*")
    trl_fail("${file}" "does not open with a doc comment (R-DOC-6)")
  endif()
  string(REGEX MATCH "@file [^ ]+" named "${lines}")
  if(NOT named)
    trl_fail("${file}" "its doc comment has no @file (R-DOC-6)")
  endif()
  string(REGEX MATCH "(TODO|FIXME|XXX)" open_work "${lines}")
  if(open_work)
    trl_fail("${file}" "${open_work}: open work goes in TODO.md, and code never cites it (R-DOC-5)")
  endif()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

# R-ERR-1, for the library's sources and headers.
function(trl_check_calls file lines)
  list(JOIN TRL_BANNED_CALLS "|" banned)
  set(number 0)
  foreach(line IN LISTS lines)
    math(EXPR number "${number} + 1")
    if(line MATCHES "(^|[^A-Za-z0-9_])(${banned})[ \t]*\\(")
      trl_fail("${file}:${number}" "calls ${CMAKE_MATCH_2}, which the library never does (R-ERR-1)")
    endif()
  endforeach()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

# R-DOC-1, for the public header: each macro's first definition, each
# type and each function documented. An include guard needs none.
function(trl_check_public file lines)
  list(LENGTH lines count)
  set(seen "")
  set(index 0)
  while(index LESS count)
    list(GET lines ${index} line)
    set(item "")
    set(macro "")
    if(line MATCHES "^#define (TRL_[A-Z0-9_]+)")
      set(macro "${CMAKE_MATCH_1}")
    endif()
    if(macro AND NOT macro MATCHES "_H$")
      if(NOT macro IN_LIST seen)
        set(item "${macro}")
        list(APPEND seen "${macro}")
      endif()
    elseif(line MATCHES "^(TRL_API|typedef) ")
      set(item "${line}")
    endif()
    if(item)
      trl_doc_before("${lines}" ${index} doc)
      if(NOT doc)
        math(EXPR number "${index} + 1")
        trl_fail("${file}:${number}" "${item} has no doc comment (R-DOC-1)")
      endif()
    endif()
    math(EXPR index "${index} + 1")
  endwhile()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

# R-TRACE-1, for a test executable's source: every case cites.
function(trl_check_cases file lines)
  list(LENGTH lines count)
  set(index 0)
  while(index LESS count)
    list(GET lines ${index} line)
    if(line MATCHES "^static void ([a-z0-9_]+)\\(trl_test \\*test\\)")
      set(name "${CMAKE_MATCH_1}")
      trl_doc_before("${lines}" ${index} doc)
      trl_test_citation("${doc}" why)
      if(why)
        math(EXPR number "${index} + 1")
        trl_fail("${file}:${number}" "test case ${name} ${why} (R-TRACE-1)")
      endif()
    endif()
    math(EXPR index "${index} + 1")
  endwhile()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE trl_files RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/include/*.h" "${SOURCE_DIR}/src/*.[ch]"
     "${SOURCE_DIR}/test/*.[ch]" "${SOURCE_DIR}/fuzz/*.[ch]")
list(SORT trl_files)
foreach(trl_file IN LISTS trl_files)
  trl_read_lines("${SOURCE_DIR}/${trl_file}" trl_lines)
  trl_check_file("${trl_file}" "${trl_lines}")
  if(trl_file MATCHES "^(src|include)/")
    trl_check_calls("${trl_file}" "${trl_lines}")
  endif()
  if(trl_file MATCHES "^include/")
    trl_check_public("${trl_file}" "${trl_lines}")
  endif()
  if(trl_file MATCHES "_test\\.c$")
    trl_check_cases("${trl_file}" "${trl_lines}")
  endif()
endforeach()

# R-TRACE-2, R-DOC-7: the citations of every file that cites, and the
# manual's layout and links.
file(GLOB_RECURSE trl_documents RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/*.md")
list(FILTER trl_documents EXCLUDE REGEX "^(build|out|\\.claude|\\.venv[^/]*)/")
file(GLOB_RECURSE trl_build_files RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/cmake/*" "${SOURCE_DIR}/.github/*.yml"
     "${SOURCE_DIR}/*CMakeLists.txt")
list(FILTER trl_build_files EXCLUDE REGEX "^(build|out|\\.claude|\\.venv[^/]*)/")
list(APPEND trl_build_files .clang-tidy .clang-format Doxyfile .githooks/pre-commit)
set(trl_citing ${trl_files} ${trl_documents} ${trl_build_files})
list(REMOVE_DUPLICATES trl_citing)
list(SORT trl_citing)
foreach(trl_file IN LISTS trl_citing)
  trl_check_citations("${trl_file}")
endforeach()
foreach(trl_document IN LISTS trl_documents)
  trl_check_links("${trl_document}")
  if(trl_document MATCHES "^doc/")
    trl_check_page("${trl_document}")
  endif()
endforeach()
trl_check_volumes()
list(APPEND trl_files ${trl_documents} ${trl_build_files})
list(REMOVE_DUPLICATES trl_files)

list(LENGTH trl_files trl_count)
if(trl_failures)
  list(JOIN trl_failures "\n" trl_text)
  string(REPLACE "<l>" "[" trl_text "${trl_text}")
  string(REPLACE "<r>" "]" trl_text "${trl_text}")
  string(REPLACE "<s>" ";" trl_text "${trl_text}")
  string(REPLACE "<b>" "\\" trl_text "${trl_text}")
  message(FATAL_ERROR "source rules broken:\n${trl_text}")
endif()
message(STATUS "source rules hold in ${trl_count} files")
