param([string]$Python = 'python')
$ErrorActionPreference = 'Stop'
$taskRoot = Split-Path -Parent $PSScriptRoot
$assets = Join-Path $taskRoot 'installer/assets'
foreach ($name in @('release.json','xploader.img','xploader-install-guard')) {
    if (-not (Test-Path -LiteralPath (Join-Path $assets $name) -PathType Leaf)) {
        throw "Build/supply the pinned installer asset first: $name"
    }
}
& $Python -m PyInstaller --noconfirm --onefile --console --name xploader-v1.0 `
    --paths $taskRoot --add-data "${assets}:installer/assets" `
    --distpath (Join-Path $taskRoot 'build/installer-dist') `
    --workpath (Join-Path $taskRoot 'build/pyinstaller') `
    --specpath (Join-Path $taskRoot 'build/pyinstaller') `
    (Join-Path $taskRoot 'installer/app.py')
if ($LASTEXITCODE -ne 0) { throw 'Installer executable build failed' }
