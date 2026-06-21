# Patched Shockolate ARM64 build

This repository builds the PortMaster Shockolate fork for ARM64 using the same
Ubuntu 19.10 ARM64 environment documented by the original porter.

Run the **Build patched Shockolate aarch64** workflow. Its
`sshock-aarch64-patched` artifact contains:

- `sshock.aarch64`
- `sshock.aarch64.sha256`

The source patch adds a configurable SDL renderer and a software-renderer
fallback for KMSDRM devices.
