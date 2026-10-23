# Contributing

- Read both README versions; keep their commands and supported targets aligned.
- Install with `uv sync --locked`; intentional dependency updates change both
  pyproject.toml and uv.lock. Arduino CLI/ESP32 versions live in tools/build.py.
- New desktop C++ follows `.clang-format`; the optional `dev` group supplies
  the pinned formatter. Leave the frozen USB descriptor fixture intentional.
- Run `test`, build both boards, and build Windows on Windows. Do not claim Linux
  support was verified without a real Linux job. Review source cleanliness after builds.
- Keep protocol code OS-independent. Put wiring/layout differences in board/display
  abstractions, not forked firmware. Keep C ABI v1 compatible.
- Never commit identities, captures, user databases, factory images, build products
  or SDK copies. Run tools/audit.py against the index before committing.
- Any USB descriptor, partition or maintenance change needs deliberate fixture
  review and accessible recovery plus a real second-write test. Do not merely
  regenerate a passing fixture to bypass a mismatch.
