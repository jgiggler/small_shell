#define _POSIX_C_SOURCE 200809L
#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <err.h>
#include <errno.h>
#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h> 
#include <signal.h>
#include <sys/signal.h>
#include <stdint.h>
#ifndef MAX_WORDS
#define MAX_WORDS 512
#endif


char *words[MAX_WORDS];
size_t wordsplit(char const *line);
char * expand(char const *word, int prevstatus, int bgpid);
void handle_SIGINT(int signo){};
void handle_SIGTSTP(int signo){};

int main(int argc, char *argv[])
{ 
  struct sigaction 
  SIGINT_action = {0}, 
  SIGTSTP_ignore = {0}, 
  SIGINT_ignore = {0}, 
  SIGINT_oldact = {0}, 
  SIGTSTP_oldact = {0};
  //SIGINT ACTION
  SIGINT_action.sa_handler = handle_SIGINT;
  sigfillset(&SIGINT_action.sa_mask);
  SIGINT_action.sa_flags = 0;
  //SIGINT/SIGTSTP IGNORES
  SIGINT_ignore.sa_handler = SIG_IGN;
  SIGTSTP_ignore.sa_handler = SIG_IGN;

  sigaction(SIGINT, &SIGINT_action, &SIGINT_oldact);
  sigaction(SIGTSTP, &SIGTSTP_ignore, &SIGTSTP_oldact);  
  FILE *input = stdin;
  char *input_fn = "(stdin)";
  if (argc == 2) {
    input_fn = argv[1];
    input = fopen(input_fn, "re");
    if (!input) err(1, "%s", input_fn);
  } else if (argc > 2) {
    errx(1, "too many arguments");
  }
  
  int exitstatus = 0;
  pid_t bgpid = 0;
  pid_t prevpid = 0;
  int sig;
  int prevstatus = 0;
  int bgstatus = 0;
  char *line = NULL;
  size_t n = 0;
  for (;;) {
prompt:
    /* TODO: Manage background processes */
    
    if (waitpid(-1, &bgstatus, WNOHANG | WUNTRACED) > 0){
        if (WIFEXITED(bgstatus)){
          bgstatus = WEXITSTATUS(bgstatus);
          fprintf(stderr, "Child process %jd done. Exit status %d.\n", (intmax_t) bgpid, bgstatus);
          }
        else if (WIFSIGNALED(bgstatus)){
          sig = WTERMSIG(bgstatus);
          fprintf(stderr, "Child process %jd done. Signaled %d.\n", (intmax_t) bgpid, sig);
          
          }
        else if (WIFSTOPPED(bgstatus)){
          fprintf(stderr, "Child process %jd stopped. Continuing.\n", (intmax_t) bgpid);
        }
        return 0;
    }
    /* TODO: prompt */
    sigaction(SIGINT, &SIGINT_action, NULL);
    if (input == stdin) {
      if (getenv("PS1") != NULL){
        char* ps1 = getenv("PS1");
        fprintf(stderr, "%s", ps1);
    }
    }
    
    ssize_t line_len = getline(&line, &n, input);
    sigaction(SIGINT, &SIGINT_ignore, NULL);
    if (strcmp(line, "\n") == 0){
      goto prompt;
    }
    if (line_len == -1){
      int error = errno;
      if (error == EINTR){
        errno = 0;
        clearerr(input);
        fprintf(stderr, "\n");
        goto prompt;
      }
    }
    if (feof(input)){
      
      return 0;
    }
    if (line_len < 0) err(1, "%s", input_fn);
    size_t nwords = wordsplit(line);
    //HANDLE expansion for $$, $?, $!, ${para}
    for (size_t i = 0; i < nwords; ++i) {
        if (strchr(words[i], '$') != NULL){
            char *exp_word = expand(words[i], prevstatus, bgpid);
            free(words[i]);
            words[i] = exp_word;
          }
    }
    // HANDLE exit and cd commands
    if (strcmp(words[0], "exit") == 0){
      if (words[1] != NULL){
      prevstatus = atoi(words[1]);
      exit(prevstatus);
      } else {
        prevstatus = 0;
        exit(prevstatus);
      }
      
    } else if (strcmp(words[0], "cd") == 0) {
      if (nwords == 2) {
        for (size_t i = 0; i < nwords; ++i) {
            char *exp_word = expand(words[i], prevstatus, bgpid);
            free(words[i]);
            words[i] = exp_word;
          }
        // printf("%s\n", words[1]);
        if (chdir(words[1]) == 0){
          chdir(words[1]);
        } else if (nwords > 2){
          perror("Error: cd has more than 1 argument\n");
          goto prompt;
        }
        else {
          perror("Error: No such file or directory with that name.\n");
          goto prompt;
        }
        
      } else {
        chdir(getenv("HOME"));
      }
    
    //HANDLE Non built in commands
    } else {
      
      char* command = words[0];
      char* args_list[20] = {NULL};
      int args_count = 0;
      
      pid_t spawnpid = -5;
      spawnpid = fork();
      
      switch (spawnpid){
        case -1:
          fprintf(stderr, "fork() failed!\n");
          exit(1);
          break;
        case 0:
          // printf("forked\n");
          sigaction(SIGINT, &SIGINT_oldact, NULL);
          sigaction(SIGTSTP, &SIGTSTP_oldact, NULL);
          
          //HANDLE REDIRECTION
          for (size_t i = 0; i < nwords; ++i){
            if (strcmp(words[i], "<") == 0){   
              /*open file for reading on stdin*/
              int file = open(words[i+1], O_RDONLY);
              if (file == -1){
                perror("Could not open file for reading");
              } else {
                dup2(file, 0);
                
              }
            }
            if (strcmp(words[i], ">") == 0){
              /* open file for writing on stdout*/
              int file = open(words[i+1], O_CREAT | O_TRUNC | O_WRONLY);
              if (chmod(words[i+1], 0777) < 0)
                perror("error in chmod");
              if (file == -1){
                perror("Could not open file for writing");
              } else {
                dup2(file, 1);
                
              }
            }
            if (strcmp(words[i], ">>") == 0){
              /* open file for appending on stdout*/
              int file = open(words[i+1], O_APPEND | O_CREAT | O_WRONLY);
              if (chmod(words[i+1], 0777) < 0)
                perror("error in chmod");
              if (file == -1){
                perror("Could not open file for writing");
              } else {
                dup2(file, 1);
                }
              }
          }
            //add args for command
            for (size_t i = 0; i < nwords; ++i){
              if (strcmp(words[i], ">") == 0
               || strcmp(words[i], ">>") == 0 
               || strcmp(words[i], "<") == 0
               || strcmp(words[i], "&") == 0){
                break;
                }
              else if (strcmp(words[i], ">") != 0
                && strcmp(words[i], ">>") != 0 
                && strcmp(words[i], "<") !=0){
                args_list[args_count] = words[i];
                args_count++;
               }      
            }
            
            int status = execvp(command, args_list);
            if (status == -1){
              fprintf(stderr, "Command doesnt exist. Error: %d\n", errno);
              exit(1);
            } 
          

        default:
          //if BG
          if (strcmp(words[nwords-1], "&") == 0){
            bgpid = spawnpid;
            goto prompt;
          }
          //if FG
          if (strcmp(words[nwords-1], "&") != 0){
            waitpid(spawnpid, &prevstatus, WUNTRACED);
            if (WIFSTOPPED(prevstatus)){
              fprintf(stderr, "Child process %d stopped. Continuing.\n", spawnpid);
              bgpid = spawnpid;
            }
            if (WIFEXITED(prevstatus)){
              prevstatus = WEXITSTATUS(prevstatus);
            }
            else if (WIFSIGNALED(prevstatus)){
              prevstatus = 128 + WTERMSIG(prevstatus);
            }
          }
          
      }

      
    }
  }
}

char *words[MAX_WORDS] = {0};


/* Splits a string into words delimited by whitespace. Recognizes
 * comments as '#' at the beginning of a word, and backslash escapes.
 *
 * Returns number of words parsed, and updates the words[] array
 * with pointers to the words, each as an allocated string.
 */
size_t wordsplit(char const *line) {
  size_t wlen = 0;
  size_t wind = 0;

  char const *c = line;
  for (;*c && isspace(*c); ++c); /* discard leading space */

  for (; *c;) {
    if (wind == MAX_WORDS) break;
    /* read a word */
    if (*c == '#') break;
    for (;*c && !isspace(*c); ++c) {
      if (*c == '\\') ++c;
      void *tmp = realloc(words[wind], sizeof **words * (wlen + 2));
      if (!tmp) err(1, "realloc");
      words[wind] = tmp;
      words[wind][wlen++] = *c; 
      words[wind][wlen] = '\0';
    }
    
    ++wind;
    wlen = 0;
    for (;*c && isspace(*c); ++c);
  }
  return wind;
}


/* Find next instance of a parameter within a word. Sets
 * start and end pointers to the start and end of the parameter
 * token.
 */
char
param_scan(char const *word, char const **start, char const **end)
{
  static char const *prev;
  if (!word) word = prev;
  
  char ret = 0;
  *start = 0;
  *end = 0;
  for (char const *s = word; *s && !ret; ++s) {
    s = strchr(s, '$');
    if (!s) break;
    switch (s[1]) {
    case '$':
    case '!':
    case '?':
      ret = s[1];
      *start = s;
      *end = s + 2;
      break;
    case '{':;
      char *e = strchr(s + 2, '}');
      if (e) {
        ret = s[1];
        *start = s;
        *end = e + 1;
      }
      break;
    }
  }
  prev = *end;
  return ret;
}

/* Simple string-builder function. Builds up a base
 * string by appending supplied strings/character ranges
 * to it.
 */
char *
build_str(char const *start, char const *end)
{
  static size_t base_len = 0;
  static char *base = 0;

  if (!start) {
    /* Reset; new base string, return old one */
    char *ret = base;
    base = NULL;
    base_len = 0;
    return ret;
  }
  /* Append [start, end) to base string 
   * If end is NULL, append whole start string to base string.
   * Returns a newly allocated string that the caller must free.
   */
  size_t n = end ? end - start : strlen(start);
  size_t newsize = sizeof *base *(base_len + n + 1);
  void *tmp = realloc(base, newsize);
  if (!tmp) err(1, "realloc");
  base = tmp;
  memcpy(base + base_len, start, n);
  base_len += n;
  base[base_len] = '\0';

  return base;
}

/* Expands all instances of $! $$ $? and ${param} in a string 
 * Returns a newly allocated string that the caller must free
 */
char *
expand(char const *word, int prevstatus, int bgpid)
{
  char const *pos = word;
  char const *start, *end;
  char c = param_scan(pos, &start, &end);
  build_str(NULL, NULL);
  build_str(pos, start);
  while (c) {
    if (c == '!'){
      //after waiting
      char bg_pid[6];
      sprintf(bg_pid, "%d", bgpid);
      build_str(bg_pid, NULL);
    } 
    
    else if (c == '$'){
      int pid = getpid();
      char mypid[6];
      sprintf(mypid, "%d", pid);
      build_str(mypid, NULL);
      }
    else if (c == '?'){
      //after waiting     
      char* status;
      asprintf(&status, "%d", prevstatus);
      build_str(status, NULL);
    } 
    else if (c == '{') {
      char* param = build_str(start + 2, end - 1);
      build_str(NULL, NULL);
      char* para = getenv(param);
      if (para == NULL){
        para = "";
      }
      build_str(para, NULL);
    }
    pos = end;
    c = param_scan(pos, &start, &end);
    build_str(pos, start);
  }
  return build_str(start, NULL);
}
