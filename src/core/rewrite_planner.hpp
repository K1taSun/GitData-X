#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/commit_node.hpp"
#include "core/commit_splitter.hpp"
#include "core/dag_snapshot.hpp"
#include "git/oid.hpp"

namespace gitdatax::core {

// Status planowanej operacji na commicie w podglądzie i kaskadzie
enum class CommitTransformType {
    Unchanged,      // Hash SHA nienaruszony
    Modify1to1,     // Bezpośrednia modyfikacja (data, autor, opis)
    Split1toK,      // Rozbicie commitu 1 -> K
    CascadeShaOnly  // Niezmieniony bezpośrednio, ale objęty kaskadową zmianą hasha SHA rodziców
};

// Plan pojedynczego commitu
struct CommitRewritePlan {
    git::Oid original_id;
    CommitTransformType type{CommitTransformType::Unchanged};

    std::optional<CommitSignature> new_author;
    std::optional<CommitSignature> new_committer;
    std::optional<std::string> new_message;
    std::optional<SplitPlan> split_plan;
};

// Moduł planowania i kalkulacji propagacji łańcuchów SHA (Sekcja 3.2).
// Pozwala zasymulować kaskadę hashy SHA przed wykonaniem jakichkolwiek zapisów w ODB.
class RewritePlanner {
public:
    explicit RewritePlanner(const DagSnapshot& dag);

    // Zaplanowanie transformacji 1 -> 1
    void set_modify_action(const git::Oid& id,
                           std::optional<CommitSignature> author = std::nullopt,
                           std::optional<CommitSignature> committer = std::nullopt,
                           std::optional<std::string> message = std::nullopt);

    // Zaplanowanie rozbicia 1 -> K
    void set_split_action(const git::Oid& id, SplitPlan plan);

    void clear_action(const git::Oid& id);

    const CommitRewritePlan* find_action(const git::Oid& id) const;

    // Symuluje kaskadę hashy SHA w porządku topologicznym Kahna.
    // Zwraca status dla każdego commitu w grafie.
    std::unordered_map<git::Oid, CommitTransformType> simulate_cascade() const;

    // Zwraca nazwy wszystkich lokalnych gałęzi, które wskazują na potomków modyfikowanych commitów
    std::vector<std::string> affected_branches() const;

private:
    const DagSnapshot& dag_;
    std::unordered_map<git::Oid, CommitRewritePlan> explicit_plans_;
};

}  // namespace gitdatax::core
