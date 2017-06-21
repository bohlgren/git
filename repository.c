#include "cache.h"
#include "repository.h"
#include "config.h"
#include "submodule-config.h"

/* The main repository */
static struct repository the_repo;
struct repository *the_repository = &the_repo;

static char *git_path_from_env(const char *envvar, const char *git_dir,
			       const char *path, int fromenv)
{
	if (fromenv) {
		const char *value = getenv(envvar);
		if (value)
			return xstrdup(value);
	}

	return xstrfmt("%s/%s", git_dir, path);
}

static int find_common_dir(struct strbuf *sb, const char *gitdir, int fromenv)
{
	if (fromenv) {
		const char *value = getenv(GIT_COMMON_DIR_ENVIRONMENT);
		if (value) {
			strbuf_addstr(sb, value);
			return 1;
		}
	}

	return get_common_dir_noenv(sb, gitdir);
}

/* called after setting gitdir */
static void repo_setup_env(struct repository *repo)
{
	struct strbuf sb = STRBUF_INIT;

	if (!repo->gitdir)
		BUG("gitdir wasn't set before setting up the environment");

	repo->different_commondir = find_common_dir(&sb, repo->gitdir,
						    !repo->ignore_env);
	repo->commondir = strbuf_detach(&sb, NULL);
	repo->objectdir = git_path_from_env(DB_ENVIRONMENT, repo->commondir,
					    "objects", !repo->ignore_env);
	repo->graft_file = git_path_from_env(GRAFT_ENVIRONMENT, repo->commondir,
					     "info/grafts", !repo->ignore_env);
	repo->index_file = git_path_from_env(INDEX_ENVIRONMENT, repo->gitdir,
					     "index", !repo->ignore_env);
}

void repo_set_gitdir(struct repository *repo, const char *path)
{
	const char *gitfile = read_gitfile(path);

	/*
	 * NEEDSWORK: Eventually we want to be able to free gitdir and the rest
	 * of the environment before reinitializing it again, but we have some
	 * crazy code paths where we try to set gitdir with the current gitdir
	 * and we don't want to free gitdir before copying the passed in value.
	 */
	repo->gitdir = xstrdup(gitfile ? gitfile : path);

	repo_setup_env(repo);
}

/*
 * Attempt to resolve and set the provided 'gitdir' for repository 'repo'.
 * Return 0 upon success and a non-zero value upon failure.
 */
static int repo_init_gitdir(struct repository *repo, const char *gitdir)
{
	int ret = 0;
	int error = 0;
	char *abspath = NULL;
	char *suspect = NULL;
	const char *resolved_gitdir;

	abspath = real_pathdup(gitdir, 0);
	if (!abspath) {
		ret = -1;
		goto out;
	}

	/* First assume 'gitdir' references the gitdir directly */
	resolved_gitdir = resolve_gitdir_gently(abspath, &error);
	/* otherwise; try 'gitdir'.git */
	if (!resolved_gitdir) {
		suspect = xstrfmt("%s/.git", abspath);
		resolved_gitdir = resolve_gitdir_gently(suspect, &error);
		if (!resolved_gitdir) {
			ret = -1;
			goto out;
		}
	}

	repo_set_gitdir(repo, resolved_gitdir);

out:
	free(abspath);
	free(suspect);
	return ret;
}

void repo_set_worktree(struct repository *repo, const char *path)
{
	repo->worktree = real_pathdup(path, 1);
}

static int verify_repo_format(struct repository_format *format,
			      const char *commondir)
{
	int ret = 0;
	struct strbuf sb = STRBUF_INIT;

	strbuf_addf(&sb, "%s/config", commondir);
	read_repository_format(format, sb.buf);
	strbuf_reset(&sb);

	if (verify_repository_format(format, &sb) < 0) {
		warning("%s", sb.buf);
		ret = -1;
	}

	strbuf_release(&sb);
	return ret;
}

/*
 * Initialize 'repo' based on the provided 'gitdir'.
 * Return 0 upon success and a non-zero value upon failure.
 */
int repo_init(struct repository *repo, const char *gitdir, const char *worktree)
{
	struct repository_format format;
	memset(repo, 0, sizeof(*repo));

	repo->ignore_env = 1;

	if (repo_init_gitdir(repo, gitdir))
		goto error;

	if (verify_repo_format(&format, repo->commondir))
		goto error;

	if (worktree)
		repo_set_worktree(repo, worktree);

	return 0;

error:
	repo_clear(repo);
	return -1;
}

void repo_clear(struct repository *repo)
{
	free(repo->gitdir);
	repo->gitdir = NULL;
	free(repo->commondir);
	repo->commondir = NULL;
	free(repo->objectdir);
	repo->objectdir = NULL;
	free(repo->graft_file);
	repo->graft_file = NULL;
	free(repo->index_file);
	repo->index_file = NULL;
	free(repo->worktree);
	repo->worktree = NULL;

	if (repo->config) {
		git_configset_clear(repo->config);
		free(repo->config);
		repo->config = NULL;
	}

	if (repo->index) {
		discard_index(repo->index);
		free(repo->index);
		repo->index = NULL;
	}

	if (repo->submodule_cache) {
		submodule_cache_free(repo->submodule_cache);
		repo->submodule_cache = NULL;
	}

	memset(repo, 0, sizeof(*repo));
}

int repo_read_index(struct repository *repo)
{
	if (!repo->index)
		repo->index = xcalloc(1, sizeof(struct index_state));
	else
		discard_index(repo->index);

	return read_index_from(repo->index, repo->index_file);
}
