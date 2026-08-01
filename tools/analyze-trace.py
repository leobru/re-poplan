#!/usr/bin/env python3
"""Summarize direct BESM-6 subroutine calls from a dispak instruction trace."""

from __future__ import annotations

import argparse
import collections
import json
import re
from dataclasses import dataclass, field
from pathlib import Path


INSTRUCTION_RE = re.compile(
    r"^(?P<pc>[0-7]{5}):\s+(?P<opcode>\S+)(?:\s+(?P<operand>\S+))?"
)
DIRECT_CALL_RE = re.compile(r"^(?P<target>[0-7]+)\((?P<link>[0-7]+)\)$")
REGISTER_TARGET_RE = re.compile(r"^\((?P<link>[0-7]+)\)$")
CONDITIONAL_RETURNS = {"u1a", "uza", "v1m", "vzm"}


def octal(value: int) -> str:
    return f"{value:05o}"


@dataclass
class Target:
    calls: int = 0
    links: collections.Counter[int] = field(default_factory=collections.Counter)
    callers: collections.Counter[int] = field(default_factory=collections.Counter)
    nested_call_observed: bool = False


@dataclass
class Frame:
    target: int
    link: int


@dataclass
class PendingReturn:
    frame: Frame
    return_pc: int
    delay: int = 0


@dataclass
class Analysis:
    instruction_lines: int = 0
    unique_pcs: set[int] = field(default_factory=set)
    targets: dict[int, Target] = field(
        default_factory=lambda: collections.defaultdict(Target)
    )


def analyze(path: Path) -> Analysis:
    result = Analysis()
    frames: list[Frame] = []
    pending_return: PendingReturn | None = None

    with path.open(encoding="utf-8", errors="replace") as stream:
        for line in stream:
            match = INSTRUCTION_RE.match(line)
            if not match:
                continue

            pc = int(match.group("pc"), 8)
            opcode = match.group("opcode")
            operand = match.group("operand") or ""
            result.instruction_lines += 1
            result.unique_pcs.add(pc)

            if pending_return:
                if pending_return.delay:
                    pending_return.delay -= 1
                else:
                    if (
                        frames
                        and frames[-1] is pending_return.frame
                        and pc == pending_return.return_pc
                    ):
                        frames.pop()
                    pending_return = None

            if opcode == "vjm":
                if frames:
                    result.targets[frames[-1].target].nested_call_observed = True

                call = DIRECT_CALL_RE.match(operand)
                if call:
                    target = int(call.group("target"), 8)
                    link = int(call.group("link"), 8)
                    stats = result.targets[target]
                    stats.calls += 1
                    stats.links[link] += 1
                    stats.callers[pc] += 1

                    frames.append(Frame(target=target, link=link))

            register_target = REGISTER_TARGET_RE.match(operand)
            if register_target and frames:
                link = int(register_target.group("link"), 8)
                if frames[-1].link == link:
                    if opcode == "uj":
                        frames.pop()
                    elif opcode in CONDITIONAL_RETURNS:
                        register = re.search(
                            rf"\br\[{link:o}\]=([0-7]+)\b", line
                        )
                        if register:
                            pending_return = PendingReturn(
                                frame=frames[-1],
                                return_pc=int(register.group(1), 8),
                            )

            if opcode == "utc" and frames:
                register_target = REGISTER_TARGET_RE.match(operand)
                if register_target:
                    link = int(register_target.group("link"), 8)
                    if frames[-1].link == link:
                        register = re.search(
                            rf"\br\[{link:o}\]=([0-7]+)\b", line
                        )
                        if register:
                            pending_return = PendingReturn(
                                frame=frames[-1],
                                return_pc=int(register.group(1), 8),
                                delay=1,
                            )

    return result


def caller_text(callers: collections.Counter[int], limit: int) -> str:
    ordered = sorted(callers.items(), key=lambda item: (-item[1], item[0]))
    shown = ordered[:limit]
    text = ", ".join(f"{octal(pc)}:{count}" for pc, count in shown)
    if len(ordered) > limit:
        text += f", +{len(ordered) - limit} more"
    return text


def markdown(path: Path, analysis: Analysis, caller_limit: int) -> str:
    static_pcs = sum(pc < 0o36000 for pc in analysis.unique_pcs)
    static_words = 0o36000
    coverage = 100.0 * static_pcs / static_words
    lines = [
        f"# Calls observed in `{path.name}`",
        "",
        f"- Instruction lines: {analysis.instruction_lines}",
        f"- Unique word addresses: {len(analysis.unique_pcs)}",
        f"- Static image addresses below `36000`: {static_pcs}/{static_words} "
        f"({coverage:.1f}%)",
        f"- Direct call targets: {len(analysis.targets)}",
        "",
        "| Address | Calls | Link registers | Observed kind | Main call sites |",
        "|---:|---:|---|---|---|",
    ]

    for address in sorted(analysis.targets):
        target = analysis.targets[address]
        links = ", ".join(f"r{link:o}" for link in sorted(target.links))
        kind = "nonleaf" if target.nested_call_observed else "leaf"
        lines.append(
            f"| {octal(address)} | {target.calls} | {links} | {kind} | "
            f"{caller_text(target.callers, caller_limit)} |"
        )

    return "\n".join(lines) + "\n"


def json_output(path: Path, analysis: Analysis) -> str:
    payload = {
        "trace": str(path),
        "instruction_lines": analysis.instruction_lines,
        "unique_word_addresses": len(analysis.unique_pcs),
        "static_image_unique_addresses": sum(
            pc < 0o36000 for pc in analysis.unique_pcs
        ),
        "static_image_words": 0o36000,
        "targets": [
            {
                "address": octal(address),
                "calls": target.calls,
                "link_registers": [f"r{link:o}" for link in sorted(target.links)],
                "observed_kind": (
                    "nonleaf" if target.nested_call_observed else "leaf"
                ),
                "callers": [
                    {"address": octal(pc), "calls": count}
                    for pc, count in sorted(
                        target.callers.items(), key=lambda item: (-item[1], item[0])
                    )
                ],
            }
            for address, target in sorted(analysis.targets.items())
        ],
    }
    return json.dumps(payload, ensure_ascii=False, indent=2) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("trace", type=Path)
    parser.add_argument(
        "--format", choices=("markdown", "json"), default="markdown"
    )
    parser.add_argument("--caller-limit", type=int, default=8)
    args = parser.parse_args()

    analysis = analyze(args.trace)
    if args.format == "json":
        print(json_output(args.trace, analysis), end="")
    else:
        print(markdown(args.trace, analysis, args.caller_limit), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
