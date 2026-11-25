# Contributing to ElmasDS

ElmasDS adheres strictly to the **ElmasCore Development Charter** and the ElmasDS Technical Specification.

## Non-Negotiable Code Standards

1. **C++20 Strict**: `CMAKE_CXX_EXTENSIONS OFF`, high warning levels (`/W4` on MSVC, `-Wall -Wextra -Wpedantic` on GCC/Clang).
2. **File Size Limit**: **350 lines maximum per file**. Split modules before they approach this limit.
3. **Function Clarity**:
   - **3 levels of nesting maximum** per function; use guard clauses and early returns.
   - **3 parameters maximum** per function; group larger parameter sets into a typed configuration struct.
   - Single level of abstraction per function; pure functions for calculations and data transformations.
   - No silent mutation of input parameters and no mutable global hardware state.
4. **Naming Convention**:
   - All C++ source/header files and domain directories use `snake_case` (`event_scheduler.hpp`, `register_bank.cpp`).
   - Types/classes/structs/enums use `PascalCase`; methods/functions/variables use `snake_case`; constants use `kPascalCase` or `UPPER_SNAKE_CASE` for bitmasks.
   - Generic catch-all folders (`utils/`, `helpers/`, `misc/`, `common/`) are forbidden. Every file belongs to a specific domain.
5. **Architectural Isolation**:
   - `elmasds_core` must never include SDL3, OpenGL, or platform GUI headers.
   - Hardware subsystems must communicate through explicit interfaces and the centralized `MemoryBus` / `EventScheduler`.
6. **Documentation & Verification**:
   - Every exported hardware/domain class or function requires Doxygen comments explaining responsibility, parameters, return values, and edge cases.
   - No feature may be marked `TESTED` or `VERIFIED` without passing automated tests executed after the final edit.

## Formatting and Linting

- Format code using `.clang-format` before committing.
- Static analysis rules are defined in `.clang-tidy`.
