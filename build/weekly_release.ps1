# 每周一自动编译并把产物收进 release/ —— 由定时任务调用，也可手工跑。
#
#   pwsh -NoProfile -File build\weekly_release.ps1
#
# 三步：update（Kconfig/Makefile/dtsi/dtb）→ build（userdebug + user 双档）→ release.py（拷四件套）。
# 每步的输出重定向到 logs\weekly_release_<时间戳>_<步骤>.log/.err（仓库根 logs/ 不入库）。
# 任一步非 0 退出即停，打印失败步骤与日志路径（定时任务的回复里直接引用这几行）。
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$py = 'D:\ExPrograms\DevelopTool\Python314\python.exe'
if (-not (Test-Path -LiteralPath $py)) { $py = 'python' }

$toolBin = (Resolve-Path 'tool\win_x86_64\bin').Path
$env:PATH = 'D:\ExPrograms\DevelopTool\msys64\mingw64\bin;' + $toolBin + ';C:\Windows\system32;C:\Windows;' + $env:PATH
$env:SHELL = 'cmd.exe'

New-Item -ItemType Directory -Force -Path 'logs' | Out-Null
$ts = Get-Date -Format 'yyyyMMdd_HHmm'

$steps = @(
    @{ name = 'update'; args = @('build/build.py', 'update') },
    @{ name = 'build';  args = @('build/build.py', 'build') },
    @{ name = 'copy';   args = @('build/release.py') }
)

foreach ($s in $steps) {
    $out = "logs\weekly_release_${ts}_$($s.name).log"
    $err = "logs\weekly_release_${ts}_$($s.name).err"
    $p = Start-Process -FilePath $py -ArgumentList $s.args -Wait -PassThru -NoNewWindow `
        -RedirectStandardOutput $out -RedirectStandardError $err
    if ($p.ExitCode -ne 0) {
        Write-Output "FAIL step=$($s.name) exit=$($p.ExitCode)"
        Write-Output "log=$out"
        Write-Output "err=$err"
        exit $p.ExitCode
    }
    Write-Output "ok step=$($s.name) log=$out"
}

Write-Output "OK weekly release done -> release\<日期>\<档位>\"
