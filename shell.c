/*
 * Assignment: shell
 * Author: Tygan Chin
 * Date: 2/11/2024
 * Description: Implementation of simple, interactive shell capable of 
 *              executing given programs and piping input and output between 
 *              programs using the "|" symbol. The shell can be exited using 
 *              the built-in command "exit".
 */



#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <assert.h>
#include <sys/wait.h>
#include <sys/types.h>

/* struct to hold a list of proccesses to be executed and their arguments */
struct Processes {
    char ***args;
    int size;
};
#define Prcs struct Processes

/* 
 * static vars to hold the size of the input buffer and maximum number of 
 * arguments and their lengths. 
 */
static const int BUFFER_SIZE = 2048;
static const int MAX_PROCESSES = 1000;
static const int MAX_WORDS = 500;
static const int MAX_WORD_LENGTH = 100;
static const int SIZE_OF_PIPE = 2;

/* helper functions */
static void execute(Prcs *currProcesses);
static int runChild(Prcs *currProcesses, int **pipes, int ind);
static int waitOnProcesses(int **pipes, int *pids, int size);
static void freePipesAndPids(int **pipes, int *pids, int size);
static int **makePipes(int size);
static void closeUnneededPipes(int **pipes, int size, int ind);
static void makeProcesses(Prcs *currProcesses, char input[]);
static char **makeProcess(char input[], int *ind);
static char *makeArgument(char input[], int *ind);
static bool endOfArg(char input[], int *ind);
static void freeProcesses(Prcs *currProcesses);

/*****************************************************************************
 *                              Command Loop                                 *
******************************************************************************/

/* 
 * name    : main
 * purpose : Main control loop for the shell. Prompts and executes given 
 *           commands by the user
 * input   : n/a
 * output  : 0 if no system errors
 */
int main(void)
{
    /* loop until "exit" is inputted */
    Prcs currProcesses;
    bool notExit = true;
    while (notExit) {

        /* read in prompt */
        printf("jsh$ ");
        char input[BUFFER_SIZE];
        if (fgets(input, sizeof(input), stdin) == NULL) {
            if (ferror(stdin)) {
                fprintf(stderr, "Error occured reading from stdin\n");
            }
            return 1;
        }
        makeProcesses(&currProcesses, input);
   
        /* check for exit or execute given program(s) */
        bool empty = (currProcesses.size == 0);
        if (!empty && strcmp(currProcesses.args[0][0], "exit") == 0) {
            notExit = false;
        } else if (!empty) {
            execute(&currProcesses);
        }
        freeProcesses(&currProcesses);
    }
}


/*****************************************************************************
 *                      Program Execution Functions                          *
******************************************************************************/

/* 
 * name    : execute
 * purpose : Execute the given program(s)
 * input:
 *      Args *currArgs : The current programs and arguments to be executed 
 * output  : n/a
 */
void execute(Prcs *currProcesses)
{
    /* create array of pipes and process ids */
    int **pipes = makePipes(currProcesses->size);
    int *pids = malloc(sizeof(pid_t) * currProcesses->size);
    assert(pids);

    /* run the given child programs */
    for (int i = 0; i < currProcesses->size; ++i) {
        pids[i] = fork();
        if (pids[i] == 0 && runChild(currProcesses, pipes, i) == -1) {
            freePipesAndPids(pipes, pids, currProcesses->size);
            freeProcesses(currProcesses);
            exit(127);
        } else if (pids[i] < 0) {
            fprintf(stderr, "Fork Error");
            exit(EXIT_FAILURE);
        }
    }

    /* wait for all processes to finish and print out last status */
    int status = waitOnProcesses(pipes, pids, currProcesses->size);
    freePipesAndPids(pipes, pids, currProcesses->size);
    printf("jsh status: %d\n", status);
}

/* 
 * name    : runChild
 * purpose : Execute the current program, using previous program's output as 
 *          input and writing output to pipe. 
 * input :
 *      Args *currArgs : The current programs and arguments to be executed 
 *         int **pipes : The array of pipes for each child program
 *             int ind : The current program to be run
 * output   : The function will not return if successful as exec is called. If 
 *            unsuccessful, the function will return -1.
 */
int runChild(Prcs *currProcesses, int **pipes, int ind)
{
    /* close pipes */
    closeUnneededPipes(pipes, currProcesses->size, ind);

    /* set output of previous program to input of next (if not first program) */
    if (ind != 0) {
        if (dup2(pipes[ind - 1][0], STDIN_FILENO) == -1) {
            fprintf(stderr, "IO Duplication Error");
            exit(127);
        }
    }

    /* set output of program to write to pipe if not last */
    if (ind != currProcesses->size - 1) {
        if (dup2(pipes[ind][1], STDOUT_FILENO) == -1) {
            fprintf(stderr, "IO Duplication Error");
            exit(127);
        }
    }

    /* execute the program and check for error*/
    execvp(currProcesses->args[ind][0], currProcesses->args[ind]);
    fprintf(stderr, "jsh error: Command not found: %s\n", 
                                        currProcesses->args[ind][0]);
    return -1;
}

/* 
 * name    : waitOnProcesses
 * purpose : Wait on all of the child processes and retrieve the exit status of 
 *          the last process
 * input :
 *      int **pipes : Array of pipes
 *        int *pids : Array of child proccess ID numbers
 *         int size : Number of processes
 * output  : The exit status of the final process in the array
 */
int waitOnProcesses(int **pipes, int *pids, int size)
{
    /* wait for all processes to finish and print out last status */
    int status;
    for (int i = 0; i < size; ++i) {
        /* close the current pipe */
        if (close(pipes[i][0]) == -1 || close(pipes[i][1]) == -1) {
            fprintf(stderr, "Error closing file descriptor");
            exit(EXIT_FAILURE);
        }

        /* wait on the current child process */
        if (waitpid(pids[i], &status, 0) == -1) {
            fprintf(stderr, "Error waiting for child process to run");
            exit(EXIT_FAILURE);
        }
    }
    return WEXITSTATUS(status);
}

/* 
 * name    : freePipesAndPids
 * purpose : Free the memory allocated for the array of FD's for the pipes and 
 *           the array of Pids
 * input:
 *      int **pipes : Array of pipes
 *        int *pids : Array of child proccess ID numbers
 *         int size : Number of processes
 * output  : n/a
 */
void freePipesAndPids(int **pipes, int *pids, int size)
{
    for (int i = 0; i < size; ++i) {
        free(pipes[i]);
    }
    free(pipes);
    free(pids);
}

/* 
 * name    : makePipes
 * purpose : Allocate an array of pipes, each containing an input and ouput side
 * input:
 *      int size : The number of pipes
 * output: A pointer to the array
 * note    : The returned array must be freed by the caller (freePipeAndPids())
 */
int **makePipes(int size)
{
    /* allocate 2d array of pipes */
    int **pipes = malloc(sizeof(int *) * size);
    assert(pipes);

    /* initialize each pipe with an input and output side */
    for (int i = 0; i < size; ++i) {
        int *currPipe = malloc(sizeof(int) * SIZE_OF_PIPE);
        assert(currPipe);
        pipes[i] = currPipe;
        if (pipe(pipes[i]) == -1) {
            fprintf(stderr, "Pipe Error");
            exit(EXIT_FAILURE);
        }
    }
    return pipes;
}

/* 
 * name    : closeUnneededPipes
 * purpose : Close the file descriptors of the pipes that are not needed by the 
 *          current child process
 * input:
 *      int **pipes : Array of pipes
 *         int size : Number of processes
 *          int ind : Current child process
 * output  : n/a
 */
void closeUnneededPipes(int **pipes, int size, int ind)
{
    for (int i = 0; i < size - 1; ++i) {
        if (i != ind - 1 && close(pipes[i][0])== -1) {
            fprintf(stderr, "Error closing file descriptor\n");
            exit(127);
        }
        if (i != ind && close(pipes[i][1]) == -1) {
            fprintf(stderr, "Error closing file descriptor\n");
            exit(127);
        }
    }
}

/*****************************************************************************
 *                       Argument Parser Functions                           *
******************************************************************************/
 
/* 
 * name    : makeProcesses
 * purpose : Populate the given Args struct with the string of arguments 
 *          contained in the given buffer
 * input:
 *      Args *currArgs : The current programs and arguments to be executed 
 *         char input[] : The input buffer
 *             int *ind : A pointer to the current character in the buffer
 * output  : n/a
 */
void makeProcesses(Prcs *currProcesses, char input[])
{
    /* allocate array of processes */
    currProcesses->args = malloc(sizeof(char**) * MAX_PROCESSES);
    assert(currProcesses->args);

    /* populate struct */
    int size = 0;
    int ind = 0;
    char **currArgs = makeProcess(input, &ind);
    while (currArgs[0] != NULL) {
        currProcesses->args[size] = currArgs;
        ++size;
        currArgs = makeProcess(input, &ind);
    }
    free(currArgs);

    /* set size of currProccesses */
    currProcesses->size = size;
}

/* 
 * name    : makeProcess
 * purpose : Parse current process in the given buffer. 
 * input:
 *      char input[] : The input buffer
 *          int *ind : A pointer to the current character in the buffer
 * output  : An array of c-strings representing user arguments
 * note    : The returned array must be freed
 */
char **makeProcess(char input[], int *ind)
{
    /* init char array of c-strings */
    int numWords = 0;
    char **args = malloc(sizeof(char *) * MAX_WORDS);
    assert(args);

    /* read in each word in the input into the char array*/
    while (!endOfArg(input, ind)) {

        /* skip leading white space */
        while (!endOfArg(input, ind) && input[*ind] == ' ') {
            ++(*ind);
        }

        /* add new argument to c-string array */
        if (!endOfArg(input, ind)) {
            args[numWords] = makeArgument(input, ind);
            ++numWords;
        }     
    }
    args[numWords] = NULL;

    /* iterate index if pointing to a pipe */
    if (*ind < BUFFER_SIZE && input[*ind] == '|') {
        ++(*ind);
    }

    return args;
}

/* 
 * name    : makeArgument
 * purpose : Reads in a single argument from the input buffer and returns a 
 *          c-string of the input
 * input:
 *      char input[] : The input buffer
 *          int *ind : A pointer to the current character in the buffer
 * output  : A pointer to a char array of a word 
 * note    : The caller must free the returned char array
 */
char *makeArgument(char input[], int *ind)
{
    /* init c-string */
    char *arg = malloc(MAX_WORD_LENGTH);
    assert(arg);

    /* add each letter of the argument to the string */
    int argInd = 0;
    while (!endOfArg(input, ind) && input[*ind] != ' ') {
        arg[argInd] = input[*ind];
        ++argInd, ++(*ind);
    }
    arg[argInd] = '\0';

    return arg;
}

/* 
 * name    : endOfArg
 * purpose : Determines if the the given index is at the end of the current 
 *          argument
 * input:
 *      char input[] : An array of characters representing user input
 *          int *ind : A pointer to the index of the current character
 * output  : True if the index is pointing to the end of an arguemnt, false 
 *           otherwise
 */
bool endOfArg(char input[], int *ind) 
{
    return (*ind >= BUFFER_SIZE || input[*ind] == '\n' 
            || input[*ind] == EOF || input[*ind] == '|');
}

/* 
 * name    : freeProcesses
 * purpose : Free an array of given c-strings 
 * input:
 *      Args *currArgs : The current programs and arguments to be executed 
 * output  : n/a
 * effects : The memory associated with the array is freed
 */
void freeProcesses(Prcs *currProcesses)
{
    /* free the the c-strings contained in the array */
    for (int i = 0; i < currProcesses->size; ++i) {
        int ind = 0;
        while (currProcesses->args[i][ind] != NULL) {
            free(currProcesses->args[i][ind]);
            ++ind;
        }
        free(currProcesses->args[i]);
    }
    free(currProcesses->args);

    /* clear the member variables */
    currProcesses->args = NULL;
    currProcesses->size = 0;
}