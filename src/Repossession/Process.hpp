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


/** Directories searched for a tool before PATH: Homebrew on Apple silicon,
    /usr/local on Intel Macs and most Linuxes, and whatever the user set in the
    context menu. Rack is usually launched from a GUI shell whose PATH is the
    system default, so "it works in my terminal" is not evidence the plugin can
    see a tool -- which is exactly why the menu entry exists. */
inline std::vector<std::string> defaultToolDirs() {
	std::vector<std::string> dirs;
#if defined ARCH_MAC
	dirs.push_back("/opt/homebrew/bin");
	dirs.push_back("/usr/local/bin");
	dirs.push_back("/opt/local/bin");
#elif defined ARCH_LIN
	dirs.push_back("/usr/local/bin");
	dirs.push_back("/usr/bin");
	dirs.push_back("/snap/bin");
#endif
	return dirs;
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
	CloseHandle(outW);
	CloseHandle(errW);
	if (!ok) {
		CloseHandle(outR);
		CloseHandle(errR);
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

	int outp[2], errp[2];
	if (::pipe(outp) != 0)
		return r;
	if (::pipe(errp) != 0) {
		::close(outp[0]);
		::close(outp[1]);
		return r;
	}

	pid_t pid = ::fork();
	if (pid < 0) {
		::close(outp[0]);
		::close(outp[1]);
		::close(errp[0]);
		::close(errp[1]);
		return r;
	}
	if (pid == 0) {
		::dup2(outp[1], STDOUT_FILENO);
		::dup2(errp[1], STDERR_FILENO);
		::close(outp[0]);
		::close(outp[1]);
		::close(errp[0]);
		::close(errp[1]);
		// A tool that decides to prompt must see EOF rather than wedge us.
		int devnull = ::open("/dev/null", O_RDONLY);
		if (devnull >= 0) {
			::dup2(devnull, STDIN_FILENO);
			::close(devnull);
		}
		::execvp(args[0], &args[0]);
		::_exit(127);
	}

	::close(outp[1]);
	::close(errp[1]);
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
