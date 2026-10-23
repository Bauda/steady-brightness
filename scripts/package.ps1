param([string]$Tag = '')
$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$version = (Get-Content -LiteralPath (Join-Path $repo 'VERSION') -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw 'VERSION must be major.minor.patch.' }
if ($Tag -and $Tag -cne "v$version") { throw "Tag $Tag does not match VERSION $version." }
$exe = Join-Path $repo 'build\SteadyBrightness.exe'
$info = (Get-Item -LiteralPath $exe).VersionInfo
if ($info.FileVersion -ne $version -or $info.ProductVersion -ne $version) { throw 'Executable version does not match VERSION.' }
$manifest = [xml](Get-Content -LiteralPath (Join-Path $repo 'app.manifest') -Raw)
if ($manifest.assembly.assemblyIdentity.version -ne "$version.0") { throw 'Manifest version does not match VERSION.' }
$dist = Join-Path $repo 'dist'
if (Test-Path -LiteralPath $dist) { throw 'dist already exists. Move it aside before packaging to avoid stale files.' }
$stage = Join-Path $dist 'SteadyBrightness'
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $exe -Destination $stage
foreach ($name in @('install.cmd', 'LICENSE', 'THIRD-PARTY-NOTICES.txt')) { Copy-Item -LiteralPath (Join-Path $repo $name) -Destination $stage }
Copy-Item -LiteralPath (Join-Path $repo 'docs\install.md') -Destination (Join-Path $stage 'INSTALL.md')
@"
Steady Brightness $version - Windows x64

Extract the whole archive and run install.cmd to install for your account.
Or run SteadyBrightness.exe directly for portable use. No admin is needed.
Click the yellow sun near the clock. It may be inside the ^ overflow area.
Enable DDC/CI in your monitor's own menu if it is unavailable, then Refresh.
Automatic startup is optional in Options > Start with Windows.

Updates: exit the app, extract a new release, and run install.cmd again.
Removal: Windows Settings > Apps > Installed apps > Steady Brightness.
This executable is unsigned. Checksums and GitHub provenance are supplied
with the release; they are not a Windows code-signing certificate.

Source, help, and download verification:
https://github.com/Bauda/steady-brightness
https://github.com/Bauda/steady-brightness/blob/main/docs/install.md

MIT License. See LICENSE and THIRD-PARTY-NOTICES.txt.
"@ | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding UTF8
$zip = Join-Path $dist 'SteadyBrightness-Windows-x64.zip'
Compress-Archive -LiteralPath $stage -DestinationPath $zip -CompressionLevel Optimal
Copy-Item -LiteralPath $exe -Destination $dist
$checksums = foreach ($file in @($zip, (Join-Path $dist 'SteadyBrightness.exe'))) {
    $hash = Get-FileHash -LiteralPath $file -Algorithm SHA256
    '{0}  {1}' -f $hash.Hash.ToLowerInvariant(), [IO.Path]::GetFileName($file)
}
$checksums | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS') -Encoding ASCII
# Check archive contents before upload, without executing anything in the package.
Add-Type -AssemblyName System.IO.Compression.FileSystem
$archive = [IO.Compression.ZipFile]::OpenRead($zip)
try {
    $expected = @('SteadyBrightness.exe','install.cmd','LICENSE','THIRD-PARTY-NOTICES.txt','INSTALL.md','README.txt')
    $entries = @($archive.Entries | Where-Object Name)
    if ($entries.Count -ne $expected.Count) { throw 'Unexpected archive file count.' }
    foreach ($name in $expected) {
        if (@($entries | Where-Object { $_.FullName.Replace('\','/') -ceq "SteadyBrightness/$name" }).Count -ne 1) { throw "Missing or unexpected archive entry: $name" }
    }
} finally { $archive.Dispose() }
Write-Host "Packaged Steady Brightness $version"
$checksums | Write-Output
