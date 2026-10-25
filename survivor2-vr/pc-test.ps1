param([string]$Python='python', [string]$ExistingFolder)
$ErrorActionPreference='Stop'
$repoTask=Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'Check Survivor 2 Files.ps1') -Library
$fixtureTask=Join-Path $repoTask 'build-research/pc-fixtures'
& $Python -I (Join-Path $PSScriptRoot 'tests/pc_fixtures.py') $fixtureTask
if ($LASTEXITCODE -ne 0) { throw 'Fixture generation failed' }
$casesTask=@{valid=$true;embedded=$true;missing_chip=$false;wrong_chip=$false;bad_duplicate=$false;bad_bios=$false;truncated=$false;missing_bios=$false;missing_zip=$false}
$failedTask=0
foreach($caseTask in $casesTask.Keys){$resultTask=Test-SurvivorFiles (Join-Path $fixtureTask $caseTask);if($resultTask.Ready -ne $casesTask[$caseTask]){Write-Host "FAIL $caseTask $($resultTask.Message)";$failedTask++}}
if($ExistingFolder){$resultTask=Test-SurvivorFiles $ExistingFolder;if(-not $resultTask.Ready){Write-Host "FAIL existing World set: $($resultTask.Message)";$failedTask++}}
$totalTask=9+$(if($ExistingFolder){1}else{0})
Write-Host "$($totalTask-$failedTask)/$totalTask PC setup checks passed"
exit $(if($failedTask){1}else{0})
