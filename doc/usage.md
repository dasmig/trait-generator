# Usage Guide

This guide covers every feature of the trait-generator library. For a quick overview, see the [README](../README.md). For the full API reference, run `doxygen Doxyfile` from the repository root and open `doc/api/html/index.html`.

[TOC]

<!-- GitHub-rendered TOC (Doxygen uses [TOC] above instead) -->
<ul>
<li><a href="#quick-start">Quick Start</a></li>
<li><a href="#installation">Installation</a></li>
<li><a href="#loading-resources">Loading Resources</a></li>
<li><a href="#generating-personalities">Generating Personalities</a></li>
<li><a href="#personality-fields">Personality Fields</a></li>
<li><a href="#generation-model">Generation Model</a></li>
<li><a href="#the-catalogue">The Catalogue</a></li>
<li><a href="#extending-data">Extending Data</a></li>
<li><a href="#seeding-and-deterministic-generation">Seeding and Deterministic Generation</a></li>
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
    std::cout << gen.get_personality(dasmig::trait_request{}) << '\n'; // e.g. "Curious, Patient, Outgoing"
}
```

## Installation

Copy `dasmig/traitgen.hpp`, `dasmig/core.hpp` and the `dasmig/core/` directory into your include path, keeping them together. Supply `resources/` next to the program, or define `DASMIG_TRAIT_EMBED` to compile the catalogue in.

## Loading Resources

`instance()` loads the `lite` tier on first use, from the compiled-in copy or from the first of `resources/`, `../resources`, `trait-generator/resources` and `../trait-generator/resources` that has it.

```cpp
dasmig::trtg gen;                          // an empty, independent instance
bool ok = gen.load(dasmig::dataset::lite); // false when the tier is not found
gen.load(std::filesystem::path{"path/to/resources/lite"}); // a directory
gen.has_data();
gen.trait_count();  // 52
gen.axis_count();   // 5
gen.has_trait("brave");
```

A directory must contain `traits.tsv`; `axes.tsv`, `loadings.tsv` and `relations.tsv` are read when present. Each `load` replaces the data and every layer loaded before.

## Generating Personalities

```cpp
auto p = gen.get_personality(dasmig::trait_request{});       // three traits, random seed
auto q = gen.get_personality(dasmig::trait_request{}, 1234); // deterministic

// A request: how many traits, which must be present, which must not.
dasmig::trait_request req{.count = 5, .include = {"brave", "loyal"}, .exclude = {"greedy"}};
auto r = gen.get_personality(req);
auto s = gen.get_personality(req, 1234);
```

These are the only generation signatures: `(request)` and `(request, seed)`.
The request defaults to `count = 3`, with empty `include` and `exclude` lists.
Every call supplies its own choices; they never become defaults for later calls.
Data loading and catalogue layers are configured on the instance.

- `count` includes the forced traits. Fewer traits are returned when nothing else is compatible with those already chosen (a small catalogue with many conflicts).
- `include` traits come first, in the order given, and the draw treats them as already chosen: their conflicts are skipped and their synergies apply. They must exist and must not conflict with each other.
- `exclude` traits are never drawn; unknown names are ignored, so a request still works after a layer removes a trait.

## Personality Fields

| Member | Meaning |
| --- | --- |
| `traits` | Each with `key`, `label`, `group` (empty for none) and `tags`. Included traits first, then in the order drawn |
| `axes` | Every axis, in catalogue order, with `key`, `label`, `base` (drawn) and `value` (base plus the chosen traits' loadings) |
| `has(key)` | Whether a trait is present |
| `value(key)` | An axis's resolved value, or empty for an unknown axis |
| `tags()` | All the traits' tags, each once, in order of appearance (for example to feed nickname-generator) |
| `seed()`, `signature()` | Replay; see below |

## Generation Model

Each step draws from its own stream split from the call seed, so adding an axis or a step later leaves the others unchanged.

1. **Axes.** Each axis draws a base value from normal(mean, sd), from stream `"axis:<key>"`. An axis with sd 0 takes its mean.
2. **Affinity.** Each trait's weight is multiplied by `exp(sum of loading × z)`, where `z` is each axis's standardised base value. With a loading of 0.9 on conscientiousness, `organized` is about 2.5 times likelier for someone one standard deviation above the mean, and 2.5 times less likely one below. The exponent is clamped to ±30.
3. **Traits.** Included traits are taken; then traits are drawn one at a time by adjusted weight (stream `"traits"`). A trait is skipped when it is already chosen, excluded, in conflict with a chosen trait, or in the same group as one. Each chosen trait multiplies the weight of its synergy partners by the synergy's factor.
4. **Resolved axes.** Each axis value is its base plus the loadings of the chosen traits. A brave person's neuroticism goes down by 0.6, so the vector agrees with the traits.

## The Catalogue

Four tables in `resources/lite` (see [Data and Method](data.md) for how they were curated):

| File | Key | Columns |
| --- | --- | --- |
| `traits.tsv` | `trait` | `label`, `weight` (`>= 0`; 0 means never drawn, only included), `group` (optional; traits in a group exclude each other), `tags` (optional, `;`-separated) |
| `axes.tsv` | `axis` | `label`, `mean`, `sd` (`>= 0`) |
| `loadings.tsv` | `trait`, `axis` | `loading` |
| `relations.tsv` | `trait`, `other` | `relation` (`conflict` or `synergy`), `factor` (synergy only, `> 0`, default 2) |

Relations apply both ways, so a pair is defined once: a second row for the same two traits, in either order, is an error.

Loadings and relations that name a trait or axis that is not loaded are ignored, so removing a trait needs no other change. `unresolved()` lists them (for example `loadings.tsv:12: unknown axis 'opennes'`), to catch misspelt names.

## Extending Data

The shipped catalogue is a base layer. Files with the same columns can be loaded on top of it to add traits, axes, loadings and relations, change them or remove them, without editing the shipped files.

```cpp
using table = dasmig::trtg::table;

// A directory may hold any of the four files; all apply, or none do.
gen.extend("mods/survival");

// One file, by name.
gen.extend("mods/traits.tsv");

// Text, for example supplied by a WebAssembly host or a game scenario.
gen.extend_from_string(table::traits, "trait\tlabel\tweight\ttags\nscavenger\tScavenger\t3\tsurvival;loot\n",
                       "game");

// An axis for the game's utility AI: no base spread, fed only by traits.
gen.extend_from_string(table::axes, "axis\tlabel\tmean\tsd\nrisk\tRisk taking\t0\t0\n", "game");
gen.extend_from_string(table::loadings, "trait\taxis\tloading\nbrave\trisk\t1\ncautious\trisk\t-1\n", "game");

// Remove a trait: a layer needs only the key and a disabled column.
gen.extend_from_string(table::traits, "trait\tdisabled\ncruel\t1\n", "family friendly");

// Add mode refuses to touch traits that are already loaded.
gen.extend("mods/traits.tsv", dasmig::core::layer_mode::add);
```

- Every value in a layer is checked, and so are relations across the tables, before anything changes. A bad layer throws `dasmig::core::tsv_error` with the file, line and column, and the data stays as it was.
- `layers(table)` lists what is loaded in a table: source, mode, rows added, replaced and removed, and warnings such as unknown columns.
- `load(...)` replaces the base and drops every layer.

## Seeding and Deterministic Generation

- `get_personality(request, seed)` gives the same result for the same seed, request and data, on every platform, without advancing the instance engine.
- `get_personality(request)` draws a fresh seed from the instance engine, initially seeded from system entropy. There are no generator-level `seed()` or `unseed()` controls.
- For deterministic sequences, manage a `dasmig::core::rng` in the caller and pass its next value as the seed: `gen.get_personality(request, rng())`.
- `p.seed()` is the seed a result was generated from: `get_personality(request, p.seed())` replays it with the same request and data. The signature does not store the request; keep it alongside the result when replay is needed.

### Migrating legacy calls

Replace `get_personality()` with `get_personality(dasmig::trait_request{})` and
`get_personality(seed)` with `get_personality(dasmig::trait_request{}, seed)`.
Replace instance `seed()` / `unseed()` calls with explicit per-call seeds (or
a caller-owned `dasmig::core::rng` for sequences).

## Replay and JSON

Every result records the seed and the data it came from:

```cpp
auto p = gen.get_personality(dasmig::trait_request{});
p.signature().seed; // the call seed
p.signature().data; // gen.data_hash() when it was generated
```

`to_json()` gives the result as JSON, the form the WebAssembly layer and the game consume:

```json
{"traits":[{"key":"calm","label":"Calm","group":"temper","tags":["rest"]},
           {"key":"resilient","label":"Resilient","group":null,"tags":["survival","willpower"]},
           {"key":"traditional","label":"Traditional","group":"novelty","tags":["faith","routine"]}],
 "axes":{"openness":-2.001703471338275,"conscientiousness":0.16975813928315747,"extraversion":-0.2986160095442788,
         "agreeableness":-0.10037693181376535,"neuroticism":-1.157169454751688},
 "signature":{"seed":"0000000000000001","data":"ff9364ce0749f37c"}}
```

Seeds are strings because JavaScript numbers cannot hold every 64-bit value. `to_json(writer)` writes into an existing `dasmig::core::json_writer`, for composing larger documents.

## Compiled-in Data

Define `DASMIG_TRAIT_EMBED` before including the header to compile the catalogue into the program (about 11 KB). `load(dataset::lite)` and `instance()` then use it and need no files. The compiled-in data has the same `data_hash()` as the files, so seeds replay the same either way.

`dasmig/traitgen_data.hpp` is generated from `resources/lite` by `make embed`; CI checks it is up to date.

## Thread Safety

Each `trtg` instance is independent. Concurrent calls to `get_personality(request)` on the **same** instance require external synchronisation (e.g. a `std::mutex`). The `(request, seed)` overload is `const` and does not touch the engine. Data loading and extension must not run concurrently with generation.

## Error Reference

| Exception | When |
|-----------|------|
| `std::runtime_error("No trait data loaded. Call load() first.")` | Any generation call before data is loaded |
| `std::invalid_argument("Unknown trait: x")` | An included trait is not loaded |
| `std::invalid_argument` | An included trait is repeated, excluded or in conflict with another included trait, or more traits are included than `count` |
| `std::invalid_argument` | `extend(path)` with a file that is not one of the four tables |
| `dasmig::core::tsv_error` | A malformed file or layer, or a relation defined twice (message gives file, line and column); the data is unchanged |
