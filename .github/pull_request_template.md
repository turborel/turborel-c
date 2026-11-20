<!--
This description is the series' cover letter (CONTRIBUTING.md, Pull
requests). Each commit message still stands alone, in Linux's form.
Base the series on main, keep it rebased, and on revising say what
changed in a comment, not here.
-->

## What and why

<!-- The problem and its impact, then the approach and the choices made.
Link the issue: "Closes #N". -->

## The series

<!-- One line per commit, in order: what it does. Each passes
`cmake --workflow --preset verify` on its own. -->

1.

## Testing

<!-- The tests added and what each verifies. For a change to the wire,
the malformed forms, the attacker's view and the capability checks done. -->

- [ ] `cmake --workflow --preset verify` passes
- [ ] `cmake --workflow --preset asan-ubsan` passes
- [ ] `cmake --workflow --preset coverage` holds the floors

## Compatibility

<!-- What a program using the C API, or a server of another release,
sees change; "none" if nothing changes. -->

## Performance

<!-- If the change claims any: what was measured, before and after,
with the numbers. Otherwise "none claimed". -->

## Checklist (RULES.md §7)

- [ ] One logical change per commit, each passing `verify`
- [ ] The spec or turborel-client cited in the code (R-DOC-3) and every test case (`Verifies:`, R-TRACE-1)
- [ ] Normal, boundary and robustness tests (R-TEST-2), a failing-first test for a fix (R-TEST-5)
- [ ] No `assert`, `abort`, `exit` or printing in the library; every status tested and allocation checked (R-ERR, R-MEM)
- [ ] Ownership said by each new function (D5); no new dependency without a decision (R-DEP-1)
- [ ] CONFORMANCE.md, RULES.md, TODO.md and `doc/` updated where they change
