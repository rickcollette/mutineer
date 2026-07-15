# Website Source Ownership

The canonical public website source is the Markdown documentation under
`docs/` plus `scripts/build-website.py`.

Generated static HTML under `website/` is checked in so releases and GitHub
Pages can publish without a Node build. CI verifies that the generated output is
fresh by running the website generator in check mode.

The Vite/React files under `website/` are experimental development material and
are not the release or publication source of truth. They must not replace the
Markdown-generated website unless this document, CI, release packaging, and the
deployment scripts are changed together.

Release package trees under `dist/` and Vite output under `website/dist/` are
generated artifacts. Rebuild them with the release or website scripts instead of
editing them by hand.
