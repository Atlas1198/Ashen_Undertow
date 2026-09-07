$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path $PSScriptRoot -Parent
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$installation = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$installation) { throw 'Visual Studio C++ tools were not found.' }
$devCmd = Join-Path $installation 'Common7\Tools\VsDevCmd.bat'
$common = 'src\Engine\ComponentManager.cpp src\Engine\ObjectManager.cpp src\Engine\Collider.cpp src\Engine\CollisionManager.cpp'
$suites = @{
    ComponentManagerTests = $common
    CollisionTests = $common
    EngineTests = "$common src\Engine\Camera.cpp src\Engine\ModelManager.cpp src\Engine\GltfLoader.cpp src\Engine\Json.cpp"
}
Push-Location $repoRoot
try {
    foreach ($name in ($suites.Keys | Sort-Object)) {
        $outputDir = "x64\Tests\$name"
        New-Item -ItemType Directory -Force -Path $outputDir | Out-Null
        $command = '"{0}" -arch=x64 -host_arch=x64 && cl /nologo /std:c++20 /EHsc /utf-8 /UNDEBUG /I src /I src\Engine tests\{1}.cpp {2} /Fo:{3}\ /Fe:{3}\{1}.exe /link /SUBSYSTEM:CONSOLE && {3}\{1}.exe' -f $devCmd, $name, $suites[$name], $outputDir
        & cmd /d /c $command
        if ($LASTEXITCODE -ne 0) { throw "$name failed (exit $LASTEXITCODE)." }
        Write-Host "$name passed."
    }
}
finally { Pop-Location }
