<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# QDR IO Package Guide

`src/io/qdr` owns QDR document/runtime IO and compatibility behavior.

## QDR Rules

- Keep QDR reader/writer changes round-trip safe for supported document data.
- Preserve format compatibility unless a task explicitly asks for a breaking change.
- Keep runtime compatibility adapters thin; they should translate between QDR data and native app
  owners, not own editor behavior.
- Add or update QDR fixtures and tests for schema, compatibility, or serialization changes.
- Do not add editor save/load workflow tests until the editor save/load surface exists.
