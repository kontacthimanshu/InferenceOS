[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ArtifactPath,
    [Parameter(Mandatory)][ValidatePattern('^[a-z0-9][a-z0-9-]*$')][string]$ArtifactKind,
    [string]$OutputPath,
    [ValidateSet('file-sha256', 'zero-filled')][string]$ContentModel = 'file-sha256',
    [string[]]$InputSpec = @(),
    [string]$PropertiesJson = '{}'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Write-Utf8NoBom([string]$Path, [string]$Text) {
    [System.IO.File]::WriteAllText($Path, $Text, [System.Text.UTF8Encoding]::new($false))
}

function Get-StringSha256([string]$Text) {
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $digest = $algorithm.ComputeHash([System.Text.Encoding]::UTF8.GetBytes($Text))
        return ([BitConverter]::ToString($digest)).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

$artifact = [System.IO.Path]::GetFullPath($ArtifactPath)
if (-not [System.IO.File]::Exists($artifact)) { throw "Artifact '$artifact' does not exist." }
$artifactItem = Get-Item -LiteralPath $artifact
try {
    $properties = ConvertFrom-Json -InputObject $PropertiesJson
} catch {
    throw "PropertiesJson is invalid: $($_.Exception.Message)"
}
if ($null -eq $properties -or $PropertiesJson.TrimStart()[0] -ne '{') {
    throw 'PropertiesJson must encode a JSON object.'
}

$inputs = foreach ($specification in $InputSpec) {
    $separator = $specification.IndexOf('=')
    if ($separator -lt 1 -or $separator -eq $specification.Length - 1) {
        throw "InputSpec '$specification' must use role=path syntax."
    }
    $role = $specification.Substring(0, $separator)
    if ($role -notmatch '^[a-z0-9][a-z0-9._-]*$') { throw "Input role '$role' is invalid." }
    $path = [System.IO.Path]::GetFullPath($specification.Substring($separator + 1))
    if (-not [System.IO.File]::Exists($path)) { throw "Manifest input '$path' does not exist." }
    $item = Get-Item -LiteralPath $path
    [ordered]@{
        role = $role
        bytes = [uint64]$item.Length
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}
$inputs = @($inputs | Sort-Object { $_.role })
for ($index = 1; $index -lt $inputs.Count; ++$index) {
    if ($inputs[$index - 1].role -ceq $inputs[$index].role) {
        throw "Duplicate manifest input role '$($inputs[$index].role)'."
    }
}

$contentIdentity = if ($ContentModel -eq 'file-sha256') {
    [ordered]@{
        model = 'file-sha256'
        logical_bytes = [uint64]$artifactItem.Length
        digest_algorithm = 'sha256'
        digest_scope = 'all-file-bytes'
        digest = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
    }
} else {
    $descriptor = "zero-filled-v1|$([uint64]$artifactItem.Length)"
    [ordered]@{
        model = 'zero-filled'
        logical_bytes = [uint64]$artifactItem.Length
        digest_algorithm = 'sha256'
        digest_scope = 'canonical-zero-filled-v1-descriptor'
        digest = Get-StringSha256 $descriptor
    }
}
$recipe = [ordered]@{
    artifact_kind = $ArtifactKind
    content_model = $ContentModel
    inputs = $inputs
    properties = $properties
}
$recipeJson = $recipe | ConvertTo-Json -Depth 12 -Compress
$manifest = [ordered]@{
    schema_version = 1
    generator = 'tools/image/write_manifest.ps1'
    artifact_kind = $ArtifactKind
    recipe_sha256 = Get-StringSha256 $recipeJson
    content = $contentIdentity
    recipe = $recipe
}
$destination = if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    "$artifact.manifest.json"
} else {
    [System.IO.Path]::GetFullPath($OutputPath)
}
$manifestJson = ($manifest | ConvertTo-Json -Depth 12).Replace("`r`n", "`n").Replace("`r", "`n") + "`n"
if ([System.IO.File]::Exists($destination)) {
    try { $previous = Get-Content -Raw -LiteralPath $destination | ConvertFrom-Json } catch {
        throw "Existing manifest '$destination' is invalid and cannot be used for a reproducibility check."
    }
    if ($previous.schema_version -eq 1 -and
        $previous.recipe_sha256 -ceq $manifest.recipe_sha256 -and
        $previous.content.digest -cne $manifest.content.digest) {
        throw "Reproducibility check failed for '$artifact': the same recipe produced different content."
    }
}
[System.IO.Directory]::CreateDirectory((Split-Path -Parent $destination)) | Out-Null
Write-Utf8NoBom $destination $manifestJson
Write-Output $destination
