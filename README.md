# NIXPAK

NIXPAK is a NixOS Power-User Package Manager capable of seamlessly managing both native NixOS packages (modifying `/etc/nixos/nixpkg-apps.nix`) and Flathub Flatpaks. It provides multiple user interfaces to suit different environments, from a full-fledged GTK4 graphical desktop app to terminal-based and framebuffer modes.

## Interfaces

NIXPAK supports four main modes of operation:

### 1. GUI Mode
A rich, graphical desktop application built with GTK4. It features:
* A modern interface for browsing and searching packages.
* Detailed package information, including screenshots, source information (Nixpkgs vs. Flathub), safety status badges, age ratings, and download sizes.
* The ability to "Try in Shell" for testing packages before installation.
* Graphical system operations (Test, Switch, Boot, Rollback, Garbage Collect).
* **Note:** This mode is deployed via the included `nixpkg-packer.sh` script, which packs a Python backend and the GTK4 UI.

### 2. TUI Mode (Terminal UI)
A fully-featured text-based user interface (`nixpkg tui`) utilizing ANSI escape codes and a 256-color palette. It provides:
* A menu-driven interface to search, install, and remove packages directly from your terminal.
* System configuration management.
* Works in standard terminal emulators without relying on a full desktop environment.

### 3. Framebuffer Mode (FB)
A direct-to-screen framebuffer UI (`nixpkg fb`) utilizing DRM/KMS and `evdev` for input.
* Operates completely independent of a display server (no X11/Wayland required).
* Requires access to `/dev/fb0` and input devices.
* Ideal for recovery scenarios or minimal TTY-only environments.

### 4. CLI Mode (Command Line)
Standard command-line usage for scripting or quick operations:
* `nixpkg install <attr>`: Install a package (adds to NixOS config or installs via Flatpak).
* `nixpkg remove <attr>`: Remove a package.
* `nixpkg search <query>`: Search for packages across Nixpkgs and Flathub.
* `nixpkg test|switch|boot`: Run `nixos-rebuild <mode>`.
* `nixpkg rollback`: Rollback the system to the previous generation.
* `nixpkg gc`: Run Nix garbage collection.

## Installation

### Core CLI, TUI, and FB Modes
To build and install the core binary (which includes CLI, TUI, and FB modes):

```sh
make
sudo make install
```
This installs `nixpkg` to `/usr/local/bin/`.

### Full GUI Mode
To build and install the full GTK4 GUI alongside the core utilities, run the packer script as root:

```sh
sudo ./nixpkg-packer.sh
```
This script handles extracting, compiling the GTK4 UI and Python backend, and deploying the application shortcuts to your system.

## Configuration (NixOS)
For native NixOS packages, NIXPAK modifies `/etc/nixos/nixpkg-apps.nix`. Ensure this file is imported in your main `configuration.nix` and contains the `# NIXPKG_START` marker so NIXPAK knows where to insert packages:

```nix
# /etc/nixos/nixpkg-apps.nix
{ config, pkgs, ... }:

{
  environment.systemPackages = with pkgs; [
    # NIXPKG_START
  ];
}
```
