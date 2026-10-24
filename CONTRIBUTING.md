# Contributing

- Read both README versions; keep their commands and supported targets aligned.
- Use CMake Presets, Ninja, CTest and CPack. Pinned dependency versions/hashes
  live in cmake/Dependencies.cmake. Product versions come from Git tags, never
  a hand-edited package/CMake version. Releases use annotated vX.Y.Z tags.
- New desktop C++ follows `.clang-format`. Leave the USB fixture intentional.
- Run CTest, build both boards, and build Windows on Windows. Do not claim Linux
  support was verified without a real Linux job. Review source cleanliness after builds.
- Keep protocol code OS-independent. Put wiring/layout differences in board/display
  abstractions, not forked firmware. Keep C ABI v1 compatible.
- Never commit identities, captures, user databases, factory images, build products
  or SDK copies. Stage changes and build target `audit` against the index before committing.
- Any USB descriptor, partition or maintenance change needs deliberate fixture
  review and accessible recovery plus a real second-write test. Do not merely
  regenerate a passing fixture to bypass a mismatch.
