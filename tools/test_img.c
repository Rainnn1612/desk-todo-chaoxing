/* test_img.c - 只测 img_import 的文件复制逻辑（不动用户的真实数据） */
#include <stdio.h>
#include "../src/app.h"

int main(void)
{
    WCHAR name[64];
    WCHAR full[MAX_PATH];
    int ok, i;

    printf("APPDATA = %ls\n", _wgetenv(L"APPDATA"));

    /* 1) 普通 png */
    ok = img_import(L"F:\\TodoList\\out\\src-pic.png", name, 64);
    printf("1) png  -> ok=%d name=%ls\n", ok, name);
    if (ok) {
        img_path_of(name, full, MAX_PATH);
        printf("   文件存在=%d\n",
               GetFileAttributesW(full) != INVALID_FILE_ATTRIBUTES);
    }

    /* 2) 大写扩展名要转小写 */
    ok = img_import(L"F:\\TodoList\\out\\SRC2.JPG", name, 64);
    printf("2) JPG  -> ok=%d name=%ls\n", ok, name);

    /* 3) 没有扩展名 */
    ok = img_import(L"F:\\TodoList\\out\\noext", name, 64);
    printf("3) 无扩展名 -> ok=%d name=%ls\n", ok, name);

    /* 4) 不存在的源文件应失败 */
    ok = img_import(L"F:\\TodoList\\out\\nope-not-here.png", name, 64);
    printf("4) 不存在 -> ok=%d (应为 0)\n", ok);

    /* 5) 空指针 */
    ok = img_import(NULL, name, 64);
    printf("5) NULL -> ok=%d (应为 0)\n", ok);

    printf("\nimg 目录内容:\n");
    {
        WIN32_FIND_DATAW fd;
        WCHAR dir[MAX_PATH], pat[MAX_PATH];
        HANDLE h;
        data_path(dir, MAX_PATH, NULL);
        wcpy(pat, dir, MAX_PATH);
        wcat(pat, L"\\img\\*", MAX_PATH);
        h = FindFirstFileW(pat, &fd);
        if (h == INVALID_HANDLE_VALUE) { printf("  (空)\n"); return 0; }
        i = 0;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            printf("  %ls\n", fd.cFileName);
            i++;
        } while (FindNextFileW(h, &fd));
        FindClose(h);
        printf("  共 %d 个\n", i);
    }
    return 0;
}
