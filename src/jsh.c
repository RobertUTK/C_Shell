#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include "fields.h"
#include "dllist.h"
#include "jrb.h"

static char* makePrompt(char**, int);
static int findAndOpenFiles(char **, int, int*);
static int findPipes(IS, int*);

int
main(int argc, char **argv)
{
	char *prompt, **argBegin;
	int pid, status, noWait, fds[2], found;
	int numPipes, pipeInd[50], i, numArgs;
	int waitrtn, prev_fd = -1, pipefd[2];
	IS is;
	JRB forks, tmp;

	if(argc > 2){
		fprintf(stderr, "usage: %s [prompt]\n", argv[0]);
		exit(1);
	}

	prompt = makePrompt(argv, argc);
	
	is = new_inputstruct(NULL);
	forks = make_jrb();

	printf("%s", prompt);
	while(get_line(is) >= 0){
		prev_fd = -1;
		noWait = 0;

		if(is->NF > 0){	
			numPipes = findPipes(is, pipeInd);
			
			if(strcmp(is->fields[is->NF-1], "&") == 0){
				is->NF--;
				is->fields[is->NF] = NULL;
				noWait = 1;
			}
			else is->fields[is->NF] = NULL;
			
			for(i = 0; i <= numPipes; i++){
				if(numPipes > 0){	
					if(pipe(pipefd) < 0){
						perror("jsh: pipe");
						exit(1);
					}
				}
				
				//Calculates the number of args for each pipe section
				if(i == 0 && numPipes > 0)	numArgs = pipeInd[i];
				else if(i != numPipes)		numArgs = pipeInd[i] - pipeInd[i-1] - 1;
				else if(numPipes > 0)		numArgs = is->NF - pipeInd[i-1] - 1;
				else						numArgs = is->NF;

				//Finds <, >, or >> and opens the files
				if(numPipes > 0 && i != 0) argBegin = is->fields+pipeInd[i-1]+1;
				else					   argBegin = is->fields;
				
				found = findAndOpenFiles(argBegin, numArgs, fds);
				//Sets <, >, or >> to NULL
				if(found) argBegin[found] = NULL;
				else	  argBegin[numArgs] = NULL;

				
				pid = fork();
				if(numPipes > 0) 
					jrb_insert_int(forks, pid, new_jval_v(NULL));
				if(pid == 0) {
					if(numPipes > 0){
						if(i == 0){
							if(dup2(pipefd[1], 1) < 0){
								perror("jsh: dup");
								exit(1);
							}
							close(pipefd[1]);
							close(pipefd[0]);
						}
						else if(i != 0 && i != numPipes){
							if(dup2(prev_fd, 0) < 0){
								perror("jsh: dup");
								exit(1);
							}
							if(dup2(pipefd[1], 1) < 0){
								perror("jsh: dup");
								exit(1);
							}
						}
						else if(i == numPipes){
							if(dup2(prev_fd, 0) < 0){
								perror("jsh: dup");
								exit(1);
							}
							close(prev_fd);
							close(pipefd[0]);
							close(pipefd[1]);
						}
					}
					
					if(fds[0] != 0){ 
						if(dup2(fds[0], 0) < 0){
							perror("jsh: fileoutdup");
							exit(1);
						}
						close(fds[0]);
					}
					
					if(fds[1] != 1){ 
						if(dup2(fds[1], 1) < 0){
							perror("jsh: fileindup");
							exit(1);
						}
						close(fds[1]);
					}
					execvp(argBegin[0], argBegin);
					perror(argBegin[0]);
					exit(1);
				}	
				else{
					if(i != 0) close(prev_fd);
					prev_fd = pipefd[0];
					close(pipefd[1]);
					if(numPipes == i) close(prev_fd);
					if(fds[0] != 0) close(fds[0]);
					if(fds[1] != 1) close(fds[1]);

					if(!noWait && numPipes == 0){
						while(pid != wait(&status));
					}
				}
			}
			while(!noWait && !jrb_empty(forks)){	
				waitrtn = wait(&status);
				tmp = jrb_find_int(forks, waitrtn);
				if(tmp != NULL){
					jrb_delete_node(tmp);
				}
				else break;
			}
			printf("%s", prompt);
		}
	}
	jrb_free_tree(forks);
	jettison_inputstruct(is);
	free(prompt);
	return 0;
}

/*makePrompt: allocates, sets, and returns the prompt string for the shell
 *Params: args, the argv arguments from main.
 *		  num, the number of arguments
 *Post-Con: memory will be allocated for the prompt
 *Returns: prompt, a cstring with the shell prompt
 */
static char*
makePrompt(char **args, int num)
{
	char *prompt;
	int len;

	if(num == 2){
		if(strcmp(args[1], "-") == 0){
			len = 1;
			prompt = (char*) malloc(sizeof(char)*len);
			prompt[0] = '\0';
		}
		else {
			len = strlen(args[1]) + 1;
			prompt = (char*) malloc(sizeof(char)*len);
			memcpy(prompt, args[1], len);
		}
	}
	else{
		len = 5;
		prompt = (char*) malloc(sizeof(char)*len);
		memcpy(prompt, "jsh:", len);
	}
	return prompt;
}

/*findAndOpenFiles: Checks the input struct for the >, >>, and < characters and
 *                  opens the corresponing files appropriately
 *Params: is, the input struct
		: fds, the array to hold file descriptors
 *Post-Cons: the appropriate file will be open and fds will contain the file descriptors for the open files
 *Returns; found, an integer where it found the first >, >>, or < character
 */
static int
findAndOpenFiles(char** fields, int numArgs, int *fds)
{
	int i, found;
	
	fds[0] = 0;
	fds[1] = 1;
	found = 0;
	
	for(i = 0; i < numArgs; i++)
	{
		if(strcmp(fields[i], ">") == 0){
			fds[1] = open(fields[i+1], O_WRONLY | O_CREAT, 0644);
			if(fds[1] < 0){
				perror("jsh:");
				exit(1);
			}
			if(found == 0) found = i;
			i++;
		}
		else if(strcmp(fields[i], ">>") == 0){
			fds[1] = open(fields[i+1], O_WRONLY | O_CREAT | O_APPEND, 0644);
			if(fds[1] < 0){
				perror("jsh:");
				exit(1);
			}
			if(found == 0) found = i;
			i++;
		}
		else if(strcmp(fields[i], "<") == 0){
			fds[0] = open(fields[i+1], O_RDONLY);
			if(fds[0] < 0){
				perror("jsh:");
				exit(1);
			}
			if(found == 0) found = i;
			i++;
		}
	}
	return found;
}

/*findPipes: fill an array of integers with the pipe location indices
 *Params: is, an input struct with the data
 *		indices, the array for the pipe locations
 *Post-Con: indices will have the pipe locations
 *Returns: the number of pipes
 */
static int
findPipes(IS is, int *indices)
{
	int i, count = 0;

	for(i = 0; i < is->NF; i++){
		if(strcmp(is->fields[i], "|") == 0){
			indices[count] = i;
			count++;
		}
	}
	return count;
}
