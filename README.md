# Custom UEFI Bootloader & VMM Initialization Engine (`kimimaro`)

A low-level UEFI bootloader and initialization module written in C using the **EDK II framework**, designed to set up and bootstrap a custom x86-64 Hypervisor / Virtual Machine Monitor (VMM).

---

## 🚀 Key Features

* **VMM Binary Loading:** Dynamically locates, opens, and loads `vmm.bin` from the UEFI file system, calculating required memory footprints and page allocations.
* **Page Table Construction:** Sets up identity mapping and custom virtual memory mappings (PML4, PDPTE, PDE) supporting both 2MB and 4KB page structures.
* **Multi-Processor (MP) Management:** Utilizes the UEFI MP Services Protocol to detect enabled CPU cores, query core topologies, and prepare Application Processor (AP) wake-up mechanisms.
* **ACPI MADT Parsing:** Automatically parses the ACPI RSDP, XSDT, and MADT tables to extract Legacy APIC IDs and x2APIC IDs for all active processors.
* **Processor State Capture:** Captures complete system states (Control/Segment registers, GDTR, IDTR, MSRs, and RFlags) via `UEFI_STATE_STRUCT` to facilitate seamless transitions into the VMM environment.
* **GDT & TSS Setup:** Initializes Global Descriptor Tables (GDT) and Task State Segments (TSS) for both 32-bit and 64-bit execution modes.

---

## 🛠️ Code Architecture & Core Modules

* **`CreateNewEnvironment()`**: Initializes the core runtime environment state structure and hooks up function pointers for setup, memory mapping, GDT/TSS configuration, and AP management.
* **`OpenVmmBinary()` & `ReadVmmBinary()`**: Handles UEFI protocol interactions (`EFI_SIMPLE_FILE_SYSTEM_PROTOCOL`, `EFI_LOADED_IMAGE_PROTOCOL`) to ingest the core VMM image.
* **`StartSetup()`**: Orchestrates the entire pre-boot sequence, allocating stacks, configuring descriptors, mapping page tables, and initializing parameters.
* **`InitVmmParameters()`**: Gathers multi-core topology, configures APIC base MSRs (`0x1b`), and populates initialization structures passed directly to the VMM payload.
* **`CreateUefiState()`**: Snapshots current CPU control registers (`CR0`, `CR2`, `CR3`, `CR4`), segment selectors, and descriptor table registers prior to environment handoff.

---

## 📂 Project Structure

```text
├──BootloaderPkg/
|   ├── asm/enter.nasm      # Initialize context switching and jump to VMM
│   ├── init/boot.h         # Boot and transition entry definitions
|   ├── init/boot.c         # Just entry code
│   ├── lib/setup.h         # Environment and state structure definitions
|   ├── lib/setup.h         # Core UEFI bootloader & VMM environment setup
└── vmm.bin                 # Target VMM binary payload (loaded at runtime)!(not here)!

💻 Requirements & Build Environment

    Firmware Interface: UEFI Development Kit (EDK II)

    OS: Linux, Windows
		if you want run on windows, you should modify little code.
		ex) enter.nasm, some functions

    Toolchain: MSVC / GCC / NASM

    Target Architecture: x86-64 (Intel)

🚀 Usage

    Place your compiled hypervisor binary as vmm.bin in the root directory of the boot medium.

    Integrate this module into your EDK II Package (.dsc / .inf).

    Boot the UEFI application to initialize the hypervisor environment and transfer control to the VMM payload.

