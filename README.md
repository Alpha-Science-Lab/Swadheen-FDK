# Build and Usage Guide

This project uses GNU Make and the RISC-V GCC toolchain to build C and Assembly programs for the processor.

The build system generates several output formats, including ELF, Intel HEX, binary, Verilog memory initialization files, and disassembly files.

## Prerequisites

Install the following tools:

* GNU Make
* RISC-V GCC toolchain
* RISC-V Binutils

The following executables must be available in your `PATH`:

```text
riscv32-unknown-elf-gcc
riscv32-unknown-elf-objcopy
riscv32-unknown-elf-objdump
make
```

You can verify the RISC-V toolchain with:

```bash
riscv32-unknown-elf-gcc --version
riscv32-unknown-elf-objcopy --version
riscv32-unknown-elf-objdump --version
```

## Directory Structure

The build system expects the following directory structure:

```text
.
├── Makefile
├── examples/
│   ├── asm/
│   │   ├── example1.s
│   │   └── example2.s
│   │
│   └── c/
│       ├── example1.c
│       └── example2.c
│
├── std/
│   ├── hades-v.ld
│   ├── include/
│   └── src/
│
└── build/
```

The `build/` directory is created automatically.

> **Note:** Do not build the project from a directory containing spaces. The Makefile explicitly rejects such directories.

---

# Building Assembly Examples

Assembly examples are automatically discovered from:

```text
examples/asm/
```

For example, if the following file exists:

```text
examples/asm/hello.s
```

build it using:

```bash
make examples/asm/hello
```

The generated files will be placed in:

```text
build/examples/asm/hello/
```

### Generated Files

| File       | Description                        |
| ---------- | ---------------------------------- |
| `out.elf`  | RISC-V executable                  |
| `out.dis`  | Disassembly and symbol information |
| `out.hex`  | Intel HEX representation           |
| `out.bin`  | Raw binary image                   |
| `init.mem` | Verilog memory initialization file |

The build process is:

```text
Assembly Source (.s)
        │
        ▼
     out.elf
     ┌──┼─────┐
     │  │     │
     ▼  ▼     ▼
  out.dis  out.hex  out.bin
                       │
                       ▼
                    init.mem
```

When the build completes successfully:

```text
Done!
```

is printed.

---

# Building C Examples

C examples are automatically discovered from:

```text
examples/c/
```

For example, if the following file exists:

```text
examples/c/blinky.c
```

build it using:

```bash
make examples/c/blinky
```

The generated files will be placed in:

```text
build/examples/c/blinky/
```

### Generated Files

| File       | Description                            |
| ---------- | -------------------------------------- |
| `out.o`    | Compiled object file                   |
| `out.elf`  | Linked RISC-V executable               |
| `out.dis`  | Disassembly for debugging              |
| `out.hex`  | Intel HEX file for bootloader transfer |
| `out.bin`  | Raw binary image                       |
| `init.mem` | Verilog memory initialization file     |

The build process is:

```text
C Source (.c)
        │
        ▼
      out.o
        │
        ▼
      out.elf
     ┌───┼────────┐
     │   │        │
     ▼   ▼        ▼
  out.dis out.hex out.bin
                     │
                     ▼
                  init.mem
```

The standard library sources from:

```text
std/src/
```

are compiled automatically and linked with the C application.

When the build completes successfully:

```text
Done!
```

is printed.

---

# RISC-V ISA Configuration

The Makefile supports two ISA configurations.

## RV32I

By default, the processor is built without the RISC-V Multiplication and Division extension.

```bash
make examples/c/blinky
```

This uses:

```text
-march=rv32i_zicsr_zifencei
-mabi=ilp32
```

The same configuration applies to Assembly examples:

```bash
make examples/asm/hello
```

## RV32IM

To enable the RISC-V `M` extension, set `M_EXT=1`:

```bash
make M_EXT=1 examples/c/blinky
```

For Assembly:

```bash
make M_EXT=1 examples/asm/hello
```

This uses:

```text
-march=rv32im_zicsr_zifencei
-mabi=ilp32
```

Use `M_EXT=1` only when the target processor supports the RISC-V Multiplication and Division extension.

---

# Using the Generated Files

## Simulation and Synthesis

Use the generated memory initialization file:

```text
init.mem
```

Example:

```text
build/examples/c/blinky/init.mem
```

This file is intended for initializing the processor instruction memory during RTL simulation or FPGA synthesis.

## Bootloader Programming

Use the generated Intel HEX file:

```text
out.hex
```

Example:

```text
build/examples/c/blinky/out.hex
```

This file can be transferred to the processor bootloader.

## Debugging

Use the ELF and disassembly files:

```text
out.elf
out.dis
```

The disassembly file can be inspected directly:

```bash
cat build/examples/c/blinky/out.dis
```

or searched for specific functions:

```bash
grep -n "main" build/examples/c/blinky/out.dis
```

---

# Cleaning the Project

To remove all generated build files:

```bash
make clean
```

This removes:

```text
build/
```

The source files in `examples/` and `std/` are not affected.

---

# Adding a New Example

## C Example

Create a new file:

```text
examples/c/my_program.c
```

Then build it with:

```bash
make examples/c/my_program
```

The output will be generated in:

```text
build/examples/c/my_program/
```

## Assembly Example

Create a new file:

```text
examples/asm/my_program.s
```

Then build it with:

```bash
make examples/asm/my_program
```

The output will be generated in:

```text
build/examples/asm/my_program/
```

No Makefile changes are required when adding new examples. The build system automatically discovers all `.c` files in `examples/c/` and all `.s` files in `examples/asm/`.

---

# Build Failure Behavior

The `Done!` message is printed only after all required build steps complete successfully.

For example:

```text
Compile
   ↓
Link
   ↓
Generate ELF
   ↓
Generate HEX
   ↓
Generate Binary
   ↓
Generate Memory File
   ↓
Generate Disassembly
   ↓
Success
   ↓
Done!
```

If any compilation, linking, or conversion command fails, GNU Make stops with an error and `Done!` is not printed.

Example:

```text
riscv32-unknown-elf-gcc: error: ...
make: *** [Makefile:XX: target] Error 1
```

This behavior assumes errors are not explicitly ignored using mechanisms such as `make -i`, a leading `-` before a command, or shell constructs such as `|| true`.
