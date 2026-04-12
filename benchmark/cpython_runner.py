#!/usr/bin/env python3

import pathlib
import sys
import time


def main() -> int:
    benchmark_path = pathlib.Path(sys.argv[1])
    n = int(sys.argv[2])

    namespace = {}
    source = benchmark_path.read_text(encoding="utf-8")
    code = compile(source, str(benchmark_path), "exec")
    exec(code, namespace, namespace)

    run = namespace.get("run")
    if run is None:
        raise RuntimeError("benchmark source must define run(n)")

    sys.stdout.write("READY\n")
    sys.stdout.flush()

    for line in sys.stdin:
        command = line.strip()
        if command.startswith("RUN "):
            batch_size = int(command.split()[1])
            start = time.perf_counter()
            result = None
            for _ in range(batch_size):
                result = run(n)
            elapsed = time.perf_counter() - start
            sys.stdout.write(f"{result} {elapsed:.17g}\n")
            sys.stdout.flush()
        elif command == "QUIT":
            return 0
        else:
            raise RuntimeError(f"unknown command: {command}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
