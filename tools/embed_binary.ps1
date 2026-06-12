param(
    [Parameter(Mandatory = $true)]
    [string] $InputPath,

    [Parameter(Mandatory = $true)]
    [string] $OutputPath,

    [Parameter(Mandatory = $true)]
    [string] $Symbol
)

$bytes = [System.IO.File]::ReadAllBytes($InputPath)
$builder = [System.Text.StringBuilder]::new()

[void] $builder.AppendLine("namespace quader::filament_assets {")
[void] $builder.AppendLine("inline constexpr unsigned char $Symbol[] = {")

for ($index = 0; $index -lt $bytes.Length; $index += 16) {
    $end = [Math]::Min($index + 15, $bytes.Length - 1)
    $values = for ($cursor = $index; $cursor -le $end; ++$cursor) {
        "0x{0:x2}" -f $bytes[$cursor]
    }
    [void] $builder.AppendLine("    " + ($values -join ", ") + ",")
}

[void] $builder.AppendLine("};")
[void] $builder.AppendLine("inline constexpr unsigned int ${Symbol}_size = $($bytes.Length)u;")
[void] $builder.AppendLine("} // namespace quader::filament_assets")

$outputDirectory = [System.IO.Path]::GetDirectoryName($OutputPath)
if (-not [string]::IsNullOrEmpty($outputDirectory)) {
    [System.IO.Directory]::CreateDirectory($outputDirectory) | Out-Null
}

$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($OutputPath, $builder.ToString(), $utf8)
