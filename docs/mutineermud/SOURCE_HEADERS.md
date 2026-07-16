# Recommended Source File Headers

## New MutineerMUD source files

```c
/*
 * MutineerMUD
 * Copyright (C) 2026 megalith <megalith@root.sh>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * This file is part of MutineerMUD and is free software: you may
 * redistribute it and/or modify it under the terms of the GNU Lesser
 * General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 */
```

## Modified DikuMUD-derived source files

Preserve the original copyright and attribution block already present in the file, then add:

```c
/*
 * MutineerMUD modifications:
 * Copyright (C) 2026 megalith <megalith@root.sh>
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 *
 * Modified as part of MutineerMUD. Original DikuMUD authorship and
 * copyright notices are preserved below or elsewhere in this file.
 */
```

Do not replace or delete an upstream file's existing license header. When an inherited file carries a different or more specific license, retain that license and document it in `NOTICE.md`.
