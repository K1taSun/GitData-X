#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "git/error.hpp"
#include "git/oid.hpp"
#include "git/raii.hpp"
#include "git/repo.hpp"

namespace gitdatax::git {

// Informacja o pojedynczym pliku zmodyfikowanym w diffie commitu.
struct FileDelta {
    std::string path;
    Oid old_blob_oid;
    Oid new_blob_oid;
    std::uint32_t filemode{0100644};
    bool is_binary{false};
    bool is_deleted{false};
    bool is_new{false};
};

// Informacja o pojedynczym bloku zmian (hunk).
struct HunkDelta {
    std::string file_path;
    std::size_t hunk_index{0};
    std::string header;
    std::string diff_text; // Tekstowa zawartość bloku zmian (hunk)
};

// Przydział zmian w kodzie do pojedynczego kroku rozbicia commitu (1 -> K).
struct SplitStepAssignment {
    std::size_t step_index{0};                  // Indeks kroku 0 .. K-1
    std::vector<FileDelta> completed_files;     // Pliki przyjmujące w tym kroku docelowy blob (Przypadek A)
    std::vector<HunkDelta> partial_hunks;       // Hunki aplikowane cząstkowo (Przypadek B)
};

// Silnik syntezy pośrednich drzew i blobów w pamięci RAM (Sekcja 5.2).
// Operuje wyłącznie na pamięci operacyjnej (wirtualny git_index_new w RAM)
// oraz bazie obiektów ODB, nie dotykając plików roboczych na dysku.
class InMemorySplitter {
public:
    explicit InMemorySplitter(Repo& repo);

    // Wylicza plikowe różnice między dwoma drzewami
    GitResult<std::vector<FileDelta>> diff_trees(const Oid& old_tree_id, const Oid& new_tree_id);

    // Wylicza hunki różnicowe dla pojedynczego zmodyfikowanego pliku
    GitResult<std::vector<HunkDelta>> diff_file_hunks(
        const Oid& old_tree_id,
        const Oid& new_tree_id,
        const std::string& path
    );

    // Bezdotykowa synteza pośredniego drzewa T_k w pamięci RAM i zapis do ODB:
    // git_index_new + git_index_read_tree + git_blob_create_from_buffer + git_index_write_tree_to
    GitResult<Oid> synthesize_intermediate_tree(
        const Oid& prev_tree_id,
        const SplitStepAssignment& assignment,
        const Oid& final_tree_id
    );

    // Tworzy lub pobiera pusty obiekt git_tree w ODB (drzewo bazowe dla commitu korzenia)
    GitResult<Oid> empty_tree_oid();

private:
    Repo& repo_;
};

}  // namespace gitdatax::git
