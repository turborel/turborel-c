# The manual's layout and every document's links (RULES.md R-DOC-7), for
# check-source.cmake, as turborel.com's documentation site reads `doc/`:
# each page opens with its `# Title`, then a blank line, then a line
# holding only `[Contents](README.md)`; each volume's README.md lists its
# pages; every relative link names a file that exists, and one from
# `doc/` stays within it, a link out of it being a full URL.

# The line a page of a volume holds third, which the site replaces with
# its own contents.
set(TRL_CONTENTS_LINE "<l>Contents<r>(README.md)")

# Every relative link of the document `file` resolves; one of `doc/` stays
# within `doc/`.
function(trl_check_links file)
  trl_read_escaped("${SOURCE_DIR}/${file}" text)
  string(REGEX MATCHALL "<r>\\(([^)# ]+)" links "${text}")
  get_filename_component(dir "${SOURCE_DIR}/${file}" DIRECTORY)
  foreach(link IN LISTS links)
    string(REGEX REPLACE "^<r>\\(" "" target "${link}")
    if(target MATCHES "^[a-z]+:")
      continue()
    endif()
    get_filename_component(resolved "${dir}/${target}" ABSOLUTE)
    if(NOT EXISTS "${resolved}")
      trl_fail("${file}" "links to ${target}, which does not exist (R-DOC-7)")
    elseif(file MATCHES "^doc/" AND NOT resolved MATCHES "^${SOURCE_DIR}/doc/")
      trl_fail("${file}" "links to ${target}, out of doc/, which takes a full URL (R-DOC-7)")
    endif()
  endforeach()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

# The page `file` of the manual opens with its title and, unless it is a
# README, its contents line.
function(trl_check_page file)
  trl_read_lines("${SOURCE_DIR}/${file}" lines)
  list(GET lines 0 title)
  if(NOT title MATCHES "^# [^ ]")
    trl_fail("${file}" "does not open with its # Title (R-DOC-7)")
  endif()
  if(file MATCHES "/README\\.md$")
    set(trl_failures "${trl_failures}" PARENT_SCOPE)
    return()
  endif()
  list(LENGTH lines count)
  set(contents "")
  if(count GREATER 2)
    list(GET lines 1 blank)
    list(GET lines 2 contents)
  endif()
  if(NOT blank STREQUAL "" OR NOT contents STREQUAL TRL_CONTENTS_LINE)
    trl_fail("${file}" "has no line [Contents](README.md) after its title (R-DOC-7)")
  endif()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()

# Each volume's README.md lists every page of its folder.
function(trl_check_volumes)
  file(GLOB volumes LIST_DIRECTORIES true "${SOURCE_DIR}/doc/*")
  foreach(volume IN LISTS volumes)
    if(NOT IS_DIRECTORY "${volume}")
      continue()
    endif()
    file(RELATIVE_PATH name "${SOURCE_DIR}" "${volume}")
    if(NOT EXISTS "${volume}/README.md")
      trl_fail("${name}" "has no README.md listing its pages (R-DOC-7)")
      continue()
    endif()
    file(READ "${volume}/README.md" readme)
    file(GLOB pages RELATIVE "${volume}" "${volume}/*.md")
    foreach(page IN LISTS pages)
      string(FIND "${readme}" "](${page})" listed)
      if(NOT page STREQUAL "README.md" AND listed EQUAL -1)
        trl_fail("${name}/README.md" "does not list ${page} (R-DOC-7)")
      endif()
    endforeach()
  endforeach()
  set(trl_failures "${trl_failures}" PARENT_SCOPE)
endfunction()
