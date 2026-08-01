#!/usr/bin/env python3
"""Run the Man-or-Boy test and measure its dynamic call nesting."""

from __future__ import annotations

import argparse
import sys
from collections import Counter
from dataclasses import dataclass, field
from typing import Callable


PopFunction = Callable[[], int]


class DoesNotTerminate(RuntimeError):
    pass


@dataclass
class CallMeter:
    current_depth: int = 0
    maximum_depth: int = 0
    current_a_depth: int = 0
    maximum_a_depth: int = 0
    calls: Counter[str] = field(default_factory=Counter)

    def enter(self, kind: str) -> None:
        self.calls[kind] += 1
        self.current_depth += 1
        self.maximum_depth = max(self.maximum_depth, self.current_depth)
        if kind == "A":
            self.current_a_depth += 1
            self.maximum_a_depth = max(
                self.maximum_a_depth, self.current_a_depth
            )

    def leave(self, kind: str) -> None:
        if kind == "A":
            self.current_a_depth -= 1
        self.current_depth -= 1


@dataclass(frozen=True)
class Result:
    k: int
    value: int
    maximum_depth: int
    maximum_a_depth: int
    a_calls: int
    b_calls: int
    leaf_calls: int

    @property
    def total_calls(self) -> int:
        return self.a_calls + self.b_calls + self.leaf_calls


def _evaluate(k: int, constants: tuple[int, ...]) -> Result:
    meter = CallMeter()

    def constant(value: int) -> PopFunction:
        def invoke() -> int:
            meter.enter("leaf")
            try:
                return value
            finally:
                meter.leave("leaf")

        return invoke

    def a(
        initial_k: int,
        functions: tuple[PopFunction, ...],
    ) -> int:
        meter.enter("A")
        try:
            k_cell = [initial_k]

            def b() -> int:
                meter.enter("B")
                try:
                    k_cell[0] -= 1
                    return a(k_cell[0], (b, *functions[:-1]))
                finally:
                    meter.leave("B")

            if k_cell[0] <= 0:
                return functions[-2]() + functions[-1]()
            return b()
        finally:
            meter.leave("A")

    value = a(k, tuple(constant(value) for value in constants))
    return Result(
        k=k,
        value=value,
        maximum_depth=meter.maximum_depth,
        maximum_a_depth=meter.maximum_a_depth,
        a_calls=meter.calls["A"],
        b_calls=meter.calls["B"],
        leaf_calls=meter.calls["leaf"],
    )


def man_or_boy(k: int, arity: int = 5) -> Result:
    """Evaluate a reduced or canonical functional-argument closure test."""

    previous_limit = sys.getrecursionlimit()
    if previous_limit < 100_000:
        sys.setrecursionlimit(100_000)
    try:
        if not 2 <= arity <= 5:
            raise ValueError("arity must be between 2 and 5")
        if arity == 2 and k > 0:
            raise DoesNotTerminate(
                "the two-argument rotation calls the captured B forever"
            )
        constants = (1, -1, -1, 1, 0)[:arity]
        return _evaluate(k, constants)
    finally:
        if previous_limit < 100_000:
            sys.setrecursionlimit(previous_limit)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "k",
        type=int,
        nargs="*",
        help="specific initial K values; defaults to every value through --max-k",
    )
    parser.add_argument("--max-k", type=int, default=10)
    parser.add_argument(
        "--arity",
        type=int,
        choices=(2, 3, 4, 5),
        default=5,
        help="number of functional arguments (default: 5)",
    )
    args = parser.parse_args()

    if args.max_k < 0:
        parser.error("--max-k must be nonnegative")
    values = args.k if args.k else range(args.max_k + 1)

    print(
        " K  RESULT  MAX_DEPTH  MAX_A_DEPTH"
        "     A_CALLS     B_CALLS  LEAF_CALLS  TOTAL_CALLS"
    )
    for value in values:
        try:
            result = man_or_boy(value, args.arity)
        except DoesNotTerminate:
            print(
                f"{value:2d} {'DIVERGES':>7} {'unbounded':>10}"
                f" {'unbounded':>12} {'-':>11} {'-':>11} {'-':>11} {'-':>12}"
            )
            continue
        print(
            f"{result.k:2d} {result.value:7d} {result.maximum_depth:10d}"
            f" {result.maximum_a_depth:12d} {result.a_calls:11d}"
            f" {result.b_calls:11d} {result.leaf_calls:11d}"
            f" {result.total_calls:12d}"
        )


if __name__ == "__main__":
    main()
