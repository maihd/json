# Simple JSON parser for ANSI C

## API
```C
json_value_t* json_parse(const char* json_code, json_state_t** out_state);
// 1. json_code: the JSON content from JSON's source (from memory, from file)
// 2. out_state: the JSON state for parsing json code, contain usage memory (can be NULL, library will hold it)

void json_release(json_state_t* state);
// Release state, when state is null, library will implicit remove states that it hold

// Find more details, or helper functions in json.h
```

## Examples
Belove code from json_test.c
```C
#ifdef _MSC_VER
#  ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS
#  endif
#endif

#include <signal.h>
#include <setjmp.h>

#define JSON_IMPL
#include "json.h"

static jmp_buf jmpenv;

void _sighandler(int sig)
{
    if (sig == SIGINT)
    {
	printf("\n");
	printf("Type '.exit' to exit\n");
	longjmp(jmpenv, 1);
    }
}

char* strtrim_fast(char* str)
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
                json_state_t* state;
                json_value_t* value = json_parse(json, &state);
	    
                if (json_get_errno(state) != JSON_ERROR_NONE)
                {
                    value = NULL;
                    printf("[ERROR]: %s\n", json_get_error(state));
                }
                else
                {
                    json_display(value, stdout); printf("\n");
                }
	    
                /* json_release(NULL) for release all memory if you don't catch the json_state_t */
                json_release(state);
	        }
	    }
    }
    
    return 0;
}
```

## Metadata
1. Author: MaiHD
2. Tools : Emacs, VSCode, Visual Studio 2017 Community