<!--
////////////////////////////////////////////////////////////////////////////////
Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
This file is part of Quader and is protected proprietary source code.
No permission is granted to use, copy, modify, distribute, or sublicense this
file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
-->
# ADR NNNN - Short Decision Title

Internal architecture decision record template. ADRs may discuss private Quader owners and migration
risks; classify finished ADR files as internal unless they are deliberately written as public docs.

## Status

Proposed | Accepted | Superseded | Deprecated

## Date

YYYY-MM-DD

## Owners

- Decision owner:
- Affected domains:
- Required AGENTS/domain docs read:

## Context

Describe the current native app behavior, source owners, public API surface, tests, guardrails, and
migration-plan item that forced this decision. Name existing implementations found during the
source-of-truth search.

## Decision

State the chosen architecture in concrete Quader terms. Identify the owner that will hold the
invariant and the public API, command, service, or projection path callers should use.

## Consequences

- Positive consequences:
- Tradeoffs:
- Risks:
- Compatibility requirements:

## Alternatives Considered

List rejected alternatives and why they were rejected. Include any option that would duplicate an
existing owner, create a parallel source of truth, bypass public API intent, or expose private
internals to future scripting.

## Migration Plan

- Files or modules expected to change:
- Guardrails or behavior tests needed before the migration:
- Temporary compatibility adapters:
- Cleanup or follow-up tasks:

## Verification

List the smallest useful checks and the required final app build when project files change. Use
`quader_guardrail_tests`, `quader_tests`, or `quader_modeling_domain_tests` according to the owner
being protected.

## Links

- Related task:
- Related migration-plan item:
- Related tests or guardrails:
