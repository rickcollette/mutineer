# Website Ownership

The public Mutineer marketing website is intentionally not part of this core BBS
repository. Its source lives in a separate repository/worktree at `../website`
in the local development layout.

This repository keeps the BBS software, operator documentation, protocol
references, tests, packaging, and runtime scripts. Website build artifacts,
Vite/React source, marketing copy, and static web assets belong to the website
repository.

Production deployment is coordinated from `../production`. That deployment
system builds the BBS package from this repository and the website package from
the separate website source, then uploads both artifacts to production.
