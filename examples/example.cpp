#include "../dasmig/traitgen.hpp"
#include <cstdint>
#include <format>
#include <iostream>

namespace
{

void print(const dasmig::personality& p)
{
    std::cout << "  " << p << '\n' << "   ";
    for (const auto& a : p.axes) std::cout << std::format(" {}={:+.2f}", a.key, a.value);
    std::cout << '\n';
}

} // namespace

int main()
{
    // The singleton loads the lite tier (compiled in, or from resources/).
    auto& gen = dasmig::trtg::instance();
    if (!gen.has_data())
    {
        std::cerr << "Could not load the trait catalogue.\n";
        return 1;
    }
    std::cout << "Loaded " << gen.trait_count() << " traits and " << gen.axis_count() << " axes.\n\n";

    std::cout << "Random personalities:\n";
    for (int i = 0; i < 3; ++i) print(gen.get_personality(dasmig::trait_request{}));

    std::cout << "\nFive traits, brave included, greedy left out:\n";
    print(gen.get_personality(dasmig::trait_request{.count = 5, .include = {"brave"}, .exclude = {"greedy"}}));

    std::cout << "\nSeed 42, as JSON:\n  "
              << gen.get_personality(dasmig::trait_request{}, std::uint64_t{42}).to_json() << '\n';

    // A game adds its own trait and an axis its AI reads; neither needs a
    // change to the library.
    gen.extend_from_string(dasmig::trtg::table::traits,
                           "trait\tlabel\tweight\ttags\nscavenger\tScavenger\t3\tsurvival;loot\n", "game");
    gen.extend_from_string(dasmig::trtg::table::axes, "axis\tlabel\tmean\tsd\nrisk\tRisk taking\t0\t0\n", "game");
    gen.extend_from_string(dasmig::trtg::table::loadings,
                           "trait\taxis\tloading\nscavenger\trisk\t0.5\nbrave\trisk\t1\ncautious\trisk\t-1\n",
                           "game");
    std::cout << "\nWith the game's layers:\n";
    for (int i = 0; i < 3; ++i)
    {
        const auto p = gen.get_personality(dasmig::trait_request{});
        std::cout << std::format("  {:<40} risk={:+.1f}\n", p.to_string(), p.value("risk").value_or(0.0));
    }
    return 0;
}
