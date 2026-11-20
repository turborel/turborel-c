# The source rules a tool can check with CMake alone (R-PROC-1), run by
# CTest as `rules.source`:
#
#   R-SIZE-2   no source file longer than 600 lines;
#   R-DOC-5    no TODO, FIXME or XXX, and so no citation of TODO.md;
#   R-DOC-6    every source file opens with a doc comment naming it;
#   R-DOC-1    every macro, type and function of include/ documented;
#   R-ERR-1    no call in the library that ends the process, prints, or
#              is unsafe by design;
#   R-TRACE-1  every test case cites what it verifies, by a heading the
#              spec or the client's manual has, or a decision RULES.md has;
#   R-DOC-8    every relative link of a Markdown document names a file
#              that exists.
#
# Takes: SOURCE_DIR.

set(TRL_MAX_FILE_LINES 600)

# The headings a test may cite, as [PROTOCOL heading] or [CLIENT heading]:
# those of doc/client/05-protocol.md, and of doc/client/02-library.md and
# 02-library-connections.md, in turborel's main at e4f08f77.
set(TRL_PROTOCOL_HEADINGS
    "The Native Protocol" "Frames" "Compressed frames" "Values and fields" "Data types" "Values"
    "Composite fields" "The tail" "Opening a connection" "Mechanisms" "Capabilities" "Requests" "Hello 'H'"
    "Answer 'A'" "Query 'Q'" "Prepare 'P'" "Execute 'E'" "Fetch 'F'" "Close 'C'" "Deallocate 'D'" "Lob 'L'"
    "Cancel 'Z'" "Terminate 'X'" "Describe 'Y'" "Ping 'G'" "Reset 'W'" "ChangeUser 'U'" "CopyIn 'I'"
    "CopyData 'J'" "CopyEnd 'N'" "Replies" "Challenge 'C'" "Ready 'R'" "Prepared 'P'" "Description 'D'"
    "Rows 'B'" "Suspended 'S'" "Complete 'K'" "Error 'E'" "Notice 'N'" "Status 'T'" "Pong 'G'"
    "Conversations" "Errors and notices" "A worked example")
set(TRL_CLIENT_HEADINGS
    "The Client Library" "Connecting" "Timeouts" "Running statements" "Prepared statements" "Large objects"
    "XML values" "UUIDs" "Importing" "What a connection asks for" "What the server tells"
    "Controlling the session" "Copying and exporting" "The SQL-agent" "Settings, Pipelines and Pools"
    "Connecting by settings" "Where settings come from" "The connection URI" "The service file"
    "The password file" "Several servers" "TLS by mode" "Pipelines" "Pools" "The SCRAM key cache")

# Calls the library never makes: they end the process, print, keep
# global state, or cannot be used safely (R-ERR-1, R-SAFE-2, R-MEM-1).
set(TRL_BANNED_CALLS
    abort exit _Exit quick_exit assert printf fprintf puts fputs perror putchar sprintf vsprintf strcpy
    strcat gets strtok atoi atol atof rand srand setjmp longjmp system signal alloca)

set(trl_failures "")

# Reports `message` about `file`, and goes on.
macro(trl_fail file message)
  list(APPEND trl_failures "${file}: ${message}")
endmacro()

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

# Whether `doc`, a test case's doc comment, cites what it verifies
# (R-TRACE-1); `out` is empty if it does, else why not.
function(trl_citation doc out)
  set(${out} "" PARENT_SCOPE)
  string(REGEX REPLACE "[ \t]+" " " doc "${doc}")
  if(doc MATCHES "Verifies: <l>(PROTOCOL|CLIENT) ([^<]+)<r>")
    set(kind "${CMAKE_MATCH_1}")
    set(heading "${CMAKE_MATCH_2}")
    if(NOT heading IN_LIST TRL_${kind}_HEADINGS)
      set(${out} "cites [${kind} ${heading}], a heading its document does not have" PARENT_SCOPE)
    endif()
  elseif(doc MATCHES "Verifies: <l>DESIGN<r> D([0-9]+)[^0-9]")
    file(STRINGS "${SOURCE_DIR}/RULES.md" decision REGEX "^#### D${CMAKE_MATCH_1}\\. ")
    if(NOT decision)
      set(${out} "cites [DESIGN] D${CMAKE_MATCH_1}, which RULES.md does not have" PARENT_SCOPE)
    endif()
  else()
    set(${out} "has no 'Verifies: [PROTOCOL ...]', '[CLIENT ...]' or '[DESIGN] D<n>'" PARENT_SCOPE)
  endif()
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
      trl_citation("${doc}" why)
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

# R-DOC-8, for every Markdown document but the build trees'.
function(trl_check_links file)
  file(READ "${SOURCE_DIR}/${file}" text)
  string(REPLACE "[" "<l>" text "${text}")
  string(REPLACE "]" "<r>" text "${text}")
  string(REPLACE ";" "<s>" text "${text}")
  string(REGEX MATCHALL "<r>\\(([^)# ]+)" links "${text}")
  get_filename_component(dir "${SOURCE_DIR}/${file}" DIRECTORY)
  foreach(link IN LISTS links)
    string(REGEX REPLACE "^<r>\\(" "" target "${link}")
    if(target MATCHES "^[a-z]+:")
      continue()
    endif()
    if(NOT EXISTS "${dir}/${target}")
      trl_fail("${file}" "links to ${target}, which does not exist (R-DOC-8)")
    endif()
  endforeach()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

file(GLOB_RECURSE trl_documents RELATIVE "${SOURCE_DIR}" "${SOURCE_DIR}/*.md")
list(FILTER trl_documents EXCLUDE REGEX "^(build|out|\\.claude)/")
list(SORT trl_documents)
foreach(trl_document IN LISTS trl_documents)
  trl_check_links("${trl_document}")
endforeach()
list(APPEND trl_files ${trl_documents})

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
