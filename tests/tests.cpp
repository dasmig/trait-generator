#include "catch_amalgamated.hpp"
#include "../dasmig/traitgen.hpp"

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using table = dasmig::trtg::table;
using dasmig::core::layer_mode;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace
{

dasmig::trtg make_loaded_generator()
{
    dasmig::trtg gen;
    if (!gen.load(dasmig::dataset::lite))
    {
        for (const auto* candidate : {"resources/lite", "../resources/lite"})
        {
            if (std::filesystem::is_directory(candidate))
            {
                gen.load(std::filesystem::path{candidate});
                break;
            }
        }
    }
    REQUIRE(gen.has_data());
    return gen;
}

// A generator with a small catalogue given as TSV text (header included).
dasmig::trtg make_small(std::string_view traits, std::string_view relations = {},
                        std::string_view axes = {}, std::string_view loadings = {})
{
    dasmig::trtg gen;
    gen.extend_from_string(table::traits, traits, "traits", layer_mode::replace);
    if (!relations.empty()) gen.extend_from_string(table::relations, relations, "relations");
    if (!axes.empty()) gen.extend_from_string(table::axes, axes, "axes");
    if (!loadings.empty()) gen.extend_from_string(table::loadings, loadings, "loadings");
    return gen;
}

const std::string abc = "trait\tlabel\tweight\na\tA\t1\nb\tB\t1\nc\tC\t1\n";

std::set<std::string> keys(const dasmig::personality& p)
{
    std::set<std::string> k;
    for (const auto& t : p.traits) k.insert(t.key);
    return k;
}

} // namespace

// ---------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------

TEST_CASE("load(dataset::lite) loads the catalogue", "[loading]")
{
    const auto gen = make_loaded_generator();
    REQUIRE(gen.trait_count() == 52);
    REQUIRE(gen.axis_count() == 5);
    REQUIRE(gen.has_trait("brave"));
    REQUIRE_FALSE(gen.has_trait("nonexistent"));
    for (const auto t : {table::traits, table::axes, table::loadings, table::relations})
    {
        REQUIRE(gen.layers(t).size() == 1);
        REQUIRE(gen.layers(t).front().warnings.empty());
    }
}

TEST_CASE("the shipped catalogue has no unresolved names", "[loading]")
{
    const auto gen = make_loaded_generator();
    INFO((gen.unresolved().empty() ? std::string{} : gen.unresolved().front()));
    REQUIRE(gen.unresolved().empty());
}

TEST_CASE("load - a path without traits.tsv is a no-op", "[loading]")
{
    dasmig::trtg gen;
    gen.load(std::filesystem::path{"no/such/directory"});
    REQUIRE_FALSE(gen.has_data());
    REQUIRE(gen.data_hash() == 0);
}

#ifndef DASMIG_TRAIT_EMBED // compiled-in data is never missing
TEST_CASE("load(dataset) returns false for a missing tier", "[loading]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "trtg_empty_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::trtg gen;
    const bool loaded = gen.load(dasmig::dataset::full);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);
    REQUIRE_FALSE(loaded);
    REQUIRE_FALSE(gen.has_data());
}
#endif

TEST_CASE("the singleton loads data on first use", "[loading]")
{
    REQUIRE(dasmig::trtg::instance().trait_count() == 52);
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

TEST_CASE("get_personality throws when no data is loaded", "[generation]")
{
    dasmig::trtg gen;
    REQUIRE_THROWS_AS(gen.get_personality(dasmig::trait_request{}), std::runtime_error);
    REQUIRE_THROWS_AS(gen.get_personality(dasmig::trait_request{}, std::uint64_t{1}), std::runtime_error);
}

TEST_CASE("a personality has three distinct traits and every axis", "[generation]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 500; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{}, s);
        REQUIRE(p.traits.size() == 3);
        REQUIRE(keys(p).size() == 3);
        REQUIRE(p.axes.size() == 5);
        REQUIRE(p.axes.front().key == "openness");
    }
}

TEST_CASE("get_personality with a seed is deterministic", "[generation][seed]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        REQUIRE(gen.get_personality(dasmig::trait_request{}, s).to_json() ==
                gen.get_personality(dasmig::trait_request{}, s).to_json());
    }
}

TEST_CASE("a result replays from its seed", "[generation][seed]")
{
    auto gen = make_loaded_generator();
    const dasmig::trait_request defaults{};
    const auto p = gen.get_personality(defaults);
    REQUIRE(gen.get_personality(defaults, p.seed()).to_json() == p.to_json());
    const dasmig::trait_request req{.count = 5, .include = {"brave"}, .exclude = {}};
    const auto q = gen.get_personality(req);
    REQUIRE(gen.get_personality(req, q.seed()).to_json() == q.to_json());
}

TEST_CASE("explicit per-call seeds replay across generator instances", "[seed]")
{
    const auto first = make_loaded_generator();
    const auto second = make_loaded_generator();
    const dasmig::trait_request request{.count = 4, .include = {"brave"}, .exclude = {"greedy"}};
    REQUIRE(first.get_personality(request, 42).to_json() == second.get_personality(request, 42).to_json());
}

TEST_CASE("shipped conflicts and groups never appear together", "[generation][rules]")
{
    const auto gen = make_loaded_generator();
    const std::vector<std::pair<std::string, std::string>> never{
        {"brave", "cowardly"}, {"calm", "anxious"},   {"reckless", "cautious"},
        {"kind", "cruel"},     {"lazy", "energetic"}, {"outgoing", "loner"},
        {"shy", "outgoing"},   {"honest", "deceitful"}};
    for (std::uint64_t s = 0; s < 3000; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{.count = 8}, s);
        std::set<std::string> groups;
        for (const auto& t : p.traits)
        {
            if (!t.group.empty()) REQUIRE(groups.insert(t.group).second);
        }
        for (const auto& [a, b] : never) REQUIRE_FALSE((p.has(a) && p.has(b)));
    }
}

TEST_CASE("a conflict keeps two traits apart", "[generation][rules]")
{
    const auto gen = make_small(abc, "trait\tother\trelation\na\tb\tconflict\n");
    int with_a = 0;
    for (std::uint64_t s = 0; s < 1000; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{.count = 3}, s);
        REQUIRE_FALSE((p.has("a") && p.has("b")));
        REQUIRE(p.traits.size() == 2); // c and one of a, b
        with_a += p.has("a") ? 1 : 0;
    }
    REQUIRE(with_a == Catch::Approx(500).margin(80));
}

TEST_CASE("traits in the same group exclude each other", "[generation][rules]")
{
    const auto gen = make_small("trait\tlabel\tweight\tgroup\na\tA\t1\tg\nb\tB\t1\tg\nc\tC\t1\t\n");
    for (std::uint64_t s = 0; s < 500; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{.count = 3}, s);
        REQUIRE(p.traits.size() == 2);
        REQUIRE(p.has("c"));
    }
}

TEST_CASE("a synergy makes the related trait likelier", "[generation][rules]")
{
    const auto plain = make_small(abc);
    const auto linked = make_small(abc, "trait\tother\trelation\tfactor\na\tb\tsynergy\t10\n");
    const auto count_ab = [](const dasmig::trtg& g) {
        int n = 0;
        for (std::uint64_t s = 0; s < 4000; ++s)
        {
            const auto p = g.get_personality(dasmig::trait_request{.count = 2}, s);
            n += (p.has("a") && p.has("b")) ? 1 : 0;
        }
        return n;
    };
    // Without the synergy a and b pair up in 1/3 of draws; with factor 10,
    // in 2/3 * 10/11 (a or b first, then the other), about 0.61.
    REQUIRE(count_ab(plain) == Catch::Approx(4000.0 / 3).margin(120));
    REQUIRE(count_ab(linked) == Catch::Approx(4000 * (2.0 / 3) * (10.0 / 11)).margin(120));
}

TEST_CASE("fewer traits are returned when none is compatible", "[generation][rules]")
{
    const auto gen = make_small(abc, "trait\tother\trelation\na\tb\tconflict\na\tc\tconflict\n");
    const auto p = gen.get_personality(dasmig::trait_request{.count = 3, .include = {"a"}}, 1);
    REQUIRE(p.traits.size() == 1);
    REQUIRE(p.has("a"));
}

TEST_CASE("included traits come first and shape the draw", "[generation][request]")
{
    const auto gen = make_loaded_generator();
    for (std::uint64_t s = 0; s < 300; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{.count = 4, .include = {"brave", "loyal"}}, s);
        REQUIRE(p.traits.size() == 4);
        REQUIRE(p.traits[0].key == "brave");
        REQUIRE(p.traits[1].key == "loyal");
        REQUIRE_FALSE(p.has("cowardly"));
    }
}

TEST_CASE("excluded traits are never drawn", "[generation][request]")
{
    const auto gen = make_small(abc);
    for (std::uint64_t s = 0; s < 200; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{.count = 3, .exclude = {"b", "nosuch"}}, s);
        REQUIRE(keys(p) == std::set<std::string>{"a", "c"});
    }
}

TEST_CASE("invalid requests throw", "[generation][request]")
{
    const auto gen = make_loaded_generator();
    using req = dasmig::trait_request;
    REQUIRE_THROWS_WITH(gen.get_personality(req{.count = 3, .include = {"nosuch"}}, 1), "Unknown trait: nosuch");
    REQUIRE_THROWS_AS(gen.get_personality(req{.count = 3, .include = {"brave", "cowardly"}}, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gen.get_personality(req{.count = 3, .include = {"brave", "brave"}}, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gen.get_personality(req{.count = 3, .include = {"brave"}, .exclude = {"brave"}}, 1),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(gen.get_personality(req{.count = 1, .include = {"brave", "loyal"}}, 1),
                      std::invalid_argument);
    REQUIRE(gen.get_personality(req{.count = 0}, 1).traits.empty());
}

// ---------------------------------------------------------------------------
// Axes
// ---------------------------------------------------------------------------

TEST_CASE("an axis value is its base plus the chosen traits' loadings", "[axes]")
{
    const auto gen = make_small(abc, {}, "axis\tlabel\tmean\tsd\nx\tX\t10\t0\ny\tY\t0\t1\n",
                                "trait\taxis\tloading\na\tx\t2\nb\tx\t-0.5\nc\ty\t0.25\n");
    for (std::uint64_t s = 0; s < 100; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{.count = 2}, s);
        REQUIRE(p.axes[0].base == 10.0); // sd 0: the mean
        const double expected_x = 10.0 + (p.has("a") ? 2.0 : 0.0) + (p.has("b") ? -0.5 : 0.0);
        REQUIRE(p.axes[0].value == expected_x);
        REQUIRE(p.value("x") == expected_x);
        REQUIRE(p.axes[1].value == p.axes[1].base + (p.has("c") ? 0.25 : 0.0));
        REQUIRE_FALSE(p.value("nosuch").has_value());
    }
}

TEST_CASE("Big Five base values are standard normal", "[axes][stats]")
{
    const auto gen = make_loaded_generator();
    constexpr int draws = 10000;
    std::vector<double> sum(5, 0.0);
    std::vector<double> sum2(5, 0.0);
    for (std::uint64_t s = 0; s < draws; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{}, s);
        for (std::size_t a = 0; a < 5; ++a)
        {
            sum[a] += p.axes[a].base;
            sum2[a] += p.axes[a].base * p.axes[a].base;
        }
    }
    for (std::size_t a = 0; a < 5; ++a)
    {
        const double mean = sum[a] / draws;
        const double sd = std::sqrt((sum2[a] / draws) - (mean * mean));
        REQUIRE(mean == Catch::Approx(0.0).margin(0.04));
        REQUIRE(sd == Catch::Approx(1.0).margin(0.03));
    }
}

TEST_CASE("traits follow the axes they load on", "[axes][stats]")
{
    // organized loads +0.9 on conscientiousness: people who have it should
    // have a clearly higher base conscientiousness than people who do not.
    const auto gen = make_loaded_generator();
    double with = 0.0;
    double without = 0.0;
    int n_with = 0;
    int n_without = 0;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{}, s);
        const double c = p.axes[1].base;
        if (p.has("organized"))
        {
            with += c;
            ++n_with;
        }
        else
        {
            without += c;
            ++n_without;
        }
    }
    REQUIRE(n_with > 100);
    REQUIRE((with / n_with) - (without / n_without) > 0.5);
}

// ---------------------------------------------------------------------------
// Conversion
// ---------------------------------------------------------------------------

TEST_CASE("string conversion, streaming and tags", "[conversion]")
{
    const auto gen = make_small("trait\tlabel\tweight\ttags\na\tA\t1\tx;y\nb\tB\t1\ty;z\n");
    const auto p = gen.get_personality(dasmig::trait_request{.count = 2, .include = {"a", "b"}}, 5);
    const std::string s = p;
    std::ostringstream os;
    os << p;
    REQUIRE(s == "A, B");
    REQUIRE(os.str() == "A, B");
    REQUIRE(p.tags() == std::vector<std::string>{"x", "y", "z"});
}

// ---------------------------------------------------------------------------
// Known answers (rule 2): the same seed and data give these exact results on
// every compiler and platform. A change here is a change of output and needs
// a major version.
// ---------------------------------------------------------------------------

TEST_CASE("known answers for fixed seeds", "[kat]")
{
    dasmig::trtg gen;
    gen.load(std::filesystem::path{"resources/lite"});
    if (!gen.has_data()) gen.load(std::filesystem::path{"../resources/lite"});
    REQUIRE(gen.has_data());
    REQUIRE(gen.data_hash() == 0xff9364ce0749f37cULL);

    REQUIRE(gen.get_personality(dasmig::trait_request{}, std::uint64_t{1}).to_json() ==
            R"({"traits":[{"key":"calm","label":"Calm","group":"temper","tags":["rest"]},{"key":"resilient","label":"Resilient","group":null,"tags":["survival","willpower"]},{"key":"traditional","label":"Traditional","group":"novelty","tags":["faith","routine"]}],)"
            R"("axes":{"openness":-2.001703471338275,"conscientiousness":0.16975813928315747,"extraversion":-0.2986160095442788,"agreeableness":-0.10037693181376535,"neuroticism":-1.157169454751688},)"
            R"("signature":{"seed":"0000000000000001","data":"ff9364ce0749f37c"}})");
    REQUIRE(gen.get_personality(dasmig::trait_request{}, std::uint64_t{2}).to_json() ==
            R"({"traits":[{"key":"curious","label":"Curious","group":"novelty","tags":["knowledge","exploration"]},{"key":"patient","label":"Patient","group":"patience","tags":["waiting"]},{"key":"outgoing","label":"Outgoing","group":"sociability","tags":["social"]}],)"
            R"("axes":{"openness":1.2592971859088102,"conscientiousness":0.032864413410737425,"extraversion":1.954276507241222,"agreeableness":0.04393141673821327,"neuroticism":-0.6908067147486393},)"
            R"("signature":{"seed":"0000000000000002","data":"ff9364ce0749f37c"}})");
    REQUIRE(gen.get_personality(dasmig::trait_request{.count = 4, .include = {"loyal"}, .exclude = {"brave"}},
                                std::uint64_t{3})
                .to_json() ==
            R"({"traits":[{"key":"loyal","label":"Loyal","group":null,"tags":["duty","social"]},{"key":"protective","label":"Protective","group":null,"tags":["duty","combat"]},{"key":"empathetic","label":"Empathetic","group":null,"tags":["care","medicine"]},{"key":"patient","label":"Patient","group":"patience","tags":["waiting"]}],)"
            R"("axes":{"openness":-1.083582304338165,"conscientiousness":0.9367184396342176,"extraversion":0.030259475139377812,"agreeableness":1.3783102877459144,"neuroticism":-0.11584813761111279},)"
            R"("signature":{"seed":"0000000000000003","data":"ff9364ce0749f37c"}})");

    std::uint64_t h = 0xcbf29ce484222325ULL;
    for (std::uint64_t s = 0; s < 10000; ++s)
    {
        for (const char c : gen.get_personality(dasmig::trait_request{}, s).to_json())
        {
            h ^= static_cast<unsigned char>(c);
            h *= 0x100000001b3ULL;
        }
    }
    REQUIRE(h == 0x4211ad5e26a4c6e6ULL);
}

// ---------------------------------------------------------------------------
// JSON and signatures
// ---------------------------------------------------------------------------

TEST_CASE("to_json and signature", "[json]")
{
    const auto gen = make_small("trait\tlabel\tweight\tgroup\ttags\na\tA \"1\"\t1\tg\tx;y\nb\tB\t1\t\t\n",
                                {}, "axis\tlabel\tmean\tsd\nx\tX\t1.5\t0\n");
    const auto p = gen.get_personality(dasmig::trait_request{.count = 2, .include = {"a", "b"}}, 99);
    REQUIRE(p.signature().seed == 99);
    REQUIRE(p.seed() == 99);
    REQUIRE(p.signature().data == gen.data_hash());
    const auto json = p.to_json();
    // Built outside REQUIRE: MSVC misreads raw strings with \" in macro arguments.
    const auto expected =
        std::format(R"({{"traits":[{{"key":"a","label":"A \"1\"","group":"g","tags":["x","y"]}},)"
                    R"({{"key":"b","label":"B","group":null,"tags":[]}}],"axes":{{"x":1.5}},)"
                    R"("signature":{{"seed":"0000000000000063","data":"{:016x}"}}}})",
                    gen.data_hash());
    REQUIRE(json == expected);

    dasmig::core::json_writer w;
    w.begin_array();
    p.to_json(w);
    w.end_array();
    REQUIRE(w.str() == "[" + json + "]");
}

// ---------------------------------------------------------------------------
// Extending data (rule 8)
// ---------------------------------------------------------------------------

TEST_CASE("extend adds, overrides and removes traits", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto base_hash = gen.data_hash();
    const auto before = gen.get_personality(dasmig::trait_request{}, std::uint64_t{7});

    const auto& info = gen.extend_from_string(table::traits,
                                              "trait\tlabel\tweight\tgroup\ttags\n"
                                              "greedy\tGreedy\t0\tgenerosity\twealth\n"
                                              "scavenger\tScavenger\t5\t\tsurvival;loot\n",
                                              "mod.tsv");
    REQUIRE(info.replaced == 1);
    REQUIRE(info.added == 1);
    REQUIRE(gen.trait_count() == 53);
    REQUIRE(gen.data_hash() != base_hash);
    REQUIRE(gen.layers(table::traits).size() == 2);
    REQUIRE(gen.get_personality(dasmig::trait_request{}, std::uint64_t{7}).signature().data == gen.data_hash());

    // A game axis with no base spread, fed by the new trait.
    gen.extend_from_string(table::axes, "axis\tlabel\tmean\tsd\nscavenging\tScavenging\t0\t0\n", "axes.tsv");
    gen.extend_from_string(table::loadings, "trait\taxis\tloading\nscavenger\tscavenging\t1\n", "load.tsv");
    int seen = 0;
    for (std::uint64_t s = 0; s < 300; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{}, s);
        REQUIRE_FALSE(p.has("greedy")); // weight 0
        REQUIRE(p.value("scavenging") == (p.has("scavenger") ? 1.0 : 0.0));
        seen += p.has("scavenger") ? 1 : 0;
    }
    REQUIRE(seen > 50);

    // Removing a trait leaves its loadings unresolved, not an error.
    gen.extend_from_string(table::traits, "trait\tdisabled\nscavenger\t1\n", "remove.tsv");
    REQUIRE(gen.trait_count() == 52);
    REQUIRE(gen.unresolved().size() == 1);
    REQUIRE(gen.unresolved().front() == "load.tsv:2: unknown trait 'scavenger'");

    // Reloading the base drops every layer.
    REQUIRE(gen.load(dasmig::dataset::lite));
    REQUIRE(gen.data_hash() == base_hash);
    REQUIRE(gen.get_personality(dasmig::trait_request{}, std::uint64_t{7}).to_json() == before.to_json());
}

TEST_CASE("unresolved lists misspelt names", "[extend]")
{
    auto gen = make_loaded_generator();
    gen.extend_from_string(table::loadings, "trait\taxis\tloading\nbrave\topennes\t0.5\n", "typo.tsv");
    REQUIRE(gen.unresolved() == std::vector<std::string>{"typo.tsv:2: unknown axis 'opennes'"});
}

TEST_CASE("a bad layer is rejected and leaves the data unchanged", "[extend][errors]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();

    REQUIRE_THROWS_WITH(gen.extend_from_string(table::traits, "trait\tlabel\tweight\nx\tX\theavy\n", "bad.tsv"),
                        "bad.tsv:2: column 'weight': expected a number, got 'heavy'");
    REQUIRE_THROWS_AS(gen.extend_from_string(table::traits, "trait\tlabel\tweight\nx\tX\t-1\n", "neg.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(table::traits, "trait\tlabel\tweight\ttags\nx\tX\t1\ta;;b\n", "t.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(table::axes, "axis\tlabel\tmean\tsd\nq\tQ\t0\t-1\n", "sd.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(table::relations, "trait\tother\trelation\na\ta\tconflict\n", "self.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(table::relations, "trait\tother\trelation\na\tb\tfriends\n", "r.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(table::relations,
                                             "trait\tother\trelation\tfactor\na\tb\tconflict\t2\n", "f.tsv"),
                      dasmig::core::tsv_error);
    REQUIRE_THROWS_AS(gen.extend_from_string(table::relations,
                                             "trait\tother\trelation\tfactor\na\tb\tsynergy\t0\n", "z.tsv"),
                      dasmig::core::tsv_error);
    // The shipped data relates brave and protective; the reverse order is a
    // second definition of the same pair.
    REQUIRE_THROWS_WITH(gen.extend_from_string(table::relations,
                                               "trait\tother\trelation\nprotective\tbrave\tconflict\n", "rev.tsv"),
                        Catch::Matchers::ContainsSubstring("already defined at"));
    REQUIRE_THROWS_AS(gen.extend_from_string(table::traits, "trait\tlabel\tweight\nbrave\tBrave\t1\n", "clash.tsv",
                                             layer_mode::add),
                      dasmig::core::tsv_error); // brave already loaded
    REQUIRE_THROWS_AS(gen.extend(std::filesystem::path{"jobs.tsv"}), std::invalid_argument);

    REQUIRE(gen.data_hash() == hash);
    REQUIRE(gen.trait_count() == 52);
    REQUIRE(gen.layers(table::traits).size() == 1);
    REQUIRE(gen.layers(table::relations).size() == 1);
}

TEST_CASE("a directory layer applies all its files or none", "[extend]")
{
    auto gen = make_loaded_generator();
    const auto hash = gen.data_hash();
    const auto dir = std::filesystem::temp_directory_path() / "trtg_layer_dir";
    std::filesystem::create_directories(dir);
    const auto write = [&dir](const char* name, const char* text) {
        std::ofstream{dir / name, std::ios::binary} << text;
    };

    write("traits.tsv", "trait\tlabel\tweight\nscavenger\tScavenger\t1\n");
    write("relations.tsv", "trait\tother\trelation\nscavenger\tlazy\tbogus\n");
    REQUIRE_THROWS_AS(gen.extend(dir), dasmig::core::tsv_error);
    REQUIRE(gen.data_hash() == hash);
    REQUIRE_FALSE(gen.has_trait("scavenger"));

    write("relations.tsv", "trait\tother\trelation\nscavenger\tlazy\tconflict\n");
    gen.extend(dir);
    std::filesystem::remove_all(dir);
    REQUIRE(gen.has_trait("scavenger"));
    REQUIRE(gen.layers(table::traits).size() == 2);
    REQUIRE(gen.layers(table::relations).size() == 2);
    for (std::uint64_t s = 0; s < 300; ++s)
    {
        const auto p = gen.get_personality(dasmig::trait_request{.count = 6, .include = {"scavenger"}}, s);
        REQUIRE_FALSE(p.has("lazy"));
    }
}

#ifdef DASMIG_TRAIT_EMBED
TEST_CASE("compiled-in data needs no files and matches them", "[embed]")
{
    const auto old_cwd = std::filesystem::current_path();
    const auto tmp = std::filesystem::temp_directory_path() / "trtg_embed_dir";
    std::filesystem::create_directories(tmp);
    std::filesystem::current_path(tmp);
    dasmig::trtg gen;
    const bool loaded = gen.load(dasmig::dataset::lite);
    std::filesystem::current_path(old_cwd);
    std::filesystem::remove_all(tmp);

    REQUIRE(loaded);
    REQUIRE(gen.layers(table::traits).front().source == "embedded:lite/traits.tsv");
    REQUIRE(gen.data_hash() == 0xff9364ce0749f37cULL); // same as the files
    REQUIRE(gen.get_personality(dasmig::trait_request{}, std::uint64_t{1}).to_json() ==
            R"({"traits":[{"key":"calm","label":"Calm","group":"temper","tags":["rest"]},{"key":"resilient","label":"Resilient","group":null,"tags":["survival","willpower"]},{"key":"traditional","label":"Traditional","group":"novelty","tags":["faith","routine"]}],)"
            R"("axes":{"openness":-2.001703471338275,"conscientiousness":0.16975813928315747,"extraversion":-0.2986160095442788,"agreeableness":-0.10037693181376535,"neuroticism":-1.157169454751688},)"
            R"("signature":{"seed":"0000000000000001","data":"ff9364ce0749f37c"}})");
}
#endif

// ---------------------------------------------------------------------------
// Batch generation
// ---------------------------------------------------------------------------

TEST_CASE("personality batch: items equal the single calls for any thread count", "[batch]")
{
    dasmig::trtg gen;
    gen.load(std::filesystem::path{"resources/lite"});
    if (!gen.has_data()) gen.load(std::filesystem::path{"../resources/lite"});
    REQUIRE(gen.has_data());
    const auto request = dasmig::trait_request{.count = 3};
    const auto batch = gen.get_personality_batch(request, {.count = 200, .threads = 1}, 77);
    REQUIRE(batch.size() == 200);
    for (std::uint64_t i = 0; i < batch.size(); ++i)
    {
        REQUIRE(batch[i].to_json() == gen.get_personality(request, dasmig::core::item_seed(77, i)).to_json());
    }

    // A range is computed directly, on any number of threads.
    const auto tail = gen.get_personality_batch(request, {.count = 100, .first = 100, .threads = 4}, 77);
    REQUIRE(tail.size() == 100);
    for (std::size_t i = 0; i < tail.size(); ++i)
    {
        REQUIRE(tail[i].to_json() == batch[100 + i].to_json());
    }
    for (const std::size_t threads : {2U, 7U, 0U})
    {
        const auto other = gen.get_personality_batch(request, {.count = 200, .threads = threads}, 77);
        REQUIRE(other.size() == batch.size());
        for (std::size_t i = 0; i < other.size(); ++i)
        {
            REQUIRE(other[i].to_json() == batch[i].to_json());
        }
    }

    // Without a seed the batch still has the requested size.
    REQUIRE(gen.get_personality_batch(request, {.count = 5}).size() == 5);
}

TEST_CASE("personality batch: streaming gives the same items in order", "[batch]")
{
    dasmig::trtg gen;
    gen.load(std::filesystem::path{"resources/lite"});
    if (!gen.has_data()) gen.load(std::filesystem::path{"../resources/lite"});
    REQUIRE(gen.has_data());
    const auto request = dasmig::trait_request{.count = 3};
    const auto batch = gen.get_personality_batch(request, {.count = 300, .first = 10, .threads = 3}, 5);
    std::uint64_t next = 10;
    std::size_t seen = 0;
    gen.for_each_personality(request, {.count = 300, .first = 10, .threads = 3}, 5,
                        [&](std::uint64_t index, const dasmig::personality& item)
                        {
                            REQUIRE(index == next);
                            REQUIRE(item.to_json() == batch[seen].to_json());
                            ++next;
                            ++seen;
                        });
    REQUIRE(seen == batch.size());
}

TEST_CASE("personality batch: known answer of 10,000 items", "[batch][kat]")
{
    dasmig::trtg gen;
    gen.load(std::filesystem::path{"resources/lite"});
    if (!gen.has_data()) gen.load(std::filesystem::path{"../resources/lite"});
    REQUIRE(gen.has_data());
    const auto request = dasmig::trait_request{.count = 3};
    std::uint64_t hash = 0xcbf29ce484222325ULL;
    gen.for_each_personality(request, {.count = 10000, .threads = 4}, 1,
                        [&](std::uint64_t, const dasmig::personality& item)
                        { hash = dasmig::core::hash_combine(hash, dasmig::core::fnv1a64(item.to_json())); });
    REQUIRE(hash == 0x8cc441b3aa7370cULL);
}
