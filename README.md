# Trait Generator for C++

> **Requires C++23** (e.g., `-std=c++23` for GCC/Clang, `/std:c++latest` for MSVC).

[![Trait Generator for C++](https://raw.githubusercontent.com/dasmig/trait-generator/main/doc/trait-generator.png)](https://github.com/dasmig/trait-generator/releases)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/dasmig/trait-generator/main/LICENSE.MIT)
[![CI](https://github.com/dasmig/trait-generator/actions/workflows/ci.yml/badge.svg)](https://github.com/dasmig/trait-generator/actions/workflows/ci.yml)
[![GitHub Releases](https://img.shields.io/github/release/dasmig/trait-generator.svg)](https://github.com/dasmig/trait-generator/releases)
[![C++23](https://img.shields.io/badge/standard-C%2B%2B23-blue.svg)](https://en.cppreference.com/w/cpp/23)
[![Header-only](https://img.shields.io/badge/type-header--only-green.svg)](https://github.com/dasmig/trait-generator#integration)
[![Documentation](https://img.shields.io/badge/docs-GitHub%20Pages-blue.svg)](https://dasmig.github.io/trait-generator/)

**[API Reference](https://dasmig.github.io/trait-generator/)** · **[Usage Guide](doc/usage.md)** · **[Data and Method](doc/data.md)** · **[Releases](https://github.com/dasmig/trait-generator/releases)**

Personality traits from a weighted catalogue with conflicts, synergies and Big Five axes.

## Features

- **Coherent Personalities**. Traits are drawn one at a time: traits that conflict with one already chosen (brave and cowardly, calm and anxious) are skipped, and synergies (kind and generous, shy and quiet) make related traits likelier.

- **Big Five Axes**. Each person draws openness, conscientiousness, extraversion, agreeableness and neuroticism, and traits that match them are likelier: a conscientious person tends to be organized. The chosen traits then shift the axes, giving a resolved vector a game AI can read.

- **A Catalogue That Is Data**. 52 curated traits with groups, tags, axis loadings and relations, in four TSV tables. A game adds its own traits, axes (for example "risk taking" for a utility AI) and relations as layers, without touching the library.

- **Requests**. Ask for any number of traits, force some in (`include`) and keep others out (`exclude`); the rest stay consistent with them.

- **Deterministic Seeding**. Per-call `get_personality(request, seed)` for reproducible results, and `personality::seed()` for replaying a previous generation with the same request and data.

- **Batch Generation**. Generate thousands of results from one seed, on several threads with the same output for any thread count, optionally unique by a key, and stream them as JSON Lines or CSV (see the usage guide).

- **Identical Everywhere**. A seed gives the same personality on every compiler, standard library and CPU (GCC, Clang, MSVC, Emscripten; x86-64 and ARM64), checked by known-answer tests.

- **Extensible Data**. Load your own TSV layers on top of the shipped catalogue to add, change or remove traits, axes and relations (`extend()`), checked before they are applied.

- **JSON and Replay**. `to_json()` on every result, and a `signature()` recording the seed and the data it came from.

- **Compiled-in Data**. Define `DASMIG_TRAIT_EMBED` to build the catalogue into the program, for WebAssembly and other builds without files.

- **Multi-Instance Support**. Construct independent `trtg` instances with their own catalogue and random engine.

## Integration

[`traitgen.hpp`](https://github.com/dasmig/trait-generator/blob/main/dasmig/traitgen.hpp) is the library [released here](https://github.com/dasmig/trait-generator/releases). Keep [`core.hpp`](https://github.com/dasmig/trait-generator/blob/main/dasmig/core.hpp) and the [`core/`](https://github.com/dasmig/trait-generator/tree/main/dasmig/core) directory ([dasmig-core](https://github.com/dasmig/dasmig-core)) in the same directory. Add

```cpp
#include <dasmig/traitgen.hpp>
```

to the files you want to use it in, and supply the [`resources`](https://github.com/dasmig/trait-generator/tree/main/resources) folder (or define `DASMIG_TRAIT_EMBED`).

## Usage

```cpp
#include <dasmig/traitgen.hpp>
#include <iostream>

auto& gen = dasmig::trtg::instance(); // loads the catalogue

auto p = gen.get_personality(dasmig::trait_request{}); // three traits
std::cout << p << '\n';               // "Calm, Resilient, Traditional"
p.has("brave");                       // false
p.value("neuroticism");               // resolved axis value
p.tags();                             // {"rest", "survival", "willpower", ...}

// Five traits, one forced in, one kept out.
const dasmig::trait_request request{.count = 5, .include = {"brave"}, .exclude = {"greedy"}};
auto q = gen.get_personality(request);

// The same seed gives the same personality everywhere.
auto again = gen.get_personality(request, q.seed());
std::cout << again.to_json() << '\n';
```

See the [usage guide](doc/usage.md) for extending the catalogue and the generation model in detail.

Generation always takes a `trait_request`: `get_personality(request)` or
`get_personality(request, seed)`. Its defaults are three traits with no inclusions
or exclusions; no generation choices are stored on the generator. Data loading
and catalogue layers remain instance-level.

## Building

```bash
make            # example
make test       # tests
make bench      # benchmarks, results in build/bench.tsv
make test-embed # tests with the lite tier compiled in
make coverage   # code coverage
make lint       # clang-tidy
make docs       # API docs
make embed      # regenerate dasmig/traitgen_data.hpp from resources/lite
```

## Compiler Support

Tested with:
- Clang 18+ (`-std=c++23`)
- GCC 14+ (`-std=c++23`)
- MSVC 19.38+ (`/std:c++latest`)
- Emscripten 3.1.74

## Dependencies

| Dependency | Version | Bundled | Purpose |
|-----------|---------|---------|---------|
| [dasmig-core](https://github.com/dasmig/dasmig-core) | 1.4 | Yes (`core.hpp`, `core/`) | Platform-independent random engine and distributions, data layers, JSON |
| [Catch2](https://github.com/catchorg/Catch2) | 3.x | Yes (amalgamated) | Unit testing |

## Related Libraries

| Library | Description |
|---------|-------------|
| [address-generator](https://github.com/dasmig/address-generator) | Postal addresses with real streets by place |
| [biodata-generator](https://github.com/dasmig/biodata-generator) | Procedural human physical characteristics |
| [birth-generator](https://github.com/dasmig/birth-generator) | Demographically plausible birthdays |
| [city-generator](https://github.com/dasmig/city-generator) | Weighted city selection by population |
| [country-generator](https://github.com/dasmig/country-generator) | Weighted country selection by population |
| [device-generator](https://github.com/dasmig/device-generator) | Devices, browsers, user agents and IP addresses that fit together |
| [email-generator](https://github.com/dasmig/email-generator) | Email addresses in each country's style |
| [entity-generator](https://github.com/dasmig/entity-generator) | ECS-based entity generation |
| [interest-generator](https://github.com/dasmig/interest-generator) | Religion, hobbies and favourite media by country, age and sex |
| [name-generator](https://github.com/dasmig/name-generator) | Culturally appropriate full names |
| [nickname-generator](https://github.com/dasmig/nickname-generator) | Gamer-style nicknames |
| [occupation-generator](https://github.com/dasmig/occupation-generator) | Employment status, education and occupation by country, age and sex |
| [persona-generator](https://github.com/dasmig/persona-generator) | Complete characters composed from the other generators |
| [phone-generator](https://github.com/dasmig/phone-generator) | Phone numbers valid in each country's numbering plan |
| [relationship-generator](https://github.com/dasmig/relationship-generator) | Families around a person: parents, siblings, partners and children |
| [user-generator](https://github.com/dasmig/user-generator) | Complete user records: account, username, email, phone and address |

## License

This library is released under the [MIT License](https://opensource.org/licenses/MIT).

```
MIT License

Copyright (c) 2026 Diego Dasso Migotto
```
