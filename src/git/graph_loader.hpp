#pragma once

#include "core/dag_snapshot.hpp"
#include "git/error.hpp"
#include "git/repo.hpp"

namespace gitdatax::git {

// Wczytuje do RAM wszystkie commity osiągalne z HEAD, gałęzi lokalnych (refs/heads/*)
// i tagów (refs/tags/*). Zwrócony zrzut nie zawiera wskaźników libgit2, więc
// po powrocie z funkcji repozytorium można zamknąć, a zrzut czytać z wielu wątków.
// Tagi, które nie prowadzą do commitu (np. do drzewa), są pomijane.
// Wywołuj z wątku silnika ODB, bo git_repository nie jest bezpieczne wątkowo.
GitResult<core::DagSnapshot> load_dag_snapshot(const Repo& repo);

}  // namespace gitdatax::git

