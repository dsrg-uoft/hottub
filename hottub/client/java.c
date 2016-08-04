#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <openssl/md5.h>
#include <time.h>
#include <stdint.h>
#include "java.h"

/* TODO error handling should be revised a bit... I think the goal should be if
 * anything we do fails just exec a normal JVM... I don't think trying to
 * recover and continue is important seeing as if any file/socket operations
 * fail with anything special the normal JVM is probably just as screwed as we
 * are
 */

/* TODO use Makefile to set log level based on openjdk build level
 * ie. if slowdebug set trace, if release set info
 */

static uint64_t t0;

/* ---------- ENTRY ---------- */
int main(int argc, char **argv, char **envp)
{
    t0 = _now();
    char id[ID_LEN + 1];
    int hottub = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strncmp(argv[i], "-hottub", 7) == 0) {
            hottub = 1;
            break;
        }
    }
    if (!hottub) {
        return exec_jvm(NULL, argc, argv);
    }

    args_info args = {0};
    args.argc = argc;
    args.argv = argv;
    if (compute_id(id, &args) == -1) {
        return exec_jvm(NULL, argc, argv);
    }

    return run_hottub(id, &args, envp);
}

/* a. tell the first non-busy jvm in pool to run ("fork")
 * b. if pool is empty: spawn a new hottub in pool
 * c. if everything in pool is busy and pool is not full: spawn a new forkvjm
 *    in pool
 *
 * if there is ever a fatal error return -1 (default to normal exec)
 * if you ever can't connect or lose connection to a jvm just go next
 */
int run_hottub(char *id, args_info *args, char** envp)
{
    /* check for existing hottub in id's pool */
    char datapath[MAX_PATH_LEN];
    int datapath_len;

    datapath_len = create_datapath(datapath);

    char jvmpath[datapath_len + ID_LEN + 1];
    sprintf(jvmpath, "%s%s", datapath, id);

    int poolno;
    for (poolno = 0; poolno < JVM_POOL_MAX; poolno++) {
        int error;
        int try_connect = 0;

        /* add/update poolno to id and path */
        id[0] = '/';
        id[ID_LEN - 1] = '0' + poolno;
        jvmpath[datapath_len + ID_LEN - 1] = '0' + poolno;
        fprintf(stderr, "[hottub][info][client run_hottub] trying id %s\n", id);

        /* try to create a directory for server 'poolno' in the pool
         * a. if it doesn't exist no server exists -> create a server
         * b. if EEXIST then try and connect to the existing server
         */
        if (mkdir(jvmpath, 0775) == 0) {
            pid_t pid = fork();
            if (pid == 0) {
                setsid();
                setup_server_logs(jvmpath);
                // TODO: we can continue with error here, but parent and child
                //       have same std* fds
                return exec_jvm(id, args->argc, args->argv);
            } else {
                create_pid_file(jvmpath, SERVER, pid);
                try_connect = 1;
            }
        } else if (errno == EEXIST) {
            try_connect = 1;
        } else {
            fprintf(stderr, "[hottub][error][client run_hottub] mkdir | "
                    "jvmpath = %s | errno = %s\n", jvmpath, strerror(errno));
            return -1;
        }

        if (try_connect && create_pid_file(jvmpath, CLIENT, getpid()) == 0) {
            int jvmfd;
            int i;
            for (i = 0; i < RETRY_COUNT; i++) {
                jvmfd = connect_sock(id);
                if (jvmfd > 0) {
                    break;
                } else {
                    if (jvmfd == -2) {
                        fprintf(stderr, "[hottub][error][client run_hottub] socket"
                                " | id = %s | errno = %s\n", id, strerror(errno));
                    } else if (jvmfd == -1 && errno != ECONNREFUSED) {
                        fprintf(stderr, "[hottub][info][client run_hottub] connect"
                                " | id = %s | errno = %s\n", id, strerror(errno));
                    }
                    struct timespec ts;
                    ts.tv_sec = 0;
                    ts.tv_nsec = 200 * 1e6;
                    struct timespec t2;
                    nanosleep(&ts, &t2);
                }
            }
            // if we never connected go next
            if (jvmfd <= 0) {
                continue;
            }

            error = send_fds(jvmfd);
            if (error == -2) {
                close(jvmfd);
                return -1;
            } else if (error == -1) {
                close(jvmfd);
                continue;
            }
            // global data for argc and argv :D
            error = send_args(jvmfd, args);
            if (error) {
                close(jvmfd);
                continue;
            }
            error = send_working_dir(jvmfd);
            if (error) {
                close(jvmfd);
                continue;
            }
            error = send_env_var(jvmfd, envp);
            if (error) {
                close(jvmfd);
                continue;
            }

            uint64_t t1 = _now();
            fprintf(stderr, "[hottub][info][client run_hottub] running server "
                    "with id %s, took %.6f\n", id, (t1 - t0) / 1e9);
            int ret_val = 255;
            if (read_sock(jvmfd, &ret_val, sizeof(int)) == -1) {
                perror("[hottub][error][client run_hottub] read_sock");
            }
            close(jvmfd);
            remove_pid_file(jvmpath, CLIENT);
            return ret_val;
        }
    }
    // if we reach here no jvm was available -> fallback exec a normal jvm
    return exec_jvm(NULL, args->argc, args->argv);
}

int compute_id(char *id, args_info *args)
{
    /* openssl is 1 success, 0 error */
    unsigned char digest[MD5_DIGEST_LENGTH];
    char *classpath = NULL;
    int i;
    MD5_CTX md5ctx;

    if (!MD5_Init(&md5ctx)) {
        fprintf(stderr, "[hottub][error][client compute_id] MD5_Init\n");
        return -1;
    }
    /* add in all the args */
    for (i = 1; i < args->argc; i++) {
        if (strlen(args->argv[i]) > 2 && args->argv[i][0] == '-' && args->argv[i][1] == 'D') {
            args->javaD_argc++;
            continue;
        }
        if (!MD5_Update(&md5ctx, args->argv[i], strlen(args->argv[i]))) {
            fprintf(stderr, "[hottub][error][client compute_id] MD5_Update\n");
            return -1;
        }
        /* locate classpath while adding args */
        if (strcmp(args->argv[i], "-classpath") == 0 ||
                strcmp(args->argv[i], "-cp") == 0) {
            classpath = args->argv[i + 1];
            i++;
            if (!MD5_Update(&md5ctx, args->argv[i], strlen(args->argv[i]))) {
                fprintf(stderr, "[hottub][error][client compute_id] MD5_Update\n");
                return -1;
            }
            continue;
        }
        if (args->argv[i][0] != '-') {
            break;
        }
    }
    // c args: (0) java.exe, (1) -foo, (2) bar, (3) baz; i == 2, java_argc = 4 - 2 - 1
    args->java_argc = args->argc - i - 1;
    args->java_argv = args->argv + i + 1;

    if (classpath == NULL) {
        classpath = getenv("CLASSPATH");
        fprintf(stderr, "[hottub][info][client compute_id] using environment "
                "classpath %s\n", classpath == NULL ? "(null)" : classpath);
        if (classpath == NULL)
            classpath = ".";
    }

    if (md5add_classpath(&md5ctx, classpath) == -1) {
        return -1;
    }

    if (!MD5_Final(digest, &md5ctx)) {
      fprintf(stderr, "[hottub][error][client compute_id] MD5_Final\n");
      return -1;
    }

    /* convert digest to id by converting digest to hex
     * leave first character blank to use as '\0' for abstract sockets
     */
    id[0] = '/';
    for (i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(id + 1 + i * 2, "%02x", digest[i]);
    }
    id[ID_LEN - 1] = '_';
    id[ID_LEN] = '\0';
    return 0;
}

/*
 * classpath cases: (only handle 1 because it is the only one that seems to matter)
 *
 * 1. For a JAR or zip file that contains class files, the class path ends
 * with the name of the zip or JAR file.
 *
 * 2. For class files in an unnamed package, the class path ends with the
 * directory that contains the class files.
 *
 * 3. For class files in a named package, the class path ends with the
 * directory that contains the root package, which is the first package in
 * the full package name.
 */
int md5add_classpath(MD5_CTX *md5ctx, const char *classpath)
{
    int cplen = strlen(classpath);
    const char *start;
    const char *cur;

    for (start = classpath, cur = classpath; cur <= classpath + cplen; cur++) {
        if ((*cur == ':' || *cur == '\0') && cur != start) {

            int len = cur - start;
            char entry[len + 1];
            strncpy(entry, start, len);
            entry[len] = '\0';

            if (is_wildcard(entry))
                md5add_wildcard(md5ctx, entry);
            else if (strsuffix(entry, ".jar") == 0)
                md5add_file(md5ctx, entry);

            start = cur + 1;
        }
    }
    return 0;
}

int is_wildcard(const char *filename)
{
    int len = strlen(filename);
    return (len > 0) &&
        (filename[len - 1] == '*') &&
        (len == 1 || filename[len - 2] == '/') &&
        (access(filename, F_OK) == 0);
}

int strsuffix(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return -1;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strcmp(str + lenstr - lensuffix, suffix);
}

char *next_dirent(DIR *dir)
{
    struct dirent *dirent = readdir(dir);
    return dirent ? dirent->d_name : NULL;
}

int md5add_wildcard(MD5_CTX *md5ctx, const char *wildcard)
{
    DIR *dir;
    int wildlen = strlen(wildcard);
    if (wildlen < 2) {
        dir = opendir(".");
    } else {
        char dirname[wildlen];
        strncpy(dirname, wildcard, wildlen);
        dirname[wildlen - 1] = '\0';
        dir = opendir(dirname);
    }
    if (dir == NULL)
        return -1;

    const char *fname;
    while ((fname = next_dirent(dir)) != NULL)
        if (strsuffix(fname, ".jar") == 0)
            md5add_file(md5ctx, fname);
    return 0;
}

int md5add_file(MD5_CTX *md5ctx, const char *filename)
{
    FILE *inFile = fopen(filename, "rb");
    if (inFile == NULL) {
        return -1;
    }

    int bytes;
    unsigned char data[1024];
    while ((bytes = fread(data, 1, 1024, inFile)) != 0) {
        if (!MD5_Update(md5ctx, data, 1024)) {
            fprintf(stderr, "[hottub][error][client md5add_file] MD5_Update\n");
            return -1;
        }
    }
    return 0;
}

int create_datapath(char *datapath) {
    int datapath_len = readlink("/proc/self/exe", datapath, MAX_PATH_LEN);
    if (datapath_len == -1) {
        perror("[hottub][error][client create_datapath] readlink");
        return -1;;
    } else if (datapath_len > MAX_PATH_LEN) {
        fprintf(stderr, "[hottub][error][client create_datapath] readlink "
                "MAX_PATH_LEN too small\n");
        return -1;
    }
    // -8 removes "bin/java"
    datapath_len -= 8;
    datapath[datapath_len] = '\0';
    strcat(datapath,"hottub/data");
    datapath_len += 11;
    return datapath_len;
}

int create_execpath(char *execpath)
{
    int exec_path_len = readlink("/proc/self/exe", execpath, MAX_PATH_LEN);
    if (exec_path_len == -1) {
        perror("[hottub][error][client create_execpath] readlink");
        return exec_path_len;
    }
    execpath[exec_path_len] = '\0';
    strcat(execpath, "_real");
    exec_path_len += 5;
    return exec_path_len;
}

int exec_jvm(const char *id, int main_argc, char **main_argv)
{
    char *argv[main_argc + 2];
    char execpath[MAX_PATH_LEN];

    create_execpath(execpath);
    argv[0] = "java_real";

    if (id == NULL) {
        memcpy(argv + 1, main_argv + 1, sizeof(argv));
        argv[main_argc] = NULL;
    } else {
        /* "-hottubid=" + '\0' = 11 */
        char hottub_arg[ID_LEN + 11];
        sprintf(hottub_arg, "-hottubid=%s", id);
        argv[1] = hottub_arg;
        memcpy(argv + 2, main_argv + 1, sizeof(argv));
        argv[main_argc + 1] = NULL;
        fprintf(stderr, "[hottub][info][client exec_jvm] exec with id %s\n", id + 1);
    }
    execv(execpath, argv);
    perror("[hottub][error][client exec_jvm] exec");
    return -1;
}

int send_fds(int jvmfd)
{
    // only send stdin, stdout, stderr
    // explicitly using fds is not allowed
    int fd;
    for (fd = 0; fd < 3; fd++) {
        if (fcntl(fd, F_GETFD) != -1) {
            if (write_fd(jvmfd, &fd, sizeof(int), fd) != sizeof(int)) {
                perror("[hottub][error][client send_fds] write_fd");
                return -1;
            }
        }
    }
    // write to the socket with no fd to signal finish
    if (write_sock(jvmfd, &fd, sizeof(int)) != sizeof(int)) {
        perror("[hottub][error][client send_fds] write_sock");
        return -1;
    }
    return 0;
}

int send_args_i(int jvmfd, int i, void *ptr, size_t len, char *val, char *tag)
{
    int wrote = write_sock(jvmfd, ptr, len);
    if (wrote != len) {
        fprintf(stderr, "[hottub][error][client send_args] write_sock | %s %d "
                "| val = %s | wrote %d, expected %zu | errno = %s\n", tag, i,
                val, wrote, len, strerror(errno));
        return -1;
    }
    return 0;
}

int send_args(int jvmfd, args_info *args)
{
    int error = send_args_i(jvmfd, -1, &args->java_argc, sizeof(int), NULL, "java_argc");
    if (error) {
        return error;
    }
    int i;
    for (i = 0; i < args->java_argc; i++) {
        int len = strlen(args->java_argv[i]) + 1;
        error = send_args_i(jvmfd, i, &len, sizeof(int), args->java_argv[i], "java_arg len");
        if (error) {
            return error;
        }
        error = send_args_i(jvmfd, i, args->java_argv[i], len, args->java_argv[i], "java_arg buf");
        if (error) {
            return error;
        }
    }

    // java -D args
    error = send_args_i(jvmfd, -1, &args->javaD_argc, sizeof(int), NULL, "argc");
    if (error) {
        return error;
    }
    for (i = 0; i < args->argc; i++) {
        int len = strlen(args->argv[i]) + 1;
        if (len <= 3 || args->argv[i][0] != '-' || args->argv[i][1] != 'D') {
            continue;
        }
        error = send_args_i(jvmfd, i, &len, sizeof(int), args->argv[i], "arg len");
        if (error) {
            return error;
        }
        error = send_args_i(jvmfd, i, args->argv[i], len, args->argv[i], "arg buf");
        if (error) {
            return error;
        }
    }
    return 0;
}

int send_working_dir(int jvmfd)
{
    char buf[MAX_PATH_LEN];
    if (getcwd(buf, MAX_PATH_LEN) == NULL) {
        perror("[hottub][error][client send_working_dir] getcwd");
        return -1;
    }
    size_t len = strlen(buf);
    size_t wrote = (size_t) write_sock(jvmfd, &len, sizeof(int));
    if (wrote != sizeof(int)) {
        perror("[hottub][error][client send_working_dir] write_sock len");
        return -1;
    }
    wrote = (size_t) write_sock(jvmfd, buf, len);
    if (wrote != len) {
        perror("[hottub][error][client send_working_dir] write_sock buf");
        return -1;
    }
    return 0;
}

int send_env_var(int jvmfd, char **envp)
{
    size_t len = -1;
    size_t wrote = -1;
    int i;
    for (i = 0; envp[i] != NULL; i++) {
        char *env_var = envp[i];
        // TODO: add trace flag?
        //fprintf(stderr, "[hottub][info][client send_env_var] len = %zu, "
        //        "env_var = %s\n", strlen(env_var), env_var);
        len = strlen(env_var);
        wrote = (size_t) write_sock(jvmfd, &len, sizeof(int));
        if (wrote != sizeof(int)) {
            perror("[hottub][error][client send_env_var] write_sock len");
            return -1;
        }
        wrote = (size_t) write_sock(jvmfd, env_var, len);
        if (wrote != len) {
            perror("[hottub][error][client send_env_var] write_sock env_var");
            return -1;
        }
    }
    // TODO: add trace flag?
    //fprintf(stderr, "[hottub][info][client send_env_varj sending done\n");
    len = 0;
    wrote = (size_t) write_sock(jvmfd, &len, sizeof(int));
    if (wrote != sizeof(int)) {
        perror("[hottub][error][client send_env_var] write_sock done (0)");
        return -1;
    }
    return 0;
}

/* auto converts to abstract socket */
int connect_sock(const char *path)
{
    struct sockaddr_un addr;
    int fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        return -2;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_path[0] = '\0';

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        close(fd);
        return -1;
    }

    return fd;
}

ssize_t write_fd(int fd, void *ptr, size_t nbytes, int sendfd)
{
    struct msghdr   msg;
    struct iovec    iov[1];

    union {
        struct cmsghdr    cm;
        char              control[CMSG_SPACE(sizeof(int))];
    } control_un;
    struct cmsghdr  *cmptr;

    memset(&msg, 0, sizeof(msg));
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    *((int *) CMSG_DATA(cmptr)) = sendfd;

    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;

    return sendmsg(fd, &msg, 0);
}

ssize_t read_sock(int fd, void *ptr, size_t nbytes)
{
    struct msghdr   msg;
    struct iovec    iov[1];

    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    return recvmsg(fd, &msg, 0);
}

ssize_t write_sock(int fd, void *ptr, size_t nbytes)
{
    struct msghdr   msg;
    struct iovec    iov[1];

    memset(&msg, 0, sizeof(msg));
    iov[0].iov_base = ptr;
    iov[0].iov_len = nbytes;
    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    return sendmsg(fd, &msg, 0);
}

int create_pid_file(const char *jvmpath, const char *filename, pid_t pid)
{
    int path_len = strlen(jvmpath) + strlen(filename) + 1;
    char path[path_len];
    snprintf(path, path_len, "%s%s", jvmpath, filename);

    int pid_fd = creat(path, 0775);
    if (pid_fd != -1) {
        FILE *f = fdopen(pid_fd, "w");
        if (f == NULL) {
            perror("[hottub][error][client create_pid_file] fdopen");
            return -1;
        }
        int ret = fprintf(f, "%d\n", pid);
        if (ret < 0) {
            fprintf(stderr, "[hottub][error][client create_pid_file] fprintf "
                    "returned %d, ferror %d\n", ret, ferror(f));
            return -1;
        }
        ret = fclose(f);
        if (ret) {
            fprintf(stderr, "[hottub][error][client create_pid_file] fclose "
                    "returned %d\n", ret);
            return -1;
        }
        return 0;
    } else if (errno == EEXIST) {
        // don't print an error as this is a valid/expected case
        return 1;
    } else {
        fprintf(stderr, "[hottub][error][client create_pid_file] creat "
                "%s errno = %s\n", filename, strerror(errno));
        return -1;
    }
}

int remove_pid_file(const char *jvmpath, const char *filename)
{
    int path_len = strlen(jvmpath) + strlen(filename) + 1;
    char path[path_len];
    snprintf(path, path_len, "%s%s", jvmpath, filename);
    if (remove(path) == -1) {
        fprintf(stderr, "[hottub][error][client remove_pid_file] remove "
                "%s errno = %s\n", filename, strerror(errno));
        return -1;
    }
    return 0;
}

int setup_server_logs(const char *jvmpath)
{
    int path_len = strlen(jvmpath) + strlen("/stdout") + 1;
    char stdout_path[path_len];
    char stderr_path[path_len];
    int ret = 0;

    strcpy(stdout_path, jvmpath);
    strcat(stdout_path, "/stdout");
    strcpy(stderr_path, jvmpath);
    strcat(stderr_path, "/stderr");

    /* server should have no stdin on its own (no client connected)
     * just closing stdin could lead to its fd used by something else and later
     * overwritten by client stdin so we set to /dev/null
     */
    if (freopen("/dev/null", "r", stdin) == NULL) {
        fprintf(stderr, "[hottub][error][client setup_server_logs] freopen "
                "stdin_path = %s errno = %s\n", "/dev/null", strerror(errno));
        ret = -1;
    }

    if (freopen(stdout_path, "w", stdout) == NULL) {
        fprintf(stderr, "[hottub][error][client setup_server_logs] freopen "
                "stdout_path = %s errno = %s\n", stdout_path, strerror(errno));
        ret = -2;
    }
    if (freopen(stderr_path, "w", stderr) == NULL) {
        fprintf(stderr, "[hottub][error][client setup_server_logs] open "
                "stderr_path = %s errno = %s\n", stderr_path, strerror(errno));
        ret = -3;
    }
    return ret;
}

uint64_t _now()
{
  struct timespec ts;
  int status = clock_gettime(CLOCK_MONOTONIC, &ts);
  (void) status;
  return (uint64_t) ts.tv_sec * (1000 * 1000 * 1000) + (uint64_t) ts.tv_nsec;
}
