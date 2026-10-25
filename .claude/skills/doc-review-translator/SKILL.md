---
name: doc-review-translator
description: "Use when reviewing technical documentation, checking consistency among requirements and architecture documents, unifying terminology, verifying traceability, or translating Japanese technical documents into English."
argument-hint: "Document paths and the requested review or translation scope"
---

# Document Review and Translation

## When to Use

- Review requirements, architecture, interface, and implementation documents.
- Check consistency across multiple Markdown documents.
- Verify requirement-to-design-to-implementation traceability.
- Create a terminology map and identify conflicting terms.
- Translate Japanese technical documentation into English while preserving meaning and structure.

## Inputs

Collect the following before starting when they are available:

- Target document paths and requested scope.
- Related specifications, architecture documents, interface definitions, and implementation files.
- Existing glossary, naming conventions, and output-file destination.

When the requested scope is unclear, state the assumptions used for the review. Do not claim a traceability gap merely because a related document was not supplied.

## Review Procedure

1. Read the target documents and only the directly relevant related materials.
2. Identify each document's purpose, scope, version or status, and normative statements such as `shall`, `must`, and `should`.
3. Build a terminology map containing the source term, preferred term, definition, and document locations.
4. Compare documents for contradictions, omissions, duplicated but divergent requirements, incompatible interfaces, and inconsistent identifiers, units, data types, and timing.
5. Trace important requirements through system requirements, software requirements, architecture, interfaces, and implementation where those artifacts are in scope.
6. Classify each finding using the severity definitions below and distinguish confirmed defects from questions or assumptions.
7. Produce a review report before making document changes. Preserve source documents unless the user explicitly requests edits.

## Severity

| Severity | Use for |
| --- | --- |
| Critical | A contradiction or omission that can cause unsafe behavior, data loss, security exposure, or make the system impossible to implement correctly. |
| Major | A requirement, interface, or traceability conflict likely to cause an implementation or integration defect. |
| Minor | Ambiguous wording, terminology drift, incomplete rationale, or formatting that impairs maintenance but has a clear likely interpretation. |
| Info | Improvement opportunity, question, or non-blocking observation. |

## Review Report

Write reports under `review_logs/` unless the user specifies another destination. Use the filename pattern `YYYY-MM-DD-<scope>-review.md`.

Use this structure:

```markdown
# Document Review: <scope>

## Scope
- Documents reviewed:
- Related artifacts checked:
- Assumptions and exclusions:

## Terminology
| Source term | Preferred term | Definition | Notes |
| --- | --- | --- | --- |

## Findings
| ID | Severity | Location | Finding | Evidence | Recommendation |
| --- | --- | --- | --- | --- | --- |

## Traceability
| Source requirement | Design or interface | Implementation or test | Status | Notes |
| --- | --- | --- | --- | --- |

## Summary
- Confirmed findings by severity:
- Open questions:
```

Reference locations precisely using a document heading, requirement ID, table entry, or file path. Keep evidence factual and recommendations actionable.

## Translation Procedure

1. Preserve the original Markdown hierarchy, requirement IDs, tables, code blocks, links, file paths, units, signal names, API names, and configuration keys.
2. Translate prose into clear technical English. Do not expand requirements, weaken normative language, or infer missing behavior.
3. Translate `shall`-equivalent obligations consistently as `shall`; retain `should` and optional language at their original strength.
4. Use the terminology map consistently. On first use, retain an established product-specific Japanese term in parentheses when translating it would reduce precision.
5. Preserve acronyms such as CAN, SOME/IP, VSS, ECU, CDC, and AUTOSAR unless a supplied glossary defines an expansion.
6. Flag text that is ambiguous, truncated, or cannot be translated confidently rather than silently guessing.
7. Save translated documents beside the source using the suffix `.en.md`, unless the user specifies a different destination. Do not overwrite the source.

## Translation Quality Check

Before delivering a translation, verify that:

- Headings, list nesting, tables, and code fences remain valid Markdown.
- Requirement identifiers, numerical values, units, version numbers, and interface names are unchanged.
- Links and relative paths remain intact.
- The translation uses the agreed terminology consistently.
- Any unresolved ambiguity appears in a short `## Translator Notes` section.

## Output

Return a concise summary containing the reviewed or translated files, confirmed findings grouped by severity, assumptions, and any open questions. Link the generated report and translation files when they were created.

# Output

review_logs
...