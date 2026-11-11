/* app.h - DeskTodo 共享定义
 * 纯 Win32 + GDI，无第三方依赖，内部字符串统一用 UTF-16(WCHAR)，
 * 只在读盘/写盘时转换成 UTF-8。 */
#ifndef APP_H
#define APP_H

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <wchar.h>
#include <string.h>
#include <stdlib.h>

#define APP_NAME   L"DeskTodo"
#define APP_TITLE  L"待办"
#define APP_MUTEX  L"DeskTodo_SingleInstance_v1"
#define RUN_KEY    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run"
#define RUN_NAME   L"DeskTodo"
#define DATA_DIR   L"DeskTodo"
#define BALLOON_ONCE L"BalloonShown"

#define WM_TRAY    (WM_APP + 1)
#define WM_SHOWAPP (WM_APP + 2)

#define ID_TRAY    1
#define ID_HOTKEY  1

#define T_ANIM     1
#define T_DAY      3
#define T_SYNC     4

/* 字号百分比范围（100 = 原始大小） */
#define FONT_MIN   70
#define FONT_MAX   200

#define MAX_TITLE    128
#define MAX_NOTE     2048
#define MAXTOK       24
#define MAX_VIS_ROWS 8

/* 调色板下标 */
enum { C_BG, C_BORDER, C_HEAD, C_TEXT, C_MUTED, C_HOVER, C_OVERDUE, C_TODAY,
       C_SOON, C_NORMAL, C_DONE, C_ACCENT, C_BOX, C_EDITBG, C_COUNT };

typedef struct {
    WCHAR      title[MAX_TITLE];
    int        has_ddl;
    SYSTEMTIME ddl;
    int        done;
    WCHAR     *note;    /* 说明文字，NULL = 没有 */
    WCHAR     *img;     /* 图片文件名（在 %APPDATA%\DeskTodo\img\ 下），NULL = 没有 */
} Todo;

typedef struct {
    Todo *v;
    int   n, cap;
    int  *view;      /* 显示顺序 -> 数组下标 */
    int   vcap;
} TodoList;

typedef struct {
    int x, y;        /* 窗口左上角 */
    int width;       /* 逻辑宽度(96dpi) */
    int topmost;
    int autostart;
    int theme;       /* 0 深色 1 浅色 */
    int manual_order;/* 1 = 手动顺序 */
    int edge;        /* 0 无 1 左 2 右 3 顶（拖到屏幕边松手就收起） */
    int peek;        /* 1 = 已收起贴边 */
    int font;        /* 字号百分比，100 = 原始大小 */
    int cl_page;     /* 自定义主题：页面背景 0xRRGGBB */
    int cl_bar;      /* 自定义主题：页边栏 */
    int cl_text;     /* 自定义主题：字体颜色 */
    int cl_glass;    /* 玻璃配色（底色） */
    int glass;       /* 1 = 启用玻璃磨砂 */
    int glass_a;     /* 玻璃不透明度 0..255 */
} Config;

/* 玻璃模式下把背景填成这个色键，让亚克力模糊层透上来 */
#define GLASS_KEY RGB(1, 0, 1)

typedef struct { int type, index, sub; } Hit;
enum { HIT_NONE, HIT_HEADER, HIT_ADD, HIT_ROW, HIT_CHECK, HIT_DEL,
       HIT_BLANK, HIT_SCROLL, HIT_BTN_ADD, HIT_BTN_MENU, HIT_NOTE };

struct App;
typedef struct App App;

struct App {
    HWND      hwnd;
    HINSTANCE hi;
    int       dpi;

    HFONT f_title, f_small, f_icon, f_strike;
    HICON hIcon, hIconSm;
    int   cursor_hand;

    HDC     memdc;
    HBITMAP membmp;
    int     memw, memh;

    TodoList todos;
    Config   cfg;

    int W, H;                 /* 客户区尺寸 */
    int header_h, row_h, add_h, pad, chk, sb_w;

    int scroll, content_h;
    int hover_row, hover_sub, hover_btn;

    int    editing;           /* 正在编辑的行号(=view下标)，-1 无 */
    int    edit_new;          /* 1 = 新增 */
    int    editlen, caret;
    WCHAR  edit[MAX_TITLE];

    int   drag_row, drag_to, dragging, pressed, press_row;
    POINT press;
    int   last_cursor_x, last_cursor_y;

    int anim_tx, anim_ty, animating;
    int tray_added;
    int quit;

    /* ---- 学习通同步 ---- */
    int    syncing;          /* 1 = 正在同步（子进程在跑） */
    HANDLE sync_proc;        /* 子进程句柄 */
    DWORD  sync_t0;          /* 开始时刻，用于超时 */
    int    sync_ticks;       /* 已等待的定时器次数 */
    WCHAR  sync_msg[96];     /* 标题栏状态文字，空则显示常规计数 */
    int    sync_msg_left;    /* 状态文字还要显示几个 tick（0 = 常驻到任务结束） */
};

/* ---------------- json.c ---------------- */
typedef enum { J_NULL, J_BOOL, J_NUM, J_STR, J_ARR, J_OBJ } JType;
typedef struct JVal JVal;
struct JVal {
    JType   t;
    double  num;
    int     b;
    WCHAR  *str;
    JVal  **items;
    int     n;
    WCHAR **keys;
};

JVal        *json_parse(const char *s);
void         json_free(JVal *v);
JVal        *json_get(JVal *o, const WCHAR *k);
const WCHAR *json_wstr(JVal *o, const WCHAR *k, const WCHAR *def);
int          json_int(JVal *o, const WCHAR *k, int def);
char        *w2u8(const WCHAR *w);
WCHAR       *u82w(const char *s);

/* ---------------- 字符串工具 (util 在 app.h 内联声明) ---------------- */
void  wcpy(WCHAR *d, const WCHAR *s, int n);
void  wcat(WCHAR *d, const WCHAR *s, int n);
void  wcat_int(WCHAR *d, int v, int n);
void  wtrim(WCHAR *s);
int   wlen(const WCHAR *s);
void  ensure_dir(const WCHAR *path);
void  ensure_data_dir(void);   /* 保证 %APPDATA%\DeskTodo 存在（全新机器首次运行必需） */
int   write_atomic(const WCHAR *path, const char *data, int len);
char *read_file(const WCHAR *path, int *lenout);

/* ---------------- store.c ---------------- */
void  store_init(App *a);
void  store_free(App *a);
int   store_load(App *a);
int   store_save(App *a);
int   cfg_load(App *a);
int   cfg_save(App *a);
void  list_build_view(App *a);
void  store_add(App *a, const WCHAR *text);
void  store_update(App *a, int todo_index, const WCHAR *text);
void  store_remove(App *a, int todo_index);
void  list_materialize_view(App *a);
void  list_reorder(App *a, int from_view, int to_view);
void  parse_todo_text(const WCHAR *in, WCHAR *title, int *has_ddl, SYSTEMTIME *ddl);
int   ddl_level(const Todo *t);
int   ddl_days(const Todo *t);
void  ddl_text(const Todo *t, WCHAR *buf, int n);
void  ddl_to_text(const SYSTEMTIME *s, WCHAR *buf, int n);
int   parse_date_str(const WCHAR *s, SYSTEMTIME *o);
void  data_path(WCHAR *out, int n, const WCHAR *file);
int   exe_path(WCHAR *out, int n);
int   autostart_get(void);
void  autostart_set(int on);
int   sys_cmp_time(const SYSTEMTIME *a, const SYSTEMTIME *b);
/* 说明 / 图片 */
int   store_set_detail(App *a, int ti, const WCHAR *note, const WCHAR *img);
int   store_add_full(App *a, const WCHAR *title, const WCHAR *note,
                     int has_ddl, const SYSTEMTIME *ddl);
void  img_path_of(const WCHAR *name, WCHAR *out, int n);
void  img_gc(App *a);
void  note_head(const WCHAR *note, WCHAR *buf, int n);
WCHAR *wdup(const WCHAR *s);

/* ---------------- detail.c ---------------- */
void  detail_open(App *a, int ti);
void  detail_destroy(void);
void  courses_open(App *a);      /* 「筛选课程」：手动维护旧课退休名单 */
void  courses_destroy(void);
HWND  detail_hwnd(void);

/* ---------------- theme.c ---------------- */
void     theme_init(App *a);
void     theme_free(App *a);
void     theme_defaults(App *a);          /* 把四个自定义色恢复成默认值 */
void     theme_apply_glass(App *a);       /* 开/关亚克力磨砂 */
void     theme_dlg_open(App *a);          /* 「主题」窗口 */
void     theme_dlg_destroy(void);
int      colorpick_open(App *a, const WCHAR *title, int *rgb, int *alpha);
COLORREF theme_color(App *a, int idx);
int      sc(App *a, int v);          /* 只按 DPI 缩放 */
int      fz(App *a, int v);          /* 按 DPI × 字号百分比缩放（界面骨架用它） */

/* ---------------- ui.c ---------------- */
void ui_metrics(App *a);
void ui_hdr_btn_rect(App *a, int which, RECT *r);
int  ui_row_count(App *a);
int  ui_needed_height(App *a);
int  ui_viewport_h(App *a);
int  ui_content_h(App *a);
void  ui_paint(App *a, HDC target);
Hit   ui_hit(App *a, int x, int y);
POINT ui_caret_pos(App *a);

/* ---------------- input.c ---------------- */
void input_mouse_move(App *a, int x, int y);
void input_lbutton_down(App *a, int x, int y);
void input_lbutton_up(App *a, int x, int y);
void input_lbutton_dbl(App *a, int x, int y);
void input_rbutton_up(App *a, int x, int y);
void input_wheel(App *a, int delta, int x, int y);
void input_key(App *a, WPARAM key);
void input_char(App *a, WPARAM ch);
void input_start_edit(App *a, int view_index, int is_new);
void input_commit(App *a);
void input_cancel(App *a);
void input_context_menu(App *a, int x, int y);
int  input_is_printable(WPARAM ch);

/* ---------------- sync.c（学习通同步，靠一个子进程跑 powershell） ---------------- */
int  sync_start(App *a);        /* 1 = 已启动，0 = 失败 */
void sync_tick(App *a);         /* 定时器里调用：检查子进程、读进度、合并结果 */
void sync_cancel(App *a);       /* 退出时清理 */
int  sync_has_script(void);     /* 账号文件与脚本是否就绪 */

/* ---------------- login.c（让用户自己输学习通账号密码） ---------------- */
void login_open(App *a);        /* 打开「登录学习通」窗口（模态） */
void login_destroy(void);       /* 主窗销毁时连带关掉 */
int  login_has_cred(void);      /* cx.dat 是否已存在 */

/* ---------------- widget.c ---------------- */
int   widget_create(App *a, HINSTANCE hi);
void  widget_fit(App *a);
void  widget_apply_region(App *a);
void  widget_save_pos(App *a);
void  widget_check_edge(App *a);
void  widget_slide_to(App *a, int tx, int ty);
void  widget_work_area(HWND hwnd, RECT *r);
void  widget_realign(App *a);
void  widget_peek_pos(App *a, int *px, int *py);
void  widget_set_topmost(App *a, int on);
void  widget_tray_add(App *a);
void  widget_tray_remove(App *a);
void  widget_show(App *a, int show);
void  widget_hide_to_tray(App *a);
void  widget_quit(App *a);
void  widget_set_caret(App *a);

#endif /* APP_H */
