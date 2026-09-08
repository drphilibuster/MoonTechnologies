#pragma once
/** A very small portable "run this program and give me its output" wrapper.

Repossession links no third-party libraries: it shells out to yt-dlp and ffmpeg,
which is the only way to touch the world's video without vendoring half of it.
That means one thing this plugin needs and Rack does not provide -- spawning a
child process, reading both of its pipes, and being able to kill it when the
user deletes the module mid-download.

Everything here is `inline` and header-only, and nothing in it touches Rack's
engine or widget trees, so it is safe to call from a worker thread. It is also
the only place in this module with a platform #ifdef.

Two rules the implementations share:

  * stdout and stderr get separate pipes. yt-dlp prints the path of the file it
    produced on stdout and its progress on stderr, so merging them would corrupt
    the one line we actually parse.
  * the wait loop wakes about every 50 ms to look at the cancel flag, rather
    than blocking in waitpid()/WaitForSingleObject(INFINITE). A module being
    deleted must not have to wait out a multi-megabyte download.
*/
#include <rack.hpp>

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if defined ARCH_WIN
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <windows.h>
	#include <io.h>
#else
	#include <cerrno>
	#include <fcntl.h>
	#include <poll.h>
	#include <signal.h>
	#include <sys/wait.h>
	#include <unistd.h>
#endif

namespace rp {


struct ProcessResult {
	/** The child's exit status, or -1 if it could not be started. */
	int status = -1;
	/** True if the cancel flag went up and we killed the child. */
	bool cancelled = false;
	bool started = false;
	std::string out;
	std::string err;
	/** Set, and `started` left false, when the program could not be launched at
	    all -- as opposed to launching and then failing. A tool that never ran
	    has no stderr to quote, so without this the caller can only report an
	    exit code the tool never chose, and the panel blames the wrong thing. */
	std::string execError;

	bool ok() const { return started && !cancelled && status == 0; }
};


/** Keeps a capture bounded by discarding from the front: for a tool's diagnostic
    output the tail is the part that says what went wrong. */
inline void appendCapped(std::string& s, const char* buf, size_t n, size_t cap) {
	s.append(buf, n);
	if (s.size() > cap)
		s.erase(0, s.size() - cap);
}


inline bool isExecutable(const std::string& path) {
#if defined ARCH_WIN
	return rack::system::isFile(path);
#else
	return rack::system::isFile(path) && ::access(path.c_str(), X_OK) == 0;
#endif
}


/** Directories searched for a tool before PATH: every place the package
    managers and installers on each platform put a binary, plus a folder
    inside Rack's own user directory (MoonTechnologies/tools) where anyone can
    drop the two programs without touching their system. Rack is usually
    launched from a GUI shell whose PATH is the system default, so "it works
    in my terminal" is not evidence the plugin can see a tool -- which is why
    the search list is this long, and why the context-menu entry exists. */
inline std::vector<std::string> defaultToolDirs() {
	std::vector<std::string> dirs;
	// the drop folder, first: it is the one place that is the same for everyone
	dirs.push_back(rack::asset::user("MoonTechnologies/tools"));
	const char* home = getenv("HOME");
#if defined ARCH_WIN
	const char* profile = getenv("USERPROFILE");
	const char* local = getenv("LOCALAPPDATA");
	const char* pdata = getenv("PROGRAMDATA");
	if (local) {
		dirs.push_back(std::string(local) + "\\Microsoft\\WinGet\\Links");
		dirs.push_back(std::string(local) + "\\Programs\\yt-dlp");
		dirs.push_back(std::string(local) + "\\Programs\\ffmpeg\\bin");
	}
	if (profile) {
		dirs.push_back(std::string(profile) + "\\scoop\\shims");
		dirs.push_back(std::string(profile) + "\\AppData\\Local\\Programs\\Python\\Scripts");
	}
	dirs.push_back(std::string(pdata ? pdata : "C:\\ProgramData") + "\\chocolatey\\bin");
	dirs.push_back("C:\\ffmpeg\\bin");
	dirs.push_back("C:\\Program Files\\ffmpeg\\bin");
#elif defined ARCH_MAC
	dirs.push_back("/opt/homebrew/bin");
	dirs.push_back("/usr/local/bin");
	dirs.push_back("/opt/local/bin");
	if (home) {
		dirs.push_back(std::string(home) + "/.local/bin");
		dirs.push_back(std::string(home) + "/bin");
	}
#elif defined ARCH_LIN
	dirs.push_back("/usr/local/bin");
	dirs.push_back("/usr/bin");
	dirs.push_back("/snap/bin");
	dirs.push_back("/var/lib/flatpak/exports/bin");
	if (home) {
		dirs.push_back(std::string(home) + "/.local/bin");
		dirs.push_back(std::string(home) + "/bin");
	}
#endif
	return dirs;
}


/** The plugin-wide tools folder, shared by every Repossession in every patch:
    MoonTechnologies/settings.json in Rack's user directory. A folder set in one
    module's menu is remembered for all of them, which is what a person who has
    just found the right folder expects. */
inline std::string globalSettingsPath() {
	return rack::asset::user("MoonTechnologies/settings.json");
}

inline std::string loadGlobalToolDir() {
	std::string path = globalSettingsPath();
	if (!rack::system::isFile(path))
		return "";
	json_error_t err;
	json_t* root = json_load_file(path.c_str(), 0, &err);
	if (!root)
		return "";
	std::string out;
	json_t* j = json_object_get(root, "toolDir");
	if (j && json_is_string(j))
		out = json_string_value(j);
	json_decref(root);
	return out;
}

inline void saveGlobalToolDir(const std::string& dir) {
	std::string path = globalSettingsPath();
	rack::system::createDirectories(rack::system::getDirectory(path));
	json_t* root = NULL;
	if (rack::system::isFile(path)) {
		json_error_t err;
		root = json_load_file(path.c_str(), 0, &err);
	}
	if (!root)
		root = json_object();
	json_object_set_new(root, "toolDir", json_string(dir.c_str()));
	json_dump_file(root, path.c_str(), JSON_INDENT(2));
	json_decref(root);
}


/** One line naming every place a tool was looked for, for the panel's
    not-found message: a person reading it should know what to do next. */
inline std::string whereLooked(const std::vector<std::string>& dirs) {
	std::string s = "PATH";
	for (size_t i = 0; i < dirs.size(); i++)
		if (!dirs[i].empty())
			s += ", " + dirs[i];
	return s;
}


/** Resolves `name` to an absolute path, or "" if it is not on disk. `extra` is
    searched first, then PATH. A name containing a separator is taken as a path
    and only checked. */
inline std::string which(const std::string& name, const std::vector<std::string>& extra) {
	if (name.empty())
		return "";
	if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
		return isExecutable(name) ? name : "";

	std::vector<std::string> dirs = extra;
	const char* env = getenv("PATH");
	if (env) {
#if defined ARCH_WIN
		const char sep = ';';
#else
		const char sep = ':';
#endif
		std::string p = env;
		size_t start = 0;
		while (start <= p.size()) {
			size_t end = p.find(sep, start);
			if (end == std::string::npos)
				end = p.size();
			if (end > start)
				dirs.push_back(p.substr(start, end - start));
			start = end + 1;
		}
	}

	for (size_t i = 0; i < dirs.size(); i++) {
		if (dirs[i].empty())
			continue;
		std::string base = rack::system::join(dirs[i], name);
#if defined ARCH_WIN
		if (isExecutable(base + ".exe"))
			return base + ".exe";
		if (isExecutable(base + ".cmd"))
			return base + ".cmd";
#endif
		if (isExecutable(base))
			return base;
	}
	return "";
}


#if defined ARCH_WIN

/** CommandLineToArgvW's quoting rules, applied in reverse: backslashes are only
    special immediately before a quote, so only those runs get doubled. */
inline std::string quoteArg(const std::string& a) {
	if (!a.empty() && a.find_first_of(" \t\"") == std::string::npos)
		return a;
	std::string q = "\"";
	size_t i = 0;
	while (i < a.size()) {
		size_t slashes = 0;
		while (i < a.size() && a[i] == '\\') {
			slashes++;
			i++;
		}
		if (i == a.size()) {
			q.append(slashes * 2, '\\');
			break;
		}
		if (a[i] == '"') {
			q.append(slashes * 2 + 1, '\\');
			q.push_back('"');
		}
		else {
			q.append(slashes, '\\');
			q.push_back(a[i]);
		}
		i++;
	}
	q.push_back('"');
	return q;
}

inline bool readPipe(HANDLE h, std::string& into, size_t cap) {
	DWORD avail = 0;
	if (!PeekNamedPipe(h, NULL, 0, NULL, &avail, NULL))
		return false;
	while (avail > 0) {
		char buf[4096];
		DWORD want = (DWORD) ((avail < sizeof(buf)) ? avail : sizeof(buf));
		DWORD got = 0;
		if (!ReadFile(h, buf, want, &got, NULL) || got == 0)
			return false;
		appendCapped(into, buf, (size_t) got, cap);
		avail -= got;
	}
	return true;
}

inline ProcessResult run(const std::vector<std::string>& argv,
                         const std::atomic<bool>* cancel) {
	ProcessResult r;
	if (argv.empty())
		return r;

	SECURITY_ATTRIBUTES sa;
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE outR = NULL, outW = NULL, errR = NULL, errW = NULL;
	if (!CreatePipe(&outR, &outW, &sa, 0))
		return r;
	if (!CreatePipe(&errR, &errW, &sa, 0)) {
		CloseHandle(outR);
		CloseHandle(outW);
		return r;
	}
	SetHandleInformation(outR, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(errR, HANDLE_FLAG_INHERIT, 0);

	std::string cmd;
	for (size_t i = 0; i < argv.size(); i++) {
		if (i)
			cmd.push_back(' ');
		cmd += quoteArg(argv[i]);
	}
	std::vector<char> cmdBuf(cmd.begin(), cmd.end());
	cmdBuf.push_back('\0');

	STARTUPINFOA si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = outW;
	si.hStdError = errW;
	si.hStdInput = NULL;

	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));

	BOOL ok = CreateProcessA(NULL, &cmdBuf[0], NULL, NULL, TRUE,
		CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	DWORD launchErr = ok ? 0 : GetLastError();
	CloseHandle(outW);
	CloseHandle(errW);
	if (!ok) {
		CloseHandle(outR);
		CloseHandle(errR);
		// Windows resolves the whole launch up front, so unlike the POSIX side
		// there is no window in which a child exists but has not exec'd: a
		// failure here is always "never started".
		r.execError = argv[0] + " could not be started (Windows error "
			+ std::to_string((unsigned long) launchErr) + ").";
		return r;
	}
	r.started = true;

	while (true) {
		readPipe(outR, r.out, 1u << 20);
		readPipe(errR, r.err, 1u << 16);
		if (cancel && cancel->load(std::memory_order_relaxed) && !r.cancelled) {
			r.cancelled = true;
			TerminateProcess(pi.hProcess, 1);
		}
		if (WaitForSingleObject(pi.hProcess, 50) == WAIT_OBJECT_0)
			break;
	}
	// Whatever the child wrote between the last peek and its exit.
	readPipe(outR, r.out, 1u << 20);
	readPipe(errR, r.err, 1u << 16);

	DWORD code = 0;
	GetExitCodeProcess(pi.hProcess, &code);
	r.status = (int) code;
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	CloseHandle(outR);
	CloseHandle(errR);
	return r;
}

#else

/** Why a child could not be started, in words a person can act on.

    The case worth naming is ENOENT on a file that is demonstrably there and
    demonstrably executable. execvp() reports the *interpreter* named on a
    script's `#!` line, not the script itself, so this is almost always a
    wrapper whose Python or shell has been moved or uninstalled -- a pip shim
    outliving the Homebrew Python it was built against being the classic. The
    panel has just printed the path it found; telling the user that same path is
    "not found" would send them looking in the wrong place entirely. */
inline std::string execFailure(const std::string& path, int e) {
	if (e == ENOENT && isExecutable(path))
		return path + " could not be started: the file is there, but something it "
		       "needs is not -- most likely the interpreter on its first line, a "
		       "stale #! wrapper. Run it in a terminal to see what it names.";
	if (e == ENOEXEC)
		return path + " could not be started: it is not a runnable program on this "
		       "machine (wrong architecture, or not a binary at all).";
	if (e == EACCES)
		return path + " could not be started: permission denied.";
	if (e == ENOENT)
		return path + " could not be started: no such file.";
	return path + " could not be started: " + std::strerror(e) + ".";
}

inline ProcessResult run(const std::vector<std::string>& argv,
                         const std::atomic<bool>* cancel) {
	ProcessResult r;
	if (argv.empty())
		return r;

	// Built before the fork: between fork() and execvp() only async-signal-safe
	// calls are legal, and this plugin forks from a thread.
	std::vector<char*> args;
	args.reserve(argv.size() + 1);
	for (size_t i = 0; i < argv.size(); i++)
		args.push_back(const_cast<char*>(argv[i].c_str()));
	args.push_back(NULL);

	int outp[2], errp[2], execp[2];
	if (::pipe(outp) != 0)
		return r;
	if (::pipe(errp) != 0) {
		::close(outp[0]);
		::close(outp[1]);
		return r;
	}
	// The exec-status pipe, and the whole reason this function can tell the two
	// failures apart. Its write end is close-on-exec: a successful execvp()
	// closes it and the parent reads EOF, while a failed one leaves the child
	// alive just long enough to write its errno down it. The alternative -- a
	// bare _exit(127) -- is indistinguishable from the tool itself exiting 127,
	// which is exactly what a shell reports for a command it could not find.
	if (::pipe(execp) != 0) {
		::close(outp[0]);
		::close(outp[1]);
		::close(errp[0]);
		::close(errp[1]);
		return r;
	}
	::fcntl(execp[1], F_SETFD, FD_CLOEXEC);

	pid_t pid = ::fork();
	if (pid < 0) {
		::close(outp[0]);
		::close(outp[1]);
		::close(errp[0]);
		::close(errp[1]);
		::close(execp[0]);
		::close(execp[1]);
		return r;
	}
	if (pid == 0) {
		::dup2(outp[1], STDOUT_FILENO);
		::dup2(errp[1], STDERR_FILENO);
		::close(outp[0]);
		::close(outp[1]);
		::close(errp[0]);
		::close(errp[1]);
		::close(execp[0]);
		// A tool that decides to prompt must see EOF rather than wedge us.
		int devnull = ::open("/dev/null", O_RDONLY);
		if (devnull >= 0) {
			::dup2(devnull, STDIN_FILENO);
			::close(devnull);
		}
		::execvp(args[0], &args[0]);
		// Still async-signal-safe: one write() and one _exit().
		int e = errno;
		ssize_t wrote = ::write(execp[1], &e, sizeof(e));
		(void) wrote;
		::_exit(127);
	}

	::close(outp[1]);
	::close(errp[1]);
	::close(execp[1]);

	// Cannot stall: between fork() and execvp() the child only dups and opens
	// /dev/null, so this returns as soon as the exec resolves either way.
	int execErrno = 0;
	{
		char* p = (char*) &execErrno;
		size_t need = sizeof(execErrno);
		size_t got = 0;
		while (got < need) {
			ssize_t n = ::read(execp[0], p + got, need - got);
			if (n > 0)
				got += (size_t) n;
			else if (n == 0)
				break;
			else if (errno != EINTR)
				break;
		}
		if (got != need)
			execErrno = 0;
	}
	::close(execp[0]);

	if (execErrno != 0) {
		// Nothing ever ran, so `started` stays false and there is no exit status
		// worth reporting -- only the reason. The child is still reaped: it is
		// sitting in _exit(127) and would otherwise linger as a zombie.
		::close(outp[0]);
		::close(errp[0]);
		int st = 0;
		while (::waitpid(pid, &st, 0) < 0 && errno == EINTR) {
			// retry
		}
		r.execError = execFailure(argv[0], execErrno);
		return r;
	}

	r.started = true;

	struct pollfd fds[2];
	fds[0].fd = outp[0];
	fds[0].events = POLLIN;
	fds[0].revents = 0;
	fds[1].fd = errp[0];
	fds[1].events = POLLIN;
	fds[1].revents = 0;

	while (fds[0].fd >= 0 || fds[1].fd >= 0) {
		if (cancel && cancel->load(std::memory_order_relaxed) && !r.cancelled) {
			r.cancelled = true;
			::kill(pid, SIGKILL);
		}
		int n = ::poll(fds, 2, 50);
		if (n < 0) {
			if (errno == EINTR)
				continue;
			break;
		}
		if (n == 0)
			continue;
		for (int i = 0; i < 2; i++) {
			if (fds[i].fd < 0 || !fds[i].revents)
				continue;
			char buf[4096];
			ssize_t got = ::read(fds[i].fd, buf, sizeof(buf));
			if (got > 0) {
				appendCapped(i == 0 ? r.out : r.err, buf, (size_t) got,
					i == 0 ? (1u << 20) : (1u << 16));
			}
			else if (got == 0 || (got < 0 && errno != EINTR && errno != EAGAIN)) {
				::close(fds[i].fd);
				fds[i].fd = -1;
			}
		}
	}
	if (fds[0].fd >= 0)
		::close(fds[0].fd);
	if (fds[1].fd >= 0)
		::close(fds[1].fd);

	int status = 0;
	while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
		// retry
	}
	if (WIFEXITED(status))
		r.status = WEXITSTATUS(status);
	else
		r.status = -1;
	return r;
}

#endif


} // namespace rp

// ---------------------------------------------------------------------------
// A long-lived child we write to, which is a different animal from run().
//
// run() starts a process, waits for it, and hands back what it said. A video
// encoder is the opposite shape: it outlives the call, it is fed for as long as
// the stream runs, and the interesting failure is not its exit code but the
// pipe closing under us halfway through. So this is its own thing rather than a
// flag on run().
//
// The one rule that matters: writing to a pipe whose reader has gone raises
// SIGPIPE, and the default disposition kills the host -- which here is Rack,
// with the user's patch in it. ffmpeg dying must not take Rack with it, so the
// signal is ignored process-wide and the failed write is handled as a value.

namespace rp {

struct Streamer {
#if defined ARCH_WIN
	HANDLE proc = NULL;
	HANDLE in = NULL;
#else
	pid_t pid = -1;
	int in = -1;
#endif
	bool running = false;
	std::string error;

	~Streamer() { stop(); }

	/** Start `argv`, with its stdin on a pipe we keep. Its stdout and stderr
	    go to the null device: a stream that has been running for an hour has
	    written more diagnostics than anything would read, and a full pipe that
	    nobody drains would block the encoder rather than the reader. */
	bool start(const std::vector<std::string>& argv);

	/** Write every byte, or fail. A short write on a pipe is normal -- it means
	    the encoder is behind -- so this loops; a write that cannot proceed at
	    all means the child is gone. */
	bool write(const uint8_t* data, size_t n);

	void stop();
};

#if defined ARCH_WIN

inline bool Streamer::start(const std::vector<std::string>& argv) {
	stop();
	if (argv.empty())
		return false;

	SECURITY_ATTRIBUTES sa;
	ZeroMemory(&sa, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	HANDLE r = NULL, w = NULL;
	if (!CreatePipe(&r, &w, &sa, 0)) {
		error = "could not create a pipe";
		return false;
	}
	// Only the read end is the child's; our end must not be inherited or the
	// child holds it open and never sees end-of-file when we close it.
	SetHandleInformation(w, HANDLE_FLAG_INHERIT, 0);

	HANDLE nul = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa,
	                         OPEN_EXISTING, 0, NULL);

	std::string cmd;
	for (size_t i = 0; i < argv.size(); i++) {
		if (i) cmd += " ";
		cmd += quoteArg(argv[i]);
	}

	STARTUPINFOA si;
	ZeroMemory(&si, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = r;
	si.hStdOutput = nul;
	si.hStdError = nul;

	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof(pi));
	std::vector<char> mut(cmd.begin(), cmd.end());
	mut.push_back('\0');
	BOOL ok = CreateProcessA(NULL, &mut[0], NULL, NULL, TRUE,
	                         CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
	CloseHandle(r);
	if (nul != INVALID_HANDLE_VALUE)
		CloseHandle(nul);
	if (!ok) {
		CloseHandle(w);
		error = "could not start " + argv[0];
		return false;
	}
	CloseHandle(pi.hThread);
	proc = pi.hProcess;
	in = w;
	running = true;
	error.clear();
	return true;
}

inline bool Streamer::write(const uint8_t* data, size_t n) {
	if (!running || in == NULL)
		return false;
	size_t off = 0;
	while (off < n) {
		DWORD put = 0;
		if (!WriteFile(in, data + off, (DWORD)(n - off), &put, NULL) || put == 0) {
			error = "the encoder closed its input";
			running = false;
			return false;
		}
		off += put;
	}
	return true;
}

inline void Streamer::stop() {
	if (in) {
		CloseHandle(in);          // end-of-file: let it flush the last segment
		in = NULL;
	}
	if (proc) {
		WaitForSingleObject(proc, 2000);
		TerminateProcess(proc, 0);
		CloseHandle(proc);
		proc = NULL;
	}
	running = false;
}

#else

inline bool Streamer::start(const std::vector<std::string>& argv) {
	stop();
	if (argv.empty())
		return false;

	int p[2];
	if (::pipe(p) != 0) {
		error = "could not create a pipe";
		return false;
	}

	pid_t child = ::fork();
	if (child < 0) {
		::close(p[0]);
		::close(p[1]);
		error = "could not fork";
		return false;
	}
	if (child == 0) {
		::dup2(p[0], STDIN_FILENO);
		::close(p[0]);
		::close(p[1]);
		int devnull = ::open("/dev/null", O_WRONLY);
		if (devnull >= 0) {
			::dup2(devnull, STDOUT_FILENO);
			::dup2(devnull, STDERR_FILENO);
			::close(devnull);
		}
		std::vector<char*> cargv;
		for (size_t i = 0; i < argv.size(); i++)
			cargv.push_back(const_cast<char*>(argv[i].c_str()));
		cargv.push_back(NULL);
		::execv(cargv[0], &cargv[0]);
		::_exit(127);
	}

	::close(p[0]);
	// Rack is the host here. A dead encoder must not raise a signal whose
	// default action is to kill the process the user's patch is living in.
	::signal(SIGPIPE, SIG_IGN);
	pid = child;
	in = p[1];
	running = true;
	error.clear();
	return true;
}

inline bool Streamer::write(const uint8_t* data, size_t n) {
	if (!running || in < 0)
		return false;
	size_t off = 0;
	while (off < n) {
		ssize_t put = ::write(in, data + off, n - off);
		if (put < 0 && errno == EINTR)
			continue;
		if (put <= 0) {
			error = "the encoder closed its input";
			running = false;
			return false;
		}
		off += (size_t)put;
	}
	return true;
}

inline void Streamer::stop() {
	if (in >= 0) {
		::close(in);              // end-of-file: let it flush the last segment
		in = -1;
	}
	if (pid > 0) {
		// Give it a moment to write its final segment, then insist.
		for (int i = 0; i < 200; i++) {
			int st = 0;
			pid_t r = ::waitpid(pid, &st, WNOHANG);
			if (r == pid) { pid = -1; break; }
			::usleep(10000);
		}
		if (pid > 0) {
			::kill(pid, SIGKILL);
			int st = 0;
			::waitpid(pid, &st, 0);
			pid = -1;
		}
	}
	running = false;
}

#endif

} // namespace rp
