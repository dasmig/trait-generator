#ifndef DASMIG_TRAITGEN_HPP
#define DASMIG_TRAITGEN_HPP

#include "core.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
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
/// @brief Trait Generator library — personality traits from a weighted catalogue with conflicts, synergies and Big Five axes for C++23.
/// @author Diego Dasso Migotto (diegomigotto at hotmail dot com)
/// @see See doc/usage.md for the narrative tutorial.

DASMIG_CORE_REQUIRE(1, 0);

namespace dasmig
{

/// @brief Return type for personality generation.
///
/// Supports implicit conversion to std::string (the label), streaming via
/// operator<< and JSON output via to_json().
class personality
{
  public:
    std::string key;   ///< Catalogue key (the `key` column).
    std::string label; ///< Display label (the `label` column).

    /// @brief Retrieve the random seed used to generate this personality.
    /// @return The call seed; pass it to the seeded get_personality() overload
    ///         to generate the same personality again.
    [[nodiscard]] std::uint64_t seed() const { return _signature.seed; }

    /// @brief The seed and the data this personality was generated from.
    /// @return The replay signature; `data` is trtg::data_hash() at the
    ///         time of generation.
    [[nodiscard]] const core::seed_signature& signature() const
    {
        return _signature;
    }

    /// @brief Human-readable summary string.
    /// @return The label.
    [[nodiscard]] std::string to_string() const { return label; }

    /// @brief Write this personality as a JSON object.
    /// @param w Writer positioned where a value is allowed.
    void to_json(core::json_writer& w) const
    {
        w.begin_object()
            .field("key", key)
            .field("label", label)
            .key("signature");
        _signature.to_json(w);
        w.end_object();
    }

    /// @brief This personality as a JSON document.
    /// @return Compact JSON, for example
    ///   `{"key":"...","label":"...","signature":{...}}`.
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
    /// @param v The personality to stream.
    /// @return The stream.
    friend std::ostream& operator<<(std::ostream& os, const personality& v)
    {
        os << v.to_string();
        return os;
    }

  private:
    core::seed_signature _signature;
    friend class trtg;
};

/// @brief Trait Generator: draws entries from a weighted catalogue.
///
/// Each draw comes from the `"personality"` stream split from the call seed, so
/// fields added later draw from streams of their own and leave this one
/// unchanged. Output is identical on every compiler and platform
/// (dasmig-core).
///
/// Can be used as a singleton via instance() or constructed
/// independently.
///
/// @par Thread safety
/// Each instance is independent.  Concurrent calls to get_personality() on the
/// **same** instance require external synchronisation.
class trtg
{
  public:
    /// @brief Default constructor — creates an empty generator with no
    ///        data.
    trtg() = default;

    trtg(const trtg&) = delete;            ///< Not copyable.
    trtg& operator=(const trtg&) = delete; ///< Not copyable.
    trtg(trtg&&) noexcept = default;       ///< Movable.
    trtg& operator=(trtg&&) noexcept = default; ///< Movable.
    ~trtg() = default;                       ///< Destructor.

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

    /// @brief Generate a random personality.
    /// @return The personality.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If every loaded entry has weight 0.
    [[nodiscard]] personality get_personality()
    {
        return get_personality(draw_seed_());
    }

    /// @brief Generate a deterministic personality.
    /// @param call_seed Seed; the same seed and data give the same result.
    /// @return The personality.
    /// @throws std::runtime_error If no data has been loaded.
    /// @throws std::invalid_argument If every loaded entry has weight 0.
    [[nodiscard]] personality get_personality(std::uint64_t call_seed) const
    {
        require_data_();
        const core::rng call{call_seed};
        auto r = call.split("personality");
        const auto& e = _entries[_weights.pick(r)];

        personality v;
        v._signature = {.seed = call_seed, .data = data_hash()};
        v.key = e.key;
        v.label = e.label;
        return v;
    }

    // -- Seeding ----------------------------------------------------------

    /// @brief Seed the internal random engine for deterministic
    ///        sequences.
    /// @param seed_value Seed.
    /// @return `*this`.
    trtg& seed(std::uint64_t seed_value)
    {
        _engine.seed(seed_value);
        return *this;
    }

    /// @brief Reseed the engine with a non-deterministic source.
    /// @return `*this`.
    trtg& unseed()
    {
        _engine.unseed();
        return *this;
    }

    // -- Data management --------------------------------------------------

    /// @brief Check whether any data has been loaded.
    /// @return True when at least one entry is loaded.
    [[nodiscard]] bool has_data() const { return !_entries.empty(); }

    /// @brief Return the number of loaded entries.
    /// @return The count.
    [[nodiscard]] std::size_t count() const { return _entries.size(); }

    /// @brief Load data from a resource directory, replacing all data and
    ///        layers loaded before.
    ///
    /// Expects the directory to contain `personality.tsv`. A path that is not
    /// a directory, or a directory without the file, is ignored.
    ///
    /// @param dir Directory such as `resources/lite`.
    /// @throws core::tsv_error If the file is malformed.
    void load(const std::filesystem::path& dir)
    {
        std::error_code ec;
        const auto file = dir / data_file_;
        if (!std::filesystem::is_directory(dir, ec) ||
            !std::filesystem::is_regular_file(file, ec))
        {
            return;
        }
        (void)extend(core::tsv_document::load(file), core::layer_mode::replace);
    }

    /// @brief Load a dataset tier, replacing all data and layers loaded
    ///        before.
    ///
    /// Uses the compiled-in copy when the library is built with
    /// `DASMIG_TRAIT_EMBED`, otherwise searches `resources/`, `../resources`,
    /// `trait-generator/resources` and `../trait-generator/resources`.
    ///
    /// @param tier The tier.
    /// @return False when the tier was not found.
    /// @throws core::tsv_error If the file is malformed.
    [[nodiscard]] bool load(dataset tier)
    {
        const std::string path = std::string{core::tier_name(tier)} + "/" + std::string{data_file_};
        auto doc = core::load_resource(path, embedded_files_(),
                                       core::find_resources(library_name_, path));
        if (!doc) return false;
        (void)extend(std::move(*doc), core::layer_mode::replace);
        return true;
    }

    /// @brief Add a data layer from a TSV file with the `personality.tsv`
    ///        columns.
    ///
    /// Rows are keyed by `key`. In the default override mode a row
    /// replaces the loaded row with the same key and new keys are added;
    /// a row with `disabled` set to 1 removes its key.
    ///
    /// @param file The layer file.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did (rows added, replaced, removed, warnings).
    /// @throws core::tsv_error If the file cannot be read or a row is
    ///         invalid; the data is then unchanged.
    const core::layer_info& extend(const std::filesystem::path& file,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        return extend(core::tsv_document::load(file), mode);
    }

    /// @brief Add a data layer from TSV text (for example from a
    ///        WebAssembly host).
    /// @param text TSV with the `personality.tsv` columns.
    /// @param source Name used in errors.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend_from_string(std::string_view text, std::string source,
                                               core::layer_mode mode = core::layer_mode::override)
    {
        return extend(core::tsv_document::parse(text, std::move(source)), mode);
    }

    /// @brief Add a data layer.
    /// @param doc TSV with the `personality.tsv` columns.
    /// @param mode How the layer combines with the data loaded before.
    /// @return What the layer did.
    /// @throws core::tsv_error If a row is invalid; the data is unchanged.
    const core::layer_info& extend(core::tsv_document doc,
                                   core::layer_mode mode = core::layer_mode::override)
    {
        const auto& info = _table.extend(std::move(doc), mode, [](const core::tsv_row& row) {
            (void)entry::parse(row);
        });
        rebuild_();
        return info;
    }

    /// @brief The data layers loaded, in order.
    /// @return The layer records.
    [[nodiscard]] const std::vector<core::layer_info>& layers() const
    {
        return _table.layers();
    }

    /// @brief Hash of the loaded data, recorded in each result's
    ///        signature().
    /// @return The hash; 0 when nothing is loaded.
    [[nodiscard]] std::uint64_t data_hash() const { return _table.data_hash(); }

  private:
    // -- Internal data structures -----------------------------------------

    static constexpr std::string_view library_name_ = "trait-generator";
    static constexpr std::string_view data_file_ = "personality.tsv";

    static core::table_schema schema_()
    {
        return {.key = {"key"}, .required = {"label", "weight"}, .optional = {}};
    }

    struct entry
    {
        std::string key;
        std::string label;
        double weight{0.0};

        // Parse and check a row; throws core::tsv_error located at the row.
        static entry parse(const core::tsv_row& row)
        {
            entry e;
            e.key = row.get<std::string>("key");
            e.label = row.get<std::string>("label");
            if (e.label.empty()) throw row.error("label", "must not be empty");
            e.weight = row.get<double>("weight");
            if (!(e.weight >= 0.0)) throw row.error("weight", "must not be negative");
            return e;
        }
    };

    core::layered_table _table{schema_()};
    std::vector<entry> _entries; // in table row order
    core::weighted_table<std::size_t> _weights; // entry index, by weight

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

    std::uint64_t draw_seed_() { return _engine(); }

    void rebuild_()
    {
        std::vector<entry> entries;
        core::weighted_table<std::size_t> weights;
        entries.reserve(_table.size());
        for (const auto& row : _table.rows())
        {
            entries.push_back(entry::parse(row));
            weights.add(entries.size() - 1, entries.back().weight);
        }
        _entries = std::move(entries);
        _weights = std::move(weights);
    }

    void require_data_() const
    {
        if (_entries.empty())
        {
            throw std::runtime_error("No personality data loaded. Call load() first.");
        }
    }
};

} // namespace dasmig

#endif // DASMIG_TRAITGEN_HPP
