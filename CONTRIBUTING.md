# Contributing

- Keep all four README versions aligned in structure, capabilities and limitations.
  README is the product overview; detailed documentation is maintained in English
  under [docs](docs/README.md). Link to shared guides instead of copying technical
  instructions into each translation. Preserve established document URLs/anchors.
  The Wiki is a navigator, not a second copy of the manuals; see
  [documentation maintenance](docs/documentation.md) for its reviewed sources
  and publication order.
- Use CMake Presets, Ninja, CTest and CPack. Pinned dependency versions/hashes
  live in cmake/Dependencies.cmake. Product versions come from Git tags, never
  a hand-edited package/CMake version. Releases use annotated vX.Y.Z tags.
- New desktop C++ follows `.clang-format`. Leave the USB fixture intentional.
- Run CTest, build both boards, and build Windows on Windows. Do not claim Linux
  support was verified without a real Linux job. Review source cleanliness after builds.
- Keep protocol code OS-independent. Put wiring/layout differences in board/display
  abstractions, not forked firmware. Keep C ABI v1 compatible.
- Never commit identities, captures, user databases, factory images, build products
  or SDK copies. Run `audit-worktree`, review/stage changes, then run `audit`,
  `audit-history` and `audit-secrets` before committing. Publication checks must
  also cover deleted historical secrets; ignoring a file is not history cleanup.
- Any USB descriptor, partition or maintenance change needs deliberate fixture
  review and accessible recovery plus a real second-write test. Do not merely
  regenerate a passing fixture to bypass a mismatch.
