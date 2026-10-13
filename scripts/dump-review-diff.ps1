$Base = "d56f17e1f965731a60b19cdb445df67ca7cad279"
$Branch = (git branch --show-current).Trim()
if ($Branch -ne "review") { Write-Error "Not on review branch (current: $Branch). Abort."; exit 1 }
git cat-file -e "$Base^{commit}" 2>$null; if ($LASTEXITCODE -ne 0) { Write-Error "Base commit missing: $Base"; exit 1 }
$Root = Split-Path $PSScriptRoot -Parent
git diff "$Base..HEAD" -- tests/ ":(exclude)*.svg" > (Join-Path $Root "review-diff-tests.txt")
git diff "$Base..HEAD" -- . ":(exclude)tests/" ":(exclude)*.svg" > (Join-Path $Root "review-diff-rest.txt")
