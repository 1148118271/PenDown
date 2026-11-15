# 检查代码里用到的消息 key 是否都存在于两套语言文件中。
# 用法：powershell -ExecutionPolicy Bypass -File tools/check-i18n.ps1
$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$files = Get-ChildItem (Join-Path $root 'src') -Recurse -Include *.cpp, *.h
$used = @{}

foreach ($file in $files) {
    $text = Get-Content -LiteralPath $file.FullName -Raw -Encoding UTF8
    $pattern = 'UiText\("([^"]+)"\)|i18n::Tr\("([^"]+)"\)|i18n::Plural\("([^"]+)",'
    foreach ($match in [regex]::Matches($text, $pattern)) {
        if ($match.Groups[1].Success) {
            $key = $match.Groups[1].Value
        } elseif ($match.Groups[2].Success) {
            $key = $match.Groups[2].Value
        } else {
            $key = $match.Groups[3].Value
        }
        $used[$key] = $true
    }
}

function Read-LanguageFile([string]$path) {
    $keys = @{}
    foreach ($line in Get-Content -LiteralPath $path -Encoding UTF8) {
        $trimmed = $line.Trim()
        if (-not $trimmed -or $trimmed.StartsWith('#') -or -not $trimmed.Contains('=')) {
            continue
        }
        $keys[$trimmed.Substring(0, $trimmed.IndexOf('=')).Trim()] = $true
    }
    return $keys
}

$languages = @{
    'zh-CN' = Read-LanguageFile (Join-Path $root 'lang\zh-CN.lang')
    'en-US' = Read-LanguageFile (Join-Path $root 'lang\en-US.lang')
}

$missing = @()
foreach ($key in $used.Keys) {
    foreach ($language in $languages.Keys) {
        $keys = $languages[$language]
        if (-not ($keys.ContainsKey($key) -or
                  $keys.ContainsKey("$key.one") -or
                  $keys.ContainsKey("$key.many"))) {
            $missing += "$language : $key"
        }
    }
}

if ($missing.Count -gt 0) {
    $missing | Sort-Object
    Write-Error "Missing message keys: $($missing.Count)"
    exit 1
}

Write-Output "OK keys=$($used.Count)"
