<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# QDR IO Package Guide

`src/io/qdr` owns native binary scene IO, validation, atomic replacement, and
developer inspection tools.

## QDR Rules

- Keep QDR reader/writer changes round-trip safe for supported binary scene data.
- Legacy JSON `.qdr` input is unsupported. Do not add JSON readers, migrations, format dispatch,
  runtime compatibility adapters, or the retired compatibility targets back.
- Keep native scene hydration/extraction around the SDK-owned binary authored-topology codec; SDK
  QDR APIs consume and return byte spans/vectors only.
- JSON from `qdr_dump --json` is write-only developer inspection output, not a persistence format.
- Add or update QDR fixtures and tests for binary schema or serialization changes.
- Do not add editor save/load workflow tests until the editor save/load surface exists.
