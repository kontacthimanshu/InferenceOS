# Implementation Plan: [FEATURE]

**Branch**: `[###-feature-name]` | **Date**: [DATE] | **Spec**: [link]

**Input**: Feature specification from `/specs/[###-feature-name]/spec.md`

## Summary

[Summarize the user-visible outcome and the technical approach selected through research.]

## Requirements Traceability

<!-- Map every user story and requirement group to a design responsibility and validation path.
Do not duplicate the full specification. -->

| Spec reference | Design responsibility | Contract / artifact | Validation approach |
|---|---|---|---|
| [US-1 / FR range] | [subsystem] | [contract or design artifact] | [test or demonstration] |

## Technical Context

**Language/Version**: [governed language/version or NEEDS CLARIFICATION]

**Primary Dependencies**: [toolchain, firmware, runtime, libraries, or NEEDS CLARIFICATION]

**Storage**: [persistent and transient storage model or N/A]

**Testing**: [unit, contract, integration, system, and fault-injection approach]

**Target Platform**: [architecture, firmware, reference environment, or NEEDS CLARIFICATION]

**Project Type**: [operating system, library, service, application, etc.]

**Performance Goals**: [measurable domain goals or NEEDS CLARIFICATION]

**Constraints**: [binding specification and constitution constraints]

**Scale/Scope**: [supported capacity, interfaces, and first-release boundaries]

## Constitution Check

*GATE: Must pass before Phase 0 research and again after Phase 1 design.*

### Pre-Research Gate

- **[Applicable constitutional gate]**: [PASS/FAIL] — [Evidence or required correction]

### Post-Design Gate

- **[Applicable constitutional gate]**: [PASS/FAIL] — [Evidence from research/design artifacts]

## Project Structure

### Documentation (this feature)

```text
specs/[###-feature]/
|-- plan.md
|-- research.md
|-- data-model.md
|-- quickstart.md
|-- contracts/
`-- tasks.md              # Created later by $speckit-tasks
```

### Source Code (repository root)

<!-- Replace the example with concrete paths selected during research. Preserve the constitution's
source-root and dependency-direction rules. Do not leave generic option labels. -->

```text
src/
|-- architecture/
|-- kernel/
|-- block/
|-- vfs/
|-- filesystems/
|-- cui/
|-- gui/
|-- shell/
`-- applications/

tests/
|-- unit/
|-- contract/
|-- integration/
`-- system/
```

**Structure Decision**: [Describe the selected concrete structure and dependency direction.]

## Phase 0 Research Decisions

<!-- Every NEEDS CLARIFICATION in Technical Context must be resolved in research.md. -->

- **Decision**: [selected approach]
  - **Rationale**: [why it satisfies the specification and constitution]
  - **Alternatives considered**: [rejected options and reasons]

## Phase 1 Design Outputs

- **Data model**: [entities, validation rules, and state transitions in data-model.md]
- **Contracts**: [system, filesystem, command, or application boundaries under contracts/]
- **Quickstart validation**: [end-to-end proof in quickstart.md]
- **Post-design constitution result**: [PASS/FAIL and any corrective design work]

## Complexity Tracking

> Fill only when a Constitution Check gate fails and an amendment or explicit justification is
> being pursued. Ordinary implementation complexity is not a constitutional exception.

| Violation | Why needed | Simpler compliant alternative rejected because |
|---|---|---|
| [gate] | [specific necessity] | [reason] |
