# Usage Guide

This guide covers every feature of the trait-generator library. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating">Generating</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#replay-and-json">Replay and JSON</a></li>
<li><a href="#compiled-in-data">Compiled-in Data</a></li>
<li><a href="#thread-safety">Thread Safety</a></li>
<li><a href="#error-reference">Error Reference</a></li>
</ul>

## Quick Start

```cpp
#include <dasmig/traitgen.hpp>
#include <iostream>

int main()
{
    auto& gen = dasmig::trtg::instance();
    std::cout << gen.get_personality() << '\n';
}
```

## Installation

Copy `dasmig/traitgen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them together. Supply `resources/` next to the program, or define `DASMIG_TRAIT_EMBED` to compile the `lite` tier in.

## Loading Resources

`instance()` loads the `lite` tier on first use (or `full` if there is no `lite`), from the compiled-in copy or from the first of `resources/`, `../resources`, `trait-generator/resources` and `../trait-generator/resources` that has it.

```cpp
dasmig::trtg gen;                       // an empty, independent instance
bool ok = gen.load(dasmig::dataset::lite); // false when the tier is not found
gen.load(std::filesystem::path{"path/to/resources/lite"}); // a directory
gen.has_data();
gen.count();
```

Each `load` replaces the data and every layer loaded before.

## Generating

```cpp
auto v = gen.get_personality();      // draws a seed from the instance engine
auto w = gen.get_personality(1234);  // deterministic; does not touch the engine
v.key;   // the catalogue key
v.label; // the display label
```

Entries are drawn by their `weight` column. An entry with weight 0 is loaded but never drawn.

## Seeding and Deterministic Generation

- `get_personality(seed)` gives the same result for the same seed and data, on every platform.
- `seed(s)` makes the instance engine deterministic, so a sequence of `get_personality()` calls repeats; `unseed()` reseeds it from the system.
- `v.seed()` is the seed a result was generated from: `get_personality(v.seed())` replays it.

## Extending Data

The shipped data is a base layer. Files with the same columns as `personality.tsv` can be loaded on top of it to add entries, change them or remove them, without editing the shipped file. Rows are keyed by `key`.

```cpp
// Override mode (the default): a row replaces the loaded row with its key;
// new keys are added.
gen.extend("mods/extra.tsv");

// From text, for example supplied by a WebAssembly host.
gen.extend_from_string("key\tlabel\tweight\nzulu\tZulu\t2\n", "scenario");

// Remove entries: a layer needs only the key and a disabled column.
gen.extend_from_string("key\tdisabled\nalpha\t1\n", "cleanup");

// Add mode refuses to touch keys that are already loaded.
gen.extend("mods/new.tsv", dasmig::core::layer_mode::add);
```

- Every value in a layer is checked before anything changes. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the data stays as it was.
- `layers()` lists what is loaded: source, mode, rows added, replaced and removed, and warnings such as unknown columns.
- `load(...)` replaces the base and drops every layer.

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto v = gen.get_personality();
v.signature().seed; // the call seed
v.signature().data; // gen.data_hash() when it was generated
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume. Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`, for composing larger documents.

## Compiled-in Data

Define `DASMIG_TRAIT_EMBED` before including the header to compile the `lite` tier into the program. `load(dataset::lite)` and `instance()` then use it and need no files. The compiled-in data has the same `data_hash()` as the file, so seeds replay the same either way.

`dasmig/traitgen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Thread Safety

Each `trtg` instance is independent. Concurrent calls to `get_personality()` on the **same** instance require external synchronisation (e.g. a `std::mutex`).

## Error Reference

| Exception | When |
|-----------|------|
| `std::runtime_error` | Any generation call before data is loaded |
| `std::invalid_argument` | Every loaded entry has weight 0 |
| `dasmig::core::tsv_error` | A malformed `personality.tsv` or layer (message gives file, line and column); the data is unchanged |
