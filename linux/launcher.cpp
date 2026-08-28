// Launcher binary -- prints the Steam launch-options string the user
// pastes into Counter-Strike: Source's Properties on Linux. We don't
// auto-launch the game (Steam owns that on Linux); the launcher just
// resolves the absolute path to the preload .so and tells the user
// what to paste, with the loud VAC warning attached.

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <climits>
#include <unistd.h>
#include <libgen.h>
#include <sys/stat.h>
#include <pwd.h>

static const char* PRELOAD_NAME = "librawinput2_linux.so";

static bool path_exists(const char* p)
{
	struct stat st;
	return stat(p, &st) == 0;
}

static const char* home_dir()
{
	const char* h = getenv("HOME");
	if (h && *h) return h;
	struct passwd* pw = getpwuid(getuid());
	return pw ? pw->pw_dir : nullptr;
}

// Walks the standard Steam install candidates and any extra libraries
// listed in libraryfolders.vdf, looking for `Counter-Strike Source/`.
// Just informational -- we don't need this to print the launch string,
// but we use it to double-check the install before nagging the user.
static bool find_css_install(char* out, size_t out_len)
{
	const char* h = home_dir();
	if (!h) return false;

	char vdf[PATH_MAX];
	snprintf(vdf, sizeof(vdf), "%s/.local/share/Steam/steamapps/libraryfolders.vdf", h);
	if (!path_exists(vdf)) {
		snprintf(vdf, sizeof(vdf), "%s/.steam/steam/steamapps/libraryfolders.vdf", h);
		if (!path_exists(vdf)) return false;
	}

	FILE* f = fopen(vdf, "r");
	if (!f) return false;

	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		// crude: any "path"  "<...>" line
		const char* k = strstr(line, "\"path\"");
		if (!k) continue;
		const char* q1 = strchr(k + 6, '"');
		if (!q1) continue;
		const char* q2 = strchr(q1 + 1, '"');
		if (!q2) continue;
		size_t n = (size_t)(q2 - q1 - 1);
		if (n == 0 || n >= PATH_MAX - 64) continue;
		char libpath[PATH_MAX];
		memcpy(libpath, q1 + 1, n);
		libpath[n] = 0;

		char candidate[PATH_MAX];
		snprintf(candidate, sizeof(candidate), "%s/steamapps/common/Counter-Strike Source", libpath);
		if (path_exists(candidate)) {
			snprintf(out, out_len, "%s", candidate);
			fclose(f);
			return true;
		}
	}
	fclose(f);
	return false;
}

int main()
{
	char exepath[PATH_MAX];
	ssize_t n = readlink("/proc/self/exe", exepath, sizeof(exepath) - 1);
	if (n <= 0) {
		fprintf(stderr, "could not resolve own path via /proc/self/exe\n");
		return 1;
	}
	exepath[n] = 0;

	char dir_buf[PATH_MAX];
	snprintf(dir_buf, sizeof(dir_buf), "%s", exepath);
	const char* dir = dirname(dir_buf);

	char so_path[PATH_MAX];
	snprintf(so_path, sizeof(so_path), "%s/%s", dir, PRELOAD_NAME);

	printf("RawInput2BunnyhopAPE -- Linux port\n");
	printf("==================================\n\n");
	printf("m_rawinput 2 mouse interpolation (always on)\n");
	printf("F7 toggles the viewpunch remover (on by default)\n");
	printf("download progress shown in the loading bar\n");
	printf("missing / differing maps fetched from fastdl.me\n\n");

	if (!path_exists(so_path)) {
		printf("WARNING: preload .so not found at:\n  %s\n\n", so_path);
		printf("Build it with `make` first.\n\n");
	}

	char css[PATH_MAX];
	if (find_css_install(css, sizeof(css))) {
		printf("Detected CS:S install:\n  %s\n\n", css);
	} else {
		printf("Could not locate Counter-Strike: Source via Steam libraryfolders.vdf.\n");
		printf("(Not fatal -- you just need CS:S installed somewhere Steam knows about.)\n\n");
	}

	printf("In Steam, right-click Counter-Strike: Source -> Properties -> Launch Options,\n");
	printf("and paste the following (all one line):\n\n");
	printf("    LD_PRELOAD=\"%s\" %%command%% -insecure\n\n", so_path);

	printf("VAC WARNING:\n");
	printf("  -insecure is REQUIRED. Joining a VAC-secured server while the\n");
	printf("  preload is loaded will earn you a VAC ban. Only join servers that\n");
	printf("  the in-game browser shows as insecure.\n\n");

	printf("Notes:\n");
	printf("  * Interpolation is always-on while the preload is loaded.\n");
	printf("    Remove `LD_PRELOAD=...` from your launch options to disable.\n");
	printf("  * Default install is silent (no log file). For debugging, prepend\n");
	printf("    RAWINPUT2_DIAG=1 to the launch options -- writes to\n");
	printf("    /tmp/rawinput2_linux.log (override path with $RAWINPUT2_LOG).\n");

	return 0;
}
