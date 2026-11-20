# Runs one command for CI, prints its output, and, if it fails, reports
# the output's last lines as a GitHub annotation, so a failure can be read
# from the run's annotations as well as from its log (RULES.md D13).
#
# Takes: COMMAND, the command as a CMake list ("cmake;--workflow;...").

# The lines of output an annotation keeps: enough for a compiler's
# error and its context.
set(TRL_ANNOTATED_LINES 60)

execute_process(
  COMMAND ${COMMAND}
  OUTPUT_VARIABLE trl_output
  ERROR_VARIABLE trl_output
  RESULT_VARIABLE trl_result)
# The output whole, as the log shows it.
execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "${trl_output}")
if(trl_result EQUAL 0)
  return()
endif()

string(REPLACE ";" "<semicolon>" trl_output "${trl_output}")
string(REPLACE "\r" "" trl_output "${trl_output}")
string(REPLACE "\n" ";" trl_lines "${trl_output}")
list(LENGTH trl_lines trl_count)
math(EXPR trl_from "${trl_count} - ${TRL_ANNOTATED_LINES}")
if(trl_from LESS 0)
  set(trl_from 0)
endif()
list(SUBLIST trl_lines ${trl_from} ${TRL_ANNOTATED_LINES} trl_tail)
list(JOIN trl_tail "%0A" trl_text)
string(REPLACE "<semicolon>" ";" trl_text "${trl_text}")
string(REPLACE "%" "%25" trl_text "${trl_text}")
string(REPLACE "%250A" "%0A" trl_text "${trl_text}")
list(JOIN COMMAND " " trl_title)
execute_process(COMMAND "${CMAKE_COMMAND}" -E echo "::error title=${trl_title} failed::${trl_text}")
message(FATAL_ERROR "${trl_title} failed (${trl_result})")
