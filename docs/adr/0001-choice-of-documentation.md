# ADR 0001: Choice of Documentation System

**Date:** 2025-10-07  
**Status:** Accepted  
**Context:**  
The project requires long-term maintainable documentation combining developer guides, architecture decisions, and auto-generated API references.

**Decision:**  
Use MkDocs + Material for the general documentation and Doxygen for API docs.
ADRs will be written in Markdown under `docs/adr/`.

**Consequences:**  
- Single source of truth (Markdown in Git)
- Fully static deployable site
- Version-controlled documentation with minimal tooling overhead