// Copyright (c) 2009 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// http://code.google.com/p/chromium/wiki/LinuxSUIDSandbox

#define _GNU_SOURCE
#include <asm/unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdbool.h>

/* ----- #include "suid_unsafe_environment_variables.h" */
static const char* kSUIDUnsafeEnvironmentVariables[] = {
  "LD_AOUT_LIBRARY_PATH",
  "LD_AOUT_PRELOAD",
  "GCONV_PATH",
  "GETCONF_DIR",
  "HOSTALIASES",
  "LD_AUDIT",
  "LD_DEBUG",
  "LD_DEBUG_OUTPUT",
  "LD_DYNAMIC_WEAK",
  "LD_LIBRARY_PATH",
  "LD_ORIGIN_PATH",
  "LD_PRELOAD",
  "LD_PROFILE",
  "LD_SHOW_AUXV",
  "LD_USE_LOAD_BIAS",
  "LOCALDOMAIN",
  "LOCPATH",
  "MALLOC_TRACE",
  "NIS_PATH",
  "NLSPATH",
  "RESOLV_HOST_CONF",
  "RES_OPTIONS",
  "TMPDIR",
  "TZDIR",
  NULL,
};

// Return a malloc allocated string containing the 'saved' environment variable
// name for a given environment variable.
static inline char* SandboxSavedEnvironmentVariable(const char* envvar) {
  const size_t envvar_len = strlen(envvar);
  const size_t saved_envvarlen = envvar_len + 1 /* NUL terminator */ +
                                 8 /* strlen("SANDBOX_") */;
  char* const saved_envvar = (char*) malloc(saved_envvarlen);
  if (!saved_envvar)
    return NULL;

  memcpy(saved_envvar, "SANDBOX_", 8);
  memcpy(saved_envvar + 8, envvar, envvar_len);
  saved_envvar[8 + envvar_len] = 0;

  return saved_envvar;
}
/* ----- */

#if !defined(CLONE_NEWPID)
#define CLONE_NEWPID 0x20000000
#endif

#if defined(LINUX_SANDBOX_CHROME_PATH)
static const char kChromeBinary[] = LINUX_SANDBOX_CHROME_PATH;
#endif

static const char kSandboxDescriptorEnvironmentVarName[] = "SBX_D";

// These are the magic byte values which the sandboxed process uses to request
// that it be chrooted.
static const char kMsgChrootMe = 'C';
static const char kMsgChrootSuccessful = 'O';

static void FatalError(const char *msg, ...)
    __attribute__((noreturn, format(printf,1,2)));

static void FatalError(const char *msg, ...) {
  va_list ap;
  va_start(ap, msg);

  vfprintf(stderr, msg, ap);
  fprintf(stderr, ": %s\n", strerror(errno));
  fflush(stderr);
  exit(1);
}

static int CloneChrootHelperProcess() {
  int sv[2];
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) == -1) {
    perror("socketpair");
    return -1;
  }

  // We create a temp directory for our chroot. Nobody should ever write into
  // it, so it's root:root mode 000.
  char kTempDirectoryTemplate[] = "/tmp/chrome-sandbox-chroot-XXXXXX";
  const char* temp_dir = mkdtemp(kTempDirectoryTemplate);
  if (!temp_dir) {
    perror("Failed to create temp directory for chroot");
    return -1;
  }

  const int chroot_dir_fd = open(temp_dir, O_DIRECTORY | O_RDONLY);
  if (chroot_dir_fd < 0) {
    rmdir(temp_dir);
    perror("Failed to open chroot temp directory");
    return -1;
  }

  if (rmdir(temp_dir)) {
    perror("rmdir");
    return -1;
  }

  char proc_self_fd_str[128];
  int printed = snprintf(proc_self_fd_str, sizeof(proc_self_fd_str),
                         "/proc/self/fd/%d", chroot_dir_fd);
  if (printed < 0 || printed >= sizeof(proc_self_fd_str)) {
    fprintf(stderr, "Error in snprintf");
    return -1;
  }

  if (fchown(chroot_dir_fd, 0 /* root */, 0 /* root */)) {
    perror("fchown");
    return -1;
  }

  if (fchmod(chroot_dir_fd, 0000 /* no-access */)) {
    perror("fchmod");
    return -1;
  }


  const pid_t pid = syscall(
      __NR_clone, CLONE_FS | SIGCHLD, 0, 0, 0);

  if (pid == -1) {
    perror("clone");
    close(sv[0]);
    close(sv[1]);
    return -1;
  }

  if (pid == 0) {
    // We share our files structure with an untrusted process. As a security in
    // depth measure, we make sure that we can't open anything by mistake.
    // TODO: drop CAP_SYS_RESOURCE / use SECURE_NOROOT

    const struct rlimit nofile = {0, 0};
    if (setrlimit(RLIMIT_NOFILE, &nofile))
      FatalError("Setting RLIMIT_NOFILE");

    if (close(sv[1]))
      FatalError("close");

    // wait for message
    char msg;
    ssize_t bytes;
    do {
      bytes = read(sv[0], &msg, 1);
    } while (bytes == -1 && errno == EINTR);

    if (bytes == 0)
      _exit(0);
    if (bytes != 1)
      FatalError("read");

    // do chrooting
    if (msg != kMsgChrootMe)
      FatalError("Unknown message from sandboxed process");

    if (fchdir(chroot_dir_fd))
      FatalError("Cannot chdir into chroot temp directory");

    struct stat st;
    if (fstat(chroot_dir_fd, &st))
      FatalError("stat");

    if (st.st_uid || st.st_gid || st.st_mode & 0777)
      FatalError("Bad permissions on chroot temp directory");

    if (chroot(proc_self_fd_str))
      FatalError("Cannot chroot into temp directory");

    if (chdir("/"))
      FatalError("Cannot chdir to / after chroot");

    const char reply = kMsgChrootSuccessful;
    do {
      bytes = write(sv[0], &reply, 1);
    } while (bytes == -1 && errno == EINTR);

    if (bytes != 1)
      FatalError("Writing reply");

    _exit(0);
  }

  if (close(chroot_dir_fd)) {
    close(sv[0]);
    close(sv[1]);
    perror("close(chroot_dir_fd)");
    return false;
  }

  if (close(sv[0])) {
    close(sv[1]);
    perror("close");
    return false;
  }

  return sv[1];
}

static bool SpawnChrootHelper() {
  const int chroot_signal_fd = CloneChrootHelperProcess();

  if (chroot_signal_fd == -1)
    return false;

  // In the parent process, we install an environment variable containing the
  // number of the file descriptor.
  char desc_str[64];
  int printed = snprintf(desc_str, sizeof(desc_str), "%d", chroot_signal_fd);
  if (printed < 0 || printed >= sizeof(desc_str)) {
    fprintf(stderr, "Failed to snprintf\n");
    return false;
  }

  if (setenv(kSandboxDescriptorEnvironmentVarName, desc_str, 1)) {
    perror("setenv");
    close(chroot_signal_fd);
    return false;
  }

  return true;
}

static bool MoveToNewPIDNamespace() {
  const pid_t pid = syscall(
      __NR_clone, CLONE_NEWPID | SIGCHLD, 0, 0, 0);

  if (pid == -1) {
    if (errno == EINVAL) {
      // System doesn't support NEWPID. We carry on anyway.
      return true;
    }

    perror("Failed to move to new PID namespace");
    return false;
  }

  if (pid)
    _exit(0);

  return true;
}

static bool DropRoot() {
  if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0)) {
    perror("prctl(PR_SET_DUMPABLE)");
    return false;
  }

  if (prctl(PR_GET_DUMPABLE, 0, 0, 0, 0)) {
    perror("Still dumpable after prctl(PR_SET_DUMPABLE)");
    return false;
  }

  gid_t rgid, egid, sgid;
  if (getresgid(&rgid, &egid, &sgid)) {
    perror("getresgid");
    return false;
  }

  if (setresgid(rgid, rgid, rgid)) {
    perror("setresgid");
    return false;
  }

  uid_t ruid, euid, suid;
  if (getresuid(&ruid, &euid, &suid)) {
    perror("getresuid");
    return false;
  }

  if (setresuid(ruid, ruid, ruid)) {
    perror("setresuid");
    return false;
  }

  return true;
}

static bool SetupChildEnvironment() {

  unsigned i;

  // ld.so may have cleared several environment variables because we are SUID.
  // However, the child process might need them so zygote_host_linux.cc saves a
  // copy in SANDBOX_$x. This is safe because we have dropped root by this
  // point, so we can only exec a binary with the permissions of the user who
  // ran us in the first place.

  for (i = 0; kSUIDUnsafeEnvironmentVariables[i]; ++i) {
    const char* const envvar = kSUIDUnsafeEnvironmentVariables[i];
    char* const saved_envvar = SandboxSavedEnvironmentVariable(envvar);
    if (!saved_envvar)
      return false;

    const char* const value = getenv(saved_envvar);
    if (value) {
      setenv(envvar, value, 1 /* overwrite */);
      unsetenv(saved_envvar);
    }

    free(saved_envvar);
  }

  return true;
}

int main(int argc, char **argv) {
  if (argc <= 1) {
    fprintf(stderr, "Usage: %s <renderer process> <args...>\n", argv[0]);
    return 1;
  }

  if (!MoveToNewPIDNamespace())
    return 1;
  if (!SpawnChrootHelper())
    return 1;
  if (!DropRoot())
    return 1;
  if (!SetupChildEnvironment())
    return 1;

  execv(argv[1], &argv[1]);
  FatalError("execv failed");

  return 1;
}
