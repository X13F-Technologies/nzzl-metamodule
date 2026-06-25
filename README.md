# NZZL — MetaModule Plugin

A custom plugin for the [4ms MetaModule](https://4mscompany.com/metamodule) Eurorack module, developed under the X13F-Technologies GitHub account.

The same source code compiles for two targets:

| Target | Output | How |
|---|---|---|
| **VCV Rack 2** | `plugin.dylib` | Local Mac build — for rapid visual development |
| **4ms MetaModule** | `NZZL.mmplugin` | GitHub Actions CI — ARM cross-compile |

---

## Prerequisites

| Tool | Version | Install |
|---|---|---|
| macOS | Apple Silicon (arm64) | — |
| Xcode Command Line Tools | any | `xcode-select --install` |
| Homebrew | any | [brew.sh](https://brew.sh) |
| CMake | 3.22+ | `brew install cmake` |
| Ninja | any | `brew install ninja` |
| GitHub CLI | any | `brew install gh` |
| VCV Rack 2 Free | 2.x | [vcvrack.com](https://vcvrack.com) |
| Rack SDK | 2.6.6 arm64 | See setup below |

The ARM cross-compiler (`arm-none-eabi-gcc 12.3`) is **not needed locally** — CI handles it.

---

## First-time setup

```bash
# 1. Clone with submodules
git clone --recurse-submodules https://github.com/X13F-Technologies/nzzl-metamodule.git
cd nzzl-metamodule

# 2. Download the Rack SDK (arm64) once — not committed to the repo
curl -L https://vcvrack.com/downloads/Rack-SDK-2.6.6-mac-arm64.zip -o /tmp/Rack-SDK.zip
unzip /tmp/Rack-SDK.zip -d ~/Documents/Rack-SDK
```

---

## Development workflow

### Build and test in VCV Rack (fast loop)

```bash
cmake -B build-vcv -DRACK_DIR="$HOME/Documents/Rack-SDK"
cmake --build build-vcv
cmake --install build-vcv
# Restart VCV Rack 2 Free — NZZL appears in the module browser
```

After the first configure you only need to re-run `cmake --build build-vcv && cmake --install build-vcv` on each change.

### Build for MetaModule hardware

Push to `main` — GitHub Actions does everything:

```bash
git add . && git commit -m "your message" && git push
```

The workflow (`.github/workflows/build.yml`) installs `arm-none-eabi-gcc 12.3` exactly, cross-compiles, and uploads `NZZL.mmplugin` as a downloadable artifact.

To download the artifact:

```bash
# via CLI (gets the latest run)
gh run download --repo X13F-Technologies/nzzl-metamodule --dir ~/Downloads/nzzl-build

# or open in browser
# https://github.com/X13F-Technologies/nzzl-metamodule/actions
# click the latest run → scroll to Artifacts → download NZZL-plugin
```

---

## Repo structure

```
nzzl-metamodule/
├── src/
│   └── plugin.cc          # Entry point — init() registers all modules
│   └── (your-module).cc   # One file per module
├── assets/                # Panel PNGs and other resources (committed via .gitkeep)
├── metamodule-plugin-sdk/ # Git submodule — 4ms SDK (rack-interface + ARM libs)
├── .github/
│   └── workflows/
│       └── build.yml      # CI: ARM cross-compile → .mmplugin artifact
├── CMakeLists.txt         # Dual-target: VCV Rack or MetaModule depending on RACK_DIR
├── plugin.json            # VCV Rack manifest (slug, modules list, version)
└── plugin-mm.json         # MetaModule manifest (maintainer info)
```

---

## Adding a new module

1. Create `src/your-module.cc` — implement a `rack::Module` subclass and a `rack::ModuleWidget` subclass using the standard Rack API (`rack.hpp`).

2. Forward-declare the model in `src/plugin.cc` and register it:
   ```cpp
   extern rack::Model* modelYourModule;

   void init(rack::Plugin* p) {
       pluginInstance = p;
       p->addModel(modelYourModule);
   }
   ```

3. Add the module's slug to `plugin.json`:
   ```json
   "modules": [
     { "slug": "YourModule", "name": "Your Module", "description": "...", "tags": [] }
   ]
   ```

4. Add the source file to `CMakeLists.txt` under `target_sources(NZZL PRIVATE ...)`.

5. Build and test in VCV Rack, then push to deploy to MetaModule.

---

## Key SDK facts

- **rack.hpp source**: for MetaModule builds, `rack.hpp` comes from `metamodule-plugin-sdk/rack-interface/include/` (a compatibility shim, not the real Rack SDK). For VCV builds it comes from the Rack SDK. The API is intentionally close to Rack SDK v2.4.1.
- **ARM GCC version**: must be exactly 12.2 or 12.3. The SDK enforces this and will error on other versions.
- **C++ standard**: C++20 required.
- **MetaModule output**: `.mmplugin` files are tar archives containing `plugin.json`, `plugin-mm.json`, `NZZL.so` (ARM shared lib), and the `assets/` directory.
- **VCV Rack output**: `plugin.dylib` installed to `~/Library/Application Support/Rack2/plugins-mac-arm64/NZZL/`.

---

## CI workflow notes

- Trigger: every push to `main`
- Runner: `ubuntu-24.04`
- ARM toolchain: installed via `carlosperate/arm-none-eabi-gcc-action@v1` at release `12.3.Rel1`
- CMake: `3.26.x` via `jwlawson/actions-setup-cmake@v1.13`
- Artifact retention: 90 days (GitHub Actions default)

---

## Submodule

The MetaModule Plugin SDK is pinned at a specific commit via the git submodule. To update it:

```bash
git submodule update --remote metamodule-plugin-sdk
git add metamodule-plugin-sdk
git commit -m "Update MetaModule SDK to latest"
```

Test locally and verify CI still passes before merging an SDK bump.
