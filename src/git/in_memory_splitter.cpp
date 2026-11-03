#include "git/in_memory_splitter.hpp"

#include <git2.h>

namespace gitdatax::git {

InMemorySplitter::InMemorySplitter(Repo& repo) : repo_(repo) {}

GitResult<Oid> InMemorySplitter::empty_tree_oid() {
    git_treebuilder* raw_tb = nullptr;
    const int code = git_treebuilder_new(&raw_tb, repo_.handle(), nullptr);
    if (code < 0) {
        return std::unexpected(make_git_error(code));
    }

    git_oid empty_oid{};
    const int write_code = git_treebuilder_write(&empty_oid, raw_tb);
    git_treebuilder_free(raw_tb);

    if (write_code < 0) {
        return std::unexpected(make_git_error(write_code));
    }
    return Oid(empty_oid);
}

GitResult<std::vector<FileDelta>> InMemorySplitter::diff_trees(
    const Oid& old_tree_id,
    const Oid& new_tree_id) {

    TreePtr old_tree;
    if (!old_tree_id.is_zero()) {
        git_tree* raw = nullptr;
        const int c = git_tree_lookup(&raw, repo_.handle(), &old_tree_id.raw());
        if (c < 0) return std::unexpected(make_git_error(c));
        old_tree.reset(raw);
    }

    TreePtr new_tree;
    if (!new_tree_id.is_zero()) {
        git_tree* raw = nullptr;
        const int c = git_tree_lookup(&raw, repo_.handle(), &new_tree_id.raw());
        if (c < 0) return std::unexpected(make_git_error(c));
        new_tree.reset(raw);
    }

    git_diff* raw_diff = nullptr;
    const int diff_code = git_diff_tree_to_tree(
        &raw_diff,
        repo_.handle(),
        old_tree.get(),
        new_tree.get(),
        nullptr
    );
    if (diff_code < 0) {
        return std::unexpected(make_git_error(diff_code));
    }
    DiffPtr diff(raw_diff);

    const std::size_t count = git_diff_num_deltas(diff.get());
    std::vector<FileDelta> result;
    result.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const git_diff_delta* d = git_diff_get_delta(diff.get(), i);
        if (d == nullptr) continue;

        FileDelta fd;
        fd.path = (d->new_file.path != nullptr && d->status != GIT_DELTA_DELETED)
                      ? d->new_file.path
                      : (d->old_file.path != nullptr ? d->old_file.path : "");
        fd.old_blob_oid = Oid(d->old_file.id);
        fd.new_blob_oid = Oid(d->new_file.id);
        fd.filemode = (d->new_file.mode != 0) ? d->new_file.mode : d->old_file.mode;
        fd.is_binary = (d->flags & GIT_DIFF_FLAG_BINARY) != 0;
        fd.is_deleted = (d->status == GIT_DELTA_DELETED);
        fd.is_new = (d->status == GIT_DELTA_ADDED);

        result.push_back(std::move(fd));
    }

    return result;
}

GitResult<std::vector<HunkDelta>> InMemorySplitter::diff_file_hunks(
    const Oid& old_tree_id,
    const Oid& new_tree_id,
    const std::string& path) {

    TreePtr old_tree;
    if (!old_tree_id.is_zero()) {
        git_tree* raw = nullptr;
        if (git_tree_lookup(&raw, repo_.handle(), &old_tree_id.raw()) == 0) {
            old_tree.reset(raw);
        }
    }

    TreePtr new_tree;
    if (!new_tree_id.is_zero()) {
        git_tree* raw = nullptr;
        if (git_tree_lookup(&raw, repo_.handle(), &new_tree_id.raw()) == 0) {
            new_tree.reset(raw);
        }
    }

    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.pathspec.count = 1;
    char* path_ptr = const_cast<char*>(path.c_str());
    opts.pathspec.strings = &path_ptr;

    git_diff* raw_diff = nullptr;
    const int code = git_diff_tree_to_tree(&raw_diff, repo_.handle(), old_tree.get(), new_tree.get(), &opts);
    if (code < 0) return std::unexpected(make_git_error(code));
    DiffPtr diff(raw_diff);

    if (git_diff_num_deltas(diff.get()) == 0) {
        return std::vector<HunkDelta>{};
    }

    git_patch* raw_patch = nullptr;
    const int patch_code = git_patch_from_diff(&raw_patch, diff.get(), 0);
    if (patch_code < 0) return std::unexpected(make_git_error(patch_code));
    PatchPtr patch(raw_patch);

    const std::size_t num_hunks = git_patch_num_hunks(patch.get());
    std::vector<HunkDelta> hunks;
    hunks.reserve(num_hunks);

    for (std::size_t h = 0; h < num_hunks; ++h) {
        const git_diff_hunk* hunk_info = nullptr;
        std::size_t lines_in_hunk = 0;
        if (git_patch_get_hunk(&hunk_info, &lines_in_hunk, patch.get(), h) < 0) {
            continue;
        }

        HunkDelta hd;
        hd.file_path = path;
        hd.hunk_index = h;
        if (hunk_info != nullptr && hunk_info->header_len > 0) {
            hd.header = std::string(hunk_info->header, hunk_info->header_len);
        }

        // Zbieramy linie bloku zmian
        for (std::size_t l = 0; l < lines_in_hunk; ++l) {
            const git_diff_line* line_info = nullptr;
            if (git_patch_get_line_in_hunk(&line_info, patch.get(), h, l) == 0 && line_info != nullptr) {
                hd.diff_text.append(line_info->content, line_info->content_len);
            }
        }
        hunks.push_back(std::move(hd));
    }

    return hunks;
}

GitResult<Oid> InMemorySplitter::synthesize_intermediate_tree(
    const Oid& prev_tree_id,
    const SplitStepAssignment& assignment,
    const Oid& /*final_tree_id*/) {

    // 1. Tworzymy wirtualny indeks w pamięci RAM (git_index_new)
    git_index* raw_idx = nullptr;
    const int idx_code = git_index_new(&raw_idx);
    if (idx_code < 0) {
        return std::unexpected(make_git_error(idx_code));
    }
    IndexPtr mem_idx(raw_idx);

    // 2. Wczytujemy drzewo z kroku poprzedniego T_{k-1}
    if (!prev_tree_id.is_zero()) {
        git_tree* raw_prev_tree = nullptr;
        const int tree_code = git_tree_lookup(&raw_prev_tree, repo_.handle(), &prev_tree_id.raw());
        if (tree_code < 0) {
            return std::unexpected(make_git_error(tree_code));
        }
        TreePtr prev_tree(raw_prev_tree);

        const int read_code = git_index_read_tree(mem_idx.get(), prev_tree.get());
        if (read_code < 0) {
            return std::unexpected(make_git_error(read_code));
        }
    }

    // 3. Przypadek A: Całe pliki kończące swoje zmiany w kroku k
    for (const auto& file : assignment.completed_files) {
        if (file.is_deleted) {
            git_index_remove_bypath(mem_idx.get(), file.path.c_str());
        } else {
            git_index_entry entry{};
            entry.path = file.path.c_str();
            entry.mode = file.filemode;
            entry.id = file.new_blob_oid.raw();
            const int add_code = git_index_add(mem_idx.get(), &entry);
            if (add_code < 0) {
                return std::unexpected(make_git_error(add_code));
            }
        }
    }

    // 4. Przypadek B: Częściowe hunki zapisywane jako nowe bloby w ODB
    for (const auto& hunk : assignment.partial_hunks) {
        if (hunk.diff_text.empty()) continue;

        git_oid partial_blob_oid{};
        const int blob_code = git_blob_create_from_buffer(
            &partial_blob_oid,
            repo_.handle(),
            hunk.diff_text.data(),
            hunk.diff_text.size()
        );
        if (blob_code < 0) {
            return std::unexpected(make_git_error(blob_code));
        }

        git_index_entry entry{};
        entry.path = hunk.file_path.c_str();
        entry.mode = 0100644;
        entry.id = partial_blob_oid;
        const int add_code = git_index_add(mem_idx.get(), &entry);
        if (add_code < 0) {
            return std::unexpected(make_git_error(add_code));
        }
    }

    // 5. Zapis wirtualnego indeksu jako nowego obiektu git_tree w ODB
    git_oid new_tree_oid{};
    const int write_code = git_index_write_tree_to(&new_tree_oid, mem_idx.get(), repo_.handle());
    if (write_code < 0) {
        return std::unexpected(make_git_error(write_code));
    }

    return Oid(new_tree_oid);
}

}  // namespace gitdatax::git
