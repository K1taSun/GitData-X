#include "git/commit.hpp"

#include <git2.h>
#include <git2/sys/commit.h>

#include <vector>

#include "git/raii.hpp"

namespace gitdatax::git {

GitResult<Oid> create_commit_in_odb(
    const Repo& repo,
    const CreateCommitParams& params) {

    // 1. Konstrukcja sygnatur autora i committera
    git_signature* raw_author = nullptr;
    const int author_res = git_signature_new(
        &raw_author,
        params.author.name.c_str(),
        params.author.email.c_str(),
        static_cast<git_time_t>(params.author.timestamp),
        params.author.tz_offset_min
    );
    if (author_res < 0) {
        return std::unexpected(make_git_error(author_res));
    }
    SignaturePtr author_ptr(raw_author);

    git_signature* raw_committer = nullptr;
    const int committer_res = git_signature_new(
        &raw_committer,
        params.committer.name.c_str(),
        params.committer.email.c_str(),
        static_cast<git_time_t>(params.committer.timestamp),
        params.committer.tz_offset_min
    );
    if (committer_res < 0) {
        return std::unexpected(make_git_error(committer_res));
    }
    SignaturePtr committer_ptr(raw_committer);

    // 2. Tablica wskaźników do OID rodziców
    std::vector<const git_oid*> parent_ptrs;
    parent_ptrs.reserve(params.parent_ids.size());
    for (const auto& p_id : params.parent_ids) {
        parent_ptrs.push_back(&p_id.raw());
    }

    // 3. Zapis commitu w bazie obiektów ODB (update_ref = nullptr)
    git_oid new_commit_oid{};
    const char* encoding_str = params.message_encoding.has_value()
                                   ? params.message_encoding->c_str()
                                   : nullptr;

    const int code = git_commit_create_from_ids(
        &new_commit_oid,
        repo.handle(),
        nullptr, // Nie modyfikujemy żadnej referencji na tym etapie
        author_ptr.get(),
        committer_ptr.get(),
        encoding_str,
        params.message.c_str(),
        &params.tree_id.raw(),
        parent_ptrs.size(),
        parent_ptrs.data()
    );

    if (code < 0) {
        return std::unexpected(make_git_error(code));
    }

    return Oid(new_commit_oid);
}

}  // namespace gitdatax::git
