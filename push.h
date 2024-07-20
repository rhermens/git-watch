#include <git2/commit.h>
#include <git2/remote.h>
#include <git2/types.h>

typedef struct status_cb_payload {
    git_index *index;
    unsigned int changes;
} status_cb_payload;

int do_push(git_repository *repository, git_remote *remote, git_commit_create_options *commit_options, git_push_options *push_options);

