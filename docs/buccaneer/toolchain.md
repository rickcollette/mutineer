# Buccaneer Toolchain

Command-line tools for compiling, checking, and running Buccaneer door programs. Sources live in `src/buccaneer/`.

## Build the tools

```bash
cd src/buccaneer
make
```

| Binary | Purpose |
|--------|---------|
| `bucc` | Compile `.bucc` → bytecode module |
| `bucc-linter` | Static analysis and policy checks |
| `bucc-formatter` | Format source consistently |
| `bucc-simulator` | Run bytecode with terminal UI |

Docker-based regression tests: `src/buccaneer/tests/run-tests-docker.sh`.

## Compile a program

```bash
./bucc examples/hello.bucc -o hello.bcm
```

Typical flags (see `./bucc --help`):

- `-o path` — output module path
- `-v` — verbose diagnostics

## Run in the simulator

```bash
./bucc-simulator hello.bcm
```

The simulator implements a full `TERM` API on stdout/stdin — ideal for developing menus and input loops before BBS embedding.

## Lint and format

```bash
./bucc-linter examples/hello.bucc
./bucc-formatter -w examples/hello.bucc
```

Run both before packaging a door for production.

## Project layout (door application)

```
my-door/
  door.json           # manifest (capabilities, entry program)
  programs/
    main.bucc
    helper.bucc
  data/               # optional static assets
  buccaneer.toml      # optional tool config
```

See [Door Packages](door-packages.md).

## Examples in the repo

| File | Demonstrates |
|------|----------------|
| `examples/hello.bucc` | Terminal I/O, `USER.NAME` |
| `examples/guess.bucc` | Input loop, conditionals |
| `examples/scoreboard.bucc` | Data-oriented door patterns |

## CMake integration note

Buccaneer currently builds via `src/buccaneer/Makefile`, not the top-level CMake graph. The main `mutineer` binary does not yet link BUCC by default — see [GitHub BUCC build issues](https://github.com/rickcollette/mutineer/issues?q=BUILD).

## Next steps

- [Programmer's Guide](programmers-guide.md)
- [Host API Reference](host-api.md)
