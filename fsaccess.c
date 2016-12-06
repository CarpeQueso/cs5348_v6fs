#include <stdint.h>
#include <stdio.h>
#include "v6fs.h"
#include <ctype.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define MAXBUFFERSIZE   80

void cleartoendofline( void );  /* ANSI function prototype */
bool isValidCommand(char* userInput, char* command);

void cleartoendofline( void )
{
    char ch;
    ch = getchar();
    while( ch != '\n' )
        ch = getchar();
}

/* Fucntion to check if entered command is valid */
bool isValidCommand(char* userInput, char* command) {
    return (strcmp(userInput, command) == 0);
}

int main(int argc, char *argv[])
{
    char    ch;                     /* handles user input */
    char    buffer[MAXBUFFERSIZE];  /* sufficient to handle one line */
    int     char_count;             /* number of characters read for this line */
    int     exit_flag = 0;
    int     valid_choice;
    char*   token;
    char*   tokens[3];
    Superblock *sb;

    //Load the filesystem
    sb = v6_loadfs(argv[1]);
    //sb = v6_loadfs("/Users/jon/UTD/CS5348/Project_2/v6fs/test.v6fs");

    while( exit_flag  == 0 ) {
        printf("%s", "v6fs: \n");
        ch = getchar();
        char_count = 0;
        while( (ch != '\n')  &&  (char_count < MAXBUFFERSIZE)) {
            buffer[char_count++] = ch;
            ch = getchar();
        }
        buffer[char_count] = 0x00;      /* null terminate buffer */

        //tokenize user input
        token = strtok(buffer, " ");
        tokens[0] = token;

        // Convert the command call to lower case
        char* token1 = tokens[0];
        const int length = strlen(tokens[0]);
        char* lower = (char*)malloc(length + 1);
        lower[length] = 0;

        for (int i = 0; i < length; i++){
            lower[i] = tolower(token1[i]);
        }
        tokens[0] = lower;

        // Pass remainder of command to token array
        int tokenIndex = 1;
        while(token != NULL) {
            token = strtok(NULL, " ");
            if(token != NULL){
                tokens[tokenIndex++] = token;
            }
        }

        // Command execution
        if (isValidCommand(tokens[0], "initfs")){
            __uint32_t numBlocks = atoi(tokens[1]);
            __uint32_t numInodes = atoi(tokens[2]);
            sb = v6_initfs(numBlocks, numInodes);
        }

        if (isValidCommand(tokens[0], "cpin")){
            int8_t cpinResult = v6_cpin(sb, tokens[1], tokens[2]);
            printf("Res: %d\n", cpinResult);
        }

        if (isValidCommand(tokens[0], "cpout")){
            v6_cpout(sb, tokens[1], tokens[2]);
        }

        if (isValidCommand(tokens[0], "mkdir")){
            v6_mkdir(sb, tokens[1]);
        }

        if (isValidCommand(tokens[0], "rm")){
            v6_rm(sb, tokens[1]);
        }

        if (isValidCommand(tokens[0], "q")){
            v6_quit(sb);
            break;
        }

//        valid_choice = 0;
//        while( valid_choice == 0 ) {
//            printf("Continue (Y/N)?\n");
//            scanf(" %c", &ch );
//            ch = toupper( ch );
//            if((ch == 'Y') || (ch == 'N') )
//                valid_choice = 1;
//            else
//                printf("\007Error: Invalid choice\n");
//            cleartoendofline();
//        }
//        if( ch == 'N' ) exit_flag = 1;
    }
}
