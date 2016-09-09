#ifndef JAVA_H
#define JAVA_H

// TODO update this text
/* -- unix abstract socket java client --
 * always do a normal jvm exec on critical error
 *
 * 1. check if it is a hottub if not just exec
 * 2. compute id using args and classpath contents
 * 3. try to mkdir for poolno
 * 4a. if mkdir success start a new hottub
 * 4b. if mkdir fails try to contact jvm with that poolno
 *      5a. if you successfully contact a jvm send fds to it
 *          6. wait until jvm finishes
 *      5b. if you never successfully contact a jvm do normal exec
 *           - this means entire pool is busy and pool is full
 */

typedef struct {
    int argc;
    char **argv;
    int java_argc;
    char **java_argv;
    int javaD_argc;
} args_info;

// heavy lifting
int exec_jvm(const char *jvmid, int main_argc, char **main_argv);
int run_hottub(char *id, args_info *args, char **envp);

// utility
void signal_handler(int signum);
int create_datapath(char *datapath);
int create_execpath(char *execpath);
int create_pid_file(const char *name, pid_t pid);
pid_t get_server_pid();
int remove_pid_file(const char *name);
int setup_server_logs();
void setup_signal_handling();
uint64_t _now();

// hottubid building
int compute_id(char *id, args_info *args);
int md5add_classpath(MD5_CTX *md5ctx, const char *classpath);
int is_wildcard(const char *filename);
int strsuffix(const char *str, const char *suffix);
char *next_dirent(DIR *dir);
int md5add_wildcard(MD5_CTX *md5ctx, const char *wildcard);
int md5add_file(MD5_CTX *md5ctx, const char *filename);

// socket stuff
int connect_sock(const char *path);
ssize_t write_fd(int fd, void *ptr, size_t nbytes, int sendfd);
ssize_t read_sock(int fd, void *ptr, size_t nbytes);
ssize_t write_sock(int fd, void *ptr, size_t nbytes);
int send_fds(int jvmfd);
int send_args(int jvmfd, args_info *args);
int send_args_i(int jvmfd, int i, void *ptr, size_t len, char *val, char *tag);
int send_working_dir(int jvmfd);
int send_env_var(int jvmfd, char **envp);


// 1st char for '\0' (abstract sockets), last char for poolno
// note an increase in pool digits requires string modification
#define TRACE        0
#define ID_LEN       1 + MD5_DIGEST_LENGTH * 2 + 1
#define MAX_PATH_LEN 1024
#define JVM_POOL_MAX 5
#define RETRY_COUNT  1024
#define SERVER       "/server.pid"
#define CLIENT       "/client.pid"

#endif // JAVA_H
