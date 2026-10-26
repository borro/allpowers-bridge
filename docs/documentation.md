# Documentation maintenance

[Documentation](README.md) · [Contributing](../CONTRIBUTING.md)

## Source of truth

The four root README files introduce the same product and limitations in English,
Russian, Simplified Chinese and Traditional Chinese. Keep their section order and
links aligned; select the matching screenshots from `docs/images/`. The language
selector belongs above the title, with the current language emphasized.

Detailed guides live in `docs/` in English. Update the relevant guide with a
behavior change instead of accumulating implementation notes in the README.
Keep the documentation index task-oriented. Release notes and validation records
are historical evidence, not general-purpose user guides. Preserve established
paths and anchors used by external links.

Before publishing, check local links, anchors and images, compare the four
overview pages, and inspect GitHub rendering at desktop and narrow widths in
light and dark themes. Run the repository's publication audits and review the
staged files. Documentation is included in the reviewed source inventory.

## Wiki navigator

Reviewed Wiki sources are [Home](wiki/Home.md) and [sidebar](wiki/_Sidebar.md).
They contain navigation to the main repository, not copies of the manuals.
Links are absolute because Wiki pages live in a separate Git repository.
There is no automatic synchronization or additional token requirement introduced
by this documentation layout.

Publish in this order:

1. Review, commit and publish the documentation to the main repository's `main`
   branch. Check that the target pages are reachable.
2. Open the repository's Wiki with an authorized account. If it has not been
   initialized, create its first Home page through GitHub. If Wiki is disabled,
   enable it in repository settings first.
3. Read the current Home and sidebar. Preserve unrelated pages and useful existing
   navigation; reconcile those links into the reviewed sources before replacing
   either page.
4. Update Home and `_Sidebar` from these sources through GitHub or a separate
   clone of `https://github.com/borro/allpowers-bridge.wiki.git`. Commit and push
   only the intended Wiki changes. Keep the checkout outside the public source
   inventory, for example under ignored `build/`.
5. Open the published Wiki and follow its links. A source file prepared locally
   is not evidence of a successful publication. If access is unavailable, retain
   the sources and report the exact remaining step.

The main repository's README links directly to `docs/`, so the manuals remain
accessible independently of Wiki initialization or availability.
