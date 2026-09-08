// Repossession -- the child-process launcher, tested off the audio thread.
//
//   make && ./test_process
//
// The case this exists for: a tool that cannot be started at all must not be
// reported as a tool that ran and exited 127. A shell reports "command not
// found" as exit 127, and the old launcher's bare _exit(127) after a failed
// execvp() was indistinguishable from it -- so a stale `#!` wrapper (a pip shim
// outliving its Homebrew Python is the classic) surfaced on the panel as
// "yt-dlp failed (exit 127)", blaming a program that was never reached.
#include "../../src/Process.hpp"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

static int g_fail = 0;
static int g_checks = 0;
static const char* g_test = "";

#define CHECK(cond, msg) do { \
	g_checks++; \
	if (!(cond)) { \
		g_fail++; \
		std::printf("  FAIL [%s] %s   (%s:%d)\n", g_test, (msg), __FILE__, __LINE__); \
	} \
} while (0)

static void startTest(const char* name) { g_test = name; std::printf("%s\n", name); }

static std::string g_tmp;

/** A file in the scratch dir with the given contents and mode. */
static std::string writeFile(const std::string& name, const std::string& body, mode_t mode) {
	std::string path = g_tmp + "/" + name;
	FILE* f = std::fopen(path.c_str(), "w");
	if (!f) {
		std::printf("  cannot write %s\n", path.c_str());
		std::exit(2);
	}
	std::fwrite(body.data(), 1, body.size(), f);
	std::fclose(f);
	::chmod(path.c_str(), mode);
	return path;
}

static rp::ProcessResult runv(const std::vector<std::string>& argv) {
	return rp::run(argv, NULL);
}

static bool contains(const std::string& hay, const char* needle) {
	return hay.find(needle) != std::string::npos;
}


// --- the normal paths, which must keep working ------------------------------

static void testSuccess() {
	startTest("a program that runs and succeeds");
	std::vector<std::string> a;
	a.push_back("/bin/echo");
	a.push_back("hello");
	rp::ProcessResult r = runv(a);
	CHECK(r.started, "started");
	CHECK(r.ok(), "ok()");
	CHECK(r.status == 0, "status 0");
	CHECK(r.execError.empty(), "no execError on success");
	CHECK(r.out == "hello\n", "stdout captured");
	CHECK(r.err.empty(), "stderr empty");
}

static void testStreamsStaySeparate() {
	startTest("stdout and stderr do not merge");
	std::vector<std::string> a;
	a.push_back("/bin/sh");
	a.push_back("-c");
	a.push_back("echo out; echo err 1>&2");
	rp::ProcessResult r = runv(a);
	CHECK(r.ok(), "ok()");
	CHECK(r.out == "out\n", "stdout is only stdout");
	CHECK(r.err == "err\n", "stderr is only stderr");
}

static void testNonZeroExit() {
	startTest("a program that runs and fails");
	std::vector<std::string> a;
	a.push_back("/bin/sh");
	a.push_back("-c");
	a.push_back("exit 3");
	rp::ProcessResult r = runv(a);
	CHECK(r.started, "started -- it really did run");
	CHECK(!r.ok(), "not ok()");
	CHECK(r.status == 3, "status is the program's own");
	CHECK(r.execError.empty(), "a program that ran has no execError");
}

/** The distinction the whole change is for. A program is entitled to exit 127
    of its own accord, and when it does, that is not a launch failure. */
static void testGenuineExit127() {
	startTest("a program that genuinely exits 127");
	std::vector<std::string> a;
	a.push_back("/bin/sh");
	a.push_back("-c");
	a.push_back("exit 127");
	rp::ProcessResult r = runv(a);
	CHECK(r.started, "started");
	CHECK(r.status == 127, "status 127 is reported as the program's own");
	CHECK(r.execError.empty(), "and is NOT mistaken for a launch failure");
}


// --- the launch failures, which used to be invisible ------------------------

static void testMissingProgram() {
	startTest("a program that is not there");
	std::vector<std::string> a;
	a.push_back(g_tmp + "/no-such-program");
	rp::ProcessResult r = runv(a);
	CHECK(!r.started, "never started");
	CHECK(!r.ok(), "not ok()");
	CHECK(!r.execError.empty(), "execError is set");
	CHECK(contains(r.execError, "no such file"), "and says so plainly");
}

/** James's actual failure, reduced: /opt/homebrew/bin/yt-dlp was a 233-byte pip
    shim whose `#!` named a python@3.11 that a Homebrew upgrade had removed. The
    file is present and executable, so which() finds it and the panel prints its
    path -- and then execvp() reports ENOENT about the *interpreter*. Telling the
    user that path is "not found" sends them looking in exactly the wrong place. */
static void testBadShebang() {
	startTest("an executable script whose interpreter is gone");
	std::string script = writeFile("stale-shim",
		"#!" + g_tmp + "/no-such-interpreter\nprint('unreachable')\n", 0755);

	CHECK(rp::isExecutable(script), "the file is present and executable");

	rp::ProcessResult r = runv(std::vector<std::string>(1, script));
	CHECK(!r.started, "never started");
	CHECK(r.status != 127, "not reported as an exit code at all");
	CHECK(!r.execError.empty(), "execError is set");
	CHECK(contains(r.execError, "interpreter"), "names the interpreter as the suspect");
	CHECK(contains(r.execError, script.c_str()), "and quotes the path it tried");
	// The regression itself: the old code produced exactly this and nothing else.
	CHECK(!(r.started && r.status == 127), "NOT indistinguishable from exit 127");
}

static void testNotExecutable() {
	startTest("a file without the executable bit");
	std::string f = writeFile("not-runnable", "#!/bin/sh\necho hi\n", 0644);
	rp::ProcessResult r = runv(std::vector<std::string>(1, f));
	CHECK(!r.started, "never started");
	CHECK(!r.execError.empty(), "execError is set");
	CHECK(contains(r.execError, "permission denied"), "says permission denied");
}

/** Not a launch failure, despite appearances. execvp() is specified to fall back
    to /bin/sh for a file the kernel rejects with ENOEXEC, so a headerless file
    really does start -- as a shell script -- and the 127 that comes back is the
    shell's own "command not found" about the garbage inside it. The launcher is
    right to report that as a program that ran, and the stderr it captures says
    so in words, which is the outcome the panel wants. */
static void testNotAProgram() {
	startTest("an executable file that is not a program");
	std::string f = writeFile("not-a-program", "\x01\x02 this is not code\n", 0755);
	rp::ProcessResult r = runv(std::vector<std::string>(1, f));
	CHECK(r.started, "started -- execvp falls back to /bin/sh");
	CHECK(r.execError.empty(), "so it is not reported as a launch failure");
	CHECK(!r.err.empty(), "and the shell's complaint is captured for the panel");
}

/** A launch failure must not leave the forked child behind. */
static void testNoZombie() {
	startTest("a failed launch leaves no zombie");
	for (int i = 0; i < 50; i++) {
		std::vector<std::string> a;
		a.push_back(g_tmp + "/no-such-program");
		rp::ProcessResult r = runv(a);
		CHECK(!r.started, "never started");
	}
	// If the children were not reaped, waitpid(-1) finds one; it must not.
	int st = 0;
	pid_t leftover = ::waitpid(-1, &st, WNOHANG);
	CHECK(leftover <= 0, "nothing left unreaped");
}

/** which() must not hand back a path it cannot run... but it does, and that is
    the point: the executable bit is all the filesystem can tell it. The launcher
    is where the truth arrives, which is why the message has to come from there. */
static void testWhichFindsTheBrokenShim() {
	startTest("which() finds a stale shim; run() is what explains it");
	writeFile("faketool", "#!" + g_tmp + "/no-such-interpreter\n", 0755);
	std::vector<std::string> dirs(1, g_tmp);
	std::string found = rp::which("faketool", dirs);
	CHECK(found == g_tmp + "/faketool", "which() resolves it");
	rp::ProcessResult r = runv(std::vector<std::string>(1, found));
	CHECK(!r.started && !r.execError.empty(), "and run() reports why it cannot be used");
}


int main() {
	char tmpl[] = "/tmp/repossession-test-XXXXXX";
	const char* d = ::mkdtemp(tmpl);
	if (!d) {
		std::printf("cannot make a scratch directory\n");
		return 2;
	}
	g_tmp = d;
	std::printf("Repossession -- process launcher\nscratch: %s\n\n", g_tmp.c_str());

	testSuccess();
	testStreamsStaySeparate();
	testNonZeroExit();
	testGenuineExit127();
	testMissingProgram();
	testBadShebang();
	testNotExecutable();
	testNotAProgram();
	testNoZombie();
	testWhichFindsTheBrokenShim();

	std::printf("\n%d checks, %d failures\n", g_checks, g_fail);
	return g_fail ? 1 : 0;
}
