#ifndef __JSON_H__
#define __JSON_H__

#ifndef JSON_API
#define JSON_API
#endif

#ifdef __cplusplus
#include <string.h>
extern "C" {
#endif

#include <stdio.h>

typedef enum
{
    JSON_NONE,
    JSON_NULL,
    JSON_ARRAY,
    JSON_OBJECT,
    JSON_NUMBER,
    JSON_STRING,
    JSON_BOOLEAN,
} json_type_t;

typedef enum
{
    JSON_ERROR_NONE,
    JSON_ERROR_MEMORY,
    JSON_ERROR_UNMATCH,
    JSON_ERROR_UNKNOWN,
    JSON_ERROR_UNEXPECTED,
} json_error_t;

typedef enum
{
	JSON_TRUE  = 1,
	JSON_FALSE = 0,
} json_bool_t;

typedef struct json_value
{
    json_type_t type;
    union
    {
		double      number;
		json_bool_t boolean;

		struct
		{
			int   length;
			char* buffer;
		} string;

		struct
		{
			int length;
			struct
			{
				struct json_value* name;
				struct json_value* value;
			}  *values;
		} object;

		struct
		{
			int                 length;
			struct json_value** values;
		} array;
    };

    #ifdef __cplusplus
public:
    inline json_value()
		: parent(0)
		, type(JSON_NONE)
		, boolean(JSON_FALSE)
	{	
	}

	inline const json_value& operator[] (int index) const
	{
		if (type != JSON_ARRAY || index < 0 || index > array.length)
		{
			return JSON_VALUE_NONE;
		}
		else
		{
			return *array.values[index];
		}	
	}

	inline const json_value& operator[] (const char* name) const
	{
		if (type != JSON_OBJECT)
		{
			return JSON_VALUE_NONE;
		}
		else
		{
			for (int i = 0, n = object.length; i < n; i++)
			{
				if (strcmp(object.values[i].name, name) == 0)
				{
					return *object.values[i].value;
				}
			}

			return JSON_VALUE_NONE;
		}	
	}

	inline operator const char* () const
	{
		if (type == JSON_STRING)
		{
			return string.buffer;
		}
		else
		{
			return "";
		}
	}

	inline operator double () const
	{
		return number;
	}

	inline operator bool () const
	{
		return !!boolean;
	}
    #endif /* __cplusplus */
} json_value_t;
    
static const json_value_t JSON_VALUE_NONE; /* auto fill with zero */

typedef struct json_state json_state_t;

JSON_API json_value_t* json_parse(const char* json, json_state_t** state);
JSON_API void          json_release(json_state_t* state);
JSON_API json_error_t  json_get_errno(const json_state_t* state);
JSON_API const char*   json_get_error(const json_state_t* state);
 
JSON_API void          json_print(const json_value_t* value, FILE* out);
JSON_API void          json_display(const json_value_t* value, FILE* out);

#ifdef __cplusplus
}
#endif
    
#endif /* __JSON_H__ */

#ifdef JSON_IMPL

#ifdef _MSC_VER
#  ifndef _CRT_SECURE_NO_WARNINGS
#  define _CRT_SECURE_NO_WARNINGS
#  endif
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <setjmp.h>

typedef struct json_pool
{
    struct json_pool* next;

    void** head;
} json_pool_t;

typedef struct json_bucket
{
    struct json_bucket* next;

    int size;
    int count;
    int capacity;
} json_bucket_t;

struct json_state
{
    struct json_state* next;
    json_pool_t* value_pool;

    json_bucket_t* values_bucket;
    json_bucket_t* string_bucket;
    
    int line;
    int column;
    int cursor;
    
    const char* buffer;
    
    json_error_t errnum;
    char*        errmsg;
    jmp_buf      errjmp;
};

static json_state_t* root_state = NULL;

#if __GNUC__
__attribute__((noreturn))
#elif defined(_MSC_VER)
__declspec(noreturn)
#endif
static void croak(json_state_t* state, json_error_t code, const char* fmt, ...)
{
    const int errmsg_size = 1024;
    
    if (state->errmsg == NULL)
    {
		state->errmsg = malloc(errmsg_size);
    }

    state->errnum = code;

    va_list varg;
    va_start(varg, fmt);
    sprintf(state->errmsg, fmt, varg);
    va_end(varg);
    
    longjmp(state->errjmp, code);
}

static json_pool_t* make_pool(json_pool_t* next, int count, int size)
{
    if (count <= 0 || size <= 0)
    {
		return NULL;
    }

    int pool_size = count * (sizeof(void*) + size);
    json_pool_t* pool = (json_pool_t*)malloc(sizeof(json_pool_t) + pool_size);
    if (pool)
    {
		pool->next = next;
		pool->head = (void**)((char*)pool + sizeof(json_pool_t));
		
		int i;
		void** node = pool->head;
		for (i = 0; i < count - 1; i++)
		{
			node = (void**)(*node = (char*)node + sizeof(void*) + size);
		}
		*node = NULL;
    }
    
    return pool;
}

static void free_pool(json_pool_t* pool)
{
    if (pool)
    {
		free_pool(pool->next);
		free(pool);
    }
}

static void* pool_extract(json_pool_t* pool)
{
    if (pool->head)
    {
		void** head = pool->head;
		void** next = (void**)(*head);
		
		pool->head = next;
		return (void*)((char*)head + sizeof(void*));
    }
    else
    {
		return NULL;
    }
}

static void pool_collect(json_pool_t* pool, void* ptr)
{
    if (ptr)
    {
		void** node = (void**)((char*)ptr - sizeof(void*));
		*node = pool->head;
		pool->head = node;
    }
}

static json_bucket_t* make_bucket(json_bucket_t* next, int count, int size)
{
    if (count <= 0 || size <= 0)
    {
        return 0;
    }

    json_bucket_t* bucket = (json_bucket_t*)malloc(sizeof(json_bucket_t) + count * size);
    if (bucket)
    {
        bucket->next     = next;
        bucket->size     = size;
        bucket->count    = 0;
        bucket->capacity = count;
    }
    return bucket;
}

static void free_bucket(json_bucket_t* bucket)
{
    if (bucket)
    {
        free_bucket(bucket->next);
        free(bucket);
    }
}

static void* bucket_extract(json_bucket_t* bucket, int count)
{
    if (!bucket || count <= 0)
    {
        return NULL;
    }
    else if (bucket->count + count <= bucket->capacity)
    {
        void* res = (char*)bucket + sizeof(json_bucket_t) + bucket->size * bucket->count;
        bucket->count += count;
        return res;
    }
    else
    {
        return NULL;
    }
}

static void* bucket_resize(json_bucket_t* bucket, int ptr, int old_count, int new_count)
{
    if (!bucket || old_count <= 0 || new_count <= 0)
    {
        return NULL;
    }

    char* begin = (char*)bucket + sizeof(json_bucket_t);
    char* end   = begin + bucket->count * bucket->count;
    if ((char*)ptr + bucket->size * old_count == end && bucket->count + (new_count - old_count) <= bucket->capacity)
    {
        bucket->count += (new_count - old_count);
        return ptr;
    }
    else
    {
        return NULL;
    }
}

static json_value_t* make_value(json_state_t* state, int type)
{
    if (!state->value_pool || !state->value_pool->head)
    {
		state->value_pool = make_pool(state->value_pool,
                                      64, sizeof(json_value_t));
		if (!state->value_pool)
		{
			croak(state, JSON_ERROR_MEMORY, "Out of memory");
		}
    }
    
    json_value_t* value = (json_value_t*)pool_extract(state->value_pool);
    if (value)
    {
		memset(value, 0, sizeof(json_value_t));
		value->type    = type;
		value->boolean = JSON_FALSE;
    }
    else
    {
		croak(state, JSON_ERROR_MEMORY, "Out of memory");
    }
    return value;
}

static void free_value(json_state_t* state, json_value_t* value)
{
    if (value)
    {
    #if 0 /* Use with json_bucket_t value no need to be freed by hand */
		int i, n;
		switch (value->type)
		{
		case JSON_ARRAY:
			for (i = 0, n = value->array.length; i < n; i++)
			{
				free_value(state, value->array.values[i]);
			}
			free(value->array.values);
			break;

		case JSON_OBJECT:
			for (i = 0, n = value->object.length; i < n; i++)
			{
				free_value(state, value->object.values[i].name);
				free_value(state, value->object.values[i].value);
			}
			free(value->object.values);
			break;
			
		case JSON_STRING:
			free(value->string.buffer);
			break;
		}

		pool_collect(state->value_pool, value);
    #endif
    }
}

static json_state_t* make_state(const char* json)
{
    json_state_t* state = (json_state_t*)malloc(sizeof(json_state_t));
    if (state)
    {
		state->next   = NULL;
		
		state->line   = 1;
		state->column = 1;
		state->cursor = 0;
		state->buffer = json;

		state->errmsg = NULL;
		state->errnum = JSON_ERROR_NONE;

		state->value_pool    = NULL;
        state->values_bucket = NULL;
        state->string_bucket = NULL;
    }
    return state;
}

static void free_state(json_state_t* state)
{
    if (state)
    {
		json_state_t* next = state->next;

        free_bucket(state->values_bucket);
        free_bucket(state->string_bucket);
		free_pool(state->value_pool);
		free(state->errmsg);
		free(state);

		free_state(next);
    }
}

static int is_eof(json_state_t* state)
{
    return state->buffer[state->cursor] <= 0;
}

static int peek_char(json_state_t* state)
{
    return state->buffer[state->cursor];
}

static int next_char(json_state_t* state)
{
    if (is_eof(state))
    {
		return -1;
    }
    else
    {
		int c = state->buffer[++state->cursor];

		if (c == '\n')
		{
			state->line++;
			state->column = 1;
		}
		else
		{
			state->column = state->column + 1;
		}
		
		return c;
    }
}

static int next_line(json_state_t* state)
{
    int c = peek_char(state);
    while (c > 0 && c != '\n')
    {
		c = next_char(state);
    }
    return next_char(state);
}

static int skip_space(json_state_t* state)
{
    int c = peek_char(state);
    while (c > 0 && isspace(c))
    {
		c = next_char(state);
    }
    return c;
}

static int match_char(json_state_t* state, int c)
{
    if (peek_char(state) == c)
    {
		return next_char(state);
    }
    else
    {
		croak(state, JSON_ERROR_UNMATCH, "Expected '%c'", c);
		return -1;
    }
}

static json_value_t* parse_single(json_state_t* state);
static json_value_t* parse_number(json_state_t* state)
{
    if (skip_space(state) < 0)
    {
		return NULL;
    }
    else
    {
		int c = peek_char(state);
		int sign = 1;
		
		if (c == '+')
		{
			c = next_char(state);
			croak(state, JSON_ERROR_UNEXPECTED,
				  "JSON does not support number start with '+'");
		}
		else if (c == '-')
		{
			sign = -1;
			c = next_char(state);
		}
		else if (c == '0')
		{
			c = next_char(state);
			if (!isspace(c) && !ispunct(c))
			{
				croak(state, JSON_ERROR_UNEXPECTED,
					  "JSON does not support number start with '0'"
					  " (only standalone '0' is accepted)");
			}
		}
		else if (!isdigit(c))
		{
			croak(state, JSON_ERROR_UNEXPECTED, "Unexpected '%c'", c);
		}

		int    dot    = 0;
		int    dotchk = 1;
		int    numpow = 1;
		double number = 0;

		while (c > 0)
		{
			if (c == '.')
			{
				if (dot)
				{
					croak(state, JSON_ERROR_UNEXPECTED,
					      "Too many '.' are presented");
				}
				if (!dotchk)
				{
					croak(state, JSON_ERROR_UNEXPECTED, "Unexpected '%c'", c);
				}
				else
				{
					dot    = 1;
					dotchk = 0;
					numpow = 1;
				}
			}
			else if (!isdigit(c))
			{
				break;
			}
			else
			{
				dotchk = 1;
				if (dot)
				{
					numpow *= 10;
					number += (c - '0') / (double)numpow;
				}
				else
				{
					number = number * 10 + (c - '0');
				}
			}

			c = next_char(state);
		}

		if (dot && !dotchk)
		{
			croak(state, JSON_ERROR_UNEXPECTED,
                  "'.' is presented in number token, "
			      "but require a digit after '.' ('%c')", c);
			return NULL;
		}
		else
		{
			json_value_t* value = make_value(state, JSON_NUMBER);
			value->number = sign * number;
			return value;
		}
    }
}

static json_value_t* parse_string(json_state_t* state)
{
    if (skip_space(state) < 0)
    {
        return NULL;
    }
    else
    {
        match_char(state, '"');

        int length = 0;
	    while (!is_eof(state) && peek_char(state) != '"')
	    {
	        length++;
	        next_char(state);
	    }
	
	    match_char(state, '"');

	    char* string = bucket_extract(state->string_bucket, length + 1);
        if (!string)
        {
            state->string_bucket = make_bucket(state->string_bucket, 4096, sizeof(char)); /* 4096 equal default memory page size */
            string = bucket_extract(state->string_bucket, length + 1);
            if (!string)
            {
                croak(state, JSON_ERROR_MEMORY, "Out of memory when create new string");
                return NULL;
            }
        }
	    string[length] = 0;
	    memcpy(string, state->buffer + state->cursor - length - 1, length);

	    json_value_t* value  = make_value(state, JSON_STRING);
	    value->string.length = length;
	    value->string.buffer = string;
	    return value;
    }
}

static json_value_t* parse_object(json_state_t* state)
{
    if (skip_space(state) < 0)
    {
        return NULL;
    }
    else
    {
        match_char(state, '{');
	
	    json_value_t* root = make_value(state, JSON_OBJECT);

	    int            length = 0;
	    json_value_t** values = 0;
	
	    while (skip_space(state) > 0 && peek_char(state) != '}')
	    {
	        if (length > 0)
	        {
                match_char(state, ',');
	        }
	    
	        json_value_t* name = NULL;
            if (skip_space(state) == '"')
	        {
                name = parse_string(state);
	        }
	        else
	        {
                croak(state, JSON_ERROR_UNEXPECTED,
		              "Expected string for name of field of object");
	        }

	        skip_space(state);
	        match_char(state, ':');
	    
	        json_value_t* value = parse_single(state);

            root->object.values = bucket_resize(state->values_bucket,
                                                root->object.values,
                                                length, ++length);
            if (!root->object.values)
            {
                state->values_bucket = make_bucket(state->values_bucket, 128, sizeof(json_value_t*));
                void* new_values = bucket_extract(state->values_bucket, length);
                if (!new_values)
                {
                    croak(state, JSON_ERROR_MEMORY, "Out of memory when create object");
                    return NULL;
                }
                else
                {
                    memcpy(new_values, root->object.values, (length - 1) * 2 * sizeof(json_value_t));
                    root->object.values = (json_value_t*)new_values;
                }
            }
	    
	        root->object.values[length - 1].name  = name;
	        root->object.values[length - 1].value = value;
	    }

	    root->object.length = length;

	    skip_space(state);
	    match_char(state, '}');
	    return root;
    }
}

static json_value_t* parse_array(json_state_t* state)
{
    if (skip_space(state) < 0)
    {
        return NULL;
    }
    else
    {
	    match_char(state, '[');
	
	    json_value_t* root = make_value(state, JSON_ARRAY);

	    int            length = 0;
	    json_value_t** values = 0;
	
	    while (skip_space(state) > 0 && peek_char(state) != ']')
	    {
	        if (length > 0)
	        {
                match_char(state, ',');
	        }
	    
	        json_value_t* value = parse_single(state);
            values = bucket_resize(state->values_bucket, values, length, ++length);
            if (!root->object.values)
            {
                state->values_bucket = make_bucket(state->values_bucket, 128, sizeof(json_value_t*));
                void* new_values = bucket_extract(state->values_bucket, length);
                if (!new_values)
                {
                    croak(state, JSON_ERROR_MEMORY, "Out of memory when create array");
                    return NULL;
                }
                else
                {
                    memcpy(new_values, values, (length - 1) * sizeof(json_value_t));
                    values = (json_value_t*)new_values;
                }
            }
	        values[length - 1] = value;
	    }

	    skip_space(state);
	    match_char(state, ']');

	    root->array.length = length;
	    root->array.values = values;
	    return root;
    }
}

static json_value_t* parse_single(json_state_t* state)
{
    if (skip_space(state) < 0)
    {
        return NULL;
    }
    else
    {
	    int c = peek_char(state);
	
	    switch (c)
	    {
	    case '[':
	        return parse_array(state);
	    
	    case '{':
	        return parse_object(state);
	    
	    case '"':
	        return parse_string(state);

	    case '+':
	    case '-':
	    case '0':
	    case '1':
	    case '2':
	    case '3':
	    case '4':
	    case '5':
	    case '6':
	    case '7':
	    case '8':
	    case '9':
	        return parse_number(state);
	    
        default:
	    {
	        int length = 0;
	        while (c > 0 && isalpha(c))
	        {
                length++;
                c = next_char(state);
	        }

	        const char* token = state->buffer + state->cursor - length;
	        if (length == 4 && strncmp(token, "true", 4) == 0)
	        {
                json_value_t* value = make_value(state, JSON_BOOLEAN);
                value->boolean = JSON_TRUE;
	        }
	        else if (length == 4 && strncmp(token, "null", 4) == 0)
            {
                return make_value(state, JSON_NULL);
            }
	        else if (length == 5 && strncmp(token, "false", 5) == 0)
	        {
                return make_value(state, JSON_BOOLEAN);
	        }
	        else
	        {
                croak(state, JSON_ERROR_UNEXPECTED, "Unexpected token '%c'", c);
	        }
	    } break;
	    /* END OF SWITCH STATEMENT */
        }

        return NULL;
    }
}

json_value_t* json_parse(const char* json, json_state_t** out_state)
{
    json_state_t* state = make_state(json);

    if (skip_space(state) == '{')
    {
        if (setjmp(state->errjmp) == 0)
        {
            json_value_t* value = parse_object(state);

            if (out_state)
            {
                *out_state = state;
            }
            else
            {
                if (state)
                {
                    state->next = root_state;
                    root_state = state;
                }
            }

            return value;
        }
    }
        
    if (out_state)
    {
        *out_state = state;
    }
    else
    {
        free_state(state);
    }

    return NULL;
}

void json_release(json_state_t* state)
{
    if (state)
    {
        free_state(state);
    }
    else
    {
        free_state(root_state);
        root_state = NULL;
    }
}

json_error_t json_get_errno(const json_state_t* state)
{
    if (state)
    {
        return state->errnum;
    }
    else
    {
        return JSON_ERROR_NONE;
    }
}

const char* json_get_error(const json_state_t* state)
{
    if (state)
    {
        return state->errmsg;
    }
    else
    {
        return NULL;
    }
}

void json_print(const json_value_t* value, FILE* out)
{
    if (value)
    {
        int i, n;

        switch (value->type)
        {
        case JSON_NULL:
            fprintf(out, "null");
            break;

        case JSON_NUMBER:
            fprintf(out, "%lf", value->number);
            break;

        case JSON_BOOLEAN:
            fprintf(out, "%s", value->boolean ? "true" : "false");
            break;

        case JSON_STRING:
            fprintf(out, "\"%s\"", value->string.buffer);
            break;

        case JSON_ARRAY:
            fprintf(out, "[");
            for (i = 0, n = value->array.length; i < n; i++)
            {
                json_print(value->array.values[i], out);
                if (i < n - 1)
                {
                    fprintf(out, ",");
                }
            }
            fprintf(out, "]");
            break;

        case JSON_OBJECT:
            fprintf(out, "{");
            for (i = 0, n = value->object.length; i < n; i++)
            {
                json_print(value->object.values[i].name, out);
                fprintf(out, " : ");
                json_print(value->object.values[i].value, out);
                if (i < n - 1)
                {
                    fprintf(out, ",");
                }            
            }
            fprintf(out, "}");
            break;

        case JSON_NONE:
        default:
            break;
        }
    }
}          

void json_display(const json_value_t* value, FILE* out)
{
    if (value)
    {
        int i, n;
        static int indent = 0;

        switch (value->type)
        {
        case JSON_NULL:
            fprintf(out, "null");
            break;

        case JSON_NUMBER:
            fprintf(out, "%lf", value->number);
            break;

        case JSON_BOOLEAN:
            fprintf(out, "%s", value->boolean ? "true" : "false");
            break;

        case JSON_STRING:
            fprintf(out, "\"%s\"", value->string.buffer);
            break;

        case JSON_ARRAY:
            fprintf(out, "[\n");

            indent++;
            for (i = 0, n = value->array.length; i < n; i++)
            {
                int j, m;
                for (j = 0, m = indent * 4; j < m; j++)
                {
                    fprintf(out, " ");
                }

                json_print(value->array.values[i], out);
                if (i < n - 1)
                {
                    fprintf(out, ",");
                }
                fprintf(out, "\n");
            }
            indent--;

            for (i = 0, n = indent * 4; i < n; i++)
            {
                fprintf(out, " ");
            }
            fprintf(out, "]");
            break;

        case JSON_OBJECT:
            fprintf(out, "{\n");

            indent++;
            for (i = 0, n = value->object.length; i < n; i++)
            {
                int j, m;
                for (j = 0, m = indent * 4; j < m; j++)
                {
                    fprintf(out, " ");
                }

                json_print(value->object.values[i].name, out);
                fprintf(out, " : ");
                json_print(value->object.values[i].value, out);
                if (i < n - 1)
                {
                    fprintf(out, ",");
                }
                fprintf(out, "\n");
            }
            indent--;

            for (i = 0, n = indent * 4; i < n; i++)
            {
                fprintf(out, " ");
            }
            fprintf(out, "}");
            break;

        case JSON_NONE:
        default:
            break;
        }
    }
}

/* END OF JSON_IMPL */
#endif /* JSON_IMPL */