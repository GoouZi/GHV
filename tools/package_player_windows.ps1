param(
    [string]$Configuration = 'Release',
    [string]$OutputRoot = ''
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not $OutputRoot) { $OutputRoot = Join-Path $repoRoot 'releases' }
$packageName = 'GHV_Player_Windows_x64'
$packageDir = Join-Path $OutputRoot $packageName
$archive = Join-Path $OutputRoot ($packageName + '.zip')

cmake -S (Join-Path $repoRoot 'native') -B (Join-Path $repoRoot 'native\build')
if ($LASTEXITCODE) { throw 'CMake configure failed' }
cmake --build (Join-Path $repoRoot 'native\build') --config $Configuration --target GHVPlayer
if ($LASTEXITCODE) { throw 'GHV Player build failed' }

$resolvedOutput = [IO.Path]::GetFullPath($OutputRoot)
$resolvedPackage = [IO.Path]::GetFullPath($packageDir)
if ([IO.Path]::GetDirectoryName($resolvedPackage) -ne $resolvedOutput) {
    throw "Refusing to clean package path outside the requested output root: $resolvedPackage"
}
if (Test-Path -LiteralPath $resolvedPackage) {
    Remove-Item -LiteralPath $resolvedPackage -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $resolvedPackage | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'native\bin\GHV Player.exe') -Destination $packageDir
Copy-Item -LiteralPath (Join-Path $repoRoot 'apps\ghv_player\README.md') -Destination (Join-Path $packageDir 'README.md')
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSE') -Destination $packageDir

$redistBase = 'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Redist\MSVC'
$redistVersion = Get-ChildItem -LiteralPath $redistBase -Directory |
    Where-Object { Test-Path -LiteralPath (Join-Path $_.FullName 'x64\Microsoft.VC145.CRT\msvcp140.dll') } |
    Sort-Object Name -Descending |
    Select-Object -First 1
if (-not $redistVersion) { throw 'Microsoft VC redistributable files were not found' }
$crt = Join-Path $redistVersion.FullName 'x64\Microsoft.VC145.CRT'
$openmp = Join-Path $redistVersion.FullName 'x64\Microsoft.VC145.OpenMP'
foreach ($name in 'msvcp140.dll','vcruntime140.dll','vcruntime140_1.dll') {
    Copy-Item -LiteralPath (Join-Path $crt $name) -Destination $packageDir
}
Copy-Item -LiteralPath (Join-Path $openmp 'vcomp140.dll') -Destination $packageDir

if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive }
Compress-Archive -Path (Join-Path $packageDir '*') -DestinationPath $archive -CompressionLevel Optimal
Write-Output $packageDir
Write-Output $archive
