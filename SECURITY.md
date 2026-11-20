# Security

Report a vulnerability privately, never in a public issue: on the
repository's **Security** tab, choose **Report a vulnerability**. Only the
maintainers see the report.

What counts, for a driver:

- TLS not used, or not verified, where it was asked for, as by a
  `tls_mode` of `require`, `verify-ca` or `verify-full`, or SCRAM's
  channel binding not made where `channel_binding=require` asked for it;
- a password, a SCRAM key or anything derived from a password, or a
  cancel key, disclosed: sent where it should not go, written to a log or
  a failure's message, or left readable where it should not be;
- a server's reply, or a file the driver reads (a service file, a
  password file, an export), that can crash the program, run code, or
  take its memory without bound.

A wrong result, a value decoded wrongly with no attacker, or a crash only
the program's own misuse of the API causes is a bug: report it with the
**Bug** form, its impact in the title (CONTRIBUTING.md, Reporting issues).

Say in the report:

- what an attacker needs, such as a server of their own, a position on
  the network, or a file the program reads;
- what they gain;
- the commit you tested (`git rev-parse --short=12 HEAD`);
- the smallest steps that show it.

You will hear back within a week. The fix is made in private and
published with an advisory that credits you, unless you ask not to be
named.
