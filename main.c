//#include <stdio.h>
/* example one, to read a word at a time */
#include <stdio.h>
#include <ctype.h>
#include <stdbool.h>
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
    return (strcmp(userInput, command) == 0 ? true : false);
}

int main()
{
    char    ch;                     /* handles user input */
    char    buffer[MAXBUFFERSIZE];  /* sufficient to handle one line */
    int     char_count;             /* number of characters read for this line */
    int     exit_flag = 0;
    int     valid_choice;
    char*   token;
    char*   tokens[3];

    while( exit_flag  == 0 ) {
        printf("Enter a line of text (<80 chars)\n");
        ch = getchar();
        char_count = 0;
        while( (ch != '\n')  &&  (char_count < MAXBUFFERSIZE)) {
            buffer[char_count++] = ch;
            ch = getchar();
        }
        buffer[char_count] = 0x00;      /* null terminate buffer */
//        printf("\nThe line you entered was:\n");
//        printf("%s\n", buffer);

        //tokenize user input
        token = strtok(buffer, " ");
        tokens[0] = token;

        int tokenIndex = 1;
        while(token != NULL) {
//            printf("token: %s\n", token[tokenIndex]);
            token = strtok(NULL, " ");
            if(token != NULL){
                tokens[tokenIndex++] = token;
            }
        }
//        printf("%d\n",isValidCommand(tokens[0], "initfs"));

        valid_choice = 0;
        while( valid_choice == 0 ) {
            printf("Continue (Y/N)?\n");
            scanf(" %c", &ch );
            ch = toupper( ch );
            if((ch == 'Y') || (ch == 'N') )
                valid_choice = 1;
            else
                printf("\007Error: Invalid choice\n");
            cleartoendofline();
        }
        if( ch == 'N' ) exit_flag = 1;
    }
}