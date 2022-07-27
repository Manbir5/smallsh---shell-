// Author: Manbir Singh
// Date: Feb 10, 2022
// Course: CS344
// Project: smallsh
// Description: This project resembles a bash shell where an user can input commands. It uses built in functions for exit
// , status, and cd. For the rest of the commands, it looks in the Path variables. It uses signal handlers to deal with 
// signals sent by ctrl + z and ctrl + c. Blank or commented lines will not generate any responses.

#include <fcntl.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h> 

// global variable needed for signal handlers
int backgroundControl = 0;

/*------------------------------------------------------------------------------------------------------------------------------------------------------
*  SECTION: TO PARSE THE COMMAND LINE INTO THE COMMAND STRUCTURE
* ------------------------------------------------------------------------------------------------------------------------------------------------------
*/

/* 
* The strucutre command where the individual parts of the command will be assigned. It also has variables that will act as boolean where
*  1 is true and 0 is false. 
*/
struct command
{
    char* statement;
    char* changeInput;
    char* changeOutput;
    int numberOfArguments;
    int validInput;
    int validOutput;
    int validBackground;
    char* arguments[513];
};

/* 
* This is a helper function that helps with variable expansion. It takes the token generated by the parsing and 
* checks if it has $$ in it. If it does have $$, it replaces it with the pid number it obtained from its 
* parameter. It uses two arrays due to the expanding memory allocation. 
*/

// Citation for the below function - line 61:
// Date: 02/06/2022
// Based on:
// Source URL: https://www.geeksforgeeks.org/strstr-in-ccpp/ and https://www.tutorialspoint.com/c_standard_library/c_function_strstr.htm

// Citation for the below function - line 71 and 77:
// Date: 02/06/2022
// Based on:
// Source URL: https://www.geeksforgeeks.org/sprintf-in-c/

// Citation for the below function - line 69 and 71:
// Date: 02/06/2022
// Based on:
// Source URL: https://www.delftstack.com/howto/c/clear-array-in-c/#:~:text=Use%20the%20memset%20Function%20to%20Clear%20Char%20Array%20in%20C,-The%20memset%20function&text=h%3E%20header%20file.,at%20the%20given%20memory%20address.

char* helperForVariableExpansion(char* token, int pid)
{
    char* bufferForExpansion;
    bufferForExpansion = (char*)malloc(300 * sizeof(char));
    memset(bufferForExpansion, 0, 300);
    char* duplicateHolder = malloc(256);
    memset(duplicateHolder, 0, 256);
    strcpy(duplicateHolder, token);
    int expansionMarker = 0;
    if (strstr(duplicateHolder, "$$") != NULL) {
        char* forLengthOfPid = malloc(22);
        sprintf(forLengthOfPid, "%d", pid);
        int lengthOfPid = strlen(forLengthOfPid);
        int secondScanner = 1;
        for (int firstScanner = 0; firstScanner < (strlen(duplicateHolder) - 1); firstScanner++) {
            if ((strncmp(&duplicateHolder[firstScanner], "$", 1) == 0) && (strncmp(&duplicateHolder[secondScanner], "$", 1) == 0)) {
                char* textBeforeExpansion = strndup(duplicateHolder, (firstScanner));
                sprintf(bufferForExpansion, "%s%d%s", textBeforeExpansion, pid, &duplicateHolder[secondScanner + 1]);
                strcpy(duplicateHolder, bufferForExpansion);
                firstScanner = firstScanner - 1 + lengthOfPid;
                secondScanner = secondScanner -1 + lengthOfPid;
                expansionMarker = 1;
            }
            secondScanner = secondScanner + 1;
        }
        free(forLengthOfPid);
    }
    free(bufferForExpansion);
    if (expansionMarker == 1) {
        return (duplicateHolder);
    }
    else {
        return (token);
    }
}


/* 
* This function takes a command line entered by the user and a parent pid. It parses the command into different tokens and
* assigns certain parts of the token to the members of this structure. Additionally, it has members that represent true 
* and false variables where 1 equals to true and 0 equals to false. The command is broken by spaces and the function watches
* for special characters such as <, >, & and assigns the token generated to certain members. 
*/
struct command* createCommand(char* currLine, int pid)
{
    struct command* currCommand = malloc(sizeof(struct command));
    // For use with strtok_r
    char* saveptr;
    char* tempToRemoveNewLine;
    int argumentCounter = 0;
    int onlyBlank;

    onlyBlank = strncmp(currLine, "\n", 1);
    if (onlyBlank == 0) {
        return NULL;
    }

    char* inputWithoutNewLine = strtok_r(currLine, "\n", &tempToRemoveNewLine);

    int commentChecker = strncmp(inputWithoutNewLine, "#", 1);
    if (commentChecker == 0) {
        return NULL;
    }

    // The first token is the command/statement
    char* token = strtok_r(inputWithoutNewLine, " ", &saveptr);
    if (token != NULL) {
        char* forExpandedform = helperForVariableExpansion(token, pid);
        currCommand->statement = calloc(strlen(forExpandedform) + 1, sizeof(char));
        strcpy(currCommand->statement, forExpandedform);
        currCommand->arguments[argumentCounter] = calloc(strlen(forExpandedform) + 1, sizeof(char));
        strcpy(currCommand->arguments[argumentCounter], forExpandedform);
    }
    else {
        return NULL;
        }
    argumentCounter = argumentCounter + 1;
    int backgroundValid = 0;
    int inputTrue = 0;
    int outputTrue = 0;
    int checkForInput = 1;
    int checkForOutput = 1;
    int checkForAmpersand = 1;
    while (token != NULL) {
        checkForInput = strncmp(saveptr, "< ", 2);
        checkForOutput = strncmp(saveptr, "> ", 2);
        checkForAmpersand = strncmp(saveptr, "&", 1);
        if (checkForInput == 0) {   // To see if the command has an input portion
            token = strtok_r(NULL, " ", &saveptr);
            token = strtok_r(NULL, " ", &saveptr);
            currCommand->changeInput = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->changeInput, token);
            inputTrue = 1;
        }
        else if (checkForOutput == 0) {    // To see if the command has an output portion
            token = strtok_r(NULL, " ", &saveptr);
            token = strtok_r(NULL, " ", &saveptr);
            currCommand->changeOutput = calloc(strlen(token) + 1, sizeof(char));
            strcpy(currCommand->changeOutput, token);
            outputTrue = 1;
        }
        else if (checkForAmpersand == 0) { // To see if the command should be run in the background or not
            token = strtok_r(NULL, "&", &saveptr);
            if (token == NULL) {
                backgroundValid = 1;
            }
            else {   // The below is just in case the & is not at the end of the program
               char* expandedForm = helperForVariableExpansion(token, pid);
               currCommand->arguments[argumentCounter] = calloc(strlen(expandedForm) + 1, sizeof(char));
               strcpy(currCommand->arguments[argumentCounter], expandedForm);
               argumentCounter = argumentCounter + 1;
                
            }
        }
        else {
            // This is for the arguments 
            token = strtok_r(NULL, " ", &saveptr);
            if (token != NULL) {
               char* expandedForm = helperForVariableExpansion(token,pid);
               currCommand->arguments[argumentCounter] = calloc(strlen(expandedForm) + 1, sizeof(char));
               strcpy(currCommand->arguments[argumentCounter], expandedForm);
               argumentCounter = argumentCounter + 1;
                }
            else {
                (*currCommand).arguments[argumentCounter + 1] = NULL; // To make sure the array ends with a null pointer
            }
              
        }
    }
    currCommand->numberOfArguments = (argumentCounter - 1);
    currCommand->validInput = inputTrue;
    currCommand->validOutput = outputTrue;
    currCommand->validBackground = backgroundValid;
    return currCommand;
    }

/*------------------------------------------------------------------------------------------------------------------------------------------------------
*  SECTION: SIGNAL HANDLERS
* ------------------------------------------------------------------------------------------------------------------------------------------------------
*/

/*
* This signal handler prevents the signal generated by ctrl+z from doing anything.
*/
void handle_SIGTSTP_When_Not_Default(int signo) {
}

/*
* This signal handler acts when it receives the signal by SIG_STP or ctrl + z.
* It uses a global variable -backgroundControl to decide what message to print. 
* It also changes -backgroundControl to it's other version (0 or 1).
*/
void handle_SIGTSTP(int signo) {
    if (backgroundControl == 0) {
        char* message = "Entering foreground-only mode (& is now ignored)\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, strlen(message));
        backgroundControl = 1;
    }
    else if (backgroundControl == 1) {
        char* message = "Exiting foreground-only mode\n";
        // We are using write rather than printf
        write(STDOUT_FILENO, message, strlen(message));
        backgroundControl = 0;
    }
}

/*
* This signal handler prevents the signal generated by ctrl+c from doing anything.
*/
void handle_SIGINT(int signo) {
}

/*
* This signal handler exits the program when it receives the signal by SIG_INT or ctrl +c.
* In this case, it exits the child process that it's part of. 
*/
void handle_SIGINT_When_Fore(int signo) {
    exit(0);
}

/*------------------------------------------------------------------------------------------------------------------------------------------------------
*  SECTION: THE SHELL
* ------------------------------------------------------------------------------------------------------------------------------------------------------
*/

/* This is the main function and it acts similar to a shell. It will run in a while loop and take input from the 
*  user. It uses a if and if else structure to go through the built in functions first (exit, status, and cd).
* Other wise, it wil run execvp on the command to get the corresponding path variable. 
*/
int main(void)
{
    char* buffer;
    int pid = getpid();
    size_t sizeOfBuffer = 2049;
    int sizeOfArrayForBackgroundPids = 0;
    int *arrayForBackgroundPids = calloc(50 , sizeof(int));
    int tempHolderForSize = 0;
    int *tempHolderForPids = calloc(50, sizeof(int));
    char enteredCommand;
    int keepRunning = 1;
    int directoryChange;
    int statusForExit = 0;
    int markerForExit;
    int signalIndicator = 0;

    // Initialize SIGINT_action struct to be empty
    struct sigaction SIGINT_action = { 0 }, SIGTSTP_action = { 0 }, ignore_action = { 0 };
    // Fill out the SIGINT_action struct
    // Register handle_SIGINT as the signal handler
    SIGINT_action.sa_handler = handle_SIGINT;
    // Block all catchable signals while handle_SIGINT is running
    sigfillset(&SIGINT_action.sa_mask);
    SIGINT_action.sa_flags = SA_RESTART;
    sigaction(SIGINT, &SIGINT_action, NULL);

    // To build the second main signal handler
    SIGTSTP_action.sa_handler = handle_SIGTSTP;
    sigfillset(&SIGTSTP_action.sa_mask);
    SIGTSTP_action.sa_flags = SA_RESTART;
    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

    buffer = (char*)malloc(sizeOfBuffer * sizeof(char));
    while (keepRunning == 1)
    {
        printf(": ");
        fflush(stdout);
        enteredCommand = getline(&buffer, &sizeOfBuffer, stdin);
        fflush(stdin);
        struct command* newCommand = createCommand(buffer, pid);
        memset(buffer, 0, sizeOfBuffer);
        if (newCommand == NULL) {
            continue;
        }
        else {
            if (strcmp(newCommand->statement, "exit") == 0) {
                markerForExit = 1;
            }
            else if (strcmp(newCommand->statement, "cd") == 0) {
                if ((newCommand -> numberOfArguments) == 0) {
                    // Change directory to home - getenv with HOME returns the path for it
                    chdir(getenv("HOME"));
                }
                else {
                    directoryChange = chdir(newCommand->arguments[1]);
                    if (directoryChange != 0) {
                        printf("That pathway does not work for this command");
                        fflush(stdout);
                        }
                }

            }
            else if (strcmp(newCommand->statement, "status") == 0) {
                if (signalIndicator == 0) {
                    printf("exit value %d\n", statusForExit);
                    fflush(stdout);
                }
                else if (signalIndicator == 1) {
                    printf("terminated by signal %d\n", statusForExit);
                }
                }

            else {
                // Based on code from exploration - module 4.
                for (int tempHolder = 0; tempHolder < tempHolderForSize; tempHolder++)
                   {
                    arrayForBackgroundPids[tempHolder] = tempHolderForPids[tempHolder];
               }
               sizeOfArrayForBackgroundPids = tempHolderForSize;
               memset(tempHolderForPids, 0, 50 * sizeof(int));
                
                int childStatus;
                // Fork a new process
                pid_t spawnPid = fork();
                switch (spawnPid) {
                case -1:
                    perror("fork()\n");
                    exit(1);
                case 0:
                    // To stop ctrl z
                    SIGTSTP_action.sa_handler = handle_SIGTSTP_When_Not_Default;
                    sigaction(SIGTSTP, &SIGTSTP_action, NULL);

                    // So that ctrl c can work here. 
                    if (newCommand->validBackground != 1) {
                        SIGINT_action.sa_handler = handle_SIGINT_When_Fore;
                        sigaction(SIGINT, &SIGINT_action, NULL);
                    }
                    if (newCommand->validInput == 1)
                    {
                        int sourceFD = open(newCommand->changeInput, O_RDONLY);
                        if (sourceFD == -1) {
                            perror("source open()");
                            exit(1);
                        }

                        // Redirect stdin to source file
                        int result = dup2(sourceFD, 0);
                        if (result == -1) {
                            perror("source dup2()");
                            exit(1);
                        }

                        fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
                    }
                    if (newCommand->validOutput == 1)
                    {
                        int targetFD = open(newCommand->changeOutput, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                        if (targetFD == -1) {
                            perror("target open()");
                            exit(1);
                        }

                        // Redirect stdout to target file
                        int secondResult = dup2(targetFD, 1);
                        if (secondResult == -1) {
                            perror("target dup2()");
                            exit(1);
                        }
                        fcntl(targetFD, F_SETFD, FD_CLOEXEC);
                    }

                    if (newCommand->validBackground == 1 && backgroundControl != 1) {
                        if (newCommand->validInput == 0)
                        {

                            int sourceFD = open("/dev/null", O_RDONLY);
                            if (sourceFD == -1) {
                                perror("source open()");
                                exit(1);
                            }

                            // Redirect stdin to source file
                            int result = dup2(sourceFD, 0);
                            if (result == -1) {
                                perror("source dup2()");
                                exit(1);
                            }

                            fcntl(sourceFD, F_SETFD, FD_CLOEXEC);
                        }

                        if ((newCommand->validOutput) == 0)
                        {
                            printf("I am here");
                            int targetFD = open("/dev/null", O_WRONLY | O_CREAT | O_TRUNC, 0644);
                            if (targetFD == -1) {
                                perror("target open()");
                                exit(1);
                            }

                            // Redirect stdout to target file
                            int secondResult = dup2(targetFD, 1);
                            if (secondResult == -1) {
                                perror("target dup2()");
                                exit(1);
                            }
                            fcntl(targetFD, F_SETFD, FD_CLOEXEC);
                        }
                    }
                    
                    if (newCommand->numberOfArguments > 0) {
                        execvp(newCommand->statement, newCommand->arguments);
                        // exec only returns if there is an error
                        perror("execvp");  
                        exit(1);
                    }
                    else {
                        execlp(newCommand->statement, newCommand->statement, NULL);
                        // exec only returns if there is an error
                        perror("execl");
                        exit(1);
                    }
                default:
                    // this is for the background tasks. The backgroundControl variable is used to determine if foreground mode is on. 
                    if ((newCommand->validBackground) == 1 && backgroundControl != 1) {
                        printf("background pid is %d\n", spawnPid);
                        arrayForBackgroundPids[sizeOfArrayForBackgroundPids] = spawnPid;
                        fflush(stdout);
                        sizeOfArrayForBackgroundPids = sizeOfArrayForBackgroundPids + 1;
                        spawnPid = waitpid(spawnPid, &childStatus, WNOHANG);
                    }
                    else {
                        // In the parent process
                        // This is for the foreground process
                        // Wait for child's terminatio
                        spawnPid = waitpid(spawnPid, &childStatus, 0);
                        // The below part is to figure out if this process was terminated by a signal or by itself.
                        if WIFSIGNALED(childStatus) {
                            printf("terminated by signal %d\n", WTERMSIG(childStatus));
                            statusForExit = WTERMSIG(childStatus);
                            signalIndicator = 1;
                        }
                        else if WIFEXITED(childStatus) {
                            statusForExit = WEXITSTATUS(childStatus);
                            if (statusForExit != 0) {
                                statusForExit = 1;
                                signalIndicator = 0;
                            }
                        }
                        else {
                            statusForExit = 1;
                            signalIndicator = 0;
                        }
                        
                    }
                }
                // The below uses two arrays to go through existing processes to see if they have terminated. It compares the id generated 
                // by waitpid and compares it to the background process' id. If it matches, then the process has terminated and a message can 
                // be printed. For the ongoing processes, the values are stored into the second array to be used again.

                // Citation for the below function - line 484 and 485:
                // Date: 02/06/2022
                // Based on:
                // Source URL: https://linux.die.net/man/2/waitpid

                int secondHolder = 0;
                for (int i = 0; i < sizeOfArrayForBackgroundPids; i++)
                {
                    spawnPid = waitpid(arrayForBackgroundPids[i], &childStatus, WNOHANG);      
                    if (spawnPid == arrayForBackgroundPids[i]){
                     if (WIFSIGNALED(childStatus)) {
                            printf("background pid %d is done: terminated by signal %d\n", arrayForBackgroundPids[i], WTERMSIG(childStatus));
                            fflush(stdout);
                     }
                    else if (WIFEXITED(childStatus)) {
                        printf("background pid %d is done: exit value %d\n", arrayForBackgroundPids[i], WEXITSTATUS(childStatus));
                        fflush(stdout);
                    }
                    }
                    else {
                        tempHolderForPids[secondHolder] = arrayForBackgroundPids[i];
                        secondHolder = secondHolder + 1;
                    }
                }   
                memset(arrayForBackgroundPids, 0, 50 * sizeof(int));
                tempHolderForSize = secondHolder;
            }

            if (markerForExit == 1) {
                for (int k = 0; k < sizeOfArrayForBackgroundPids; k++) {
                    kill(tempHolderForPids[k], SIGKILL);
                }
                keepRunning = 2;
           }
        }
        free(newCommand); 
    }
    free(tempHolderForPids); // freeing the memory allocated
    free(arrayForBackgroundPids); 
    return EXIT_SUCCESS; // exiting the program by returing this
}


// Citation for the above return value - line 515
// Date: 02/06/2022
// Based on:
// Source URL: https://iq.opengenus.org/ways-to-terminate-a-program-in-c/