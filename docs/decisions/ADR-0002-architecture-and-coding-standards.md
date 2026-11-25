# ADR-0002: Project Layout and Coding Standards

- **Status**: Accepted
- **Date**: 2026-11-25

## Context
Both the ElmasCore Development Charter (`ELMAS.md`) and Section 6.2/13 of the ElmasDS Specification govern repository layout and code quality. Specifically:
- `ELMAS.md` mandates domain-driven organization, prohibits catch-all `utils/`/`helpers/`/`misc/`/`common/` folders, caps files at 350 lines, caps nesting at 3 levels, caps parameters at 3, and requires one consistent file-naming convention.
- The ElmasDS specification uses `snake_case` filenames (e.g., `emulator.hpp`, `input_manager.hpp`) and separates `core`, `cpu`, `memory`, `hardware`, `gpu`, `audio`, `input`, `storage`, and `frontend`.

## Decision
1. **File Naming**: All C++ headers and sources use `snake_case` (`register_bank.hpp`, `event_scheduler.cpp`). Types/classes/enums use `PascalCase`; functions/variables use `snake_case`.
2. **Domain-Specific Primitives Instead of `common/utilities`**: Instead of creating `src/common/utilities/`, foundational cross-domain types live in `src/primitives/` with explicit domain files (`fixed_width_types.hpp`, `bit_operations.hpp`, `status_result.hpp`, `diagnostic_log.hpp`). Shared ARMv4T/ARMv5TE architectural building blocks live in `src/cpu/arm_architecture/` rather than `src/cpu/common/`.
3. **Hard Structural Limits**:
   - Maximum **350 lines** per `.hpp` / `.cpp` file.
   - Maximum **3 nesting levels** per function (use guard clauses and early returns).
   - Maximum **3 parameters** per function (group larger signatures in typed structs).
   - Zero mutable global hardware state; all emulator state lives inside an explicit `Emulator` instance.

## Consequences
- Every file has a single unambiguous domain owner.
- Code remains modular, easy to review, and straightforward to test in isolation.
