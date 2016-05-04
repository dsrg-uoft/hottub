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

/* 1st char for '\0' (abstract sockets), last char for poolno */
#define ID_LEN          1 + MD5_DIGEST_LENGTH * 2 + 1
#define MSG_LEN         5 /* fd limit is 4k so 4 char + null */
#define MAX_PATH_SIZE   1024
/* note an increase in pool digits requires string modification */
#define JVM_POOL_MAX 5

/* -- unix abstract socket java client --
 * always do a normal jvm exec on critical error
 *
 * 1. check if it is a forkjvm if not just exec
 * 2. compute id using args and classpath contents
 * 3. try to mkdir for poolno
 * 4a. if mkdir success start a new forkjvm
 * 4b. if mkdir fails try to contact jvm with that poolno
 *      5a. if you successfully contact a jvm send fds to it
 *          6. wait until jvm finishes
 *      5b. if you never successfully contact a jvm do normal exec
 *           - this means entire pool is busy and pool is full
 */

/* id building */
int compute_id(char *id, int argc, char **argv);
int md5add_classpath(MD5_CTX *md5ctx, const char *classpath);
int is_wildcard(const char *filename);
int strsuffix(const char *str, const char *suffix);
char *next_dirent(DIR *dir);
int md5add_wildcard(MD5_CTX *md5ctx, const char *wildcard);
int md5add_file(MD5_CTX *md5ctx, const char *filename);

/* path building */
int create_datapath(char *datapath);
int create_execpath(char *execpath);

/* socket stuff */
int connect_sock(const char *path);
ssize_t write_fd(int fd, void *ptr, size_t nbytes, int sendfd);
ssize_t read_sock(int fd, void *ptr, size_t nbytes);
ssize_t write_sock(int fd, void *ptr, size_t nbytes);
int send_fds(int jvmfd);

/* set up args and call exec */
int exec_jvm(const char *jvmid, int main_argc, char **main_argv);

/* try to use a jvm from pool or add new jvm to pool */
int run_forkjvm(char *id);

/* server initialization utilities */
int write_server_pid(const char* jvmpath, int pid);
int setup_server_logs(const char* jvmpath);

/* fork/re-run jvm */

/* ---------- ENTRY ---------- */
int main(int argc, char **argv)
{
    int status;
    char id[ID_LEN + 1];
    int forkjvm = 0;
    int i;

    /* check if this is a forkjvm */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-forkjvm") == 0) {
            forkjvm = 1;
            break;
        }
    }
    if (!forkjvm)
        return exec_jvm(NULL, argc, argv);

    /* compute identity */
    if (compute_id(id, argc, argv) == -1)
        return exec_jvm(NULL, argc, argv);

    /* try to contact a forkjvm and send fds */
    status = run_forkjvm(id);
    if (status == 1)
        return exec_jvm(id, argc, argv);
    else if (status == -1)
        return exec_jvm(NULL, argc, argv);

    /* will only reach on successful forkjvm run */
    return 0;
}

/* a. tell the first non-busy jvm in pool to run ("fork")
 * b. if pool is empty: spawn a new forkjvm in pool
 * c. if everything in pool is busy and pool is not full: spawn a new forkvjm in pool
 *
 * if there is ever a fatal error return -1 (default to normal exec)
 * if you ever can't connect or lose connection to a jvm just go next
 */
int run_forkjvm(char *id)
{
    /* check for existing forkjvm in id's pool */
    int done = 0;
    char datapath[MAX_PATH_SIZE];
    int datapath_len;

    datapath_len = create_datapath(datapath);

    char jvmpath[datapath_len + ID_LEN + 1];
    sprintf(jvmpath, "%s%s", datapath, id);

    int poolno;
    for (poolno = 0; !done && poolno < JVM_POOL_MAX; poolno++) {
        int error;
        char msg[MSG_LEN];

        /* add/update poolno to id and path */
        id[0] = '/';
        id[ID_LEN - 1] = '0' + poolno;
        jvmpath[datapath_len + ID_LEN - 1] = '0' + poolno;
        fprintf(stderr, "[forkjvm][info] (run_forkjvm) trying id %s\n", id);

        /* check if a directory exists make it if it doesn't
         * if success then we need to start a new jvm
         * if fail try to contact jvm
         */
        if (mkdir(jvmpath, 0775) == 0) {
            /* cool hack so server also returns:
             * fork and make the child exec to become the jvm server
             * parent retires the poolno again after sleeping to give the serve a chance to start
             */

            int pid = fork();
            if (pid == 0) {
                setsid();
                setup_server_logs(jvmpath);
                // TODO: we can continue with error here, but parent and child have same std* fds
                return 1;
            } else {
                write_server_pid(jvmpath, pid);
                // TODO: handle error?
                poolno--;
                sleep(3);
                continue;
            }

        } else if (errno == EEXIST) {
            int jvmfd;
            jvmfd = connect_sock(id);
            if (jvmfd <= 0) {
                if (jvmfd == -2)
                    fprintf(stderr, "[forkjvm][error] socket | id = %s | errno = %s\n", id, strerror(errno));
                else if (jvmfd == -1)
                    fprintf(stderr, "[forkjvm][info] connect | id = %s | errno = %s\n", id, strerror(errno));
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

            fprintf(stderr, "[forkjvm][info] (run_forkjvm) running server with id %s\n", id);
            memset(msg, 0, MSG_LEN);
            if (read_sock(jvmfd, msg, MSG_LEN) == -1)
                perror("[forkjvm][error] read_sock");
            close(jvmfd);
            done = 1;
        } else {
            fprintf(stderr, "[forkjvm][error] (run_forkjvm) mkdir | jvmpath = %s | errno = %s\n",
                    jvmpath, strerror(errno));
            return -1;
        }
    }
    /* if no jvm was available and the pool is full fallback exec a normal jvm */
    return done ? 0 : -1;
}

int compute_id(char *id, int argc, char **argv)
{
    /* openssl is 1 success, 0 error */
    unsigned char digest[MD5_DIGEST_LENGTH];
    char *classpath = NULL;
    int i;
    MD5_CTX md5ctx;

    if (!MD5_Init(&md5ctx)) {
        fprintf(stderr, "[forkjvm][error] MD5_Init\n");
        return -1;
    }
    /* add in all the args */
    for (i = 0; i < argc; i++) {
        if (!MD5_Update(&md5ctx, argv[i], strlen(argv[i]))) {
            fprintf(stderr, "[forkjvm][error] MD5_Update\n");
            return -1;
        }
        /* locate classpath while adding args */
        if (strcmp(argv[i], "-classpath") == 0 ||
            strcmp(argv[i], "-cp") == 0) {
          classpath = argv[i + 1];
        }
    }

    if (classpath == NULL)
        classpath = getenv("CLASSPATH");
        if (classpath == NULL)
            classpath = ".";

    if (md5add_classpath(&md5ctx, classpath) == -1) {
        return -1;
    }

    if (!MD5_Final(digest, &md5ctx)) {
      fprintf(stderr, "[forkjvm][error] MD5_Final\n");
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
            fprintf(stderr, "[forkjvm][error] MD5_Update\n");
            return -1;
        }
    }
    return 0;
}

int create_datapath(char *datapath) {
    int datapath_len = readlink("/proc/self/exe", datapath, MAX_PATH_SIZE);
    if (datapath_len == -1) {
        perror("[forkjvm][error] readlink");
        return -1;;
    } else if (datapath_len > MAX_PATH_SIZE) {
        fprintf(stderr, "[forkjvm][error] readlink MAX_PATH_SIZE too small\n");
        return -1;
    }
    // -8 removes "bin/java"
    datapath_len -= 8;
    datapath[datapath_len] = '\0';
    strcat(datapath,"forkjvm/data");
    datapath_len += 12;
    return datapath_len;
}

int create_execpath(char *execpath)
{
    int exec_path_len = readlink("/proc/self/exe", execpath, MAX_PATH_SIZE);
    if (exec_path_len == -1) {
        perror("[forkjvm][error] readlink");
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
    char execpath[MAX_PATH_SIZE];

    create_execpath(execpath);
    argv[0] = "java_real";

    if (id == NULL) {
        memcpy(argv + 1, main_argv + 1, sizeof(argv));
        argv[main_argc] = NULL;
    } else {
        /* "-forkjvmid=" + '\0' = 12 */
        char forkjvm_arg[ID_LEN + 12];
        sprintf(forkjvm_arg, "-forkjvmid=%s", id);
        argv[1] = forkjvm_arg;
        memcpy(argv + 2, main_argv + 1, sizeof(argv));
        argv[main_argc + 1] = NULL;
        fprintf(stderr, "[forkjvm][info] (exec_jvm) exec with id %s\n", id + 1);
    }
    execv(execpath, argv);
    perror("[forkjvm][error] exec");
    return -1;
}

int send_fds(int jvmfd)
{
    /* get the limit of fds, go through them checking if they are open and sending to server */
    struct rlimit fd_lim;
    char msg[MSG_LEN];
    memset(msg, 0, MSG_LEN);

    /* send fds to forkjvm */
    if (getrlimit(RLIMIT_NOFILE, &fd_lim) == -1) {
        perror("[forkjvm][error] (send_fds) getrlimit");
        return -2;
    }

    int fd;
    for (fd = 0; fd < 3 /* rlim.rlim_cur */; fd++) {
        if (fcntl(fd, F_GETFD) != -1) {
            sprintf(msg, "%d", fd);
            if (write_fd(jvmfd, msg, MSG_LEN, fd) != MSG_LEN) {
                perror("[forkjvm][error] (send_fds) write_fd");
                return -1;
            }
        }
    }
    /* send go msg aka no fd */
    if (write_sock(jvmfd, msg, MSG_LEN) != MSG_LEN) {
        perror("[forkjvm][error] (send_fds) write_sock");
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
    if (fd == -1)
        return -2;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof(addr.sun_path) - 1);
    addr.sun_path[0] = '\0';

    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1)
        return -1;

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

int write_server_pid(const char* jvmpath, int pid) {
    // build path <jvmpath>/pid.txt
    int jvmpath_len = strlen(jvmpath);
    int pidfile_len = strlen("pid.txt");
    char path[jvmpath_len + 1 + pidfile_len + 1];
    strcpy(path, jvmpath);
    path[jvmpath_len] = '/';
    strcpy(path + jvmpath_len + 1, "pid.txt");
    path[jvmpath_len + 1 + pidfile_len] = '\0';

    FILE* f = fopen(path, "w");
    if (f == NULL) {
        perror("[forkjvm][error] (write_server_pid) fopen");
        return errno;
    }
    int ret = fprintf(f, "%d\n", pid);
    if (ret < 0) {
        fprintf(stderr, "[forkjvm][error] (write_server_pid) fprintf returned %d, ferror %d\n", ret, ferror(f));
    }
    ret = fclose(f);
    if (ret) {
        fprintf(stderr, "[forkjvm][error] (write_server_pid) fclose returned %d\n", ret);
    }
    return ret;
}

int setup_server_logs(const char *jvmpath) {
    int path_len = strlen(jvmpath) + strlen("/stdout") + 1;
    char stdout_path[path_len];
    char stderr_path[path_len];
    int ret = 0;

    strcpy(stdout_path, jvmpath);
    strcat(stdout_path, "/stdout");
    strcpy(stderr_path, jvmpath);
    strcat(stderr_path, "/stderr");

    /* server should have no stdin on its own (no client connected)
     * just closing stdin could lead to its fd used by something else and later overwritten by client stdin
     * so we set to /dev/null
     */
    if (freopen("/dev/null", "r", stdin) == NULL) {
        fprintf(stderr, "[forkjvm][error] (setup_server_logs) freopen stdin_path = %s errno = %s\n",
                "/dev/null", strerror(errno));
        ret = -1;
    }

    if (freopen(stdout_path, "w", stdout) == NULL) {
        fprintf(stderr, "[forkjvm][error] (setup_server_logs) freopen stdout_path = %s errno = %s\n",
                stdout_path, strerror(errno));
        ret = -2;
    }
    if (freopen(stderr_path, "w", stderr) == NULL) {
        fprintf(stderr, "[forkjvm][error] (setup_server_logs) open stderr_path = %s errno = %s\n",
                stderr_path, strerror(errno));
        ret = -3;
    }
    return ret;
}
