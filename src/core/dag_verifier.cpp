#include "core/dag_verifier.hpp"

namespace gitdatax::core {

std::expected<void, VerificationError> DagVerifier::verify(
    const DagSnapshot& original_dag,
    const std::unordered_map<git::Oid, std::vector<git::Oid>>& chain_map,
    const std::unordered_map<git::Oid, CommitNode>& new_commits) {

    // 1. Bilans wierzchołków: całkowita liczba nowych commitów musi równać się sumie długości łańcuchów
    std::size_t total_expected_commits = 0;
    for (const auto& [orig_id, chain] : chain_map) {
        if (chain.empty()) {
            return std::unexpected(VerificationError{
                .invariant_name = "Bilans wierzcholkow",
                .commit_id = orig_id,
                .details = "Lancuch odwzorowania dla commitu jest pusty (K_c < 1)"
            });
        }
        total_expected_commits += chain.size();
    }

    if (new_commits.size() != total_expected_commits) {
        return std::unexpected(VerificationError{
            .invariant_name = "Bilans wierzcholkow",
            .commit_id = git::Oid{},
            .details = "Liczba nowych commitow (" + std::to_string(new_commits.size()) +
                       ") nie zgadza sie z suma dlugosci lancuchow (" +
                       std::to_string(total_expected_commits) + ")"
        });
    }

    // Weryfikacja dla każdego oryginalnego commitu
    for (const auto& [orig_id, chain] : chain_map) {
        const CommitNode* orig_node = original_dag.find(orig_id);
        if (orig_node == nullptr) {
            return std::unexpected(VerificationError{
                .invariant_name = "Spojnosc referencyjna",
                .commit_id = orig_id,
                .details = "Oryginalny commit z mapy lancuchow nie istnieje w DagSnapshot"
            });
        }

        // 2. Niezmienność końcowego drzewa (Boundary Tree Invariant)
        // tree_id(M(c).back()) == tree_id(c)
        const git::Oid last_new_id = chain.back();
        const auto it_last = new_commits.find(last_new_id);
        if (it_last == new_commits.end()) {
            return std::unexpected(VerificationError{
                .invariant_name = "Boundary Tree Invariant",
                .commit_id = last_new_id,
                .details = "Ostatnie ogniwo lancucha nie istnieje w nowo utworzonych commitach"
            });
        }

        if (it_last->second.tree_id != orig_node->tree_id) {
            return std::unexpected(VerificationError{
                .invariant_name = "Boundary Tree Invariant",
                .commit_id = orig_id,
                .details = "Drzewo koncowe nowego commitu (" + it_last->second.tree_id.to_hex() +
                           ") rozni sie bitowo od oryginalnego (" + orig_node->tree_id.to_hex() + ")"
            });
        }

        // 3. Spójność wewnętrzna łańcuchów (1 -> K)
        const std::size_t K_c = chain.size();
        for (std::size_t j = 0; j < K_c; ++j) {
            const git::Oid current_id = chain[j];
            const auto it_curr = new_commits.find(current_id);
            if (it_curr == new_commits.end()) {
                return std::unexpected(VerificationError{
                    .invariant_name = "Spojnosc lancucha",
                    .commit_id = current_id,
                    .details = "Ogniwo " + std::to_string(j + 1) + "/" + std::to_string(K_c) +
                               " nie istnieje w bazie obiektow"
                });
            }

            if (it_curr->second.tree_id.is_zero()) {
                return std::unexpected(VerificationError{
                    .invariant_name = "Spojnosc lancucha",
                    .commit_id = current_id,
                    .details = "Drzewo posredniego commitu jest zerowe (brak git_tree w ODB)"
                });
            }

            if (j > 0) {
                // Każde kolejne ogniwo j in {1, ..., K_c - 1} musi mieć dokładnie 1 rodzica będącego chain[j - 1]
                const auto& parents = it_curr->second.parents;
                if (parents.size() != 1 || parents[0] != chain[j - 1]) {
                    return std::unexpected(VerificationError{
                        .invariant_name = "Spojnosc lancucha",
                        .commit_id = current_id,
                        .details = "Ogniwo " + std::to_string(j + 1) + " nie wskazuje bezposrednio na ogniwo " +
                                   std::to_string(j) + " jako jedynego rodzica"
                    });
                }
            }
        }

        // 4. Izomorfizm krawędzi zewnętrznych
        // Pierwszy commit w łańcuchu M(c)[0] przejmuje jako rodziców końcowe ogniwa łańcuchów oryginalnych rodziców
        // z zachowaniem ich ścisłej kolejności.
        const git::Oid first_new_id = chain.front();
        const auto it_first = new_commits.find(first_new_id);
        const auto& actual_parents = it_first->second.parents;

        std::vector<git::Oid> expected_parents;
        expected_parents.reserve(orig_node->parents.size());

        for (const git::Oid& orig_parent : orig_node->parents) {
            const auto it_p = chain_map.find(orig_parent);
            if (it_p != chain_map.end()) {
                // Potomkowie podpinają się zawsze do chain.back()
                expected_parents.push_back(it_p->second.back());
            } else {
                // Rodzic nie był przepisywany (pozostaje oryginalny OID)
                expected_parents.push_back(orig_parent);
            }
        }

        if (actual_parents != expected_parents) {
            return std::unexpected(VerificationError{
                .invariant_name = "Izomorfizm krawedzi zewnetrznych",
                .commit_id = orig_id,
                .details = "Niezgodnosc wektora rodzicow pierwszego ogniwa: rodzice nie odpowiadaja koncom lancuchow oryginalnych rodzicow"
            });
        }
    }

    return {};
}

}  // namespace gitdatax::core
