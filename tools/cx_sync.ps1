# cx_sync.ps1 - 学习通作业同步器（只读，供 DeskTodo 调用）
# 输出 %APPDATA%\DeskTodo\sync.json 与 sync.progress
# 要点：
#   1. 只靠 sectionId=0 查课程会漏掉「当前学期」的课，必须再按学期筛选查一次（sectionId + semesterNum）
#   2. 学习通会限流：请求过快时会返回一个不含 oldenc 的提示页，所以要放慢节奏并重试
#   3. 作业 DDL 要取「截止时间」，不是「开始时间」
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Security
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$DataDir = Join-Path $env:APPDATA 'DeskTodo'
if (-not (Test-Path -LiteralPath $DataDir)) { New-Item -ItemType Directory -Path $DataDir -Force | Out-Null }
$OutJson  = Join-Path $DataDir 'sync.json'
$OutProg  = Join-Path $DataDir 'sync.progress'
$RetireFile = Join-Path $DataDir 'sync_retired.txt'
$CredFile = Join-Path $DataDir 'cx.dat'

# 发包节奏与重试
$PaceMs   = 3500
$TryMax   = 3
$DoExam   = $false   # 考试页结构未验证，默认不做，避免多一倍请求触发限流
# 一门课「最新的作业」都比这个天数还老 → 认定是已经结束的旧课，记进退休名单，
# 以后每次同步都直接跳过（不再请求它的入口页和作业列表）。
$RetireDays = 100
# 一次同步最多跑这么久（秒）。超时就带着已拿到的结果收工，别让用户干等，
# 也别撞上主程序那边的 600 秒超时（那会直接杀掉进程、结果全丢）。
$BudgetSec = 460

function Clean([string]$s) {
  if ($null -eq $s) { return '' }
  return ($s -replace '&nbsp;', ' ' -replace '&amp;', '&' -replace '&lt;', '<' -replace '&gt;', '>' -replace '\s+', ' ').Trim()
}
function WriteProg([string]$s) {
  try { [System.IO.File]::WriteAllText($OutProg, $s, (New-Object System.Text.UTF8Encoding($false))) } catch { }
}
function WriteResult($obj) {
  $json = $obj | ConvertTo-Json -Depth 6 -Compress
  [System.IO.File]::WriteAllText($OutJson, $json, (New-Object System.Text.UTF8Encoding($false)))
}
function Fail([string]$msg) {
  WriteResult @{ ok = $false; at = (Get-Date).ToString('s'); error = $msg; skipped = 0; total = 0; items = @() }
  WriteProg ('失败: ' + $msg)
  exit 1
}
function Pace { Start-Sleep -Milliseconds $PaceMs }

WriteProg '读取账号'
if (-not (Test-Path -LiteralPath $CredFile)) { Fail '没有保存学习通账号（cx.dat 不存在）' }
try {
  $blob = [System.IO.File]::ReadAllBytes($CredFile)
  $plain = [System.Text.Encoding]::UTF8.GetString([System.Security.Cryptography.ProtectedData]::Unprotect($blob, $null, 'CurrentUser'))
  $parts = $plain -split "`n", 2
  $cxUser = $parts[0].Trim()
  $cxPass = $parts[1]
} catch { Fail '账号文件解密失败' }
if (-not $cxUser -or -not $cxPass) { Fail '账号或密码为空' }

$ua = 'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/131.0.0.0 Safari/537.36 Edg/131.0.0.0'
$sess = $null

WriteProg '登录学习通'
$loginOk = $false
for ($try = 1; $try -le 2 -and -not $loginOk; $try++) {
  try {
    $body = @{ uname = $cxUser; password = $cxPass; refer = 'https://i.mooc.chaoxing.com'; t = 'true'; forbidotherlogin = '0' }
    $r = Invoke-WebRequest -Uri 'https://passport2.chaoxing.com/fanyalogin' -Method Post -Body $body -UseBasicParsing -SessionVariable sess -UserAgent $ua -TimeoutSec 30
    $j = $r.Content | ConvertFrom-Json
    if ($j.status) { $loginOk = $true }
    else {
      if ($try -ge 2) { Fail ('登录失败: ' + [string]$j.msg2) }
      Start-Sleep -Seconds 2
    }
  } catch {
    if ($try -ge 2) { Fail ('登录异常: ' + $_.Exception.Message) }
    Start-Sleep -Seconds 2
  }
}

WriteProg '获取课程入口'
try {
  $space = (Invoke-WebRequest -Uri 'https://i.mooc.chaoxing.com/space/index?ws=1' -UseBasicParsing -WebSession $sess -UserAgent $ua -TimeoutSec 40).Content
  $m = [regex]::Match($space, "switchM\('zne_kc_icon','([^']+)'\)")
  if (-not $m.Success) { Fail '空间页里没找到课程入口令牌' }
  $coursePageUrl = $m.Groups[1].Value
  $fu = [uri]$coursePageUrl
  $hostBase = $fu.Scheme + '://' + $fu.Host
  $refer = $coursePageUrl
  Pace
  $cp = (Invoke-WebRequest -Uri $coursePageUrl -UseBasicParsing -WebSession $sess -UserAgent $ua -TimeoutSec 60).Content
} catch { Fail ('获取课程页异常: ' + $_.Exception.Message) }

# 学期下拉：<option value="44312" semesterNum="20261" selected="true" >2026秋</option>
# 注意两点：
#   ① 不能只认「下拉第一项」——页面排序会变，真正代表当前学期的是 selected="true" 那一项。
#   ② 不能假设属性顺序固定，所以先把整个 <select name="xq"> 抠出来，再逐个 option 解析。
# 第四列 = 是否选中。
$semOpts = @()
$selBlock = [regex]::Match($cp, '(?s)<select[^>]*name="xq"[^>]*>(.*?)</select>')
if ($selBlock.Success) {
  foreach ($o in [regex]::Matches($selBlock.Groups[1].Value, '<option([^>]*)>([^<]*)</option>')) {
    $attrs = $o.Groups[1].Value
    $mv = [regex]::Match($attrs, 'value="(\d+)"')
    if (-not $mv.Success) { continue }
    if ($mv.Groups[1].Value -eq '0') { continue }
    $mn = [regex]::Match($attrs, 'semesterNum="(\d+)"')
    $semNum = ''
    if ($mn.Success) { $semNum = $mn.Groups[1].Value }
    $semSel = 0
    if ($attrs -match 'selected') { $semSel = 1 }
    $semOpts += , @($mv.Groups[1].Value, $semNum, (Clean $o.Groups[2].Value), $semSel)
  }
}
# 兜底：有的页面把下拉拆在不同结构里，整页再扫一遍
if ($semOpts.Count -eq 0) {
  foreach ($o in [regex]::Matches($cp, '<option value="(\d+)" semesterNum="(\d+)"([^>]*)>\s*([^<]*?)\s*</option>')) {
    if ($o.Groups[1].Value -eq '0') { continue }
    $semSel = 0
    if ($o.Groups[3].Value -match 'selected') { $semSel = 1 }
    $semOpts += , @($o.Groups[1].Value, $o.Groups[2].Value, (Clean $o.Groups[4].Value), $semSel)
  }
}

# 每门课自己的 cpi（账号 personId）→ 换账号也能用；取不到时回落到本次抓到的第一个
$cpiMap = @{}
$cpiDefault = ''
function Get-Cpi([string]$cid, [string]$clz) {
  $k = $cid + '_' + $clz
  if ($cpiMap.ContainsKey($k) -and $cpiMap[$k]) { return $cpiMap[$k] }
  if ($cpiDefault) { return $cpiDefault }
  return ''
}

function FetchCourses([string]$sectionId, [string]$semesterNum) {
  $u = $hostBase + '/fyportal/courselist/getStudyCourse?sectionId=' + $sectionId + '&semesterNum=' + $semesterNum + '&coursesource=0&coursename=&searchkkstatus=0&belongSchoolId=0'
  $f = (Invoke-WebRequest -Uri $u -UseBasicParsing -WebSession $sess -UserAgent $ua -Headers @{ Referer = $coursePageUrl } -TimeoutSec 60).Content
  $ckmap = @{}
  foreach ($m in [regex]::Matches($f, 'entercoursenewfy\?role=3&courseId=(\d+)&clazzId=(\d+)&cpi=(\d+)&ckenc=([0-9a-f]+)')) {
    $k = $m.Groups[1].Value + '_' + $m.Groups[2].Value
    $ckmap[$k] = $m.Groups[4].Value
    # cpi 是账号级的 personId（每个账号不同），必须从课程卡片链接里按课程取，绝不能写死
    $cv = $m.Groups[3].Value
    if ($cv) {
      # 必须显式写 $script: —— 函数内部的赋值会创建局部变量，外面根本看不到
      $script:cpiMap[$k] = $cv
      if (-not $script:cpiDefault) { $script:cpiDefault = $cv }
    }
  }
  $out = @()
  foreach ($c in [regex]::Matches($f, '<li class="w_couritem[^>]*cid="(\d+)"\s+classid="(\d+)"\s+cname="([^"]*)"')) {
    $cid = $c.Groups[1].Value; $clz = $c.Groups[2].Value
    $out += , @($cid, $clz, $c.Groups[3].Value, $ckmap[$cid + '_' + $clz])
  }
  return , $out
}

$courses = @()
$seenCou = @{}
$dbgCurN = 0
$dbgAllN = 0
$dbgWarn = ''
$noSem = 0
try {
  # 要查哪些学期：① 带 selected="true" 的那一项（页面自己标的当前学期）
  #               ② 下拉里的前两项（兜底，防止排序不同导致 ① 不是真正上课的那个学期）
  # 多查一次只多一个请求，但能避免「只同步到老课」这种最要命的错误。
  # 先只查「当前学期」（页面 selected="true" 的那一项，没有标记就取第一项）。
  # 限流是按请求量算的，所以额外学期只在「当前学期查到 0 门」时才补查。
  $selIdx = -1
  for ($i = 0; $i -lt $semOpts.Count; $i++) {
    if ($semOpts[$i][3] -eq 1) { $selIdx = $i; break }
  }
  if ($selIdx -lt 0) { $selIdx = 0 }
  if ($semOpts.Count -gt 0) {
    $cur = $semOpts[$selIdx]
    WriteProg ('查询 ' + $cur[2] + ' 课程')
    Pace
    $lst = FetchCourses $cur[0] $cur[1]
    $dbgCurN = $lst.Count
    foreach ($c in $lst) {
      $key = $c[0] + '_' + $c[1]
      if (-not $seenCou.ContainsKey($key)) { $seenCou[$key] = 1; $courses += , @($c[0], $c[1], $c[2], $c[3], 1) }
    }
    # 兜底：当前学期一门都没查到，可能是排序/标记不同，试着补查下拉里的其它学期
    if ($lst.Count -eq 0) {
      $extra = 0
      for ($i = 0; $i -lt $semOpts.Count -and $extra -lt 2; $i++) {
        if ($i -eq $selIdx) { continue }
        $o = $semOpts[$i]
        WriteProg ('兜底查询 ' + $o[2] + ' 课程')
        Pace
        $l2 = FetchCourses $o[0] $o[1]
        $extra++
        foreach ($c in $l2) {
          $key = $c[0] + '_' + $c[1]
          if (-not $seenCou.ContainsKey($key)) { $seenCou[$key] = 1; $courses += , @($c[0], $c[1], $c[2], $c[3], 1) }
        }
      }
    }
  } else {
    # 有些人的门户（例如 fycourse.fanya.chaoxing.com/courselist/mycourse）根本没有学期下拉，
    # 课程只能从 sectionId=0 拿。这种账号把拿到的课一律按「当前学期」处理：重试给满、
    # 且不参与自动退休 —— 宁可慢一点，也不要因为判错学期把作业漏掉。
    $dbgWarn = '没有学期下拉，按全部课程处理'
    $noSem = 1
  }
  $defCur = 0
  if ($noSem -eq 1) { $defCur = 1 }

  # sectionId=0 = 历史学期（当前学期拿不到，必须靠上面那次）
  WriteProg '查询全部课程'
  Pace
  $all = FetchCourses '0' ''
  $dbgAllN = $all.Count
  foreach ($c in $all) {
    $key = $c[0] + '_' + $c[1]
    if (-not $seenCou.ContainsKey($key)) { $seenCou[$key] = 1; $courses += , @($c[0], $c[1], $c[2], $c[3], $defCur) }
  }
} catch { Fail ('获取课程列表异常: ' + $_.Exception.Message) }

if ($courses.Count -eq 0) { Fail '课程列表为空（可能登录态失效）' }

# 上一次被限流丢掉的课，这一次排到最前面先处理：
# 用户最需要的（当前学期有作业的）课通常在列表尾部，限流往往也是尾部先中招，
# 把上一次没拿到的那几门提到队首，就有最大的机会在额度还没耗尽时拿到。
$pendingFile = Join-Path $DataDir 'sync_pending.txt'
$pend = @{}
if (Test-Path -LiteralPath $pendingFile) {
  foreach ($ln in (Get-Content -LiteralPath $pendingFile -Encoding UTF8)) {
    if ($ln -match '^(\d+_\d+)') { $pend[$Matches[1]] = 1 }
  }
}
if ($pend.Count -gt 0) {
  $head = @()
  $tail = @()
  foreach ($c in $courses) {
    if ($pend.ContainsKey($c[0] + '_' + $c[1])) { $head += , $c } else { $tail += , $c }
  }
  $courses = @($head) + @($tail)
  WriteProg ('上次未完成的 ' + $head.Count + ' 门课优先处理')
}

$items = New-Object System.Collections.ArrayList
$total = $courses.Count
$idx = 0
$skipped = 0
$thr = 0
$retiredN = 0
$failNames = New-Object System.Collections.ArrayList
$failKeys = New-Object System.Collections.ArrayList

# 读退休名单（cid_clz \t 课程名 \t 最后作业日期 / 无作业 / 手动）
# 第三列写「手动」的是用户在「筛选课程」里手工加进来的，那门课即使属于当前学期也照样跳过。
$retired = @{}
$retiredMan = @{}
$t0 = Get-Date
$budgetHit = $false
if (Test-Path -LiteralPath $RetireFile) {
  foreach ($ln in (Get-Content -LiteralPath $RetireFile -Encoding UTF8)) {
    if ($ln -match '^(\d+_\d+)\t([^\t]*)(?:\t(.*))?$') {
      $k = $Matches[1]
      $retired[$k] = 1
      if ($Matches[3] -ne $null -and $Matches[3].Trim() -eq '手动') { $retiredMan[$k] = 1 }
    }
  }
}

# 读「保留名单」：用户在筛选窗口里取消勾选的课，永远不自动退休
$keep = @{}
$keepFile = Join-Path $DataDir 'sync_keep.txt'
if (Test-Path -LiteralPath $keepFile) {
  foreach ($ln in (Get-Content -LiteralPath $keepFile -Encoding UTF8)) {
    if ($ln -match '^(\d+_\d+)\t') { $keep[$Matches[1]] = 1 }
  }
}

foreach ($c in $courses) {
  # 总时长预算：宁可带着已拿到的结果收工，也别拖到主程序 600 秒超时被杀掉（那样一条都留不下）
  if (((Get-Date) - $t0).TotalSeconds -gt $BudgetSec) { $budgetHit = $true; break }
  $cid = $c[0]; $clz = $c[1]; $cname = $c[2]; $ck = $c[3]; $isCur = $c[4]
  $couKey = $cid + '_' + $clz
  $idx++
  # 当前学期的课不会被「自动退休」误伤，但用户手动拉黑的照样跳（筛选课程窗口）；
  # 反过来，进了 sync_keep.txt 的课是要保留的，即使名单里有它也照常同步。
  if ($retired.ContainsKey($couKey) -and -not $keep.ContainsKey($couKey) -and
      ($retiredMan.ContainsKey($couKey) -or -not $isCur)) {
    $retiredN++
    WriteProg ("$idx/$total " + $cname + ' 旧课跳过')
    continue
  }
  WriteProg ("$idx/$total " + $cname)
  if (-not $ck) { $skipped++; [void]$failNames.Add($cname + '(无课enc)'); [void]$failKeys.Add($couKey); continue }

  $page = $null
  $ref = $refer
  $oldenc = ''; $openc = ''; $enc = ''
  $maxDl = [datetime]::MinValue
  # 历史学期的课只试一次：这类课以失败告终是常态，重试 3 轮纯浪费时间和限流额度
  $tmax = if ($isCur) { $TryMax } else { 1 }
  for ($try = 1; $try -le $tmax; $try++) {
    try {
      Pace
      $entry = $hostBase + '/fyportal/courselist/entercoursenewfy?role=3&courseId=' + $cid + '&clazzId=' + $clz + '&cpi=' + (Get-Cpi $cid $clz) + '&ckenc=' + $ck
      $x = Invoke-WebRequest -Uri $entry -UseBasicParsing -WebSession $sess -UserAgent $ua -TimeoutSec 45
      $page = $x.Content
      $ref = $x.BaseResponse.ResponseUri.ToString()
      $enc    = [regex]::Match($page, 'id="enc"[^>]*value="([^"]*)"').Groups[1].Value
      $openc  = [regex]::Match($page, 'id="openc"[^>]*value="([^"]*)"').Groups[1].Value
      $oldenc = [regex]::Match($page, 'id="oldenc"[^>]*value="([^"]*)"').Groups[1].Value
      if ($oldenc) { break }
      WriteProg ("$idx/$total " + $cname + ' 限流重试')
      Start-Sleep -Seconds (15 * $try)
    } catch {
      Start-Sleep -Seconds (15 * $try)
    }
  }
  if (-not $oldenc) {
    $thr++
    $skipped++
    [void]$failNames.Add($cname + '(无入口页)'); [void]$failKeys.Add($couKey)
    # 连续 3 门拿不到入口页就整体歇 60 秒：限流是按请求量算的，硬撞只会更糟
    if ($thr -ge 3) { WriteProg '限流休息 90 秒'; Start-Sleep -Seconds 90; $thr = 0 }
    continue
  }
  $thr = 0

  try {
    Pace
    $wu = 'https://mooc1.chaoxing.com/work/getAllWork?courseId=' + $cid + '&classId=' + $clz + '&isdisplaytable=2&ut=s&cpi=' + (Get-Cpi $cid $clz) + '&openc=' + $openc + '&enc=' + $oldenc
    $wh = (Invoke-WebRequest -Uri $wu -UseBasicParsing -WebSession $sess -UserAgent $ua -Headers @{ Referer = $ref } -TimeoutSec 45).Content
    $chunks = [regex]::Split($wh, 'class="titTxt"')
    $rowN = $chunks.Count - 1
    for ($k = 1; $k -lt $chunks.Count; $k++) {
      $ch = $chunks[$k]
      $mw = [regex]::Match($ch, 'workId=(\d+)')
      $mt = [regex]::Match($ch, '<a[^>]*title="([^"]*)"')
      if (-not $mt.Success) { continue }
      $title = Clean $mt.Groups[1].Value
      if (-not $title) { continue }
      $wid = if ($mw.Success) { $mw.Groups[1].Value } else { '' }
      $dl = [regex]::Match($ch, '截止时间：</span>\s*([0-9\-: ]+)')
      $st = [regex]::Match($ch, '作业状态：</span>\s*<strong>\s*([^<]*)')
      $status = if ($st.Success) { Clean $st.Groups[1].Value } else { '' }
      $deadline = if ($dl.Success) { Clean $dl.Groups[1].Value } else { '' }
      if ($deadline) {
        $dtx = [datetime]::MinValue
        if ([datetime]::TryParseExact($deadline, 'yyyy-MM-dd HH:mm', $null, 'None', [ref]$dtx)) {
          if ($dtx -gt $maxDl) { $maxDl = $dtx }
        }
      }
      if ($status -match '已完成|待批阅') { continue }
      # 已过期且超过 30 天的老作业一律不再收，否则整屏都是上学期的红色条目。
      # 注意：不能只对「作业状态=已过期」的行走这条规则 —— 有些页面的状态栏是空的，
      # 只看截止时间才靠得住。
      if ($deadline) {
        $dtOld = [datetime]::MinValue
        if ([datetime]::TryParseExact($deadline, 'yyyy-MM-dd HH:mm', $null, 'None', [ref]$dtOld)) {
          if ($dtOld -lt (Get-Date).AddDays(-30)) { continue }
        }
      }
      [void]$items.Add(@{ kind = 'work'; course = $cname; title = $title; status = $status; ddl = $deadline; sid = $wid })
    }

    # 整门课最新的作业都很久以前 → 认定是已经结束的旧课，记进退休名单。
    # 以后每次同步直接跳过它，连入口页请求都不发，省时间也省限流额度。
    # 「一条作业都没有」也算旧课，但要先确认拿到的是一张真正的列表页（限流的壳页只有几百字节）。
    # 用户在「筛选课程」里取消勾选的课会进 sync_keep.txt，那是有意要保留的，不许自动退休。
    if (-not $isCur -and -not $keep.ContainsKey($couKey)) {
      $stale = $false
      if ($rowN -eq 0) {
        if ($wh -match '暂无|没有相关|无作业' -or $wh.Length -gt 25000) { $stale = $true }
      } elseif ($maxDl -ne [datetime]::MinValue -and $maxDl -lt (Get-Date).AddDays(-$RetireDays)) {
        $stale = $true
      }
      if ($stale) {
        $retired[$couKey] = 1
        try {
          $why = if ($rowN -eq 0) { '无作业' } else { $maxDl.ToString('yyyy-MM-dd') }
          $line = $couKey + "`t" + $cname + "`t" + $why + "`r`n"
          [System.IO.File]::AppendAllText($RetireFile, $line, (New-Object System.Text.UTF8Encoding($false)))
          $retiredN++
          WriteProg ("$idx/$total " + $cname + ' 记为旧课')
        } catch { }
      }
    }
  } catch {
    WriteProg ("$idx/$total " + $cname + ' 作业读取失败')
    $skipped++
    continue
  }

  if ($DoExam) {
    try {
      Pace
      $eu = 'https://mooc1.chaoxing.com/exam/test?courseId=' + $cid + '&classId=' + $clz + '&ut=s&cpi=' + (Get-Cpi $cid $clz) + '&openc=' + $openc + '&enc=' + $enc
      $eh = (Invoke-WebRequest -Uri $eu -UseBasicParsing -WebSession $sess -UserAgent $ua -Headers @{ Referer = $ref } -TimeoutSec 45).Content
      if ($eh -notmatch '暂无内容') {
        $echunks = [regex]::Split($eh, 'class="titTxt"')
        for ($k = 1; $k -lt $echunks.Count; $k++) {
          $ch = $echunks[$k]
          $mt = [regex]::Match($ch, '<a[^>]*title="([^"]*)"')
          if (-not $mt.Success) { continue }
          $title = Clean $mt.Groups[1].Value
          if (-not $title) { continue }
          $me = [regex]::Match($ch, '(?:examId|testId|id)=(\d+)')
          $dl = [regex]::Match($ch, '截止时间：</span>\s*([0-9\-: ]+)')
          $st = [regex]::Match($ch, '考试状态：</span>\s*<strong>\s*([^<]*)')
          $status = if ($st.Success) { Clean $st.Groups[1].Value } else { '' }
          $deadline = if ($dl.Success) { Clean $dl.Groups[1].Value } else { '' }
          if ($status -match '已完成|已批阅') { continue }
          $eid = if ($me.Success) { $me.Groups[1].Value } else { '' }
          [void]$items.Add(@{ kind = 'exam'; course = $cname; title = $title; status = $status; ddl = $deadline; sid = $eid })
        }
      }
    } catch { }
  }
}

# 给「筛选课程」窗口用的课程清单（含是否当前学期、是否已在退休名单里）
try {
  $cl = New-Object System.Collections.ArrayList
  foreach ($c in $courses) {
    $k = $c[0] + '_' + $c[1]
    $nm = $c[2]
    if ($nm -eq $null) { $nm = '' }
    $nm = $nm.Replace('\', '\\').Replace('"', '\"')
    $r  = if ($retired.ContainsKey($k)) { 1 } else { 0 }
    $m  = if ($retiredMan.ContainsKey($k)) { 1 } else { 0 }
    [void]$cl.Add('{ "key": "' + $k + '", "name": "' + $nm + '", "cur": ' + $c[4] + ', "retired": ' + $r + ', "manual": ' + $m + ' }')
  }
  $cj = '{ "at": "' + (Get-Date).ToString('s') + '", "courses": [' + ($cl -join ',') + '] }'
  [System.IO.File]::WriteAllText((Join-Path $DataDir 'courses.json'), $cj, (New-Object System.Text.UTF8Encoding($false)))
} catch { }

$dbg = @{
  sem    = [Math]::Min($semOpts.Count, 9)
  cur    = $dbgCurN
  all    = $dbgAllN
  warn   = $dbgWarn
  cpi    = $cpiDefault
  secs   = [int]((Get-Date) - $t0).TotalSeconds
  failed = @($failNames | Select-Object -First 8)
}
# 记下这次被限流挡住的课，下次点同步时它们排到队首
try {
  if ($failKeys.Count -gt 0) {
    $pl = @()
    foreach ($c in $courses) {
      if ($failKeys.Contains($c[0] + '_' + $c[1])) { $pl += (($c[0] + '_' + $c[1]) + "`t" + $c[2]) }
    }
    if ($pl.Count -gt 0) {
      [System.IO.File]::WriteAllText($pendingFile, (($pl -join "`r`n") + "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
    } else {
      Remove-Item -LiteralPath $pendingFile -Force -ErrorAction SilentlyContinue
    }
  } else {
    Remove-Item -LiteralPath $pendingFile -Force -ErrorAction SilentlyContinue
  }
} catch { }
WriteResult @{ ok = $true; at = (Get-Date).ToString('s'); error = ''; skipped = $skipped; retired = $retiredN; total = $total; partial = $budgetHit; failed = @($failNames | Select-Object -First 8); debug = $dbg; items = @($items) }
if ($retiredN -gt 0) { WriteProg ('完成: 新增 ' + $items.Count + ' 条，跳过旧课 ' + $retiredN + ' 门') }
else { WriteProg ('完成: 新增 ' + $items.Count + ' 条') }
