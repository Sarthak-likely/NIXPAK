/* json.c — minimal JSON parser, no external libs */
#include "common.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

typedef struct {
    const char *p;
    const char *end;
} jctx;

static void skip_ws(jctx *c) {
    while (c->p < c->end && (*c->p==' '||*c->p=='\t'||*c->p=='\n'||*c->p=='\r')) c->p++;
}

static json_value_t *new_val(json_type_t t) {
    json_value_t *v = calloc(1, sizeof(*v));
    if (v) v->type = t;
    return v;
}

static json_value_t *parse_value(jctx *c);

static char *parse_string_raw(jctx *c) {
    if (c->p >= c->end || *c->p != '"') return NULL;
    c->p++;
    size_t cap = 32, n = 0;
    char *buf = malloc(cap);
    if (!buf) return NULL;
    while (c->p < c->end && *c->p != '"') {
        char ch = *c->p++;
        if (ch == '\\' && c->p < c->end) {
            char e = *c->p++;
            switch (e) {
                case 'n': ch = '\n'; break;
                case 't': ch = '\t'; break;
                case 'r': ch = '\r'; break;
                case 'b': ch = '\b'; break;
                case 'f': ch = '\f'; break;
                case '/': ch = '/';  break;
                case '\\': ch = '\\'; break;
                case '"': ch = '"'; break;
                case 'u': {
                    if (c->end - c->p < 4) { free(buf); return NULL; }
                    char hex[5] = {c->p[0],c->p[1],c->p[2],c->p[3],0};
                    c->p += 4;
                    unsigned cp = (unsigned)strtoul(hex, NULL, 16);
                    if (cp < 0x80) ch = (char)cp;
                    else if (cp < 0x800) {
                        if (n+2 >= cap) { cap*=2; buf = realloc(buf, cap); if(!buf) return NULL; }
                        buf[n++] = (char)(0xC0 | (cp >> 6));
                        ch = (char)(0x80 | (cp & 0x3F));
                    } else {
                        if (n+3 >= cap) { cap*=2; buf = realloc(buf, cap); if(!buf) return NULL; }
                        buf[n++] = (char)(0xE0 | (cp >> 12));
                        buf[n++] = (char)(0x80 | ((cp >> 6) & 0x3F));
                        ch = (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: ch = e; break;
            }
        }
        if (n+1 >= cap) {
            cap *= 2;
            char *nb = realloc(buf, cap);
            if (!nb) { free(buf); return NULL; }
            buf = nb;
        }
        buf[n++] = ch;
    }
    if (c->p >= c->end) { free(buf); return NULL; }
    c->p++;
    buf[n] = 0;
    return buf;
}

static json_value_t *parse_string(jctx *c) {
    char *s = parse_string_raw(c);
    if (!s) return NULL;
    json_value_t *v = new_val(JSON_STR);
    if (!v) { free(s); return NULL; }
    v->u.str = s;
    return v;
}

static json_value_t *parse_number(jctx *c) {
    char *endp = NULL;
    double d = strtod(c->p, &endp);
    if (endp == c->p) return NULL;
    c->p = endp;
    json_value_t *v = new_val(JSON_NUM);
    if (v) v->u.num = d;
    return v;
}

static json_value_t *parse_array(jctx *c) {
    c->p++;
    json_value_t *v = new_val(JSON_ARR);
    if (!v) return NULL;
    size_t cap = 8;
    v->u.arr.items = malloc(cap * sizeof(*v->u.arr.items));
    v->u.arr.n = 0;
    skip_ws(c);
    if (c->p < c->end && *c->p == ']') { c->p++; return v; }
    for (;;) {
        json_value_t *item = parse_value(c);
        if (!item) { json_free(v); return NULL; }
        if (v->u.arr.n == cap) {
            cap *= 2;
            json_value_t **ni = realloc(v->u.arr.items, cap * sizeof(*ni));
            if (!ni) { json_free(item); json_free(v); return NULL; }
            v->u.arr.items = ni;
        }
        v->u.arr.items[v->u.arr.n++] = item;
        skip_ws(c);
        if (c->p >= c->end) { json_free(v); return NULL; }
        if (*c->p == ',') { c->p++; skip_ws(c); continue; }
        if (*c->p == ']') { c->p++; return v; }
        json_free(v); return NULL;
    }
}

static json_value_t *parse_object(jctx *c) {
    c->p++;
    json_value_t *v = new_val(JSON_OBJ);
    if (!v) return NULL;
    size_t cap = 8;
    v->u.obj.keys = malloc(cap * sizeof(char*));
    v->u.obj.vals = malloc(cap * sizeof(json_value_t*));
    v->u.obj.n = 0;
    skip_ws(c);
    if (c->p < c->end && *c->p == '}') { c->p++; return v; }
    for (;;) {
        skip_ws(c);
        char *k = parse_string_raw(c);
        if (!k) { json_free(v); return NULL; }
        skip_ws(c);
        if (c->p >= c->end || *c->p != ':') { free(k); json_free(v); return NULL; }
        c->p++;
        json_value_t *val = parse_value(c);
        if (!val) { free(k); json_free(v); return NULL; }
        if (v->u.obj.n == cap) {
            cap *= 2;
            char **nk = realloc(v->u.obj.keys, cap * sizeof(char*));
            json_value_t **nv = realloc(v->u.obj.vals, cap * sizeof(json_value_t*));
            if (!nk || !nv) { free(k); json_free(val); json_free(v); return NULL; }
            v->u.obj.keys = nk;
            v->u.obj.vals = nv;
        }
        v->u.obj.keys[v->u.obj.n] = k;
        v->u.obj.vals[v->u.obj.n] = val;
        v->u.obj.n++;
        skip_ws(c);
        if (c->p >= c->end) { json_free(v); return NULL; }
        if (*c->p == ',') { c->p++; continue; }
        if (*c->p == '}') { c->p++; return v; }
        json_free(v); return NULL;
    }
}

static json_value_t *parse_value(jctx *c) {
    skip_ws(c);
    if (c->p >= c->end) return NULL;
    char ch = *c->p;
    if (ch == '{') return parse_object(c);
    if (ch == '[') return parse_array(c);
    if (ch == '"') return parse_string(c);
    if (ch == 't') {
        if (c->end - c->p >= 4 && !memcmp(c->p, "true", 4)) {
            c->p += 4;
            json_value_t *v = new_val(JSON_BOOL);
            if (v) v->u.b = 1;
            return v;
        }
    }
    if (ch == 'f') {
        if (c->end - c->p >= 5 && !memcmp(c->p, "false", 5)) {
            c->p += 5;
            json_value_t *v = new_val(JSON_BOOL);
            if (v) v->u.b = 0;
            return v;
        }
    }
    if (ch == 'n') {
        if (c->end - c->p >= 4 && !memcmp(c->p, "null", 4)) {
            c->p += 4;
            return new_val(JSON_NULL);
        }
    }
    if (ch == '-' || isdigit((unsigned char)ch)) return parse_number(c);
    return NULL;
}

json_value_t *json_parse(const char *text, size_t len) {
    jctx c = { text, text + len };
    return parse_value(&c);
}

void json_free(json_value_t *v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STR: free(v->u.str); break;
        case JSON_ARR:
            for (size_t i = 0; i < v->u.arr.n; i++) json_free(v->u.arr.items[i]);
            free(v->u.arr.items);
            break;
        case JSON_OBJ:
            for (size_t i = 0; i < v->u.obj.n; i++) {
                free(v->u.obj.keys[i]);
                json_free(v->u.obj.vals[i]);
            }
            free(v->u.obj.keys);
            free(v->u.obj.vals);
            break;
        default: break;
    }
    free(v);
}

json_value_t *json_obj_get(json_value_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJ) return NULL;
    for (size_t i = 0; i < obj->u.obj.n; i++) {
        if (!strcmp(obj->u.obj.keys[i], key)) return obj->u.obj.vals[i];
    }
    return NULL;
}

json_value_t *json_arr_get(json_value_t *arr, size_t idx) {
    if (!arr || arr->type != JSON_ARR || idx >= arr->u.arr.n) return NULL;
    return arr->u.arr.items[idx];
}
