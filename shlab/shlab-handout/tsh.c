/* 
 * tsh - A tiny shell program with job control
 * 
 * Jamie Chen
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include "csapp.h"

/* Misc manifest constants */
#define MAXLNSZ    1024   /* max line size */
#define MAXARGS     128   /* max args on a command line */
#define MAXJOBS      16   /* max jobs at any point in time */
#define MAXJID    1<<16   /* max job ID */

/* Job states */
#define UNDEF 0 /* undefined */
#define FG 1    /* running in foreground */
#define BG 2    /* running in background */
#define ST 3    /* stopped */

/* 
 * Jobs states: FG (foreground), BG (background), ST (stopped)
 * Job state transitions and enabling actions:
 *     FG -> ST  : ctrl-z       DONE
 *     ST -> FG  : fg command
 *     ST -> BG  : bg command   DONE
 *     BG -> FG  : fg command
 * At most 1 job can be in the FG state.
 */

/* Global variables */
extern char **environ;      /* defined in libc */
char prompt[] = "tsh> ";    /* command line prompt (DO NOT CHANGE) */
int verbose = 0;            /* if true, print additional output */
int nextjid = 1;            /* next job ID to allocate */
char sbuf[MAXLNSZ];         /* for composing sprintf messages */

struct job_t {              /* The job struct */
    pid_t pid;              /* job PID */
    int jid;                /* job ID [1, 2, ...] */
    int state;              /* UNDEF, BG, FG, or ST */
    char cmdline[MAXLNSZ];  /* command line */
};
struct job_t jobs[MAXJOBS]; /* The job list */

// pid_t pid;
int fg_done;  /* flag that fg job is done */
sigset_t mask_all, mask_sigchld;

/* End global variables */


/* Function prototypes */

/* Here are the functions that you will implement */
void eval(char *cmdline);
int builtin_cmd(char **argv);
void do_bgfg(char **argv);
void waitfg(pid_t pid, sigset_t *mask);

void sigchld_handler(int sig);
void sigtstp_handler(int sig);
void sigint_handler(int sig);

/* Here are helper routines that we've provided for you */
int parseline(const char *cmdline, char **argv); 
void sigquit_handler(int sig);

void clearjob(struct job_t *job);
void initjobs(struct job_t *jobs);
int maxjid(struct job_t *jobs); 
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline);
int deletejob(struct job_t *jobs, pid_t pid); 
pid_t fgpid(struct job_t *jobs);
struct job_t *getjobpid(struct job_t *jobs, pid_t pid);
struct job_t *getjobjid(struct job_t *jobs, int jid); 
int pid2jid(pid_t pid); 
void listjobs(struct job_t *jobs);
void printjobpid_safe(struct job_t *jobs, pid_t pid);
void printjobpid(struct job_t *jobs, pid_t pid);

void usage(void);
typedef void handler_t(int);

/*
 * main - The shell's main routine 
 */
int main(int argc, char **argv) 
{
    /* Init masks */
    Sigfillset(&mask_all);
    Sigemptyset(&mask_sigchld);
    Sigaddset(&mask_sigchld, SIGCHLD);

    char c;
    char cmdline[MAXLNSZ];
    int emit_prompt = 1; /* emit prompt (default) */

    /* Redirect stderr to stdout (so that driver will get all output
     * on the pipe connected to stdout) */
    dup2(1, 2);

    /* Parse the command line */
    while ((c = getopt(argc, argv, "hvp")) != EOF) {
        switch (c) {
        case 'h':             /* print help message */
            usage();
	        break;
        case 'v':             /* emit additional diagnostic info */
            verbose = 1;
	        break;
        case 'p':             /* don't print a prompt */
            emit_prompt = 0;  /* handy for automatic testing */
	        break;
	    default:
            usage();
	}
    }

    /* Install the signal handlers */

    /* These are the ones you will need to implement */
    Signal(SIGINT,  sigint_handler);   /* ctrl-c */
    Signal(SIGTSTP, sigtstp_handler);  /* ctrl-z */
    Signal(SIGCHLD, sigchld_handler);  /* Terminated or stopped child */

    /* This one provides a clean way to kill the shell */
    Signal(SIGQUIT, sigquit_handler); 

    /* Initialize the job list */
    initjobs(jobs);

    /* Execute the shell's read/eval loop */
    while (1) {

        /* Read command line */
        if (emit_prompt) {
            printf("%s", prompt);
            fflush(stdout);
        }
        if ((fgets(cmdline, MAXLNSZ, stdin) == NULL) && ferror(stdin))
            app_error("fgets error");
        if (feof(stdin)) { /* End of file (ctrl-d) */
            fflush(stdout);
            exit(0);
        }

        /* Evaluate the command line */
        eval(cmdline);
        fflush(stdout);
        fflush(stdout);
    }

    exit(0); /* control never reaches here */
}
  
/* 
 * eval - Evaluate the command line that the user has just typed in
 * 
 * If the user has requested a built-in command (quit, jobs, bg or fg)
 * then execute it immediately. Otherwise, fork a child process and
 * run the job in the context of the child. If the job is running in
 * the foreground, wait for it to terminate and then return.  Note:
 * each child process must have a unique process group ID so that our
 * background children don't receive SIGINT (SIGTSTP) from the kernel
 * when we type ctrl-c (ctrl-z) at the keyboard.  
*/
void eval(char *cmdline) 
{
    /* Parse cmdline into argv */
    char* argv[MAXARGS];
    int bg = parseline(cmdline, argv);

    /* Skip empty line */
    if (argv[0] == NULL)
        return;

    /* Run command */
    if (!builtin_cmd(argv)) {
        pid_t pid;
        static sigset_t prev_all, prev_sigchld;

        /* Block SIGCHLD so that job is added before sigchld handler runs */
        Sigprocmask(SIG_BLOCK, &mask_sigchld, &prev_sigchld);

        /* Child calls execve */
        if ((pid = Fork()) == 0)  {
            Setpgid(0, 0);  // give child unique gpid, see note above
            Sigprocmask(SIG_UNBLOCK, &mask_sigchld, NULL);  // unblock sigchld for child
            if (execve(argv[0], argv, environ) < 0) {
                sio_puts(argv[0]);
                sio_error(": Command not found\n");  // child exits
            }
        }

        Sigprocmask(SIG_SETMASK, &mask_all, &prev_all);  // block all
        addjob(jobs, pid, bg ? BG : FG, cmdline);  // Add child to job list
        // if (bg) {  // print background job info
        //     printjobpid_safe(jobs, pid);
        //     sio_puts(" "); sio_puts(cmdline);
        // }
        Sigprocmask(SIG_SETMASK, &prev_all, NULL);  // undo block-all

        /* Wait for foreground job to terminate with sigsuspend */
        if (!bg)
            waitfg(fgpid(jobs), &prev_sigchld);

        /* Unblock SIGCHLD */
        Sigprocmask(SIG_SETMASK, &prev_sigchld, NULL);
    }
}

/* 
 * parseline - Parse the command line and build the argv array.
 * 
 * Characters enclosed in single quotes are treated as a single
 * argument.  Return true if the user has requested a BG job, false if
 * the user has requested a FG job.  
 */
int parseline(const char *cmdline, char **argv) 
{
    static char array[MAXLNSZ]; /* holds local copy of command line */
    char *buf = array;          /* ptr that traverses command line */
    char *delim;                /* points to first space delimiter */
    int argc;                   /* number of args */
    int bg;                     /* background job? */

    strcpy(buf, cmdline);
    buf[strlen(buf)-1] = ' ';  /* replace trailing '\n' with space */
    while (*buf && (*buf == ' ')) /* ignore leading spaces */
	    buf++;

    /* Build the argv list */
    argc = 0;
    if (*buf == '\'') {
        buf++;
        delim = strchr(buf, '\'');
    }
    else {
	    delim = strchr(buf, ' ');
    }

    while (delim) {
        argv[argc++] = buf;
        *delim = '\0';
        buf = delim + 1;
        while (*buf && (*buf == ' ')) /* ignore spaces */
            buf++;

        if (*buf == '\'') {
            buf++;
            delim = strchr(buf, '\'');
        }
        else {
            delim = strchr(buf, ' ');
        }
    }
    argv[argc] = NULL;
    
    if (argc == 0)  /* ignore blank line */
	    return 1;

    /* should the job run in the background? */
    if ((bg = (*argv[argc-1] == '&')) != 0) {
	    argv[--argc] = NULL;
    }
    return bg;
}

/* 
 * builtin_cmd - If the user has typed a built-in command then execute
 *    it immediately.  
 */
int builtin_cmd(char **argv) 
{
    char *cmd = argv[0];

    if (!strcmp(cmd, "quit")) {  /* Exit shell */
        exit(0);
    }
    if (!strcmp(cmd, "jobs")) {  /* List jobs */
        listjobs(jobs);
        return 1;
    }
    if (!strcmp(cmd, "bg") || !strcmp(cmd, "fg")) {
        do_bgfg(argv);
        return 1;
    }
    if (!strcmp(cmd, "&"))     /* ignore singleton & */
        return 1;

    return 0;     /* not a builtin command */
}

int my_atoi(const char* str)
{
    char* end;
    int res = strtol(str, &end, 10);
    if (end == str || *end != '\0' || errno == ERANGE)
        return -1;
    return res;
}

/* 
 * do_bgfg - Execute the builtin bg and fg commands
 */
void do_bgfg(char **argv) 
{
    char *cmd = argv[0];
    char *pidjid = argv[1];

    if (pidjid == NULL) {
        printf("%s command requires PID or %%jobid argument\n", cmd);
        return;
    }

    // find job
    struct job_t *job;
    int id;     // pid or jid
    if (pidjid[0] == '%') {  // jid
        id = my_atoi(&pidjid[1]);
        if (id < 0) {
            printf("%s: argument must be a PID or %%jobid\n", cmd);
            return;
        }
        if ((job = getjobjid(jobs, id)) == NULL) {
            printf("%s: No such job\n", pidjid);
            return;
        }
    } else {  // pid
        id = my_atoi(pidjid);
        if (id < 0) {
            printf("%s: argument must be a PID or %%jobid\n", cmd);
            return;
        }
        if ((job = getjobpid(jobs, id)) == NULL) {
            printf("(%s): No such process\n", pidjid);
            return;
        }
    }

    sigset_t prev;
    Sigprocmask(SIG_SETMASK, &mask_all, &prev);
    if (!strcmp(cmd, "bg")) {  // bg
        printf("[%d] (%d) %s", job->jid, job->pid, job->cmdline);
        job->state = BG;
        Kill(-job->pid, SIGCONT);  // Send SIGCONT to job to wake it up
    } else {  // fg
        job->state = FG;
        Kill(-job->pid, SIGCONT);
        waitfg(job->pid, &prev);  // block and wait for fg job to terminate
    }
    Sigprocmask(SIG_SETMASK, &prev, NULL);
}

/* 
 * waitfg - Block until process pid is no longer the foreground process
 */
void waitfg(pid_t pid, sigset_t *mask)
{
    fg_done = 0;

    /* 
     * A while loop is needed here because we have no way to tell 
     * which type of signal has arrived. Only the sigchld handler
     * can set the global variable fg_done to true.
     */
    while (!fg_done)       // sigchld blocked
        Sigsuspend(mask);  // sigchld unblocked

    if (verbose) {
        sio_puts("waitfg: Process (");
        sio_putl((long)pid);
        sio_puts(") no longer the fg process\n");
    }
}

/*****************
 * Signal handlers
 *****************/

/* 
 * sigchld_handler - The kernel sends a SIGCHLD to the shell whenever
 *     a child job terminates (becomes a zombie), or stops because it
 *     received a SIGSTOP or SIGTSTP signal. The handler reaps all
 *     available zombie children, but doesn't wait for any other
 *     currently running children to terminate.  
 */
void sigchld_handler(int sig) 
{
    fflush(stdout);

    if (verbose)
        sio_puts("sigchld_handler: entering\n");

    // int olderrno = errno;
    int status;
    int fg_pid = fgpid(jobs);
    sigset_t prev;
    pid_t pid;

    // while-waitpid loop here to reap all dead children
    while ((pid = waitpid(-1, &status, WNOHANG | WUNTRACED)) > 0) {
        
        fg_done = pid == fg_pid;
        
        if (WIFEXITED(status)) {
            if (verbose) {
                sio_puts("sigchld_handler: ");
                printjobpid_safe(jobs, pid);
                sio_puts(" deleted\n");
                int es = WEXITSTATUS(status);
                sio_puts("sigchld_handler: ");
                printjobpid_safe(jobs, pid);
                sio_puts(" terminates OK (status "); sio_putl(es); sio_puts(")\n");
            }
            Sigprocmask(SIG_SETMASK, &mask_all, &prev);
            deletejob(jobs, pid);
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        } else if (WIFSIGNALED(status)) {
            if (verbose) {
                sio_puts("sigchld_handler: ");
                printjobpid_safe(jobs, pid);
                sio_puts(" deleted\n");
            }
            int ts = WTERMSIG(status);
            printjobpid_safe(jobs, pid);
            sio_puts(" terminated by signal "); sio_putl(ts); sio_puts("\n");
            Sigprocmask(SIG_SETMASK, &mask_all, &prev);
            deletejob(jobs, pid);
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        } else if (WIFSTOPPED(status)) {
            int ss = WSTOPSIG(status);
            printjobpid_safe(jobs, pid);
            sio_puts(" stopped by signal "); sio_putl(ss); sio_puts("\n");
            Sigprocmask(SIG_SETMASK, &mask_all, &prev);
            getjobpid(jobs, pid)->state = ST;
            Sigprocmask(SIG_SETMASK, &prev, NULL);
        } else {
            sio_error("sigchld: Unknown status\n");
        }
    }

    // errno = olderrno;
    if (verbose)
        sio_puts("sigchld_handler: exiting\n");
}

/* 
 * sigint_handler - The kernel sends a SIGINT to the shell whenver the
 *    user types ctrl-c at the keyboard.  Catch it and send it along
 *    to the foreground job.  
 */
void sigint_handler(int sig) 
{
    fflush(stdout);

    if (verbose)
        sio_puts("sigint_handler: entering\n");

    /* Terminate foreground job */
    pid_t fg_pid = fgpid(jobs);
    if (fg_pid != 0) {
        if (verbose) {
            sio_puts("sigint_handler: ");
            printjobpid_safe(jobs, fg_pid);
            sio_puts(" killed\n");
        }
        Kill(-fg_pid, SIGINT);
    }

    if (verbose)
        sio_puts("sigint_handler: exiting\n");
}

/*
 * sigtstp_handler - The kernel sends a SIGTSTP to the shell whenever
 *     the user types ctrl-z at the keyboard. Catch it and suspend the
 *     foreground job by sending it a SIGTSTP.  
 */
void sigtstp_handler(int sig) 
{
    fflush(stdout);
    
    /* Suspend foreground job */
    pid_t fg_pid;
    if ((fg_pid = fgpid(jobs)) != 0)
        Kill(-fg_pid, SIGTSTP);
}

/*********************
 * End signal handlers
 *********************/

/***********************************************
 * Helper routines that manipulate the job list
 **********************************************/

/* clearjob - Clear the entries in a job struct */
void clearjob(struct job_t *job) {
    job->pid = 0;
    job->jid = 0;
    job->state = UNDEF;
    job->cmdline[0] = '\0';
}

/* initjobs - Initialize the job list */
void initjobs(struct job_t *jobs) {
    for (int i = 0; i < MAXJOBS; i++)
	    clearjob(&jobs[i]);
}

/* maxjid - Returns largest allocated job ID */
int maxjid(struct job_t *jobs) 
{
    int i, max=0;

    for (i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid > max)
            max = jobs[i].jid;
    return max;
}

/* addjob - Add a job to the job list */
int addjob(struct job_t *jobs, pid_t pid, int state, char *cmdline) 
{
    if (pid < 1)
	    return 0;

    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == 0) {
            jobs[i].pid = pid;
            jobs[i].state = state;
            jobs[i].jid = nextjid++;
            if (nextjid > MAXJOBS)
                nextjid = 1;
            strcpy(jobs[i].cmdline, cmdline);
            if (verbose) {
                // sio_puts("added job\n");
                printf("Added <%s> job [%d] %d %s\n", 
                        state == FG ? "FG" : "BG", 
                        jobs[i].jid, jobs[i].pid, jobs[i].cmdline);
                fflush(stdout);
            }
            if (state == BG) {
                printjobpid(jobs, pid);
                printf(" %s", cmdline);
            }
            return 1;
        }
    }
    sio_puts("Tried to create too many jobs\n");
    return 0;
}

/* deletejob - Delete a job whose PID=pid from the job list */
int deletejob(struct job_t *jobs, pid_t pid) 
{
    if (pid < 1)
	    return 0;

    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid == pid) {
            clearjob(&jobs[i]);
            nextjid = maxjid(jobs)+1;
            return 1;
        }
    }
    return 0;
}

/* fgpid - Return PID of current foreground job, 0 if no such job */
pid_t fgpid(struct job_t *jobs) {
    for (int i = 0; i < MAXJOBS; i++)
	    if (jobs[i].state == FG)
	        return jobs[i].pid;
    return 0;
}

/* getjobpid  - Find a job (by PID) on the job list */
struct job_t *getjobpid(struct job_t *jobs, pid_t pid) {
    if (pid < 1)
	    return NULL;
    for (int i = 0; i < MAXJOBS; i++)
	    if (jobs[i].pid == pid)
	        return &jobs[i];
    return NULL;
}

/* getjobjid  - Find a job (by JID) on the job list */
struct job_t *getjobjid(struct job_t *jobs, int jid) 
{
    if (jid < 1)
	    return NULL;
    for (int i = 0; i < MAXJOBS; i++)
        if (jobs[i].jid == jid)
            return &jobs[i];
    return NULL;
}

/* pid2jid - Map process ID to job ID */
int pid2jid(pid_t pid) 
{
    if (pid < 1)
	    return 0;
    for (int i = 0; i < MAXJOBS; i++)
	    if (jobs[i].pid == pid)
            return jobs[i].jid;
    return 0;
}

/* listjobs - Print the job list */
void listjobs(struct job_t *jobs) 
{
    for (int i = 0; i < MAXJOBS; i++) {
        if (jobs[i].pid != 0) {
            printf("[%d] (%d) ", jobs[i].jid, jobs[i].pid);
            switch (jobs[i].state) {
            case BG: 
                printf("Running ");
                break;
            case FG: 
                printf("Foreground ");
                break;
            case ST: 
                printf("Stopped ");
                break;
            default:
                printf("listjobs: Internal error: job[%d].state=%d ", 
                i, jobs[i].state);
            }
            printf("%s", jobs[i].cmdline);
        }
    }
}
/******************************
 * end job list helper routines
 ******************************/


/***********************
 * Other helper routines
 ***********************/

/*
 * usage - print a help message
 */
void usage(void) 
{
    printf("Usage: shell [-hvp]\n");
    printf("   -h   print this message\n");
    printf("   -v   print additional diagnostic information\n");
    printf("   -p   do not emit a command prompt\n");
    exit(1);
}

/*
 * sigquit_handler - The driver program can gracefully terminate the
 *    child shell by sending it a SIGQUIT signal.
 */
void sigquit_handler(int sig) 
{
    sio_puts("Terminating after receipt of SIGQUIT signal\n");
    exit(1);
}

// utility function to print job info using pid
void printjobpid_safe(struct job_t *jobs, pid_t pid)
{
    sio_puts("Job ["); sio_putl(pid2jid(pid)); sio_puts("] ");
    sio_puts("("); sio_putl(pid); sio_puts(")");
}

void printjobpid(struct job_t *jobs, pid_t pid) 
{
    printf("Job [%d] (%d)", pid2jid(pid), pid);
}
