#pragma once

#include <expected>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/commit_node.hpp"
#include "core/dag_snapshot.hpp"
#include "git/oid.hpp"

namespace gitdatax::core {

// Wynik weryfikacji formalnych niezmienników DAG.
struct VerificationError {
    std::string invariant_name; // Nazwa naruszonego niezmiennika (np. "Boundary Tree Invariant")
    git::Oid commit_id;         // Identyfikator powiązanego commitu
    std::string details;        // Szczegółowy opis techniczny błędu
};

// Formalny weryfikator struktury grafu i rozbitych commitów (Sekcja 1.4 i 8.2).
// Zapewnia, że modyfikacja historii nie wprowadza żadnych uszkodzeń danych ani konfliktów w drzewach.
class DagVerifier {
public:
    // Weryfikuje 4 formalne niezmienniki na zrzucie przed i po transformacji:
    // 1. Bilans wierzchołków (|Commits_new| == sum(K_c))
    // 2. Niezmienność końcowego drzewa (tree(M(c).back()) == tree(c))
    // 3. Spójność wewnętrzna łańcuchów (1 -> K)
    // 4. Izomorfizm krawędzi zewnętrznych (kolejność i wiązania rodziców)
    static std::expected<void, VerificationError> verify(
        const DagSnapshot& original_dag,
        const std::unordered_map<git::Oid, std::vector<git::Oid>>& chain_map,
        const std::unordered_map<git::Oid, CommitNode>& new_commits
    );
};

}  // namespace gitdatax::core
