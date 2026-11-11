# 参与贡献

欢迎提 Issue 和 PR。这个项目刻意保持「小」：单个 exe、零运行时依赖、不引入第三方库。

## 编译

只支持 Windows，工具链是 MinGW-w64（gcc）：

```bat
winget install BrechtSanders.WinLibs.POSIX.UCRT
build.bat
```

`build.bat` 会按 `MINGW_BIN` 环境变量 → PATH 里的 gcc → winget 安装目录 的顺序找编译器，找不到会打印安装提示。
产物在 `out\DeskTodo.exe`。

需要 Python 3（`build.bat` 用 `tools/make_icon.py` 生成 `res/app.ico`）。

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `src/widget.c` | 无边框窗口、拖动、贴边收起、托盘、热键、消息循环 |
| `src/ui.c` | 全部绘制（双缓冲 GDI）、命中测试、控件几何 |
| `src/input.c` | 鼠标键盘、行内编辑、拖拽调序、右键菜单 |
| `src/store.c` | 数据模型、JSON 读写、日期解析、原子落盘 |
| `src/json.c` | 极简 JSON 解析器 |
| `src/theme.c` | 调色板、DPI 与字号缩放、玻璃效果 |
| `src/themdlg.c` / `src/colorpick.c` | 「主题」窗口与 HSV 取色器 |
| `src/detail.c` | 「备注」窗口 |
| `src/courses.c` | 「筛选课程」窗口 |
| `src/trash.c` | 「垃圾箱」窗口 |
| `src/sync.c` | 学习通同步的子进程调度与结果合并 |
| `src/main.c` | 启动、单实例、命令行开关 |

## 提交前请自查

1. **能编译且零警告**（`-Wall -Wextra`，构建脚本已经开了）
2. **别引入第三方库**；能用 Win32 API 手写就手写
3. **别写 `printf` 族**——项目里没有 `stdio.h`，格式化请用 `store.c` 的 `SB` 构建器或 `wcpy/wcat/wcat_int`
4. `.bat` 文件必须是纯 ASCII + CRLF；`.ps1` 文件必须是 **UTF-8 with BOM**（PowerShell 5.1 否则按 GBK 解码，中文全乱）
5. 改动 `tools/cx_sync.ps1` 后跑一遍语法检查：

```powershell
[System.Management.Automation.Language.Parser]::ParseFile("tools\cx_sync.ps1", [ref]$null, [ref]$errs)
```

6. **不要把任何账号、手机号、密码、`cx.dat`、`todos.json` 提交进仓库**。同步用的凭据只以 DPAPI 加密形式存在于 `%APPDATA%\DeskTodo\cx.dat`。

## 关于学习通同步

同步功能是**模拟登录 + 页面解析**，仅用于把用户自己的作业清单备份到桌面。请勿提交任何强化爬取频率、绕过验证码、批量抓取他人数据的改动。
