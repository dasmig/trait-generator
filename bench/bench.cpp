// Benchmarks for trait-generator; see bench.hpp for the options.

#include "../dasmig/traitgen.hpp"
#include "bench.hpp"

int main(int argc, char** argv)
{
    dasmig::bench::runner run{"trait-generator", argc, argv};

    run.load("load", [&] {
        dasmig::trtg gen;
        return gen.load(run.tier());
    });

    dasmig::trtg gen;
    if (!gen.load(run.tier())) return run.missing_tier();

    run.calls("get_personality", [&](std::uint64_t seed) { return gen.get_personality({}, seed); });
    run.calls("get_personality 8 traits", [&](std::uint64_t seed) {
        return gen.get_personality({.count = 8}, seed);
    });
    run.calls("get_personality to_json", [&](std::uint64_t seed) {
        return gen.get_personality({}, seed).to_json();
    });

    run.batches("get_personality_batch", [&](std::size_t count, std::size_t threads, std::uint64_t seed) {
        return gen.get_personality_batch({}, {.count = count, .threads = threads}, seed);
    });

    return run.finish();
}
