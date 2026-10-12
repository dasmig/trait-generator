# Data and Method

The data in `resources/` and how trait-generator uses it (rule 6 of the dasmig standard).

**Everything here is curated.** No dataset gives trait frequencies, conflicts or synergies for a game catalogue, so the four tables were written by hand for this library (2026-10-12). Only the direction of the main Big Five loadings is anchored in the literature, and only for some traits (below). The numbers are design choices, chosen to give coherent and varied characters, not measurements.

## Tiers

| Tier | Traits | Axes | Loadings | Relations |
| --- | --- | --- | --- | --- |
| `lite` | 52 | 5 | 97 | 48 (18 conflicts, 30 synergies) |

There is no `full` tier.

## Tables and Columns

### `traits.tsv` (key `trait`)

| Column | Meaning | Source |
| --- | --- | --- |
| `trait`, `label` | Everyday personality words a player recognises, chosen to cover all five Big Five domains in both directions, plus traits a survival game's AI can use (brave, cautious, greedy, loyal, from the game's design notes) | Curated |
| `weight` | Base frequency: 1 for ordinary traits, up to 1.2 for common ones (kind, honest, hardworking, practical), down to 0.3 for traits that would make most characters unpleasant (cruel 0.3, deceitful 0.4, cowardly 0.5) | Curated |
| `group` | Opposite poles that cannot coexist (brave, cowardly and reckless form `courage`) | Curated |
| `tags` | Broad themes for other generators and the game (`combat`, `care`, `social`, `safety`, ...) | Curated |

### `axes.tsv` (key `axis`)

The Big Five (openness, conscientiousness, extraversion, agreeableness, neuroticism), each standardised: mean 0, sd 1. This follows the usual convention of reporting the factors as z-scores; it is not fitted to any population.

### `loadings.tsv` (keys `trait`, `axis`)

Each trait loads on one main axis (magnitude 0.5 to 0.9) and on up to two others (0.1 to 0.4). Where a trait is one of Saucier's Big Five Mini-Markers (Saucier 1994, a short form of Goldberg's 1992 adjective markers), its main axis and sign follow the marker:

| Axis | Marker traits in the catalogue (sign) |
| --- | --- |
| Extraversion | bold (+), energetic (+), talkative (+), quiet (−), shy (−) |
| Agreeableness | kind (+) |
| Conscientiousness | organized (+), practical (+), disorganized (−) |
| Neuroticism (the reverse of the markers' emotional stability) | moody (+), calm (− , "relaxed") |
| Openness (intellect) | creative (+), imaginative (+), philosophical (+) |

The other traits' axes follow common usage (cruel and generous on agreeableness, anxious and confident on neuroticism, ...). All magnitudes and secondary loadings are curated.

### `relations.tsv` (keys `trait`, `other`)

Conflicts are pairs that read as contradictions without being exact opposites (calm and anxious, bold and shy, lazy and energetic); exact opposites are groups instead. Synergies are pairs that often go together (kind and generous, shy and quiet, reckless and impulsive), with factors from 1.5 to 2.5. The set was checked by reading sampled personalities and removing combinations that read badly (confident and shy, confident and anxious, confident and cowardly, shy and talkative were added that way).

## Generation Model

See [Generation Model](usage.md#generation-model) in the usage guide. In short: Big Five base values are drawn, traits matching them are likelier (weight × exp(Σ loading × z)), traits are drawn one at a time respecting conflicts, groups and synergies, and the chosen traits' loadings are added to the axes.

Over 20,000 personalities of three traits, every trait appears, from 2.6% (cruel) to 9.8% (kind) of personalities.

## Rebuilding the Data

There is nothing to rebuild: the tables are the source. `make embed` regenerates the compiled-in copy, `dasmig/traitgen_data.hpp`.

## Known Limitations

- **The numbers are not empirical.** Weights, loadings and synergy factors are design choices. The loadings are not factor loadings from a study, and the resolved axis values should not be read as test scores.
- **The Big Five are drawn independently.** In real populations the factors correlate a little (for example, neuroticism with low agreeableness and low conscientiousness), and their means vary with age and sex. Here they are independent standard normals, the same for everyone.
- **No conditioning on the rest of the persona.** Country, culture, age, sex and job do not change trait frequencies. A caller can bias a personality with `include` and `exclude`, or with layers.
- **English labels only.**

## References

- Goldberg, L. R. (1992). The development of markers for the Big-Five factor structure. *Psychological Assessment*, 4(1), 26–42.
- Saucier, G. (1994). Mini-Markers: A brief version of Goldberg's unipolar Big-Five markers. *Journal of Personality Assessment*, 63(3), 506–516.
