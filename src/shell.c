/**
 *Name: Yashwardhann Kumar (u3566269@connect.hku.hk)
 *Student No: 3035662699
 *Development Platform: MacOS, Tested on academy11
 *Remark: Implemented all features except bonus (tried it though)
 */
#include <errno.h>                    /*errno */
#include <signal.h>                   /*signal handlers */
#include <stdbool.h>                  /*bool datatype */
#include <stdio.h>                    /*standard io */
#include <stdlib.h>                   /*std lib */
#include <string.h>                   /*string ops */
#include <sys/queue.h>                /*queue */
#include <sys/resource.h>             /*rusage */
#include <sys/wait.h>                 /*wait, waitpid */
#include <unistd.h>                   /*for proc func */

#define _BUFSZ 1024                    /*standard buffer limit */
#define _ARGVSZ 30                     /*max size of argv */
#define _ARGSZ 100                     /*max length of argument */
#define _PIPE_LIMIT 10                 /*pipe number limit */
#define PIPE_RD 0                      /*pipe read index */
#define PIPE_WR 1                      /*pipe write index */
#define clear() printf("\033[H\033[J") /*clear terminal */

struct Result {
  char *resbuf[_BUFSZ];
  pid_t pid;
  int status;
};

struct Arguments {
  size_t argc;         /*number of args */
  char *argv[_ARGVSZ]; /*args vector */
  bool logstat;        /*flag for stat logging */
  bool pipe_enable;    /*flag to check for piping */
  bool background;     /*flag to check for background proc */
};

typedef struct Arguments Arguments;

/*Global variables */
bool EXIT = 0;
const char *term_name = "3230shell";
char rdbuf[_BUFSZ];
pid_t cur_child_proc = 0;

void init_shell(void);
void main_loop(void);

/*Register Signal handlers
 *for the parent process.
 *
 *Supported signals:
 *1) SIGINT
 *2) SIGCHLD
 *3) SIGUSR1
 */

void _SIGKILL_HANDLER() {
  char *msg = "Killed\n";
  write(STDOUT_FILENO, msg, strlen(msg) + 1);
}

void _SIGINT_HANDLER(int arg) {
  char *msg = "Interrupt!\n";
  char *wait_msg = "$$ 3230shell ## ";
  // write messages to stdout
  write(STDOUT_FILENO, msg, strlen(msg) + 1);
  write(STDOUT_FILENO, wait_msg, strlen(wait_msg) + 1);

  // if there is active child process then
  // send sigint to child
  if (cur_child_proc != 0) {
    kill(cur_child_proc, SIGINT);
  }
}

void _SIGUSR1_HANDLER(int sig) {
  if (cur_child_proc != 0) {
    // send signal to current child
    kill(cur_child_proc, SIGUSR1);
  }
}

/*SIGCHLD handler not fully implemented */
void _SIGCHLD_HANDLER() {
  pid_t pid;
  int status;
  while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
    // get data from the child
    // to do....
  }
}

/****Signal Handlers End****/

/*[DEBUG] Print tasks */
void print_tasks(Arguments **tasks) {
  for (int i = 0; i < _PIPE_LIMIT; ++i) {
    printf("task %d:", i);

    if (tasks[i] == NULL) {
      printf("(null)\n");
      break;
    }

    printf("    argc = %zu, argv = ", tasks[i]->argc);
    for (int j = 0; j <= tasks[i]->argc; ++j) {
      printf("%s ", tasks[i]->argv[j]);
    }

    printf("\n");
  }
}

// Moves string by k positions
void strmv(char *str, size_t mv_amt) {
  size_t i = 0;
  while (i < _ARGSZ && str[i] != '\0') {
    str[i] = str[i + mv_amt];
    ++i;
  }

  str[i] = '\0';
}

/*Count number of processes in given task */
size_t cproc(Arguments **tasks) {
  if (tasks == NULL)
    return 0;
  size_t num_tasks = 0;
  for (int i = 0; i < _PIPE_LIMIT; ++i) {
    if (tasks[i] == NULL) {
      return num_tasks;
    }

    ++num_tasks;
  }

  return num_tasks;
}

/*Executes multiple tasks connected via pipes */
void __exec2(Arguments **t) {
  size_t nproc = cproc(t);        /*number of proc */
  pid_t pid[nproc];               /*pid of child proc */
  int status[nproc];              /*stores status of child proc */
  int pipefd[nproc][2];           /*stores pipe fd for each proc */
  bool logstat = 0;               /*flag to check for child proc stat log */
  struct rusage usagestat[nproc]; /*usage stat for child proc */

  // create pipe for each child proc
  for (int i = 0; i < nproc; ++i) {
    if (pipe(pipefd[i]) == -1) {
      perror("pipe failed");
    }
  }

  // check for timeX command
  if (t[0]->logstat == true) {
    if (t[0]->argc == 0) {
      fprintf(stderr, "3230shell: %s \n",
              "\"timeX\" cannot be a standalone command");
      return;
    }

    logstat = true;
  }

  // iterate over processes and execute sequentially
  for (int i = 0; i < nproc; ++i) {
    pid[i] = fork(); // fork new child process

    if (i == 0)
      cur_child_proc = pid[i]; /*set current proc as active */

    if (pid[i] < 0) {
      // handle fork failure
      perror("fork failed");
      exit(1);
    }

    if (pid[i] == 0) { /*CHILD CODE */
      if (i == 0) {
        // dup stdout to current pipe write
        dup2(pipefd[i][PIPE_WR], STDOUT_FILENO);
      } else if (i == nproc - 1) {
        // dup current pipe write to std out
        dup2(STDOUT_FILENO, pipefd[i][PIPE_WR]);
        // dup stdin to previous pipe read
        dup2(pipefd[i - 1][PIPE_RD], STDIN_FILENO);
      } else {
        // dup stdout to current pipe write
        dup2(pipefd[i][PIPE_WR], STDOUT_FILENO);
        // dup stdin to previous pipe read
        dup2(pipefd[i - 1][PIPE_RD], STDIN_FILENO);
      }

      // close read and write pipes
      for (int i = 0; i < nproc; ++i) {
        close(pipefd[i][PIPE_RD]);
        close(pipefd[i][PIPE_WR]);
      }

      /*Block till SIGUSR1 is recieved for the first proc */
      if (i == 0) {
        int sig, *sigptr = &sig, ret_val;
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGUSR1);
        sigprocmask(SIG_BLOCK, &set, NULL);
        ret_val = sigwait(&set, sigptr);

        if (ret_val == -1) {
          perror("Sigwait failed!");
          exit(EXIT_FAILURE);
        }
      }

      if (*t[i]->argv[0] == '/') {
        execv(*t[i]->argv, t[i]->argv);
      } else {
        execvp(*t[i]->argv, t[i]->argv);
      }

      /*Handle execvp error */
      fprintf(stderr, "3230shell: '%s': %s \n", *t[i]->argv, strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  // Close all pipes in the parent process
  for (int i = 0; i < nproc; ++i) {
    close(pipefd[i][PIPE_RD]);
    close(pipefd[i][PIPE_WR]);
  }

  // Wait for child processes to finish executing
  for (int i = 0; i < nproc; ++i) {
    wait4(pid[i], &status[i], WUNTRACED, &usagestat[i]);
  }

  cur_child_proc = 0; /*set child proc as inactive */

  if (logstat) {
    for (int i = 0; i < nproc; ++i) {
      printf("(PID)%d  (CMD)%s    ", pid[i], *t[i]->argv);
      printf("(user)%ld.%ld s  ", usagestat[i].ru_utime.tv_sec,
             usagestat[i].ru_utime.tv_usec);
      printf("(sys) %ld.%ld s\n", usagestat[i].ru_stime.tv_sec,
             usagestat[i].ru_stime.tv_usec);
    }
  }
}

/*executes background proc */
void __exec3(char **argv, bool logstat) {
  pid_t pid;
  int status;
  int pipefd[2];

  // struct Result* res = (struct Result*) malloc(sizeof(struct Result));
  pid = fork();

  if (pipe(pipefd) == -1) {
    perror("Error creating pipe!");
    exit(1);
  }

  cur_child_proc = pid;

  if (pid == 0) {
    // block till sigusr1 is recv
    int sig, *sigptr = &sig;
    int ret_val;

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);
    ret_val = sigwait(&set, sigptr);

    if (ret_val == -1) {
      perror("Sigwait failed!");
      exit(EXIT_FAILURE);
    } else {
      execvp(*argv, argv);
    }

    fprintf(stderr, "3230shell: '%s': %s\n", *argv, strerror(errno));
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    perror("fork err");
  }

  return;
}

/*Executes command */
void __exec(char **argv, bool logstat, bool background) {
  pid_t pid, wpid;         /*pid of child proc */
  int status;              /*Status of child proc */
  struct rusage usagestat; /*Usage statistics */
  int pipefd[2];           /*fifo pipe for child proc */

  pid = fork();

  if (pipe(pipefd) == -1) {
    perror("Error creating pipe.");
    return;
  }

  cur_child_proc = pid;

  if (pid == 0) {
    int sig, *sigptr = &sig, ret_val;

    // Child process
    dup2(STDOUT_FILENO, pipefd[PIPE_WR]);
    // dup2(pipefd[0], STDIN_FILENO);
    close(pipefd[PIPE_RD]);
    close(pipefd[PIPE_WR]);

    // Try to find absolute path
    // execv(*args.argv, args.argv);
    // If fail exec path

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGUSR1);
    sigprocmask(SIG_BLOCK, &set, NULL);
    ret_val = sigwait(&set, sigptr);

    if (ret_val == -1) {
      perror("Sigwait failed!");
      exit(EXIT_FAILURE);
    } else {
      if (*argv[0] == '/') {
        execv(*argv, argv);
      } else {
        execvp(*argv, argv);
      }
    }

    fprintf(stderr, "3230shell: '%s': %s \n", *argv, strerror(errno));
    exit(EXIT_FAILURE);
  } else if (pid < 0) {
    // Error forking
    perror("fork err");
  } else {
    // Parent process
    do {
      wpid = waitpid(pid, &status, WUNTRACED);
    } while (!WIFEXITED(status) && !WIFSIGNALED(status));
  }

  if (logstat) {
    getrusage(RUSAGE_CHILDREN, &usagestat);
    printf("(PID)%d  (CMD)%s    ", pid, *argv);
    printf("(user)%ld.%ld s  ", usagestat.ru_utime.tv_sec,
           usagestat.ru_utime.tv_usec);
    printf("(sys) %ld.%ld s\n", usagestat.ru_stime.tv_sec,
           usagestat.ru_stime.tv_usec);
  }

  return;
}

void _terminate() {
  EXIT = 1;
  printf("%s: Terminated\n", term_name);
}

void __timex(Arguments args) {
  if (args.argc == 0) {
    fprintf(stderr, "3230shell: %s \n",
            "\"timeX\" cannot be a standalone command");
    return;
  }

  if (args.background == true) {
    fprintf(stderr, "3230shell: \"timeX\" cannot be run in background mode!\n");
    return;
  }

  __exec(args.argv, args.logstat, args.background);
}

void parse(Arguments args) {
  if (args.logstat == true) {
    __timex(args);
  } else {
    if (strcmp(*args.argv, "exit") == 0) {
      if (args.argc > 2) {
        fprintf(stderr, "3230shell: %s \n", "\"exit\" with other arguments!!!");
      } else {
        _terminate();
      }
    } else {
      __exec(args.argv, args.logstat, args.background);
    }
  }
}

void flush_str(char *str, size_t sz) {
  int i = 0;
  while (i < sz) {
    str[i] = 0;
    ++i;
  }
}

struct Arguments **get_cmd() {
  int i, j = 0;
  bool prev_pipe = false;
  // Initialise tasks array
  Arguments **tasks = (Arguments **)malloc(sizeof(Arguments) * _PIPE_LIMIT);

  for (int i = 0; i < _PIPE_LIMIT; ++i) {
    tasks[i] = malloc(sizeof(struct Arguments));
    tasks[i]->argc = 0;
    tasks[i]->logstat = false;
    tasks[i]->pipe_enable = false;
    tasks[i]->background = false;
  }

  printf("$$ %s ## ", term_name);
  fgets(rdbuf, _BUFSZ, stdin);

  if (rdbuf[0] == '\n') {
    return NULL;
  }

  // remove new line character
  rdbuf[strlen(rdbuf) - 1] = '\0';

  // check if it is background process

  char *token = strtok(rdbuf, " ");
  while (token != NULL) {
    // printf("Token: .%s.\n", token);
    if (strcmp(token, "|") == 0) {
      if (prev_pipe) {
        // throw error
        fprintf(
            stderr, "3230shell: %s \n",
            "should not have two consecutive | without in-between command!");
        return NULL;
      }

      prev_pipe = true;
      tasks[0]->pipe_enable = true;
      ++j;
    } else if (strcmp(token, "&") == 0) {
      tasks[0]->background = true;
    } else {
      prev_pipe = false;
      bool incr_task = false;
      if (token[strlen(token) - 1] == ';') {
        incr_task = true;
        token[strlen(token) - 1] = '\0';
      }

      if (strcmp(token, "timeX") == 0) {
        tasks[0]->logstat = true;
      } else {
        tasks[j]->argv[tasks[j]->argc++] = token;
      }

      if (incr_task)
        ++j;
    }

    token = strtok(NULL, " ");
  }

  for (int i = 0; i < j; ++i) {
    tasks[i]->argv[tasks[i]->argc] = NULL;
  }

  tasks[++j] = NULL;

  return tasks;
}

void register_signal_handlers() {
  signal(SIGINT, &_SIGINT_HANDLER);
  signal(SIGUSR1, &_SIGUSR1_HANDLER);
  signal(SIGCHLD, &_SIGCHLD_HANDLER);
  signal(SIGKILL, &_SIGKILL_HANDLER);
}

void main_loop() {
  while (!EXIT) {
    struct Arguments **tasks = get_cmd();

    if (tasks == NULL) {
      continue;
    }

    size_t sz_ = cproc(tasks);

    if (sz_ == 1)
      parse(*tasks[0]);
    else {
      if (tasks[0]->pipe_enable) {
        __exec2(tasks);
      } else {
        for (int i = 0; i < sz_; ++i) {
          parse(*tasks[i]);
        }
      }
    };

    free(tasks);
  }
}

/*Initialises the shell */
void init_shell() {
  register_signal_handlers();
  main_loop();
}

int main(int argc, char **argv) {
  printf("%d\n", getpid());
  clear();
  init_shell();
  // kill the shell on exit
  kill(getpid(), SIGKILL);
}