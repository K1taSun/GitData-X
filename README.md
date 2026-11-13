# GitData X

Narzędzie terminalowe (TUI) w C++23 do edycji metadanych czasowych oraz dzielenia commitów bezpośrednio w bazie obiektów Git (ODB), bez modyfikacji drzewa roboczego i systemowego indeksu.

## Funkcjonalności

- **Niemutowalny model DAG w RAM**: pełna izolacja interfejsu od struktur `libgit2` oraz deterministyczne sortowanie topologiczne (algorytm Kahna).
- **Operacje na czasie (6 trybów)**:
  1. Czas bezwzględny lub relatywny względem rodzica.
  2. Stałe przesunięcie o deltę (`+2h`, `-15m`).
  3. Rozkład losowy (Jitter) w zadanym przedziale z zachowaniem monotoniczności.
  4. Rzutowanie do godzin roboczych z obsługą przepełnienia doby (Day Overflow).
  5. Synchronizacja sygnatur autora/committera oraz automatyczna naprawa chronologii (Auto-Heal).
  6. Odcinkowy Random Walk z dokładnym domknięciem do docelowego znacznika czasu.
- **Podział commitu (1 -> K)**: dekompozycja zmian na $K$ pośrednich drzew przy użyciu wirtualnego indeksu w pamięci (`git_index_new`). Ostatni commit zachowuje oryginalne `tree_id`.
- **Bezpieczeństwo**: weryfikacja 4 niezmienników grafu przed zapisem, automatyczne punkty przywracania w `refs/gitdatax/backup/` oraz atomowe transakcje referencji (CAS).

## Wymagania

- Kompilator wspierający C++23 (MSVC 2022, GCC 13+, Clang 16+)
- CMake 3.21+
- Ninja
- vcpkg (zależności zdefiniowane w `vcpkg.json`: `libgit2`, `ftxui`, `catch2`, `rapidcheck`)

## Budowanie i testy

Konfiguracja i kompilacja za pomocą presetów CMake (wymagana zmienna środowiskowa `VCPKG_ROOT`):

```powershell
cmake --preset default
cmake --build build
```

Uruchomienie testów jednostkowych i integracyjnych:

```powershell
ctest --test-dir build --output-on-failure
```

Opcjonalnie na systemie Windows z użyciem skryptu pomocniczego ładującego środowisko MSVC:

```powershell
.\scripts\build.ps1
```

## Użycie

Uruchomienie w bieżącym repozytorium lub ze wskazaniem ścieżki:

```powershell
./build/gitdatax [ścieżka_do_repozytorium]
```

### Skróty klawiszowe TUI

- `Space` – zaznaczenie commitu
- `1`–`6` – wybór trybu transformacji czasu
- `s` – otwarcie kreatora podziału commitu (1 -> K)
- `R` – przelosowanie ziarna (seed) dla rozkładu czasu
- `p` – zatwierdzenie planu i zapis zmian w ODB
- `u` – wycofanie ostatniej operacji z kopii zapasowej (Undo)
- `q` / `Esc` – wyjście

## Struktura projektu

- `src/git/` – niskopoziomowe wrappery RAII nad `libgit2`, transakcje CAS, operacje na ODB i wirtualnym indeksie.
- `src/core/` – model grafu `DagSnapshot`, sortowanie Kahna, algorytmy czasu, weryfikator niezmienników, silnik przepisywania.
- `src/tui/` – interfejs terminalowy oparty na FTXUI.
- `tests/` – testy jednostkowe i integracyjne (Catch2).

