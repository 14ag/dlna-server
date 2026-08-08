<#
.SYNOPSIS
    Release DLNA Server assets.
.PARAMETER Notes
    Switch to generate AI release notes using Gemini.
.PARAMETER Update
    Existing tag to update by adding assets (instead of creating new release).
#>
param(
    [switch]$Notes,
    [string]$Update = ""
)

$ErrorActionPreference = "Stop"

$repo = Split-Path -Parent $PSScriptRoot
$OutputDir = Join-Path $repo "output"

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw "gh CLI not found."
}

$Tag = $Update
if (-not $Tag) {
    if ($env:GITHUB_REF_NAME) {
        $Tag = $env:GITHUB_REF_NAME
    } else {
        $Tag = & git -C $repo describe --tags --exact-match HEAD 2>$null
        if (-not $Tag) {
            throw "No tag found. Pass -Update [tag] or set GITHUB_REF_NAME."
        }
    }
}

$releaseNotes = "Release assets build."
if ($Notes) {
    $GeminiApiKey = $env:GEMINI_API_KEY
    if (-not $GeminiApiKey) {
        throw "GEMINI_API_KEY environment variable is not set."
    }
    
    $prevTag = & git -C $repo describe --tags --abbrev=0 "$Tag^" 2>$null
    if ($LASTEXITCODE -ne 0 -or -not $prevTag) {
        $commits = & git -C $repo log --oneline
    } else {
        $commits = & git -C $repo log --oneline "$prevTag..$Tag"
    }
    if (-not $commits) { $commits = "No commits found." }
    $commitText = $commits -join "`n"

    $prompt = @"
You are a technical writer. Given the following git commit log for release $Tag of DLNA Server (a C++ UPnP/DLNA media server), write concise GitHub release notes in markdown.
Rules:
- Group changes under headings: ## What's New, ## Bug Fixes, ## Improvements
- Use bullet points
- Do not include the commit hashes
Commit log:
$commitText
"@
    $body = @{ contents = @( @{ parts = @( @{ text = $prompt } ) } ) } | ConvertTo-Json -Depth 10
    $apiUrl = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent?key=$GeminiApiKey"
    try {
        $response = Invoke-RestMethod -Uri $apiUrl -Method Post -ContentType "application/json" -Body $body
        $releaseNotes = $response.candidates[0].content.parts[0].text
    } catch {
        Write-Warning "Gemini API failed: $_"
        $releaseNotes = "## Changes`n`n$commitText"
    }
}

$assets = Get-ChildItem -LiteralPath $OutputDir -Recurse -File |
    Where-Object { $_.Extension -in @(".zip", ".deb", ".AppImage", ".flatpak") } |
    Select-Object -ExpandProperty FullName

if ($Update) {
    Write-Host "Updating release $Tag with assets..."
    foreach ($asset in $assets) {
        & gh release upload $Tag $asset --clobber
    }
} else {
    Write-Host "Creating release $Tag..."
    $notesFile = [System.IO.Path]::GetTempFileName()
    try {
        [System.IO.File]::WriteAllText($notesFile, $releaseNotes, [System.Text.Encoding]::UTF8)
        $ghArgs = @("release", "create", $Tag, "--title", $Tag, "--notes-file", $notesFile)
        $ghArgs += $assets
        & gh @ghArgs
    } finally {
        Remove-Item -LiteralPath $notesFile -Force -ErrorAction SilentlyContinue
    }
}
