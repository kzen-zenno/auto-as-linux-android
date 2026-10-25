# Shared Agent Instructions

This is the canonical instruction file for coding agents in this repository.
Platform-specific entry points must import or direct the agent to this file
instead of duplicating these rules.

## Project Overview

This repository is a learning-oriented mini Cockpit Domain Controller (CDC)
project for understanding an automotive software system.

## Components

- `auto-as-linux/autoas-as/`: AUTOSAR Gateway ECU. It receives and sends CAN frames, converts
  CAN data to signals, exposes signals through SOME/IP communication, and
  provides vehicle-state services.
- `auto-as-linux/automotive_linux/`: Linux vehicle-state stack containing the KUKSA provider
  and KUKSA Data Broker integration.
- `aaos16`: Android Automotive OS 16 Development Build and Virtual Environment Setup.
- `aaos16/workspace`:There is a large amount of AAOS 16 source code under the workspace directory. Do not read or analyze it unless explicitly instructed to do so.
- `specs/`: System requirements, software requirements, software architecture,
  and interface definitions.


## Working Rules

- Read the relevant documents under `specs/` before changing implementation or
  specifications.
- Keep specifications aligned with the implementation. Follow each document's
  existing structure when editing it.
- Record a concise, traceable summary of each agent session in `thinking_logs/`.
- Keep changes scoped to the requested behavior and do not revert unrelated
  workspace changes.

## Shared Skill Location

Store project skills in `.claude/skills/<skill-name>/SKILL.md`. This location is
recognized by both Claude Code and GitHub Copilot in this workspace. The skill
name in its YAML frontmatter must match its directory name.

## Compatibility Entry Points

- Claude Code loads `CLAUDE.md`, which imports this file.
- GitHub Copilot loads `.github/copilot-instructions.md`, which directs the
  agent to this file.
- Agents that discover `AGENTS.md` directly must treat this file as authoritative.