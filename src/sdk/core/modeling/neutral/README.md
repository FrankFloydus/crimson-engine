<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# Neutral Modeling Core Headers

This source-only include surface is the single declaration boundary shared by
the portable SDK implementation and native Quader modeling adapters. It owns
the low-level `quader_geometry` and `quader_poly` contracts implemented under
`../internal/mesh`.

Native code may include these headers through
`quader/modeling/neutral/...`; it must not include SDK `internal` paths. These
headers are deliberately not installed with the public modeling SDK. External
SDK consumers use the stable `quader/modeling/...` API instead.

Do not recreate `src/mesh`, add a second copy of these declarations, or expose
this directory through the installed SDK export.
