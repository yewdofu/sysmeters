# vim: set ft=ps1 fenc=utf-8 ff=unix sw=4 ts=4 et :
# ==================================================
# 依存ヘッダオンリーライブラリの取得スクリプト
# toml11 と nlohmann/json を include/ に配置する
# ==================================================
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$includeDir = Join-Path $PSScriptRoot "..\include"
if (-not (Test-Path $includeDir)) { New-Item -ItemType Directory -Path $includeDir | Out-Null }

# toml11 シングルヘッダ版
$tomlDest = Join-Path $includeDir "toml.hpp"
if (-not (Test-Path $tomlDest)) {
    Write-Host "Downloading toml11..."
    $tomlUrl = "https://raw.githubusercontent.com/ToruNiina/toml11/v4.2.0/single_include/toml.hpp"
    Invoke-WebRequest -Uri $tomlUrl -OutFile $tomlDest
    Write-Host "  -> $tomlDest"
}
else {
    Write-Host "toml.hpp already exists, skipping."
}

# nlohmann/json シングルヘッダ版
$jsonDest = Join-Path $includeDir "json.hpp"
if (-not (Test-Path $jsonDest)) {
    Write-Host "Downloading nlohmann/json..."
    $jsonUrl = "https://raw.githubusercontent.com/nlohmann/json/v3.11.3/single_include/nlohmann/json.hpp"
    Invoke-WebRequest -Uri $jsonUrl -OutFile $jsonDest
    Write-Host "  -> $jsonDest"
}
else {
    Write-Host "json.hpp already exists, skipping."
}

Write-Host "All dependencies ready."
