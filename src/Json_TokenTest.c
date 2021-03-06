#include <ctype.h>
#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <string.h>

#include "Json.h"
#include "JsonEx.h"

static jmp_buf jmpenv;

static void _sighandler(int sig)
{
    if (sig == SIGINT)
    {
        printf("\n");
        printf("Type '.exit' to exit\n");
        longjmp(jmpenv, 1);
    }
}

static char* strtrim_fast(char* str)
{
    while (isspace(*str))
    {
        str++;
    }

    char* ptr = str;
    while (*ptr > 0)      { ptr++; }; ptr--;
    while (isspace(*ptr)) { ptr--; }; ptr[1] = 0;

    return str;
}

int main(int argc, char* argv[])
{
    signal(SIGINT, _sighandler);
    
    printf("JSON token testing prompt\n");
    printf("Type '.exit' to exit\n");
    
    char input[1024];
    while (1)
    {
	    if (setjmp(jmpenv) == 0)
	    {
	        printf("> ");
	        fgets(input, sizeof(input), stdin);

	        const char* json = strtrim_fast(input);
	        if (strcmp(json, ".exit") == 0)
	        {
                break;
	        }
	        else
            {
                Json* value = Json_parse(json, strlen(json), JsonFlags_None);
                if (Json_getError(value) != JsonError_None)
                {
                    value = NULL;
                    printf("[ERROR]: %s\n", Json_getErrorMessage(value));
                }
                else
                {
                    Json_print(value, stdout); printf("\n");
                }
                Json_release(value);
	        }
	    }
    }

    /* JsonRelease(NULL) for release all memory, if there is leak */
    Json_release(NULL);
    
    return 0;
}
