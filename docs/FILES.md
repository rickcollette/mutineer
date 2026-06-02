# File areas — current behavior

What works
- Areas in SQLite (`file_areas`) with per-area ACS.
- Listing shows filename/size/uploader/description; totals exposed via MCI.
- Uploads: copy from provided path into area directory; DB entry added; size captured; traversal guard.
- Downloads: size/exists check, credits/ratio enforcement, and configured external transfer protocols.
- Checksums: SHA-256 stored per file; duplicate detection warns.
- Batch queue: users can queue file IDs then run batch download.

What’s missing
- Archive viewing/conversion hooks.
- Rich progress/resume, bandwidth accounting, virus-scan quarantine.
- Better batch UX (per-protocol, resume).

Notes
- Credits/time updated on upload/download; file_points accrue on upload.
- Protocol commands are configured in `protocols` table; Zmodem/Xmodem commands can be wired there.
