#include <git2/checkout.h>
#include <git2/remote.h>
#include <git2/types.h>

typedef struct fastforward_cb_payload {
    git_repository *repository;
    git_checkout_options *checkout_options;
} fastforward_cb_payload;

int do_fastforward(git_repository *repository, git_remote *remote, git_fetch_options *fetch_options, git_checkout_options *checkout_options);
