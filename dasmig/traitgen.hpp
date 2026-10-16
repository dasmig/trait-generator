// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Diego Dasso Migotto

#ifndef DASMIG_TRAITGEN_HPP
#define DASMIG_TRAITGEN_HPP

#include "core.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <ostream>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef DASMIG_TRAIT_EMBED
#include "traitgen_data.hpp"
#endif

/// @file traitgen.hpp
/// @brief Trait generator library — personality traits from a weighted
///        catalogue with conflicts, synergies and Big Five axes, for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 0);

namespace dasmig
{

// Members have {} initializers so designated initializers may leave them
// out without -Wmissing-designated-field-initializers warnings.
// NOLINTBEGIN(readability-redundant-member-init)

/// @brief What to generate: how many traits, and traits to include or
///        leave out.
struct trait_request
{
    /// Number of traits, included ones counted. Fewer are returned when no
    /// other trait is compatible with those already chosen.
    std::size_t count{3};
    /// Traits that must be present, in this order, before any is drawn.
    /// They must exist and must not conflict with each other.
    std::vector<std::string> include{};
    /// Traits that must not be drawn. Unknown names are ignored.
    std::vector<std::string> exclude{};
};

// NOLINTEND(readability-redundant-member-init)

/// @brief Return type for personality generation: traits and the axis
///        values they resolve to.
///
/// Supports implicit conversion to std::string (the trait labels),
/// streaming via operator<< and JSON output via to_json().
class personality
{
  public:
    /// @brief One trait of a personality.
    struct trait
    {
        std::string key;               ///< Catalogue key (`trait` column).
        std::string label;             ///< Display label.
        std::string group;             ///< Exclusive group; empty for none.
        std::vector<std::string> tags; ///< Tags, in catalogue order.
    };

    /// @brief One axis of a personality.
    struct axis
    {
        std::string key;   ///< Axis key (`axis` column).
        std::string label; ///< Display label.
        double base{0.0};  ///< Value drawn before traits.
        double value{0.0}; ///< Base plus the loadings of the traits.
    };

    std::vector<trait> traits; ///< Included traits first, then drawn ones.
    std::vector<axis> axes;    ///< Every axis, in catalogue order.

    /// @brief Whether the personality has a trait.
    /// @param key Trait key.
    /// @return True when present.
    [[nodiscard]] bool has(std::string_view key) const
    {
        return std::ranges::any_of(traits, [key](const trait& t) { return t.key == key; });
    }

    /// @brief The resolved value of an axis.
    /// @param key Axis key.
    /// @return The value, or empty for an unknown axis.
    [[nodiscard]] std::optional<double> value(std::string_view key) const
    {
        const auto it = std::ranges::find(axes, key, &axis::key);
        if (it == axes.end()) return std::nullopt;
        return it->value;
    }

    /// @brief The tags of all traits, each once, in order of appearance.
    /// @return The tags.
    [[nodiscard]] std::vector<std::string> tags() const
    {
        std::vector<std::string> out;
        for (const auto& t : traits)
        {
            for (const auto& tag : t.tags)
            {
                if (std::ranges::find(out, tag) == out.end()) out.push_back(tag);
            }
        }
        return out;
    }

    /// @brief Retrieve the random seed used to generate this personality.
    /// @return The call seed; pass it to get_personality(request, seed)
    ///         with the same request and data to replay this personality.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this personality was generated from.
    /// @return The replay signature; `data` is trtg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const
    {
        return _signature;
    }

    /// @brief Human-readable summary string.
    /// @return The trait labels, comma separated.
    [[nodiscard]] std::string to_string() const
    {
        std::string s;
        for (const auto& t : traits)
        {
            if (!s.empty()) s += ", ";
            s += t.label;
        }
        return s;
    }

    /// @brief Write this personality as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object().key("traits").begin_array();
        for (const auto& t : traits)
        {
            w.begin_object().field("key", t.key).field("label", t.label).key("group");
            if (t.group.empty()) w.value(nullptr);
            else w.value(t.group);
            w.key("tags").begin_array();
            for (const auto& tag : t.tags) w.value(tag);
            w.end_array().end_object();
        }
        w.end_array().key("axes").begin_object();
        for (const auto& a : axes) w.field(a.key, a.value);
        w.end_object().key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This personality as a JSON document.
    /// @return Compact JSON, for example
    ///   `{"traits":[{"key":"brave","label":"Brave","group":"courage",
    ///   "tags":["combat","risk"]},...],"axes":{"openness":0.42,...},
    ///   "signature":{...}}`.
    [[nodiscard]] std::string to_json() const
    {
        core::json_writer w;
        to_json(w);
        return w.str();
    }

    /// @brief Implicit conversion to std::string.
    /// @return Summary string.
    operator std::string() const // NOLINT(hicpp-explicit-conversions)
    {
        return to_string();
    }

    /// @brief Stream the summary to an output stream.
    /// @param os Output stream.
    /// @param p Personality to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const personality& p)
    {
        os << p.to_string();
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class trtg;
};

/// @brief Personality generator: draws traits from a weighted catalogue,
///        shaped by Big Five axes, conflicts and synergies.
///
/// The generation pipeline, each step drawing from its own stream split
/// from the call seed:
/// 1. Each axis draws a base value from normal(mean, sd) (stream
///    `"axis:<key>"`; an axis with sd 0 takes its mean).
/// 2. Each trait's weight is multiplied by its affinity,
///    `exp(sum of loading * standardised base value)`, so a person high in
///    an axis is likelier to have the traits that load on it.
/// 3. Included traits are taken; then traits are drawn one at a time
///    (stream `"traits"`), skipping traits already chosen, excluded,
///    conflicting with a chosen trait or in a chosen trait's group, and
///    multiplying weights by the synergy factors of the chosen traits.
/// 4. Each axis value is its base plus the loadings of the chosen traits.
///
/// The catalogue is four tables (see table), extended by layers. Output is
/// identical on every compiler and platform (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed
/// independently.
///
/// @par Thread safety
/// Each instance is independent.  Concurrent calls to get_personality()
/// on the **same** instance require external synchronisation.
class trtg
{
  public:
    /// @brief The data tables.
    enum class table : std::uint8_t
    {
        traits,    ///< `traits.tsv`: key `trait`; label, weight, group, tags.
        axes,      ///< `axes.tsv`: key `axis`; label, mean, sd.
        loadings,  ///< `loadings.tsv`: key `trait` and `axis`; loading.
        relations  ///< `relations.tsv`: key `trait` and `other`; relation, factor.
    };

    /// @brief Default constructor — creates an empty generator with no
    ///        data.
    trtg() = default;

    trtg(const trtg&) = delete;            ///< Not copyable.
    trtg& operator=(const trtg&) = delete; ///< Not copyable.
    trtg(trtg&&) noexcept = default;       ///< Movable.
    trtg& operator=(trtg&&) noexcept = default; ///< Movable.
    ~trtg() = default;                     ///< Destructor.

    /// @brief Access the global singleton instance.
    ///
    /// Loads the `lite` tier (or `full` if `lite` is missing) on first
    /// access, from compiled-in data or an auto-probed `resources/`.
    /// @return The singleton.
    static trtg& instance()
    {
        static trtg inst{auto_probe_tag{}};
        return inst;
    }

    // -- Generation -------------------------------------------------------

    /// @brief Generate a random personality as requested.
    ///        Use trait_request{} for three traits without constraints.
    /// @param request Count, and traits to include or leave out.
    /// @return The personality.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If an included trait is unknown,
    ///         repeated, excluded or in conflict with another included
    ///         trait, or more traits are included than requested.
    [[nodiscard]] personality get_personality(const trait_request& request)
    {
        return generate_(request, draw_seed_());
    }

    /// @brief Generate a deterministic personality as requested.
    /// @param request Count, and traits to include or leave out.
    /// @param call_seed Seed; the same seed, request and data give the same
    ///        result.
    /// @return The personality.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument As get_personality(const trait_request&).
    [[nodiscard]] personality get_personality(const trait_request& request,
                                              std::uint64_t call_seed) const
    {
        return generate_(request, call_seed);
    }

    // -- Catalogue --------------------------------------------------------

    /// @brief Check whether any data has been loaded.
    /// @return True when at least one trait is loaded.
    [[nodiscard]] bool has_data() const { return !_model.traits.empty(); }

    /// @brief Return the number of loaded traits.
    /// @return The count.
    [[nodiscard]] std::size_t trait_count() const { return _model.traits.size(); }

    /// @brief Return the number of loaded axes.
    /// @return The count.
    [[nodiscard]] std::size_t axis_count() const { return _model.axes.size(); }

    /// @brief Whether a trait is loaded.
    /// @param key Trait key.
    /// @return True when loaded.
    [[nodiscard]] bool has_trait(std::string_view key) const
    {
        return _model.trait_index.contains(key);
    }

    /// @brief Loadings and relations that name a trait or axis that is not
    ///        loaded. They are ignored, so removing a trait needs no other
    ///        change; this lists them to catch misspelt names.
    /// @return One description per ignored row, such as
    ///         `loadings.tsv:12: unknown axis 'opennes'`.
    [[nodiscard]] const std::vector<std::string>& unresolved() const
    {
        return _model.unresolved;
    }

    // -- Data management --------------------------------------------------

    /// @brief Load a resource directory, replacing all data and layers
    ///        loaded before.
    ///
    /// The directory must contain `traits.tsv`; `axes.tsv`, `loadings.tsv`
    /// and `relations.tsv` are read when present (a missing one leaves its
    /// table empty). A path without `traits.tsv` is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If a file is malformed; the data is then
    ///         unchanged.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec) ||
            !std::filesystem::is_regular_file(dir / file_name_(table::traits), ec))
        {
            return;
        }
        apply_(read_directory_(dir), core::layer_mode::replace, true);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_TRAIT_EMBED`, otherwise searches `resources/`,
    /// `../resources`, `trait-generator/resources` and
    /// `../trait-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If a file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string tier_dir{core::tier_name(tier)};
        const auto marker = tier_dir + "/" + std::string{file_name_(table::traits)};
        const auto root = core::find_resources(library_name_, marker);
        documents docs;
        for (const auto t : all_tables_)
        {
            docs.at(index_(t)) = core::load_resource(tier_dir + "/" + std::string{file_name_(t)},
                                                     embedded_files_(), root);
        }
        if (!docs.at(index_(table::traits))) return false;
        apply_(std::move(docs), core::layer_mode::replace, true);
        return true;
    }

    /// @brief Add data layers from a directory or a file.
    ///
    /// A directory may hold any of `traits.tsv`, `axes.tsv`,
    /// `loadings.tsv` and `relations.tsv`; all are applied together or, if
    /// any fails, none. A file must have one of those names.
    ///
    /// @param path Directory or file.
    /// @param mode How the layers combine with the data loaded before.
    /// @throws core::tsv_error If a file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    /// @throws std::invalid_argument If @p path is a file with another name.
    void extend(const std::filesystem::path& path, core::layer_mode mode = core::layer_mode::override)
    {
        std::error_code ec;
        if (std::filesystem::is_directory(path, ec))
        {
            apply_(read_directory_(path), mode, false);
            return;
        }
        for (const auto t : all_tables_)
        {
            if (path.filename() == file_name_(t))
            {
                (void)extend(t, core::tsv_document::load(path), mode);
                return;
            }
        }
        throw std::invalid_argument("extend: expected traits.tsv, axes.tsv, loadings.tsv or "
                                    "relations.tsv, got " + path.filename().string());
    }

    /// @brief Add a data layer to one table.
    /// @param which The table.
    /// @param doc TSV with that table's columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(table which, core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        documents docs;
        docs.at(index_(which)).emplace(std::move(doc));
        apply_(std::move(docs), mode, false);
        return _tables.at(index_(which)).layers().back();
    }

    /// @brief Add a data layer to one table from TSV text (for example
    ///        from a WebAssembly host).
    /// @param which The table.
    /// @param text TSV with that table's columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(table which, std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(which, core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief The layers in effect for one table, in order.
    /// @param which The table.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers(table which) const
    {
        return _tables.at(index_(which)).layers();
    }

    /// @brief Hash of the loaded data (all four tables), recorded in each
    ///        result's signature().
    /// @return The hash; 0 when nothing is loaded.
    [[nodiscard]] std::uint64_t data_hash() const
    {
        std::uint64_t h = 0;
        bool any = false;
        for (const auto& t : _tables)
        {
            any = any || !t.layers().empty();
            h = core::hash_combine(h, t.data_hash());
        }
        return any ? h : 0;
    }

  private:
    // -- Internal data structures -----------------------------------------

    static constexpr std::string_view library_name_ = "trait-generator";
    static constexpr std::size_t table_count_ = 4;
    static constexpr std::array<table, table_count_> all_tables_{
        table::traits, table::axes, table::loadings, table::relations};
    // Affinity exponents are clamped to this, so a large loading cannot
    // overflow a weight.
    static constexpr double max_exponent_ = 30.0;
    static constexpr double default_synergy_ = 2.0;

    using documents = std::array<std::optional<core::tsv_document>, table_count_>;
    using tables = std::array<core::layered_table, table_count_>;

    static constexpr std::size_t index_(table t) { return static_cast<std::size_t>(t); }

    static constexpr std::string_view file_name_(table t)
    {
        switch (t)
        {
        case table::traits: return "traits.tsv";
        case table::axes: return "axes.tsv";
        case table::loadings: return "loadings.tsv";
        case table::relations: return "relations.tsv";
        }
        return "";
    }

    static core::table_schema schema_(table t)
    {
        switch (t)
        {
        case table::traits:
            return {.key = {"trait"}, .required = {"label", "weight"}, .optional = {"group", "tags"}};
        case table::axes: return {.key = {"axis"}, .required = {"label", "mean", "sd"}, .optional = {}};
        case table::loadings: return {.key = {"trait", "axis"}, .required = {"loading"}, .optional = {}};
        case table::relations:
            return {.key = {"trait", "other"}, .required = {"relation"}, .optional = {"factor"}};
        }
        return {};
    }

    static tables make_tables_()
    {
        return {core::layered_table{schema_(table::traits)}, core::layered_table{schema_(table::axes)},
                core::layered_table{schema_(table::loadings)},
                core::layered_table{schema_(table::relations)}};
    }

    struct trait_entry
    {
        personality::trait info;
        double weight{0.0};
    };

    struct axis_entry
    {
        std::string key;
        std::string label;
        double mean{0.0};
        double sd{0.0};
    };

    enum class relation_kind : std::uint8_t
    {
        conflict,
        synergy
    };

    struct relation_entry
    {
        std::string trait;
        std::string other;
        relation_kind kind{relation_kind::conflict};
        double factor{1.0};
    };

    // -- Row parsing (throws core::tsv_error located at the row) ------------

    static std::string label_(const core::tsv_row& row)
    {
        auto label = row.get<std::string>("label");
        if (label.empty()) throw row.error("label", "must not be empty");
        return label;
    }

    static trait_entry parse_trait_(const core::tsv_row& row)
    {
        trait_entry e;
        e.info.key = row.get<std::string>("trait");
        e.info.label = label_(row);
        e.weight = row.get<double>("weight");
        if (e.weight < 0.0) throw row.error("weight", "must not be negative");
        e.info.group = row.get_or<std::string>("group", "");
        if (row.has("tags"))
        {
            for (const auto tag : row.list("tags"))
            {
                if (tag.empty()) throw row.error("tags", "empty tag");
                e.info.tags.emplace_back(tag);
            }
        }
        return e;
    }

    static axis_entry parse_axis_(const core::tsv_row& row)
    {
        axis_entry a{.key = row.get<std::string>("axis"),
                     .label = label_(row),
                     .mean = row.get<double>("mean"),
                     .sd = row.get<double>("sd")};
        if (a.sd < 0.0) throw row.error("sd", "must not be negative");
        return a;
    }

    static relation_entry parse_relation_(const core::tsv_row& row)
    {
        relation_entry r{.trait = row.get<std::string>("trait"), .other = row.get<std::string>("other")};
        if (r.trait == r.other) throw row.error("other", "a trait cannot relate to itself");
        const auto kind = row.get<std::string_view>("relation");
        if (kind == "conflict")
        {
            r.kind = relation_kind::conflict;
            if (row.has("factor")) throw row.error("factor", "a conflict takes no factor");
        }
        else if (kind == "synergy")
        {
            r.kind = relation_kind::synergy;
            r.factor = row.get_or<double>("factor", default_synergy_);
            if (!(r.factor > 0.0)) throw row.error("factor", "must be positive");
        }
        else
        {
            throw row.error("relation", "expected conflict or synergy, got '" + std::string{kind} + "'");
        }
        return r;
    }

    static void validate_(table t, const core::tsv_row& row)
    {
        switch (t)
        {
        case table::traits: (void)parse_trait_(row); break;
        case table::axes: (void)parse_axis_(row); break;
        case table::loadings: (void)row.get<double>("loading"); break;
        case table::relations: (void)parse_relation_(row); break;
        }
    }

    // -- State --------------------------------------------------------------

    // The generation model, built from the tables by build_model_().
    struct model
    {
        std::vector<trait_entry> traits; // in table row order
        std::vector<axis_entry> axes;    // in table row order
        std::map<std::string, std::size_t, std::less<>> trait_index;
        std::map<std::string, std::size_t, std::less<>> axis_index;
        std::vector<std::vector<double>> loadings;      // [trait][axis]
        std::vector<std::vector<std::size_t>> conflicts; // [trait] -> traits, sorted
        std::vector<std::vector<std::pair<std::size_t, double>>> synergies; // [trait] -> (trait, factor)
        core::weighted_table<std::size_t> weights;      // trait index, by weight
        std::vector<std::string> unresolved;
    };

    tables _tables{make_tables_()};
    model _model;

    core::rng _engine{core::rng::from_entropy()};

    struct auto_probe_tag {};

    explicit trtg(auto_probe_tag /*tag*/)
    {
        if (!load(dataset::lite)) (void)load(dataset::full);
    }

    // -- Helpers ----------------------------------------------------------

    static std::span<const core::embedded_file> embedded_files_()
    {
#ifdef DASMIG_TRAIT_EMBED
        return embedded::traitgen::files;
#else
        return {};
#endif
    }

    static documents read_directory_(const std::filesystem::path& dir)
    {
        documents docs;
        for (const auto t : all_tables_)
        {
            const auto file = dir / file_name_(t);
            std::error_code ec;
            if (std::filesystem::is_regular_file(file, ec))
            {
                docs.at(index_(t)) = core::tsv_document::load(file);
            }
        }
        return docs;
    }

    // Apply layers to copies of the tables, then commit: all or nothing.
    // With @p reset (load), tables without a document are emptied.
    void apply_(documents docs, core::layer_mode mode, bool reset)
    {
        tables next = _tables;
        for (const auto t : all_tables_)
        {
            auto& doc = docs.at(index_(t));
            auto& tbl = next.at(index_(t));
            if (!doc)
            {
                if (reset) tbl.clear();
                continue;
            }
            (void)tbl.extend(std::move(*doc), mode, [t](const core::tsv_row& row) { validate_(t, row); });
        }
        auto built = build_model_(next);
        _tables = std::move(next);
        _model = std::move(built);
    }

    // -- Building the model -------------------------------------------------

    static std::string where_(const core::tsv_row& row)
    {
        return std::format("{}:{}", row.source(), row.line());
    }

    // The index of the trait named in a column, or empty (recorded in
    // unresolved) when it is not loaded.
    static std::optional<std::size_t> resolve_trait_(model& m, const core::tsv_row& row, std::string_view column)
    {
        const auto name = row[column];
        const auto it = m.trait_index.find(name);
        if (it != m.trait_index.end()) return it->second;
        m.unresolved.push_back(std::format("{}: unknown trait '{}'", where_(row), name));
        return std::nullopt;
    }

    static std::optional<std::size_t> resolve_axis_(model& m, const core::tsv_row& row)
    {
        const auto name = row["axis"];
        const auto it = m.axis_index.find(name);
        if (it != m.axis_index.end()) return it->second;
        m.unresolved.push_back(std::format("{}: unknown axis '{}'", where_(row), name));
        return std::nullopt;
    }

    // Throws core::tsv_error for a relation defined twice (in either order).
    static void add_relations_(model& m, const core::layered_table& relations)
    {
        std::map<std::pair<std::string, std::string>, std::string> pairs; // unordered pair -> where
        for (const auto& row : relations.rows())
        {
            const auto r = parse_relation_(row);
            const auto [first, second] = std::minmax(r.trait, r.other);
            const auto [it, inserted] = pairs.emplace(std::pair{first, second}, where_(row));
            if (!inserted)
            {
                throw row.error("other", std::format("the relation between '{}' and '{}' is already "
                                                     "defined at {}", r.trait, r.other, it->second));
            }
            const auto a = resolve_trait_(m, row, "trait");
            const auto b = resolve_trait_(m, row, "other");
            if (!a || !b) continue;
            if (r.kind == relation_kind::conflict)
            {
                m.conflicts[*a].push_back(*b);
                m.conflicts[*b].push_back(*a);
            }
            else
            {
                m.synergies[*a].emplace_back(*b, r.factor);
                m.synergies[*b].emplace_back(*a, r.factor);
            }
        }
    }

    // Traits in the same group conflict; conflict lists end sorted and
    // without repeats.
    static void add_group_conflicts_(model& m)
    {
        std::map<std::string, std::vector<std::size_t>, std::less<>> groups;
        for (std::size_t i = 0; i < m.traits.size(); ++i)
        {
            if (!m.traits[i].info.group.empty()) groups[m.traits[i].info.group].push_back(i);
        }
        for (const auto& [name, members] : groups)
        {
            for (const auto i : members)
            {
                std::ranges::copy_if(members, std::back_inserter(m.conflicts[i]),
                                     [i](std::size_t j) { return j != i; });
            }
        }
        for (auto& c : m.conflicts)
        {
            std::ranges::sort(c);
            const auto dup = std::ranges::unique(c);
            c.erase(dup.begin(), dup.end());
        }
    }

    static model build_model_(const tables& t)
    {
        model m;
        for (const auto& row : t.at(index_(table::traits)).rows())
        {
            const std::size_t i = m.traits.size();
            m.traits.push_back(parse_trait_(row));
            m.trait_index.emplace(m.traits.back().info.key, i);
            m.weights.add(i, m.traits.back().weight);
        }
        for (const auto& row : t.at(index_(table::axes)).rows())
        {
            m.axis_index.emplace(row.get<std::string>("axis"), m.axes.size());
            m.axes.push_back(parse_axis_(row));
        }
        m.loadings.assign(m.traits.size(), std::vector<double>(m.axes.size(), 0.0));
        for (const auto& row : t.at(index_(table::loadings)).rows())
        {
            const auto ti = resolve_trait_(m, row, "trait");
            const auto ai = resolve_axis_(m, row);
            if (ti && ai) m.loadings[*ti][*ai] = row.get<double>("loading");
        }
        m.conflicts.resize(m.traits.size());
        m.synergies.resize(m.traits.size());
        add_relations_(m, t.at(index_(table::relations)));
        add_group_conflicts_(m);
        return m;
    }

    // -- Generation ---------------------------------------------------------

    std::uint64_t draw_seed_() { return _engine(); }

    void require_data_() const
    {
        if (_model.traits.empty())
        {
            throw std::runtime_error("No trait data loaded. Call load() first.");
        }
    }

    // The traits chosen so far, and what they rule out and favour.
    class selection
    {
      public:
        explicit selection(const model& m)
            : _m{&m}, _blocked(m.traits.size(), 0), _boost(m.traits.size(), 1.0)
        {
        }

        void block(std::size_t i) { _blocked[i] = 1; }

        [[nodiscard]] bool blocked(std::size_t i) const { return _blocked[i] != 0; }

        void choose(std::size_t i)
        {
            chosen.push_back(i);
            _blocked[i] = 1;
            for (const auto c : _m->conflicts[i]) _blocked[c] = 1;
            for (const auto& [other, factor] : _m->synergies[i]) _boost[other] *= factor;
        }

        // The weight to draw trait i with, given its base weight and affinity.
        [[nodiscard]] double weight(std::size_t i, double base, double affinity) const
        {
            return blocked(i) ? 0.0 : core::math::fenced(base * affinity * _boost[i]);
        }

        std::vector<std::size_t> chosen; // in the order chosen

      private:
        const model* _m;
        std::vector<char> _blocked;
        std::vector<double> _boost;
    };

    // Validate the request and take its included traits.
    [[nodiscard]] selection start_(const trait_request& request) const
    {
        if (request.include.size() > request.count)
        {
            throw std::invalid_argument(std::format("{} traits included but only {} requested",
                                                    request.include.size(), request.count));
        }
        selection sel{_model};
        for (const auto& key : request.exclude)
        {
            const auto it = _model.trait_index.find(key);
            if (it != _model.trait_index.end()) sel.block(it->second);
        }
        for (const auto& key : request.include)
        {
            const auto it = _model.trait_index.find(key);
            if (it == _model.trait_index.end())
            {
                throw std::invalid_argument(std::format("Unknown trait: {}", key));
            }
            if (sel.blocked(it->second))
            {
                throw std::invalid_argument(std::format(
                    "Included trait {} is repeated, excluded or conflicts with another included trait", key));
            }
            sel.choose(it->second);
        }
        return sel;
    }

    // Step 1: draw each axis's base value; returns the standardised values.
    std::vector<double> draw_axes_(const core::rng& call, personality& p) const
    {
        std::vector<double> z(_model.axes.size(), 0.0);
        p.axes.reserve(_model.axes.size());
        for (std::size_t a = 0; a < _model.axes.size(); ++a)
        {
            const auto& ax = _model.axes[a];
            double base = ax.mean;
            if (ax.sd > 0.0)
            {
                auto r = call.split("axis:" + ax.key);
                base = core::normal(r, ax.mean, ax.sd);
                z[a] = (base - ax.mean) / ax.sd;
            }
            p.axes.push_back({.key = ax.key, .label = ax.label, .base = base, .value = base});
        }
        return z;
    }

    // Step 2: each trait's affinity, exp(sum of loading * z).
    [[nodiscard]] std::vector<double> affinities_(const std::vector<double>& z) const
    {
        std::vector<double> affinity(_model.traits.size(), 1.0);
        for (std::size_t i = 0; i < affinity.size(); ++i)
        {
            double s = 0.0;
            for (std::size_t a = 0; a < z.size(); ++a) s += core::math::fenced(_model.loadings[i][a] * z[a]);
            affinity[i] = core::math::exp(std::clamp(s, -max_exponent_, max_exponent_));
        }
        return affinity;
    }

    [[nodiscard]] personality generate_(const trait_request& request, std::uint64_t call_seed) const
    {
        require_data_();
        auto sel = start_(request);

        const core::rng call{call_seed};
        personality p;
        p._signature = {.seed = call_seed, .data = data_hash()};
        const auto affinity = affinities_(draw_axes_(call, p));

        // Step 3: draw the remaining traits.
        auto r = call.split("traits");
        while (sel.chosen.size() < request.count)
        {
            const auto pick = _model.weights.try_pick_index(
                r, [&](const std::size_t& i, double w) { return sel.weight(i, w, affinity[i]); });
            if (!pick) break;
            sel.choose(*pick);
        }

        // Step 4: traits and resolved axis values.
        p.traits.reserve(sel.chosen.size());
        for (const auto i : sel.chosen)
        {
            p.traits.push_back(_model.traits[i].info);
            for (std::size_t a = 0; a < p.axes.size(); ++a) p.axes[a].value += _model.loadings[i][a];
        }
        return p;
    }
};

} // namespace dasmig

#endif // DASMIG_TRAITGEN_HPP
