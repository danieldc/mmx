/*
    mm_json.h - zlib - Micha Mettke

ABOUT:
    This is a single header JSON parser header and implementation without
    any dependencies (even the standard library),
    string memory allocation or complex tree generation.
    Instead this library focuses on parsing tokens from a previously
    loaded in memory JSON file. Each token thereby references the JSON file
    string and limits the allocation to the initial JSON file instead of
    allocating a new string for each token.

QUICK:
    To use this file do:
    #define JSON_IMPLEMENTATION
    before you include this file in *one* C or C++ file to create the implementation

    If you want to keep the implementation in that file you have to do
    #define JSON_STATIC before including this file

    If you want to use asserts to add validation add
    #define JSON_ASSERT before including this file

    To overwrite the default seperator character used inside
    the query functions
    #define JSON_DELIMITER (character) before including this file

LICENSE: (zlib)
    Copyright (c) 2016 Micha Mettke

    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1.  The origin of this software must not be misrepresented; you must not
        claim that you wrote the original software. If you use this software
        in a product, an acknowledgment in the product documentation would be
        appreciated but is not required.
    2.  Altered source versions must be plainly marked as such, and must not be
        misrepresented as being the original software.
    3.  This notice may not be removed or altered from any source distribution.

USAGE:
    This file behaves differently depending on what symbols you define
    before including it.

    Header-File mode:
    If you do not define JSON_IMPLEMENTATION before including this file, it
    will operate in header only mode. In this mode it declares all used structs
    and the API of the library without including the implementation of the library.

    Implementation mode:
    If you define JSON_IMPLEMENTATIOn before including this file, it will
    compile the implementation of the JSON parser. To specify the visibility
    as private and limit all symbols inside the implementation file
    you can define JSON_STATIC before including this file.
    Make sure that you only include this file implementation in *one* C or C++ file
    to prevent collisions.

EXAMPLES:*/
#if 0
    /* Parser example */
    const char *json = "{...}";
    size_t len = strlen(json);

    /* load content into token array */
    size_t read = 0;
    size_t num = json_num(json, len);
    struct json_token *toks = calloc(num, sizeof(struct json_token));
    json_load(toks, num, &read, json, len);

    /* query token */
    struct json_token *t0 = json_query(toks, num, "map.entity[4].position");

    /* query string */
    char buffer[64];
    size_t size;
    json_query_string(buffer, 64, &size, toks, num, "map.entity[4].name");

    /* query number */
    json_number num;
    json_query_number(&num, toks, num, "map.soldier[2].position.x");

    /* query type */
    int type0 = json_query_number(toks, num, "map.soldier[2]");

    /* sub-queries */
    json_token *entity = json_query(toks, num, "map.entity[4]");
    json_token *position = json_query(entity, entity->sub, "position");
    json_token *rotation = json_query(entity, entity->sub, "rotation");
#endif

 /* ===============================================================
 *
 *                          HEADER
 *
 * =============================================================== */
#ifndef JSON_H_
#define JSON_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifdef JSON_STATIC
#define JSON_API static
#else
#define JSON_API extern
#endif

/*-------------------------------------------------------------------------
                                API
  -------------------------------------------------------------------------*/
#ifndef JSON_DELIMITER
#define JSON_DELIMITER '.'
#endif

typedef double json_number;

enum json_token_type {
    JSON_NONE,      /* invalid token */
    JSON_OBJECT,    /* subobject */
    JSON_ARRAY,     /* subarray */
    JSON_NUMBER,    /* float point number token */
    JSON_STRING,    /* string text token */
    JSON_TRUE,      /* true constant token */
    JSON_FALSE,     /* false constant token*/
    JSON_NULL,      /* null constant token */
    JSON_MAX
};

struct json_token {
    enum json_token_type type;
    const char *str;
    int len;
    int children; /* number of direct child tokens */
    int sub; /* total number of subtokens (note: not pairs)*/
};

struct json_pair {
    struct json_token name;
    struct json_token value;
};

enum json_status {
    JSON_OK = 0,
    JSON_INVAL,
    JSON_OUT_OF_TOKEN,
    JSON_PARSING_ERROR
};

/* parse JSON into token array */
JSON_API int                json_num(const char *json, int length);
JSON_API enum json_status   json_load(struct json_token *toks, int max, int *read, const char *json, int length);

/* access nodes inside token array */
JSON_API struct json_token *json_query(struct json_token *toks, int count, const char *path);
JSON_API int                json_query_number(json_number*, struct json_token *toks, int count, const char *path);
JSON_API int                json_query_string(char*, int max, int *size, struct json_token*, int count, const char *path);
JSON_API int                json_query_type(struct json_token *toks, int count, const char *path);

/*--------------------------------------------------------------------------
                                INTERNAL
  -------------------------------------------------------------------------*/
struct json_iter {
    int len;
    unsigned short err;
    unsigned depth;
    const char *go;
    const char *src;
};

/* tokenizer */
JSON_API struct json_iter   json_begin(const char *json, int length);
JSON_API struct json_iter   json_read(struct json_token*, const struct json_iter*);
JSON_API struct json_iter   json_parse(struct json_pair*, const struct json_iter*);

/* utility */
JSON_API int                json_cmp(const struct json_token*, const char*);
JSON_API int                json_cpy(char*, int, const struct json_token*);
JSON_API int                json_convert(json_number *, const struct json_token*);
JSON_API void               json_init(void); /* Inits internal parser lookup tables. (only required if used with MT */

#ifdef __cplusplus
}
#endif
#endif /* JSON_H_ */

/*===============================================================
 *
 *                          IMPLEMENTATION
 *
 * =============================================================== */
#ifdef JSON_IMPLEMENTATION
/* this flag inserts the <assert.h> header into the json.c file and adds
 assert call to every function in DEBUG mode. If activated then
 the clib will be used. So if you want to compile without clib then
 deactivate this flag. */
#ifdef JSON_USE_ASSERT
#ifndef JSON_ASSERT
#include <assert.h>
#define JSON_ASSERT(expr) assert(expr)
#endif
#else
#define JSON_ASSERT(expr)
#endif

#define JSON_INTERN static
#define JSON_GLOBAL static

#ifndef NULL
#define NULL ((void*)0)
#endif

#ifndef MAX
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#endif

/* Main token parsing function states */
enum json_parser_states {
    JSON_STATE_FAILED,
    JSON_STATE_LOOP,
    JSON_STATE_SEP,
    JSON_STATE_UP,
    JSON_STATE_DOWN,
    JSON_STATE_QUP,
    JSON_STATE_QDOWN,
    JSON_STATE_ESC,
    JSON_STATE_UNESC,
    JSON_STATE_BARE,
    JSON_STATE_UNBARE,
    JSON_STATE_UTF8_2,
    JSON_STATE_UTF8_3,
    JSON_STATE_UTF8_4,
    JSON_STATE_UTF8_NEXT,
    JSON_STATE_MAX
};

/* Main token to number conversion states */
enum json_nuber_states {
    JSON_STATE_NUM_FAILED,
    JSON_STATE_NUM_LOOP,
    JSON_STATE_NUM_FLT,
    JSON_STATE_NUM_EXP,
    JSON_STATE_NUM_BREAK,
    JSON_STATE_NUM_MAX
};

/* global parser jump tables */
JSON_GLOBAL char json_go_struct[256];
JSON_GLOBAL char json_go_bare[256];
JSON_GLOBAL char json_go_string[256];
JSON_GLOBAL char json_go_utf8[256];
JSON_GLOBAL char json_go_esc[256];
JSON_GLOBAL char json_go_num[256];
JSON_GLOBAL const struct json_iter JSON_ITER_NULL = {0,0,0,0,0};
JSON_GLOBAL const struct json_token JSON_TOKEN_NULL = {0,0,0,0,JSON_NONE};
JSON_GLOBAL int json_is_initialized;

/*--------------------------------------------------------------------------
 *
                                HELPER

  -------------------------------------------------------------------------*/
/* initializes the parser jump tables */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wchar-subscripts"
#endif

JSON_API void
json_init(void)
{
    int i;
    if (json_is_initialized) return;
    json_is_initialized = 1;
    for (i = 48; i <= 57; ++i)
        json_go_struct[i] = JSON_STATE_BARE;
    json_go_struct['\t'] = JSON_STATE_LOOP;
    json_go_struct['\r'] = JSON_STATE_LOOP;
    json_go_struct['\n'] = JSON_STATE_LOOP;
    json_go_struct[' '] = JSON_STATE_LOOP;
    json_go_struct['"'] = JSON_STATE_QUP;
    json_go_struct[':'] = JSON_STATE_SEP;
    json_go_struct['='] = JSON_STATE_SEP;
    json_go_struct[','] = JSON_STATE_LOOP;
    json_go_struct['['] = JSON_STATE_UP;
    json_go_struct[']'] = JSON_STATE_DOWN;
    json_go_struct['{'] = JSON_STATE_UP;
    json_go_struct['}'] = JSON_STATE_DOWN;
    json_go_struct['-'] = JSON_STATE_BARE;
    json_go_struct['t'] = JSON_STATE_BARE;
    json_go_struct['f'] = JSON_STATE_BARE;
    json_go_struct['n'] = JSON_STATE_BARE;

    for (i = 32; i <= 126; ++i)
        json_go_bare[i] = JSON_STATE_LOOP;
    json_go_bare['\t'] = JSON_STATE_UNBARE;
    json_go_bare['\r'] = JSON_STATE_UNBARE;
    json_go_bare['\n'] = JSON_STATE_UNBARE;
    json_go_bare[','] = JSON_STATE_UNBARE;
    json_go_bare[']'] = JSON_STATE_UNBARE;
    json_go_bare['}'] = JSON_STATE_UNBARE;

    for (i = 32; i <= 126; ++i)
        json_go_string[i] = JSON_STATE_LOOP;
    for (i = 192; i <= 223; ++i)
        json_go_string[i] = JSON_STATE_UTF8_2;
    for (i = 224; i <= 239; ++i)
        json_go_string[i] = JSON_STATE_UTF8_3;
    for (i = 240; i <= 247; ++i)
        json_go_string[i] = JSON_STATE_UTF8_4;
    json_go_string['\\'] = JSON_STATE_ESC;
    json_go_string['"'] = JSON_STATE_QDOWN;
    for (i = 128; i <= 191; ++i)
        json_go_utf8[i] = JSON_STATE_UTF8_NEXT;

    json_go_esc['"'] = JSON_STATE_UNESC;
    json_go_esc['\\'] = JSON_STATE_UNESC;
    json_go_esc['/'] = JSON_STATE_UNESC;
    json_go_esc['b'] = JSON_STATE_UNESC;
    json_go_esc['f'] = JSON_STATE_UNESC;
    json_go_esc['n'] = JSON_STATE_UNESC;
    json_go_esc['r'] = JSON_STATE_UNESC;
    json_go_esc['t'] = JSON_STATE_UNESC;
    json_go_esc['u'] = JSON_STATE_UNESC;

    for (i = 48; i <= 57; ++i)
        json_go_num[i] = JSON_STATE_LOOP;
    json_go_num['-'] = JSON_STATE_NUM_LOOP;
    json_go_num['+'] = JSON_STATE_NUM_LOOP;
    json_go_num['.'] = JSON_STATE_NUM_FLT;
    json_go_num['e'] = JSON_STATE_NUM_EXP;
    json_go_num['E'] = JSON_STATE_NUM_EXP;
    json_go_num[' '] = JSON_STATE_NUM_BREAK;
    json_go_num['\n'] = JSON_STATE_NUM_BREAK;
    json_go_num['\t'] = JSON_STATE_NUM_BREAK;
    json_go_num['\r'] = JSON_STATE_NUM_BREAK;
}

#ifdef __clang__
#pragma clang diagnostic pop
#endif

/* checks and returns the type of a token */
JSON_INTERN enum json_token_type
json_type(const struct json_token *tok)
{
    if (!tok || !tok->str || !tok->len)
        return JSON_NONE;
    if (tok->str[0] == '{')
        return JSON_OBJECT;
    if (tok->str[0] == '[')
        return JSON_ARRAY;
    if (tok->str[0] == '\"')
        return JSON_STRING;
    if (tok->str[0] == 't')
        return JSON_TRUE;
    if (tok->str[0] == 'f')
        return JSON_FALSE;
    if (tok->str[0] == 'n')
        return JSON_NULL;
    return JSON_NUMBER;
}

/* dequotes a string token */
JSON_INTERN void
json_deq(struct json_token *tok)
{
    if (tok->str[0] == '\"' && tok->len >= 2) {
        tok->str++; tok->len-=2;
    }
}

/* simple power function for json exponent numbers */
JSON_INTERN json_number
json_ipow(int base, unsigned exp)
{
    long res = 1;
    while (exp) {
        if (exp & 1)
            res *= base;
        exp >>= 1;
        base *= base;
    }
    return (json_number)res;
}

/* converts a token containing a integer value into a number */
JSON_INTERN json_number
json_stoi(struct json_token *tok)
{
    json_number n = 0;
    int i = 0;
    int off;
    int neg;
    if (!tok->str || !tok->len)
        return 0;

    off = (tok->str[0] == '-' || tok->str[0] == '+') ? 1 : 0;
    neg = (tok->str[0] == '-') ? 1 : 0;
    for (i = off; i < tok->len; i++) {
        if ((tok->str[i] >= '0') && (tok->str[i] <= '9'))
            n = (n * 10) + tok->str[i]  - '0';
    }
    return (neg) ? -n : n;
}

/* converts a token containing a real value into a floating point number */
JSON_INTERN json_number
json_stof(struct json_token *tok)
{
    int i = 0;
    json_number n = 0;
    json_number f = 0.1;
    if (!tok->str || !tok->len) return 0;
    for (i = 0; i < tok->len; i++) {
        if ((tok->str[i] >= '0') && (tok->str[i] <= '9')) {
            n = n + (tok->str[i] - '0') * f;
            f *= 0.1;
        }
    }
    return n;
}

/* compares a size limited string with a string inside a token */
JSON_INTERN int
json_lcmp(const struct json_token* tok, const char* str, int len)
{
    int i;
    JSON_ASSERT(tok);
    JSON_ASSERT(str);
    if (!tok || !str || !len) return 1;
    for (i = 0; (i < tok->len && i < len); i++, str++){
        if (tok->str[i] != *str)
            return 1;
    }
    return 0;
}

/*--------------------------------------------------------------------------
 *
                                UTILITY

  -------------------------------------------------------------------------*/
JSON_API int
json_convert(json_number *num, const struct json_token *tok)
{
    int len;
    const char *cur;
    json_number i, f, e, p;
    enum {INT, FLT, EXP, TOKS};
    struct json_token nums[TOKS];
    struct json_token *write = &nums[INT];

    JSON_ASSERT(num);
    JSON_ASSERT(tok);
    if (!num || !tok || !tok->str || !tok->len)
        return JSON_NONE;

    nums[INT] = JSON_TOKEN_NULL;
    nums[FLT] = JSON_TOKEN_NULL;
    nums[EXP] = JSON_TOKEN_NULL;
    len = tok->len;
    write->str = tok->str;

    for (cur = tok->str; len; cur++, len--) {
        char state =  json_go_num[*(const unsigned char*)cur];
        switch (state) {
            case JSON_STATE_NUM_FAILED: {
                return JSON_NONE;
            } break;
            case JSON_STATE_NUM_FLT: {
                if (nums[FLT].str)
                    return JSON_NONE;
                if (nums[EXP].str)
                    return JSON_NONE;
                write->len = (int)(cur - write->str);
                write = &nums[FLT];
                write->str = cur + 1;
            } break;
            case JSON_STATE_NUM_EXP: {
                if (nums[EXP].str)
                    return JSON_NONE;
                write->len = (int)(cur - write->str);
                write = &nums[EXP];
                write->str = cur + 1;
            } break;
            case JSON_STATE_NUM_BREAK: {
                len = 1;
            } break;
            default: break;
        }
    }
    write->len = (int)(cur - write->str);

    i = json_stoi(&nums[INT]);
    f = json_stof(&nums[FLT]);
    e = json_stoi(&nums[EXP]);
    p = json_ipow(10, (unsigned)((e < 0) ? -e : e));
    if (e < 0) p = (1 / p);
    *num = (i + ((i < 0) ? -f : f)) * p;
    return JSON_NUMBER;
}

JSON_API int
json_cpy(char *dst, int max, const struct json_token* tok)
{
    int i = 0;
    int ret;
    int siz;

    JSON_ASSERT(dst);
    JSON_ASSERT(tok);
    if (!dst || !max || !tok)
        return 0;

    ret = (max < (tok->len + 1)) ? max : tok->len;
    siz = (max < (tok->len + 1)) ? max-1 : tok->len;
    for (i = 0; i < siz; i++)
        dst[i] = tok->str[i];
    dst[siz] = '\0';
    return ret;
}

JSON_API int
json_cmp(const struct json_token* tok, const char* str)
{
    int i;
    JSON_ASSERT(tok);
    JSON_ASSERT(str);
    if (!tok || !str) return 1;
    for (i = 0; (i < tok->len && *str); i++, str++){
        if (tok->str[i] != *str)
            return 1;
    }
    return 0;
}
/*--------------------------------------------------------------------------
 *
                                TOKENIZER

  -------------------------------------------------------------------------*/
JSON_API struct json_iter
json_begin(const char *str, int len)
{
    struct json_iter iter = JSON_ITER_NULL;
    json_init();
    iter.src = str;
    iter.len = len;
    return iter;
}

JSON_API struct json_iter
json_read(struct json_token *obj, const struct json_iter* prev)
{
    struct json_iter iter;
    int len;
    const char *cur;
    int utf8_remain = 0;
    unsigned char c;

    JSON_ASSERT(obj);
    JSON_ASSERT(prev);
    if (!prev || !obj || !prev->src || !prev->len || prev->err) {
        /* case either invalid iterator or eof  */
        struct json_iter it = JSON_ITER_NULL;
        *obj = JSON_TOKEN_NULL;
        it.err = 1;
        return it;
    }

    iter = *prev;
    *obj = JSON_TOKEN_NULL;
    iter.err = 0;
    if (!iter.go) /* begin of parsing process */
        iter.go = json_go_struct;

    len = iter.len;
    for (cur = iter.src; len; cur++, len--) {
        const unsigned char *tbl = (const unsigned char*)iter.go;
        c = (unsigned char)*cur;
        if (c == '\0') goto l_end;
        if (iter.depth == 1 && (c == '}' || (c == ']')) && len == 1 && !obj->str) {
            iter.src = 0;
            iter.len = 0;
            iter.depth = 0;
            return iter;
        }

        switch (tbl[c]) {
        case JSON_STATE_FAILED: {
            iter.err = 1;
            return iter;
        } break;
        case JSON_STATE_LOOP: break;
        case JSON_STATE_SEP: {
            if (iter.depth == 2)
                obj->children--;
        } break;
        case JSON_STATE_UP: {
            if (iter.depth > 1) {
                if (iter.depth == 2)
                    obj->children++;
                obj->sub++;
            }
            if (iter.depth++ == 1)
                obj->str = cur;
        } break;
        case JSON_STATE_DOWN: {
            if (--iter.depth == 1) {
                obj->len = (int)(cur - obj->str) + 1;
                if (iter.depth != 1 || !obj->str)
                    goto l_loop;
                goto l_yield;
            }
        } break;
        case JSON_STATE_QUP: {
            iter.go = json_go_string;
            if (iter.depth <= 1) {
                obj->str = cur;
            } else {
                if (iter.depth == 2)
                    obj->children++;
                obj->sub++;
            }
        } break;
        case JSON_STATE_QDOWN: {
            iter.go = json_go_struct;
            if (iter.depth <= 1) {
                obj->len = (int)(cur - obj->str) + 1;
                if (iter.depth > 1 || !obj->str)
                    goto l_loop;
                goto l_yield;
            }
        } break;
        case JSON_STATE_ESC: {
            iter.go = json_go_esc;
        } break;
        case JSON_STATE_UNESC: {
            iter.go = json_go_string;
        } break;
        case JSON_STATE_BARE: {
            if (iter.depth <= 1) {
                obj->str = cur;
            } else {
                if (iter.depth == 2)
                    obj->children++;
                obj->sub++;
            }
            iter.go = json_go_bare;
        } break;
        case JSON_STATE_UNBARE: {
            iter.go = json_go_struct;
            if (iter.depth <= 1) {
                obj->len = (int)(cur - obj->str);
                obj->type = (enum json_token_type)json_type(obj);
                if (obj->type == JSON_STRING)
                    json_deq(obj);
                iter.src = cur;
                iter.len = len;
                return iter;
            }
            cur--; len++;
        } break;
        case JSON_STATE_UTF8_2: {
            iter.go = json_go_utf8;
            utf8_remain = 1;
        } break;
        case JSON_STATE_UTF8_3: {
            iter.go = json_go_utf8;
            utf8_remain = 2;
        } break;
        case JSON_STATE_UTF8_4: {
            iter.go = json_go_utf8;
            utf8_remain = 3;
        } break;
        case JSON_STATE_UTF8_NEXT: {
            if (!--utf8_remain)
                iter.go = json_go_string;
        } break;
        default:
            break;
        }
        l_loop:;
    }

l_end:
    if (!iter.depth) {
        /* reached eof */
        iter.src = 0;
        iter.len = 0;
        if (obj->str) {
            obj->len = (c == '}') ? (int)((cur-1) - obj->str): (int)(cur - obj->str);
            obj->type = (enum json_token_type)json_type(obj);
            if (obj->type == JSON_STRING)
                json_deq(obj);
        }
        return iter;
    }
    return iter;

l_yield:
    iter.src = cur + 1;
    iter.len = len - 1;
    obj->type = json_type(obj);
    if (obj->type == JSON_STRING)
        json_deq(obj);
    return iter;
}

JSON_API struct json_iter
json_parse(struct json_pair *p, const struct json_iter* it)
{
    struct json_iter next;
    JSON_ASSERT(p);
    JSON_ASSERT(it);
    next = json_read(&p->name, it);
    if (next.err) return next;
    return json_read(&p->value, &next);
}

/*--------------------------------------------------------------------------
 *
                                PARSER

  -------------------------------------------------------------------------*/
JSON_API int
json_num(const char *json, int length)
{
    struct json_iter iter;
    struct json_token tok;
    int count = 0;

    JSON_ASSERT(json);
    JSON_ASSERT(length > 0);
    if (!json || !length)
        return 0;

    iter = json_begin(json, length);
    iter = json_read(&tok, &iter);
    while (!iter.err && iter.src && tok.str) {
        count += (1 + tok.sub);
        iter = json_read(&tok, &iter);
    }
    return count;
}

JSON_API enum json_status
json_load(struct json_token *toks, int max, int *read,
            const char *json, int length)
{
    enum json_status status = JSON_OK;
    struct json_token tok;
    struct json_iter iter;

    JSON_ASSERT(toks);
    JSON_ASSERT(json);
    JSON_ASSERT(length > 0);
    JSON_ASSERT(max > 0);
    JSON_ASSERT(read);

    if (!toks || !json || !length || !max || !read)
        return JSON_INVAL;
    if (*read >= max)
        return JSON_OUT_OF_TOKEN;

    iter = json_begin(json, length);
    iter = json_read(&tok, &iter);
    if (iter.err && iter.len)
        return JSON_PARSING_ERROR;

    while (iter.len) {
        toks[*read] = tok;
        *read += 1;
        if (*read > max) return JSON_OUT_OF_TOKEN;
        if (toks[*read-1].type == JSON_OBJECT ||  toks[*read-1].type == JSON_ARRAY) {
            status = json_load(toks, max, read, toks[*read-1].str, toks[*read-1].len);
            if (status != JSON_OK) return status;
        }

        iter = json_read(&tok, &iter);
        if (iter.err && iter.src && iter.len)
            return JSON_PARSING_ERROR;
    }
    return status;
}
/*--------------------------------------------------------------------------
 *
                                QUERY

  -------------------------------------------------------------------------*/
JSON_INTERN const char*
json_strchr(const char *str, char c, int len)
{
    int neg = (len < 0) ? 1: 0;
    int dec = neg ? 0 : 1;
    len = neg ? 0 : len;
    if (!str) return NULL;
    while (*str && len >= 0) {
        if (*str == c)
            return str;
        len -= dec;
        str++;
    }
    if (neg) return str;
    return NULL;
}

JSON_INTERN const char*
json_path_parse_name(struct json_token *tok, const char *path,
    char delimiter)
{
    const char *del;
    const char *begin, *end;
    if (!path || *path == '\0')
        return NULL;

    tok->str = path;
    del = json_strchr(tok->str, delimiter, -1);
    begin = json_strchr(tok->str, '[', -1);
    end = json_strchr(tok->str, ']', -1);

    /* array part left */
    if (begin && end && begin == tok->str) {
        tok->len = (int)((end - begin) + 1);
        if (*(end + 1) == '\0')
            return NULL;
        if (*(end + 1) == '.')
            return(end + 2);
        else return(end + 1);
    }

    /* only array after name */
    if (begin < del) {
        tok->len = (int)(begin - tok->str);
        return begin;
    }

    if (!del) return NULL;
    if (*del == '\0') {
        tok->len = (int)(del - tok->str);
        return NULL;
    }
    tok->len = (int)(del - tok->str);
    return del+1;
}

JSON_INTERN int
json_path_parse_array(struct json_token *array, const struct json_token *token)
{
    const char *begin;
    const char *end;

    array->str = token->str;
    begin = json_strchr(array->str, '[', (int)token->len);
    if (!begin || ((int)(begin - array->str) >= token->len))
        return 0;

    end = json_strchr(begin, ']', (int)(token->len - (int)(begin - array->str)));
    if (!end || ((int)(end - array->str) >= token->len))
        return 0;

    array->str = begin + 1;
    array->len = (int)((end-1) - begin);
    return 1;
}

JSON_API struct json_token*
json_query(struct json_token *toks, int count, const char *path)
{
    int i = 0;
    int begin = 1;
    struct json_token *iter = NULL;
    /* iterator to step over each token in the toks array */
    struct json_token name;
    /* current segment in the path to search in the tree for */
    struct json_token array;
    /* array token to store the current path segment array index */
    struct json_object {int index, size;} obj;
    /* current object iterator with current pair index and total pairs in object */

    JSON_ASSERT(toks);
    JSON_ASSERT(count > 0);
    if (!toks || !count) return iter;
    if (!path) return &toks[i];

    iter = &toks[i];
    array.len = 0;
    array.str = NULL;

    path = json_path_parse_name(&name, path, JSON_DELIMITER);
    while (1) {
        if (iter->type == JSON_OBJECT || iter->type == JSON_ARRAY || begin) {
            /* setup iteration over elements inside a object or array */
            obj.index = 0;
            if (begin) {
                begin = 0;
                obj.size = count;
            } else if (iter->type == JSON_OBJECT) {
                obj.size = iter->children;
                if ((i + 1) > count) return NULL;
                iter = &toks[++i];
            } else {
                json_number n;
                int j = 0;

                /* array object so set iterator to array index */
                if (!json_path_parse_array(&array, &name))
                    return NULL;
                if ((i+1) >= count)
                    return NULL;
                if (array.len < 1)
                    return NULL;
                if (json_convert(&n, &array) != JSON_NUMBER)
                    return NULL;
                if ((int)n >= iter->children)
                    return NULL;
                array.str = NULL;
                array.len = 0;

                /* iterate over each array element and find the correct index */
                iter++; i++;
                for (j = 0; j < n; ++j) {
                    if (iter->type == JSON_ARRAY || iter->type == JSON_OBJECT) {
                        i = i + (iter->sub) + 1;
                    } else i += 1;
                    if (i > count)
                        return NULL;
                    iter = &toks[i];
                }
                if (!path) return iter;
                path = json_path_parse_name(&name, path, JSON_DELIMITER);
            }
            continue;
        }
        {
            /* check if current table element is equal to the current path  */
            if (!json_lcmp(iter, name.str, name.len)) {
                /* correct token found and end of path */
                if (!path) {
                    if ((i + 1) > count)
                        return NULL;
                    return (iter + 1);
                }
                /* check if path points to invalid token */
                if ((i+1) > count)
                    return NULL;
                if(toks[i+1].type != JSON_OBJECT && toks[i+1].type != JSON_ARRAY)
                    return NULL;

                /* look deeper into child object/array */
                iter = &toks[++i];
                path = json_path_parse_name(&name, path, JSON_DELIMITER);
            } else {
                /* key is not correct iterate until end of object */
                if (++obj.index >= obj.size)
                    return NULL;
                if ((i + 1) >= count)
                    return NULL;
                if (iter[1].type == JSON_ARRAY || iter[1].type == JSON_OBJECT) {
                    i = i + (iter[1].sub + 2);
                } else i = i + 2;
                if (i >= count)
                    return NULL;
                iter = &toks[i];
            }
        }
    }
    return iter;
}

JSON_API int
json_query_number(json_number *num, struct json_token *toks, int count,
    const char *path)
{
    struct json_token *tok;
    JSON_ASSERT(toks);
    JSON_ASSERT(count > 0);
    JSON_ASSERT(num);
    JSON_ASSERT(path);
    if (!toks || !count || !num || !path)
        return JSON_NONE;

    tok = json_query(toks, count, path);
    if (!tok) return JSON_NONE;
    if (tok->type != JSON_NUMBER)
        return tok->type;
    return json_convert(num, tok);

}

JSON_API int
json_query_string(char *buffer, int max, int *size,
    struct json_token *toks, int count, const char *path)
{
    struct json_token *tok;
    JSON_ASSERT(toks);
    JSON_ASSERT(count > 0);
    JSON_ASSERT(buffer);
    JSON_ASSERT(size);
    JSON_ASSERT(path);
    if (!toks || !count || !buffer || !size || !path)
        return JSON_NONE;

    tok = json_query(toks, count, path);
    if (!tok) return JSON_NONE;
    if (tok->type != JSON_STRING)
        return tok->type;
    *size = json_cpy(buffer, max, tok);
    return tok->type;

}

JSON_API int
json_query_type(struct json_token *toks, int count, const char *path)
{
    struct json_token *tok;
    JSON_ASSERT(toks);
    JSON_ASSERT(count > 0);
    JSON_ASSERT(path);
    if (!toks || !count || !path)
        return JSON_NONE;

    tok = json_query(toks, count, path);
    if (!tok) return JSON_NONE;
    return tok->type;
}

#endif
