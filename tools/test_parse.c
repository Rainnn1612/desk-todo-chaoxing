/* test_parse.c - 自然语言解析自测（控制台，读 tools/cases.txt，UTF-8） */
#include <stdio.h>
#include "../src/app.h"

int main(void)
{
    int len = 0, i = 0, idx = 0;
    char *raw = read_file(L"tools\\cases.txt", &len);
    SYSTEMTIME now;
    if (!raw) { printf("cases.txt not found\n"); return 1; }
    GetLocalTime(&now);
    printf("today = %04d-%02d-%02d\n", now.wYear, now.wMonth, now.wDay);
    printf("%-28s | %-22s | %-16s | %s\n", "input", "title", "ddl", "days/lvl/text");

    while (i < len) {
        int s = i, e;
        WCHAR *line;
        WCHAR title[MAX_TITLE];
        int has = 0, days, lvl;
        SYSTEMTIME ddl;
        WCHAR dtxt[64];
        Todo t;

        while (i < len && raw[i] != '\n' && raw[i] != '\r') i++;
        e = i;
        while (i < len && (raw[i] == '\n' || raw[i] == '\r')) i++;
        raw[e] = 0;
        if (e > s) {
            char dbuf[32];
            line = u82w(raw + s);
            idx++;
            parse_todo_text(line, title, &has, &ddl);
            t.has_ddl = has;
            t.ddl = ddl;
            t.done = 0;
            wcpy(t.title, title, MAX_TITLE);
            days = has ? ddl_days(&t) : 0;
            lvl = has ? ddl_level(&t) : 0;
            if (has) ddl_text(&t, dtxt, 64);
            if (has) sprintf(dbuf, "%04d-%02d-%02d %02d:%02d",
                             ddl.wYear, ddl.wMonth, ddl.wDay, ddl.wHour, ddl.wMinute);
            else wcpy(dtxt, L"-", 64), dbuf[0] = '-', dbuf[1] = 0;
            printf("%-28s | %-22s | %-16s | %d/%d/%s\n",
                   w2u8(line), w2u8(title), dbuf, days, lvl, w2u8(dtxt));
            free(line);
        }
    }
    (void)idx;
    free(raw);
    return 0;
}
