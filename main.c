#include <stdio.h>
#include <ctype.h>

#define BUFFER_SIZE 80

char buffer[BUFFER_SIZE];
char ch;
int charCount;
int exitFlag = 0;
int validChoice;

int int main(int argc, char const *argv[])
{
	while(exitFlag == 0){
	printf("$ ");
	ch = getchar();
	charCount = 0;
	while((ch != "\n") && (charCount < BUFFER_SIZE)) {
		buffer[charCount++] = ch;
		ch = getchar();
		putchar(buffer[charCount]);
	}


//	buffer[charCount] = 0x00;
//	printf("%s ", buffer);

	validChoice = 0;
	while(validChoice == 0) {
		/*



			EXECUTE COMMANDS HERE


		*/
	}
}
	return 0;
}

