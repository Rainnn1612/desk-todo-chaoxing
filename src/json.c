/* json.c - 极简 JSON 解析器 + UTF-8/UTF-16 转换
 * 只处理我们自己写出的结构，但严格到能读手改过的文件：
 * 支持对象/数组/字符串(含 \uXXXX 与代理对)/数字/true/false/null。 */
#include "app.h"

/* ---------------- UTF-8 <-> UTF-16 ---------------- */

static int u8_encode(unsigned cp, char *out)
{
    if (cp < 0x80) { out[0] = (char)cp; return 1; }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static unsigned u8_decode(const char **pp)
{
    const unsigned char *s = (const unsigned char *)*pp;
    unsigned cp, need, i;
    if (s[0] < 0x80) { *pp += 1; return s[0]; }
    else if ((s[0] & 0xE0) == 0xC0) { cp = s[0] & 0x1F; need = 1; }
    else if ((s[0] & 0xF0) == 0xE0) { cp = s[0] & 0x0F; need = 2; }
    else if ((s[0] & 0xF8) == 0xF0) { cp = s[0] & 0x07; need = 3; }
    else { *pp += 1; return 0xFFFD; }
    for (i = 0; i < need; i++) {
        if ((s[1 + i] & 0xC0) != 0x80) { *pp += 1; return 0xFFFD; }
        cp = (cp << 6) | (s[1 + i] & 0x3F);
    }
    *pp += need + 1;
    return cp;
}

char *w2u8(const WCHAR *w)
{
    int n = wlen(w), i = 0;
    char *out = (char *)malloc((size_t)n * 4 + 1);
    if (!out) return NULL;
    while (*w) {
        unsigned cp = (unsigned)*w++;
        if (cp >= 0xD800 && cp <= 0xDBFF && *w >= 0xDC00 && *w <= 0xDFFF) {
            unsigned lo = (unsigned)*w++;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        i += u8_encode(cp, out + i);
    }
    out[i] = 0;
    return out;
}

WCHAR *u82w(const char *s)
{
    int cap = 1, n = 0;
    WCHAR *out;
    const char *p = s;
    if (!s) s = "";
    cap = (int)strlen(s) + 1;
    out = (WCHAR *)malloc((size_t)cap * sizeof(WCHAR));
    if (!out) return NULL;
    while (*p) {
        unsigned cp = u8_decode(&p);
        if (cp >= 0x10000) {
            cp -= 0x10000;
            if (n + 2 >= cap) { cap = cap * 2 + 8; out = (WCHAR *)realloc(out, (size_t)cap * sizeof(WCHAR)); }
            out[n++] = (WCHAR)(0xD800 + (cp >> 10));
            out[n++] = (WCHAR)(0xDC00 + (cp & 0x3FF));
        } else {
            if (n + 1 >= cap) { cap = cap * 2 + 8; out = (WCHAR *)realloc(out, (size_t)cap * sizeof(WCHAR)); }
            out[n++] = (WCHAR)cp;
        }
    }
    out[n] = 0;
    return out;
}

/* ---------------- 解析 ---------------- */

typedef struct { const char *p; int err; } JP;

static JVal *jnew(JType t)
{
    JVal *v = (JVal *)calloc(1, sizeof(JVal));
    if (v) v->t = t;
    return v;
}

static void jskip(JP *j)
{
    for (;;) {
        while (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r') j->p++;
        if (j->p[0] == '/' && j->p[1] == '/') {           /* 容忍 // 注释 */
            while (*j->p && *j->p != '\n') j->p++;
            continue;
        }
        break;
    }
}

static void wpush(WCHAR **buf, int *cap, int *n, WCHAR c)
{
    if (*n + 1 >= *cap) {
        *cap = *cap * 2 + 16;
        *buf = (WCHAR *)realloc(*buf, (size_t)*cap * sizeof(WCHAR));
    }
    (*buf)[(*n)++] = c;
}

static JVal *jvalue(JP *j);

static int hex4(const char *p)
{
    int i, v = 0;
    for (i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9') v |= c - '0';
        else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
        else return -1;
    }
    return v;
}

static JVal *jstring(JP *j)
{
    WCHAR *buf = NULL;
    int cap = 0, n = 0;
    JVal *v;
    j->p++;                                   /* 开引号 */
    while (*j->p && *j->p != '"') {
        if (*j->p == '\\') {
            j->p++;
            switch (*j->p) {
            case 'n': wpush(&buf, &cap, &n, L'\n'); j->p++; break;
            case 't': wpush(&buf, &cap, &n, L'\t'); j->p++; break;
            case 'r': wpush(&buf, &cap, &n, L'\r'); j->p++; break;
            case 'b': wpush(&buf, &cap, &n, L'\b'); j->p++; break;
            case 'f': wpush(&buf, &cap, &n, L'\f'); j->p++; break;
            case '"': wpush(&buf, &cap, &n, L'"');  j->p++; break;
            case '\\': wpush(&buf, &cap, &n, L'\\'); j->p++; break;
            case '/': wpush(&buf, &cap, &n, L'/');  j->p++; break;
            case 'u': {
                int hi = hex4(j->p + 1);
                if (hi < 0) { j->err = 1; goto done; }
                j->p += 5;
                if (hi >= 0xD800 && hi <= 0xDBFF && j->p[0] == '\\' && j->p[1] == 'u') {
                    int lo = hex4(j->p + 2);
                    if (lo >= 0xDC00 && lo <= 0xDFFF) {
                        unsigned cp = 0x10000 + ((unsigned)(hi - 0xD800) << 10) + (unsigned)(lo - 0xDC00);
                        j->p += 6;
                        wpush(&buf, &cap, &n, (WCHAR)(0xD800 + (cp >> 10)));
                        wpush(&buf, &cap, &n, (WCHAR)(0xDC00 + (cp & 0x3FF)));
                        break;
                    }
                }
                wpush(&buf, &cap, &n, (WCHAR)hi);
                break;
            }
            default: wpush(&buf, &cap, &n, (WCHAR)*j->p); if (*j->p) j->p++; break;
            }
        } else {
            unsigned cp = u8_decode(&j->p);
            if (cp >= 0x10000) {
                cp -= 0x10000;
                wpush(&buf, &cap, &n, (WCHAR)(0xD800 + (cp >> 10)));
                wpush(&buf, &cap, &n, (WCHAR)(0xDC00 + (cp & 0x3FF)));
            } else {
                wpush(&buf, &cap, &n, (WCHAR)cp);
            }
        }
    }
    if (*j->p != '"') { j->err = 1; goto done; }
    j->p++;
done:
    if (!buf) { buf = (WCHAR *)malloc(sizeof(WCHAR)); if (buf) buf[0] = 0; }
    else wpush(&buf, &cap, &n, 0);
    v = jnew(J_STR);
    if (v) v->str = buf; else free(buf);
    return v;
}

static JVal *jvalue(JP *j)
{
    JVal *v = NULL;
    jskip(j);
    switch (*j->p) {
    case '{': case '[': {
        int isobj = (*j->p == '{');
        int cap = 0;
        j->p++;
        v = jnew(isobj ? J_OBJ : J_ARR);
        if (!v) return NULL;
        jskip(j);
        if ((isobj && *j->p == '}') || (!isobj && *j->p == ']')) { j->p++; return v; }
        for (;;) {
            WCHAR *key = NULL;
            JVal *item;
            jskip(j);
            if (isobj) {
                JVal *kv;
                if (*j->p != '"') { j->err = 1; goto fail; }
                kv = jstring(j);
                if (j->err) { json_free(kv); goto fail; }
                key = kv->str;
                kv->str = NULL;
                json_free(kv);
                jskip(j);
                if (*j->p != ':') { free(key); j->err = 1; goto fail; }
                j->p++;
            }
            item = jvalue(j);
            if (j->err || !item) { free(key); json_free(item); goto fail; }
            if (v->n + 1 >= cap) {
                cap = cap * 2 + 8;
                v->items = (JVal **)realloc(v->items, (size_t)cap * sizeof(JVal *));
                if (isobj) v->keys = (WCHAR **)realloc(v->keys, (size_t)cap * sizeof(WCHAR *));
            }
            v->items[v->n] = item;
            if (isobj) v->keys[v->n] = key;
            v->n++;
            jskip(j);
            if (*j->p == ',') { j->p++; continue; }
            if ((isobj && *j->p == '}') || (!isobj && *j->p == ']')) { j->p++; break; }
            j->err = 1;
            goto fail;
        }
        return v;
fail:
        json_free(v);
        return NULL;
    }
    case '"':
        return jstring(j);
    case 't':
        if (!strncmp(j->p, "true", 4)) { j->p += 4; v = jnew(J_BOOL); if (v) v->b = 1; return v; }
        j->err = 1; return NULL;
    case 'f':
        if (!strncmp(j->p, "false", 5)) { j->p += 5; v = jnew(J_BOOL); if (v) v->b = 0; return v; }
        j->err = 1; return NULL;
    case 'n':
        if (!strncmp(j->p, "null", 4)) { j->p += 4; return jnew(J_NULL); }
        j->err = 1; return NULL;
    default: {
        char *endp = NULL;
        double d = strtod(j->p, &endp);
        if (endp == j->p) { j->err = 1; return NULL; }
        j->p = endp;
        v = jnew(J_NUM);
        if (v) v->num = d;
        return v;
    }
    }
}

JVal *json_parse(const char *s)
{
    JP j;
    JVal *v;
    if (!s) return NULL;
    j.p = s;
    j.err = 0;
    v = jvalue(&j);
    if (j.err) { json_free(v); return NULL; }
    return v;
}

void json_free(JVal *v)
{
    int i;
    if (!v) return;
    free(v->str);
    for (i = 0; i < v->n; i++) {
        if (v->items) json_free(v->items[i]);
        if (v->keys) free(v->keys[i]);
    }
    free(v->items);
    free(v->keys);
    free(v);
}

JVal *json_get(JVal *o, const WCHAR *k)
{
    int i;
    if (!o || o->t != J_OBJ) return NULL;
    for (i = 0; i < o->n; i++)
        if (o->keys[i] && !wcscmp(o->keys[i], k)) return o->items[i];
    return NULL;
}

const WCHAR *json_wstr(JVal *o, const WCHAR *k, const WCHAR *def)
{
    JVal *v = json_get(o, k);
    if (v && v->t == J_STR && v->str) return v->str;
    return def;
}

int json_int(JVal *o, const WCHAR *k, int def)
{
    JVal *v = json_get(o, k);
    if (!v) return def;
    if (v->t == J_NUM) return (int)v->num;
    if (v->t == J_BOOL) return v->b;
    return def;
}
