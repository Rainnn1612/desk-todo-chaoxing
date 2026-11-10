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
$PaceMs   = 2600
$TryMax   = 3
$DoExam   = $false   # 考试页结构未验证，默认不做，避免多一倍请求触发限流
# 一门课「最新的作业」都比这个天数还老 → 认定是已经结束的旧课，记进退休名单，
# 以后每次同步都直接跳过（不再请求它的入口页和作业列表）。
$RetireDays = 100

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
$semOpts = @()
foreach ($o in [regex]::Matches($cp, '<option value="(\d+)" semesterNum="(\d+)"[^>]*>\s*([^<]*?)\s*</option>')) {
  if ($o.Groups[1].Value -eq '0') { continue }
  $semOpts += , @($o.Groups[1].Value, $o.Groups[2].Value, $o.Groups[3].Value)
}

function FetchCourses([string]$sectionId, [string]$semesterNum) {
  $u = $hostBase + '/fyportal/courselist/getStudyCourse?sectionId=' + $sectionId + '&semesterNum=' + $semesterNum + '&coursesource=0&coursename=&searchkkstatus=0&belongSchoolId=0'
  $f = (Invoke-WebRequest -Uri $u -UseBasicParsing -WebSession $sess -UserAgent $ua -Headers @{ Referer = $coursePageUrl } -TimeoutSec 60).Content
  $ckmap = @{}
  foreach ($m in [regex]::Matches($f, 'entercoursenewfy\?role=3&courseId=(\d+)&clazzId=(\d+)&cpi=(\d+)&ckenc=([0-9a-f]+)')) {
    $ckmap[$m.Groups[1].Value + '_' + $m.Groups[2].Value] = $m.Groups[4].Value
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
try {
  WriteProg '查询全部课程'
  Pace
  # 当前学期（下拉第一项）必须排在最前面查：限流时循环尾部的课最先被放弃，
  # 而当前学期的作业正是用户最需要的那批。sectionId=0 拿不到当前学期的课。
  if ($semOpts.Count -gt 0) {
    $cur = $semOpts[0]
    WriteProg ('查询 ' + $cur[2] + ' 课程')
    Pace
    $curList = FetchCourses $cur[0] $cur[1]
    foreach ($c in $curList) {
      $key = $c[0] + '_' + $c[1]
      if (-not $seenCou.ContainsKey($key)) { $seenCou[$key] = 1; $courses += , @($c[0], $c[1], $c[2], $c[3], 1) }
    }
  }
  $all = FetchCourses '0' ''
  foreach ($c in $all) {
    $key = $c[0] + '_' + $c[1]
    if (-not $seenCou.ContainsKey($key)) { $seenCou[$key] = 1; $courses += , @($c[0], $c[1], $c[2], $c[3], 0) }
  }
} catch { Fail ('获取课程列表异常: ' + $_.Exception.Message) }

if ($courses.Count -eq 0) { Fail '课程列表为空（可能登录态失效）' }

$items = New-Object System.Collections.ArrayList
$total = $courses.Count
$idx = 0
$skipped = 0
$thr = 0
$retiredN = 0

# 读退休名单（cid_clz \t 课程名 \t 最后作业日期 / 无作业 / 手动）
# 第三列写「手动」的是用户在「筛选课程」里手工加进来的，那门课即使属于当前学期也照样跳过。
$retired = @{}
$retiredMan = @{}
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
  if (-not $ck) { $skipped++; continue }

  $page = $null
  $ref = $refer
  $oldenc = ''; $openc = ''; $enc = ''
  $maxDl = [datetime]::MinValue
  # 历史学期的课只试一次：这类课以失败告终是常态，重试 3 轮纯浪费时间和限流额度
  $tmax = if ($isCur) { $TryMax } else { 1 }
  for ($try = 1; $try -le $tmax; $try++) {
    try {
      Pace
      $entry = $hostBase + '/fyportal/courselist/entercoursenewfy?role=3&courseId=' + $cid + '&clazzId=' + $clz + '&cpi=482497561&ckenc=' + $ck
      $x = Invoke-WebRequest -Uri $entry -UseBasicParsing -WebSession $sess -UserAgent $ua -TimeoutSec 45
      $page = $x.Content
      $ref = $x.BaseResponse.ResponseUri.ToString()
      $enc    = [regex]::Match($page, 'id="enc"[^>]*value="([^"]*)"').Groups[1].Value
      $openc  = [regex]::Match($page, 'id="openc"[^>]*value="([^"]*)"').Groups[1].Value
      $oldenc = [regex]::Match($page, 'id="oldenc"[^>]*value="([^"]*)"').Groups[1].Value
      if ($oldenc) { break }
      WriteProg ("$idx/$total " + $cname + ' 限流重试')
      Start-Sleep -Seconds (5 * $try)
    } catch {
      Start-Sleep -Seconds (5 * $try)
    }
  }
  if (-not $oldenc) {
    $thr++
    $skipped++
    # 连续 3 门拿不到入口页就整体歇 60 秒：限流是按请求量算的，硬撞只会更糟
    if ($thr -ge 3) { WriteProg '限流休息 60 秒'; Start-Sleep -Seconds 60; $thr = 0 }
    continue
  }
  $thr = 0

  try {
    Pace
    $wu = 'https://mooc1.chaoxing.com/work/getAllWork?courseId=' + $cid + '&classId=' + $clz + '&isdisplaytable=2&ut=s&cpi=482497561&openc=' + $openc + '&enc=' + $oldenc
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
      # 已过期且超过 30 天的老作业不再收，否则整屏都是上学期的红色条目
      if ($status -match '已过期' -and $deadline) {
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
      $eu = 'https://mooc1.chaoxing.com/exam/test?courseId=' + $cid + '&classId=' + $clz + '&ut=s&cpi=482497561&openc=' + $openc + '&enc=' + $enc
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

WriteResult @{ ok = $true; at = (Get-Date).ToString('s'); error = ''; skipped = $skipped; retired = $retiredN; total = $total; items = @($items) }
if ($retiredN -gt 0) { WriteProg ('完成: 新增 ' + $items.Count + ' 条，跳过旧课 ' + $retiredN + ' 门') }
else { WriteProg ('完成: 新增 ' + $items.Count + ' 条') }
