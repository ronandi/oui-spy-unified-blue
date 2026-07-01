# OUI-SPY firmware — dev tasks. Run `just --list` to see everything.
# PlatformIO is invoked via mise so it works without shims on PATH.

# Default env: the Cardputer ADV node (most code paths: LCD + GPS).
default_env := "v3_app_controlled_cardputer_adv"

# Show available recipes.
default:
    @just --list

# One-time toolchain setup (installs pio via mise; bootstraps pip for uv venvs).
setup:
    # mise's pipx backend uses uv, whose venvs omit pip — PlatformIO needs it
    # for some tool packages (e.g. tool-esptoolpy's contrib deps).
    mise use "pipx:platformio"
    "$(mise where pipx:platformio)/platformio/bin/python" -m ensurepip --upgrade
    @echo "pio ready: $(mise exec -- pio --version)"

# Build one env (default: Cardputer ADV node). e.g. `just build v3_node_manager_wroom`
build env=default_env:
    mise exec -- pio run -e {{env}}

# Build every env in platformio.ini (the full matrix).
build-all:
    mise exec -- pio run

# Build + flash one env over USB (put board in download mode first).
flash env=default_env:
    mise exec -- pio run -e {{env}} -t upload

# Serial monitor @ 115200.
monitor:
    mise exec -- pio device monitor -b 115200

# Generate compile_commands.json (clangd / IDE intellisense) for one env.
compiledb env=default_env:
    mise exec -- pio run -e {{env}} -t compiledb

# Format C/C++ under src/ with .clang-format. WARNING: reflows the whole tree.
fmt:
    find src -type f \( -name '*.cpp' -o -name '*.h' -o -name '*.c' \) -print0 \
      | xargs -0 clang-format -i

# Remove build artifacts.
clean:
    mise exec -- pio run -t clean
