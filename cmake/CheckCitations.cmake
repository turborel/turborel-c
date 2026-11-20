# Citations checked (RULES.md D6, R-TRACE-1, R-TRACE-2), for
# check-source.cmake: a test's tag, and every citation of a heading, a
# decision or a rule, in code, tests, the build and the documents, held to
# what exists.

# The headings a citation may name, as [PROTOCOL <heading>] or [CLIENT <heading>]:
# those of doc/client/05-protocol.md, and of doc/client/02-library.md and
# 02-library-connections.md, in turborel's main at 1c4187e4.
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
# This file's decisions and rules, as RULES.md has them.
file(STRINGS "${SOURCE_DIR}/RULES.md" trl_decision_lines REGEX "^#### D[0-9]+\\. ")
set(TRL_DECISIONS "")
foreach(trl_line IN LISTS trl_decision_lines)
  string(REGEX MATCH "^#### (D[0-9]+)\\." trl_match "${trl_line}")
  list(APPEND TRL_DECISIONS "${CMAKE_MATCH_1}")
endforeach()
file(STRINGS "${SOURCE_DIR}/RULES.md" trl_rule_lines REGEX "^\\| R-[A-Z]+-[0-9]+ \\|")
set(TRL_RULES "")
foreach(trl_line IN LISTS trl_rule_lines)
  string(REGEX MATCH "^\\| (R-[A-Z]+-[0-9]+) \\|" trl_match "${trl_line}")
  list(APPEND TRL_RULES "${CMAKE_MATCH_1}")
endforeach()

# Whether `doc`, a test case's doc comment, cites what it verifies with a
# tag of D6 that names what exists (R-TRACE-1); `out` is empty if it
# does, else why not.
function(trl_test_citation doc out)
  set(${out} "" PARENT_SCOPE)
  string(REGEX REPLACE "[ \t]+" " " doc "${doc}")
  if(doc MATCHES "Verifies: <l>(PROTOCOL|CLIENT) ([^<]+)<r>")
    set(kind "${CMAKE_MATCH_1}")
    set(heading "${CMAKE_MATCH_2}")
    if(NOT heading IN_LIST TRL_${kind}_HEADINGS)
      set(${out} "cites [${kind} ${heading}], a heading its document does not have" PARENT_SCOPE)
    endif()
  elseif(doc MATCHES "Verifies: <l>DESIGN<r> (D[0-9]+)")
    if(NOT CMAKE_MATCH_1 IN_LIST TRL_DECISIONS)
      set(${out} "cites [DESIGN] ${CMAKE_MATCH_1}, which RULES.md does not have" PARENT_SCOPE)
    endif()
  elseif(doc MATCHES "Verifies: <l>TOOL<r> (R-[A-Z]+-[0-9]+)")
    if(NOT CMAKE_MATCH_1 IN_LIST TRL_RULES)
      set(${out} "cites [TOOL] ${CMAKE_MATCH_1}, which RULES.md does not have" PARENT_SCOPE)
    endif()
  else()
    set(${out} "has no 'Verifies:' and a tag of RULES.md D6" PARENT_SCOPE)
  endif()
endfunction()

# The text of `path`, its \ ; [ ] spelled <b> <s> <l> <r>, so CMake's
# strings and lists keep it whole.
function(trl_read_escaped path out)
  file(READ "${path}" text)
  string(REPLACE "\\" "<b>" text "${text}")
  string(REPLACE ";" "<s>" text "${text}")
  string(REPLACE "[" "<l>" text "${text}")
  string(REPLACE "]" "<r>" text "${text}")
  set(${out} "${text}" PARENT_SCOPE)
endfunction()

# Each heading `text` cites as [kind heading] is one its document has.
function(trl_check_headings file text kind)
  string(REGEX MATCHALL "<l>${kind} [^<>]+<r>" cited "${text}")
  foreach(citation IN LISTS cited)
    string(REGEX REPLACE "^<l>${kind} (.*)<r>$" "\\1" heading "${citation}")
    if(NOT heading IN_LIST TRL_${kind}_HEADINGS)
      trl_fail("${file}" "cites [${kind} ${heading}], a heading its document does not have (R-TRACE-2)")
    endif()
  endforeach()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

# Each of this file's decisions `text` cites, as RULES.md D<n> (and its
# list) or [DESIGN] D<n>, is one RULES.md has; Turborel's own RULES.md,
# cited as Turborel's, is not this file.
function(trl_check_decisions file text)
  string(REGEX REPLACE "[Tt]urborel's RULES\\.md D[0-9]+((, | and )D[0-9]+)*" "" text "${text}")
  string(REGEX MATCHALL "(RULES\\.md|<l>DESIGN<r>) D[0-9]+((, | and )D[0-9]+)*" cited "${text}")
  foreach(citation IN LISTS cited)
    string(REGEX MATCHALL "D[0-9]+" decisions "${citation}")
    foreach(decision IN LISTS decisions)
      if(NOT decision IN_LIST TRL_DECISIONS)
        trl_fail("${file}" "cites ${decision}, which RULES.md does not have (R-TRACE-2)")
      endif()
    endforeach()
  endforeach()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

# R-TRACE-2 for the file `file`: every heading, decision and rule it cites
# exists.
function(trl_check_citations file)
  trl_read_escaped("${SOURCE_DIR}/${file}" text)
  trl_check_headings("${file}" "${text}" PROTOCOL)
  trl_check_headings("${file}" "${text}" CLIENT)
  trl_check_decisions("${file}" "${text}")
  string(REGEX MATCHALL "R-[A-Z]+-[0-9]+" rules "${text}")
  list(REMOVE_DUPLICATES rules)
  foreach(rule IN LISTS rules)
    if(NOT rule IN_LIST TRL_RULES)
      trl_fail("${file}" "cites ${rule}, which RULES.md does not have (R-TRACE-2)")
    endif()
  endforeach()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()
