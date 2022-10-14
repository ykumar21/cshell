#include <stdio.h>  /* standard io */ 
#include <stdlib.h> /* std lib */ 
#include <stdbool.h>    /* bool datatype */ 
#include <string.h> /* string ops */ 
#include <unistd.h> /* for proc func */
#include <errno.h>  /* errno */ 
#include <sys/wait.h>   /* wait, waitpid */ 
#include <sys/resource.h>   /* rusage */  
#include <signal.h> /* signal handlers */ 

#define _BUFSZ 1024 /* standard buffer limit */ 
#define _ARGVSZ 30  /* max size of argv */
#define _ARGSZ 100  /* max length of argument */
#define _PIPE_LIMIT 10  /* pipe number limit */ 
#define PIPE_RD 0   /* pipe read index */ 
#define PIPE_WR 1   /* pipe write index */ 

#define clear() printf("\033[H\033[J")  /* clear terminal */ 


struct Arguments {
    size_t  argc;       /* number of args */
    char    *argv[_ARGVSZ]; /* args vector */ 
};

typedef struct Arguments Arguments;

/* Global variables */ 
bool EXIT = 0;
const char *term_name = "3230shell";

/* Register Signal handlers 
 * for the parent process. 
 * 
 * Supported signals: 
 *  1) SIGINT
 *  2) SIGCHLD
 *  3) SIGEXIT 
 */
void _SIGINT_HANDLER(int arg) {
    char* msg = "Interrupt!\n\0";
    write( STDOUT_FILENO, msg, strlen(msg)+1 );
    fflush(stdout);
}

void _SIGCHD_HANDLER() {
    //char* msg = "Child Terminated!\n";
    //write( STDOUT_FILENO, msg, strlen(msg)+1 );
}
/**** Signal Handlers End ****/ 

/* Print tasks */ 
void print_tasks(Arguments** tasks) {
    for ( int i = 0; i  < _PIPE_LIMIT; ++i ) {
        printf("task %d:", i);
        
        if ( tasks[i] == NULL ) {
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

/* Count number of processes in given task */ 
size_t cproc(Arguments** tasks) {
    if ( tasks == NULL ) return 0;
    size_t num_tasks = 0;
    for (int i = 0; i < _PIPE_LIMIT; ++i) {
        if ( tasks[i] == NULL ) {
            return num_tasks;
        }
        ++num_tasks;
    }
    return num_tasks;
} 

/* Executes multiple tasks connected via pipes */
void __exec_multi( Arguments** t ) {
    size_t          nproc=cproc(t); /* number of proc */ 
    pid_t           pid[nproc]; /* pid of child proc */ 
    int             pipefd[nproc][2];   /* stores pipe fd for each proc */
    bool            logstat; 
    struct rusage   usagestat[nproc]; /* usage stat for child proc */ 

    // create pipe for each child proc 
    for ( int i = 0; i < nproc; ++i ) {
        if ( pipe(pipefd[i]) == -1 ) {
            perror( "pipe failed" );
        }
    }

    // check for timeX command 
    printf("%s\n", *t[0]->argv);

    // iterate over processes and execute sequentially
    for ( int i = 0; i < nproc; ++i ) {
        pid[i] = fork(); // fork new child process 

        if ( pid[i] < 0 ) {
            // handle fork failure
            perror("fork failed");
            exit(1);
        }

        if ( pid[i] == 0 ) {
            // printf("Child %d (pid = %d)\n", i, getpid());
            
            if ( i == 0 ) {
                // dup stdout to current pipe write 
                dup2(pipefd[i][PIPE_WR], STDOUT_FILENO);
            }  else if ( i == nproc-1 ) {
                // dup current pipe write to std out 
                dup2( STDOUT_FILENO, pipefd[i][PIPE_WR] );
                // dup stdin to previous pipe read 
                dup2( pipefd[i-1][PIPE_RD], STDIN_FILENO );
            } else {
                // dup stdout to current pipe write 
                dup2( pipefd[i][PIPE_WR], STDOUT_FILENO );
                // dup stdin to previous pipe read 
                dup2( pipefd[i-1][PIPE_RD], STDIN_FILENO );
            }

            // close read and write pipes 
            for ( int i = 0; i < nproc; ++i ) {
                close(pipefd[i][PIPE_RD]);
                close(pipefd[i][PIPE_WR]);
            }

            execvp( *t[i]->argv, t[i]->argv );

            /* Handle execvp error */ 
            fprintf( stderr, "3230shell: '%s': %s \n", *t[i]->argv, strerror(errno) );
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
        waitpid(pid[i], NULL, 0);
    } 
}


/* Executes command using given pipe fd */ 
void __exec2(char** argv, bool logstat, int read_fd, int write_fd) {
    pid_t           pid, wpid;  /* pid of child proc */ 
    int             status; /* Status of child proc */
    struct rusage   usagestat;  /* Usage statistics */

    pid = fork();
    
    int pipefd[2] = { read_fd, write_fd };

    if (pid == 0) {
        // Child process
        dup2(STDOUT_FILENO, pipefd[PIPE_WR]);
        //dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[PIPE_RD]);
        close(pipefd[PIPE_WR]);
        
        // Try to find absolute path 
        //execv(*args.argv, args.argv);
        // If fail exec path 
        execvp(*argv, argv);

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
        printf("(user)%ld.%02d s  ", usagestat.ru_utime.tv_sec, usagestat.ru_utime.tv_usec);
        printf("(sys) %ld.%02d s\n", usagestat.ru_stime.tv_sec, usagestat.ru_stime.tv_usec);
    }
    
}

void __exec(char** argv, bool logstat) {
    int pipefd[2];
    pipe(pipefd);
    __exec2( argv, logstat, pipefd[PIPE_RD], pipefd[PIPE_WR] );
};

void _terminate() { 
    EXIT = 1; 
    printf("%s: Terminated\n", term_name);
}

void __timex(Arguments args) {
    if ( args.argc == 1 ) {
        fprintf(stderr, "3230shell: %s \n", "\"timeX\" cannot be a standalone command");
        return;
    }

    char* argv[_ARGVSZ];
    for ( int i = 1; i <= args.argc; ++i ) {
        argv[i-1] = args.argv[i];
    }

    // create pipe 

    __exec(argv, true);

}

void parse(Arguments args) {
    // printf("Command: %s\n", cmd);
    /* lol please change dis */ 
    if (strcmp(*args.argv, "exit") == 0) {
        if ( args.argc > 2 ) {
            fprintf(stderr, "3230shell: %s \n", "\"exit\" with other arguments!!!");
        } else {
            _terminate();
        }
    } else if (strcmp(*args.argv, "timeX") == 0) {
        __timex(args);
    } else {
        __exec(args.argv, false);
    }
} 

void flush_str(char* str, size_t sz) {
    int i = 0;  
    while ( i < sz ) {
        str[i] = 0;
        ++i;
    }
} 

struct Arguments** get_cmd() {
    int         i, j=0, l=0;
    size_t      cmdlen = 0, argc=0;
    char        *argptr;
    Arguments   **tasks; 
    char        rdbuf[_BUFSZ];
    
    // Initialise tasks array
    tasks = (Arguments **) malloc(sizeof(Arguments) * _PIPE_LIMIT);
    
    for (int i = 0; i < _PIPE_LIMIT; ++i) {
        tasks[i] = malloc( sizeof( struct Arguments ) );
        tasks[i]->argc = 0;
    }
    
    printf("$$ %s ## ", term_name);
    fgets(rdbuf, _BUFSZ, stdin);

    if ( rdbuf[0] == '\n' ) {
        return NULL;
    }

    // remove new line character 
    rdbuf[strlen(rdbuf)-1] = '\0';

    char* token = strtok(rdbuf, " ");
    while (token != NULL) {
        //printf("Token: .%s.\n", token);
        if (strcmp(token, "|") == 0) {
            ++j;
        } else {
            tasks[j]->argv[ tasks[j]->argc++ ] = token;    
        }
        token = strtok(NULL, " ");
    }

    for ( int i = 0; i < j; ++i ) {
        tasks[i]->argv[ tasks[i]->argc ] = NULL;
    }

    tasks[++j] = NULL;
  
    
    return tasks;
}

void register_signal_handlers() {
    signal(SIGINT, _SIGINT_HANDLER);
    signal(SIGCHLD, _SIGCHD_HANDLER);
}   

/* Initialises the shell */
void init_shell() { 
    clear();

    register_signal_handlers();
    
    // Main loop
    while (!EXIT) {
        struct Arguments** tasks = get_cmd();
        int i = 0; 
        size_t sz_ = cproc(tasks);
        if ( sz_ == 1 ) parse(*tasks[0]);
        else __exec_multi(tasks); 
    }

}
