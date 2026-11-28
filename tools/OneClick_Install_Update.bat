@echo off
chcp 65001 >nul
title DST HyperShade (HyperSmooth) / HyperCore Installer
set "HS_DIR=%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -Command "$t=Join-Path $env:HS_DIR ([string]::Concat([char]0x4E00,[char]0x952E,[char]0x5B89,[char]0x88C5,[char]0x4E0E,[char]0x66F4,[char]0x65B0,'.bat')); $env:HS_SELF=$t; $c=[IO.File]::ReadAllText($t,[Text.Encoding]::UTF8); $m='#===PS_'+'START==='; $i=$c.IndexOf($m); if($i -lt 0){exit 1}; &([scriptblock]::Create($c.Substring($i)))"
exit /b %ERRORLEVEL%
