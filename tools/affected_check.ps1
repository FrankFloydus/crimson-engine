################################################################################
# Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
# This file is part of Quader and is protected proprietary source code.
# No permission is granted to use, copy, modify, distribute, or sublicense this
# file except through prior written authorization from Francesco Di Blasi.
################################################################################
[CmdletBinding()]
param(
    [string]$BaseRef = "HEAD",
    [string[]]$ChangedPath = @(),
    [string]$ConfigurePreset = "vs2026",
    [string]$BuildPreset = "vs2026-debug",
    [string]$TestPreset = "vs2026-debug",
    [string]$Configuration = "Debug",
    [switch]$PlanOnly,
    [switch]$NoConfigure,
    [switch]$NoBuild,
    [switch]$NoTest,
    [switch]$IncludeApp,
    [switch]$MeasureFull,
    [switch]$NoUntracked
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$SourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$script:CMakePresetsDocument = $null
$script:CMakeTargetSourceCache = @{}

function Get-CMakePresetsDocument {
    if ($null -ne $script:CMakePresetsDocument) {
        return $script:CMakePresetsDocument
    }
    $presetsPath = Join-Path $SourceRoot "CMakePresets.json"
    if (-not (Test-Path $presetsPath -PathType Leaf)) {
        throw "CMakePresets.json was not found at '$presetsPath'."
    }
    $script:CMakePresetsDocument = Get-Content $presetsPath -Raw | ConvertFrom-Json
    return $script:CMakePresetsDocument
}

function Get-CMakePresetCollection {
    param([string]$Kind)

    $document = Get-CMakePresetsDocument
    switch ($Kind) {
        "configure" { return @($document.configurePresets) }
        "build" { return @($document.buildPresets) }
        "test" { return @($document.testPresets) }
        default { throw "Unsupported CMake preset kind '$Kind'." }
    }
}

function Get-CMakePreset {
    param([string]$Kind, [string]$Name)

    foreach ($preset in @(Get-CMakePresetCollection $Kind)) {
        $nameProperty = $preset.PSObject.Properties["name"]
        if ($null -ne $nameProperty -and [string]$nameProperty.Value -eq $Name) {
            return $preset
        }
    }
    return $null
}

function Get-CMakePresetProperty {
    param(
        [string]$Kind,
        [string]$Name,
        [string]$PropertyName,
        [string[]]$Visited = @()
    )

    $key = "${Kind}:${Name}"
    if ($Visited -contains $key) {
        throw "Cyclic CMake preset inheritance detected at '$key'."
    }

    $preset = Get-CMakePreset $Kind $Name
    if ($null -eq $preset) {
        return $null
    }

    $property = $preset.PSObject.Properties[$PropertyName]
    if ($null -ne $property) {
        return $property.Value
    }

    $inheritsProperty = $preset.PSObject.Properties["inherits"]
    if ($null -eq $inheritsProperty) {
        return $null
    }

    foreach ($parent in @($inheritsProperty.Value)) {
        $value = Get-CMakePresetProperty $Kind ([string]$parent) $PropertyName ($Visited + $key)
        if ($null -ne $value) {
            return $value
        }
    }
    return $null
}

function Expand-CMakePresetPath {
    param([string]$Path)

    $expanded = $Path.Replace('${sourceDir}', $SourceRoot)
    $expanded = [regex]::Replace(
        $expanded,
        '\$env\{([^}]+)\}',
        [System.Text.RegularExpressions.MatchEvaluator]{
            param($match)
            $value = [Environment]::GetEnvironmentVariable($match.Groups[1].Value)
            if ($null -eq $value) {
                return ""
            }
            return $value
        })
    return ($expanded -replace "/", [System.IO.Path]::DirectorySeparatorChar)
}

function Resolve-CMakePresetBinaryDir {
    param([string]$PresetName)

    $binaryDir = Get-CMakePresetProperty "configure" $PresetName "binaryDir"
    if ($null -eq $binaryDir -or [string]::IsNullOrWhiteSpace([string]$binaryDir)) {
        throw "Configure preset '$PresetName' does not define binaryDir. Add one so focused test executables can be located."
    }
    return Expand-CMakePresetPath ([string]$binaryDir)
}

if (-not $PSBoundParameters.ContainsKey("ConfigurePreset")) {
    $derivedConfigurePreset = Get-CMakePresetProperty "build" $BuildPreset "configurePreset"
    if ($null -ne $derivedConfigurePreset -and -not [string]::IsNullOrWhiteSpace([string]$derivedConfigurePreset)) {
        $ConfigurePreset = [string]$derivedConfigurePreset
    }
}

if (-not $PSBoundParameters.ContainsKey("Configuration")) {
    $derivedConfiguration = Get-CMakePresetProperty "build" $BuildPreset "configuration"
    if ($null -ne $derivedConfiguration -and -not [string]::IsNullOrWhiteSpace([string]$derivedConfiguration)) {
        $Configuration = [string]$derivedConfiguration
    }
}

$BuildDir = Join-Path (Resolve-CMakePresetBinaryDir $ConfigurePreset) $Configuration

function Add-Unique {
    param(
        [System.Collections.Generic.List[string]]$List,
        [string]$Value
    )
    if ([string]::IsNullOrWhiteSpace($Value)) {
        return
    }
    if (-not $List.Contains($Value)) {
        $List.Add($Value)
    }
}

function Normalize-RepoPath {
    param([string]$Path)
    return ($Path -replace "\\", "/").TrimStart("./")
}

function Get-ChangedPaths {
    if ($ChangedPath.Count -gt 0) {
        return $ChangedPath |
            ForEach-Object { $_ -split "," } |
            ForEach-Object { Normalize-RepoPath $_ } |
            Where-Object { $_ -ne "" } |
            Sort-Object -Unique
    }

    Push-Location $SourceRoot
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = "Continue"
        $paths = @()
        $diff = & git diff --name-only --diff-filter=ACMRTUXB $BaseRef -- 2>$null
        if ($LASTEXITCODE -ne 0) {
            throw "git diff failed for base ref '$BaseRef'. Pass -ChangedPath for an explicit plan."
        }
        $paths += $diff
        if (-not $NoUntracked) {
            $paths += (& git ls-files --others --exclude-standard 2>$null)
        }
        return $paths | ForEach-Object { Normalize-RepoPath $_ } | Where-Object { $_ -ne "" } | Sort-Object -Unique
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
        Pop-Location
    }
}

function Get-GTestSuites {
    param([string[]]$Paths)
    $suites = New-Object "System.Collections.Generic.List[string]"
    foreach ($path in $Paths) {
        $absolute = Join-Path $SourceRoot $path
        if (-not (Test-Path $absolute -PathType Leaf)) {
            continue
        }
        foreach ($line in Get-Content $absolute) {
            if ($line -match '^\s*TEST(?:_F|_P)?\s*\(\s*([A-Za-z_][A-Za-z0-9_]*)\s*,') {
                Add-Unique $suites "$($Matches[1]).*"
            }
        }
    }
    return @($suites)
}

function Join-GTestFilter {
    param([string[]]$Filters)
    $unique = @($Filters |
        ForEach-Object { $_ -split ":" } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique)
    if ($unique.Count -eq 0) {
        return ""
    }
    return ($unique -join ":")
}

function Get-TestFileFilter {
    param([string]$Path)
    return Join-GTestFilter @(Get-GTestSuites @($Path))
}

function Get-CMakeTargetSourcePaths {
    param([string]$TargetName)

    if ($script:CMakeTargetSourceCache.ContainsKey($TargetName)) {
        return @($script:CMakeTargetSourceCache[$TargetName])
    }

    $cmakePath = Join-Path $SourceRoot "CMakeLists.txt"
    if (-not (Test-Path $cmakePath -PathType Leaf)) {
        throw "CMakeLists.txt was not found at '$cmakePath'."
    }

    $source = Get-Content $cmakePath -Raw
    $pattern = "add_executable\s*\(\s*$([regex]::Escape($TargetName))(?<body>.*?)\)"
    $match = [regex]::Match($source, $pattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) {
        throw "Target '$TargetName' was not found in CMakeLists.txt."
    }

    $paths = New-Object "System.Collections.Generic.List[string]"
    foreach ($sourceMatch in [regex]::Matches($match.Groups["body"].Value, '(?m)^\s*([A-Za-z0-9_./-]+\.(?:cpp|hpp|h|cxx|cc))\s*$')) {
        Add-Unique $paths (Normalize-RepoPath $sourceMatch.Groups[1].Value)
    }

    $script:CMakeTargetSourceCache[$TargetName] = @($paths | Sort-Object -Unique)
    return @($script:CMakeTargetSourceCache[$TargetName])
}

function Test-IsGuardrailTestPath {
    param([string]$Path)
    return @(Get-CMakeTargetSourcePaths "quader_guardrail_tests").Contains($Path)
}

function Add-QuaderTestsFilter {
    param([string]$Filter, [string]$Reason)
    Add-Unique $script:QuaderTestsFilters $Filter
    Add-Unique $script:Reasons $Reason
}

function Add-GuardrailTestsFilter {
    param([string]$Filter, [string]$Reason)
    Add-Unique $script:GuardrailTestsFilters $Filter
    Add-Unique $script:Reasons $Reason
}

function Add-ModelingDomainFilter {
    param([string]$Filter, [string]$Reason)
    Add-Unique $script:ModelingDomainFilters $Filter
    Add-Unique $script:Reasons $Reason
}

function Add-SdkConsumerFilter {
    param([string]$Filter, [string]$Reason)
    Add-Unique $script:SdkConsumerFilters $Filter
    Add-Unique $script:Reasons $Reason
}

function Add-SdkParityFilter {
    param([string]$Filter, [string]$Reason)
    Add-Unique $script:SdkParityFilters $Filter
    Add-Unique $script:Reasons $Reason
}

function Add-ModelingLoopFilter {
    param([string]$Target, [string]$Filter, [string]$Reason)
    if (-not $script:ModelingLoopFilters.ContainsKey($Target)) {
        $script:ModelingLoopFilters[$Target] = New-Object "System.Collections.Generic.List[string]"
    }
    $filters = [System.Collections.Generic.List[string]]($script:ModelingLoopFilters[$Target])
    Add-Unique $filters $Filter
    Add-Unique $script:ModelingLoopTargets $Target
    Add-Unique $script:BuildTargets $Target
    Add-Unique $script:Reasons $Reason
}

function Add-PolygonModelingLoopFilter {
    param([string]$Filter, [string]$Reason)
    Add-ModelingLoopFilter "quader_modeling_domain_polygon_loop_tests" $Filter $Reason
}

function Add-RuntimeModelingLoopFilter {
    param([string]$Filter, [string]$Reason)
    Add-ModelingLoopFilter "quader_modeling_domain_runtime_loop_tests" $Filter $Reason
}

function Add-MetadataModelingLoopFilter {
    param([string]$Filter, [string]$Reason)
    Add-ModelingLoopFilter "quader_modeling_domain_metadata_loop_tests" $Filter $Reason
}

function Get-ModelingLoopTargetForTestPath {
    param([string]$Path)
    if ($Path -match '^tests/modeling_domain/editor_metadata/') {
        return "quader_modeling_domain_metadata_loop_tests"
    }
    if ($Path -match '^tests/modeling_domain/tool/' -or
            $Path -match '^tests/modeling_domain/operation/qdr_modeling_runtime_' -or
            $Path -match '^tests/modeling_domain/qdr_modeling_runtime_' -or
            $Path -match '^tests/modeling_domain/qdr_custom_engine_modeling_runtime_' -or
            $Path -match '^tests/modeling_domain/qdr_runtime_io_' -or
            $Path -match '^tests/modeling_domain/qdr_modeling_domain_smoke\.cpp$') {
        return "quader_modeling_domain_runtime_loop_tests"
    }
    if ($Path -match '^tests/modeling_domain/operation/qdr_document_' -or
            $Path -match '^tests/modeling_domain/qdr_geometry_' -or
            $Path -match '^tests/modeling_domain/qdr_poly_' -or
            $Path -match '^tests/modeling_domain/qdr_document_' -or
            $Path -match '^tests/modeling_domain/qdr_io_document_') {
        return "quader_modeling_domain_polygon_loop_tests"
    }
    return ""
}

function Get-PolygonOperationFilterForSourcePath {
    param([string]$Path)

    $logicalPath = $Path -replace `
        '^src/sdk/core/modeling/neutral/include/quader/modeling/neutral/polygon/',
        'src/sdk/core/modeling/internal/mesh/polygon/'

    if ($logicalPath -match '^src/sdk/core/modeling/internal/mesh/polygon/(operations/)?poly_operation_registry\.(cpp|hpp)$') {
        return Get-TestFileFilter "tests/modeling_domain/operation/qdr_document_operation_registry_tests.cpp"
    }
    if ($logicalPath -notmatch '^src/sdk/core/modeling/internal/mesh/polygon/operations/([^/]+)_operation\.cpp$') {
        return ""
    }

    $slug = $Matches[1]
    $testSlugOverrides = @{
        "fill_hole_from_edges" = "fill_hole"
        "flip_face_normals" = "flip_normals"
        "radial_align" = "radial_align_selection"
        "recalculate_face_normals" = "recalculate_normals"
        "slice_quads" = "slice_quad"
    }
    $testSlug = $slug
    if ($testSlugOverrides.ContainsKey($slug)) {
        $testSlug = $testSlugOverrides[$slug]
    }

    $testPath = "tests/modeling_domain/operation/qdr_document_operation_${testSlug}_tests.cpp"
    $absoluteTestPath = Join-Path $SourceRoot $testPath
    if (Test-Path $absoluteTestPath -PathType Leaf) {
        return Get-TestFileFilter $testPath
    }
    return ""
}

function Get-RuntimeOperationFilterForSourcePath {
    param([string]$Path)

    if ($Path -match '^src/editor/modeling/operations/(operation_command|operation_registry|editor_operation|operation_schema|portable_operation_mapping)\.(cpp|hpp)$') {
        return "QdrModelingRuntimeOperationCommandTests.*:QdrModelingRuntimeToolOperationContractTests.*"
    }
    return ""
}

function Add-BuildTarget {
    param([string]$Target, [string]$Reason)
    Add-Unique $script:BuildTargets $Target
    Add-Unique $script:Reasons $Reason
}

function Add-SdkVerification {
    param([string]$Reason)
    Add-BuildTarget "quader_modeling_sdk_consumer_tests" $Reason
    Add-BuildTarget "quader_modeling_sdk_parity_tests" $Reason
    Add-SdkConsumerFilter "*" $Reason
    Add-SdkParityFilter "*" $Reason
}

function Add-SdkPublicHeaderVerification {
    param([string]$Reason)
    Add-SdkVerification $Reason
    Add-BuildTarget "quader_guardrail_tests" $Reason
    Add-GuardrailTestsFilter "BuildGuardrailTests.ModelingSdkPublicHeadersStayNativeOwnerFree:ModelingContractHeaderGuardrailTests.*" "guard portable modeling SDK public headers"
}

function Add-FullVerification {
    param([string]$Reason)
    $script:NeedsConfigure = $true
    $script:NeedsFullCtest = $true
    Add-BuildTarget "quader_guardrail_tests" $Reason
    Add-BuildTarget "quader_tests" $Reason
    Add-BuildTarget "quader_modeling_domain_tests" $Reason
    Add-BuildTarget "quader_modeling_sdk_consumer_tests" $Reason
    Add-BuildTarget "quader_modeling_sdk_parity_tests" $Reason
    if ($IncludeApp) {
        Add-BuildTarget "quader" $Reason
    }
}

function Add-TestFilePlan {
    param([string]$Path)
    $suiteFilters = Get-GTestSuites @($Path)
    $filter = Join-GTestFilter $suiteFilters
    if ($Path -eq "tests/sdk/modeling_sdk_consumer_tests.cpp") {
        Add-BuildTarget "quader_modeling_sdk_consumer_tests" "changed SDK consumer test source"
        if ($filter -eq "") {
            $filter = "*"
        }
        Add-SdkConsumerFilter $filter "run suites declared by changed SDK consumer test source"
        return
    }

    if ($Path -eq "tests/sdk/modeling_sdk_parity_tests.cpp") {
        Add-BuildTarget "quader_modeling_sdk_parity_tests" "changed SDK parity test source"
        if ($filter -eq "") {
            $filter = "*"
        }
        Add-SdkParityFilter $filter "run suites declared by changed SDK parity test source"
        return
    }

    if ($Path.StartsWith("tests/modeling_domain/")) {
        $loopTarget = Get-ModelingLoopTargetForTestPath $Path
        $testFilter = $filter
        if ($testFilter -eq "") {
            $testFilter = "Qdr*"
        }
        if ($loopTarget -ne "") {
            Add-ModelingLoopFilter $loopTarget $testFilter "changed modeling-domain test source"
            return
        }

        Add-BuildTarget "quader_modeling_domain_tests" "changed modeling-domain test source"
        if ($filter -ne "") {
            Add-ModelingDomainFilter $filter "run suites declared by changed modeling-domain test source"
        } else {
            Add-ModelingDomainFilter "Qdr*" "changed modeling-domain test source without parseable suite"
        }
        return
    }

    if (Test-IsGuardrailTestPath $Path) {
        Add-BuildTarget "quader_guardrail_tests" "changed guardrail test source"
        if ($filter -ne "") {
            Add-GuardrailTestsFilter $filter "run suites declared by changed guardrail test source"
        } else {
            Add-GuardrailTestsFilter "*" "changed guardrail test source without parseable suite"
        }
        return
    }

    Add-BuildTarget "quader_tests" "changed app test source"
    if ($filter -ne "") {
        Add-QuaderTestsFilter $filter "run suites declared by changed app test source"
    } else {
        Add-QuaderTestsFilter "*" "changed app test source without parseable suite"
    }
}

function Add-PathPlan {
    param([string]$Path)

    if ($Path -match '(^|/)AGENTS\.md$|^(architecture-review|INPUT_KEYBINDINGS|memory|tasklist)\.md$') {
        Add-BuildTarget "quader_guardrail_tests" "repository guidance changed"
        Add-GuardrailTestsFilter "AgentInstructionRoutingTests.*:BuildGuardrailTests.ProjectOwnedCommentCapableFilesCarryProprietaryBanner:BuildGuardrailTests.ArchitectureReviewNamesRealModelingDomainTargets:BuildGuardrailTests.TestingDocsExplainPlacementAndSelectionRules:BuildGuardrailTests.AffectedCheckToolPrintsBeforeAfterAndUsesFilteredGoogleTestRuns" "guard instruction routing"
        return
    }

    if ($Path -match '^(CMakeLists\.txt|CMakePresets\.json|cmake/|vcpkg\.json|thirdparty/)') {
        Add-FullVerification "build-system or dependency policy changed"
        return
    }

    if ($Path -match '^tools/') {
        Add-BuildTarget "quader_guardrail_tests" "developer tooling changed"
        Add-GuardrailTestsFilter "BuildGuardrailTests.ProjectOwnedCommentCapableFilesCarryProprietaryBanner" "guard developer tooling policy"
        if ($Path -eq "tools/affected_check.ps1") {
            Add-GuardrailTestsFilter "BuildGuardrailTests.AffectedCheck*" "guard affected workflow tool"
        } elseif ($Path -eq "tools/boot_smoke.ps1") {
            Add-GuardrailTestsFilter "BuildGuardrailTests.BootSmokeFailsOnLoggedErrorsAndRequiresLogCopy" "guard boot smoke tool"
        } else {
            Add-GuardrailTestsFilter "BuildGuardrailTests.ProjectOwnedFilesDoNotContainBrokenHeaderExtensionTypos" "guard developer tooling policy"
        }
        return
    }

    if ($Path -match '^tests/support/') {
        Add-BuildTarget "quader_guardrail_tests" "shared test support changed"
        Add-BuildTarget "quader_tests" "shared test support changed"
        Add-BuildTarget "quader_modeling_domain_tests" "shared test support changed"
        Add-GuardrailTestsFilter "*" "shared guardrail test support changed"
        Add-QuaderTestsFilter "*" "shared app test support changed"
        Add-ModelingDomainFilter "*" "shared modeling-domain test support changed"
        return
    }

    if ($Path -match '^tests/.*\.(cpp|hpp)$') {
        Add-TestFilePlan $Path
        return
    }

    if ($Path -match '^resources/locales/') {
        Add-BuildTarget "quader" "localization resource changed"
        Add-BuildTarget "quader_guardrail_tests" "localization resource changed"
        Add-BuildTarget "quader_tests" "localization resource changed"
        Add-GuardrailTestsFilter "BuildGuardrailTests.LocalizationResourcesArePackagedWithRuntime" "verify localization resource packaging"
        Add-QuaderTestsFilter "LocalizationTests.*" "verify localization behavior"
        return
    }

    if ($Path -match '^resources/ibl/') {
        Add-BuildTarget "quader" "IBL resource changed"
        Add-BuildTarget "quader_guardrail_tests" "IBL resource changed"
        Add-BuildTarget "quader_tests" "IBL resource changed"
        Add-GuardrailTestsFilter "OopArchitectureGuardrailTests.SceneEnvironmentCatalogOwnsIblResourceContract" "verify IBL resource packaging and catalog contract"
        Add-QuaderTestsFilter "Render*:SceneRenderBridge*:AppStartupTests.*" "verify render environment behavior"
        return
    }

    if ($Path -match '^resources/(fonts|icons|materials|modeling_assets)/') {
        Add-BuildTarget "quader" "runtime resource changed"
        Add-BuildTarget "quader_guardrail_tests" "runtime resource changed"
        Add-BuildTarget "quader_tests" "runtime resource changed"
        Add-GuardrailTestsFilter "BuildGuardrailTests.QtWidgetsRuntimeAndLicenseEvidenceAreConfigured:BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime" "verify runtime resource packaging"
        Add-QuaderTestsFilter "UI*:Ui*:Viewport*:QtViewportEdgeLengthHudTests.*:GroundGridMaterialTests.*:QuaderMeshMaterialTests.*" "verify runtime resource behavior"
        return
    }

    if ($Path -match '^examples/') {
        Add-BuildTarget "quader_example_create_box" "public example changed"
        Add-BuildTarget "quader_example_script_modeling_workflow" "public example changed"
        Add-BuildTarget "quader_guardrail_tests" "public example changed"
        Add-GuardrailTestsFilter "ProgrammaticApiGuardrailTests.PublicCppExamplesAreCompiledOrMarkedDocumentationOnly:ProgrammaticApiGuardrailTests.PublicCppExamplesUseCallableResultHelpers:ProgrammaticApiGuardrailTests.PublicExamplesDoNotStartFromInternalOwnerHeaders:ProgrammaticApiGuardrailTests.PublicExamplesStartFromEditorApiOrScriptSurface" "verify public examples"
        return
    }

    if ($Path -match '^src/sdk/core/modeling/(internal/mesh/geometry/|neutral/include/quader/modeling/neutral/geometry/)') {
        Add-BuildTarget "quader_geometry" "canonical geometry source changed"
        Add-SdkVerification "canonical geometry source changed"
        Add-PolygonModelingLoopFilter "QdrGeometryTests.*:QdrPolyGeometryCharacterizationTests.*" "native geometry-domain parity tests"
        return
    }

    $polygonOperationFilter = Get-PolygonOperationFilterForSourcePath $Path
    if ($polygonOperationFilter -ne "") {
        Add-BuildTarget "quader_modeling_polygon" "polygon operation source changed"
        Add-SdkVerification "canonical polygon operation source changed"
        Add-PolygonModelingLoopFilter $polygonOperationFilter "focused polygon operation tests"
        return
    }

    if ($Path -match '^src/sdk/core/modeling/(internal/mesh/polygon/|neutral/include/quader/modeling/neutral/polygon/)') {
        Add-BuildTarget "quader_modeling_polygon" "canonical polygon source changed"
        Add-SdkVerification "canonical polygon source changed"
        Add-PolygonModelingLoopFilter "QdrDocument*:QdrPoly*:QdrDocumentOperation*" "native polygon parity tests"
        return
    }

    if ($Path -match '^src/sdk/core/modeling/(src/qdr_document\.cpp|include/quader/modeling/io/qdr_document\.hpp)$') {
        Add-BuildTarget "quader_modeling_sdk_qdr_document" "portable binary QDR source changed"
        if ($Path -match '^src/sdk/core/modeling/include/quader/modeling/') {
            Add-SdkPublicHeaderVerification "portable binary QDR source changed"
        } else {
            Add-SdkVerification "portable binary QDR source changed"
        }
        Add-BuildTarget "quader_tests" "portable binary QDR source changed"
        Add-QuaderTestsFilter "Qdr*:ProjectionOwnershipGuardrailTests.*" "native binary QDR integration tests"
        return
    }

    if ($Path -match '^src/sdk/core/modeling/include/quader/modeling/') {
        Add-SdkPublicHeaderVerification "portable modeling SDK public header changed"
        return
    }

    if ($Path -match '^src/sdk/core/modeling/(src/|internal/)') {
        Add-SdkVerification "portable modeling SDK source changed"
        Add-BuildTarget "quader_guardrail_tests" "portable modeling SDK source changed"
        Add-GuardrailTestsFilter "PolygonOperationRegistryTests.*" "portable modeling SDK source policy"
        return
    }

    if ($Path -match '^src/io/qdr/') {
        Add-BuildTarget "quader_project_qdr_io" "native binary QDR IO source changed"
        Add-BuildTarget "quader_tests" "QDR IO source changed"
        Add-QuaderTestsFilter "Qdr*:ProjectionOwnershipGuardrailTests.*" "QDR app IO tests"
        return
    }

    $runtimeOperationFilter = Get-RuntimeOperationFilterForSourcePath $Path
    if ($runtimeOperationFilter -ne "") {
        Add-BuildTarget "quader_modeling_runtime" "modeling runtime operation source changed"
        Add-RuntimeModelingLoopFilter $runtimeOperationFilter "focused modeling runtime operation tests"
        Add-MetadataModelingLoopFilter "QdrEditorMetadata*" "modeling metadata tests affected by operation descriptors"
        return
    }

    if ($Path -match '^src/editor/(array|history|placement|session)/' -or
        $Path -match '^src/editor/commands/(asset_command_service|document_command_service|editor_command_registry|modeling_command_execution_service)\.(cpp|hpp)$' -or
        $Path -match '^src/editor/input/quader_keybinding_target_catalog\.(cpp|hpp)$' -or
        $Path -match '^src/editor/modeling/services/primitive_creation_tool_service\.(cpp|hpp)$' -or
        $Path -match '^src/editor/selection/editor_selection_service\.(cpp|hpp)$') {
        Add-BuildTarget "quader_editor_services" "editor service source changed"
        Add-BuildTarget "quader_guardrail_tests" "editor service source changed"
        Add-BuildTarget "quader_tests" "editor service source changed"
        Add-GuardrailTestsFilter "CommandAndKeybindingOwnershipGuardrailTests.*:ConversionMappingGuardrailTests.*:DocumentCommandServiceTests.*:DocumentCommandTests.*:ModelingSessionServiceSplitTests.*:ProgrammaticApiGuardrailTests.*" "editor service source policy"
        Add-QuaderTestsFilter "EditorApiTests.*:ProgrammaticApiTests.*:ProgrammaticBehaviorScenarioTests.*:DocumentCommand*:Selection*:History*:Keybinding*" "editor service behavior tests"
        return
    }

    if ($Path -match '^src/editor/(instances|material)/' -or
        $Path -match '^src/editor/session/project_session\.(cpp|hpp)$') {
        Add-BuildTarget "quader_project_session" "project session source changed"
        Add-BuildTarget "quader_guardrail_tests" "project session source changed"
        Add-BuildTarget "quader_tests" "project session source changed"
        Add-GuardrailTestsFilter "ConversionMappingGuardrailTests.*:StandardMaterialEditorPresentationTests.*" "project session source policy"
        Add-QuaderTestsFilter "Project*:Material*:StandardMaterial*:EditorApiTests.*Project*" "project session behavior tests"
        return
    }

    if ($Path -match '^src/editor/(modeling/runtime|interactions|modeling/operations)/') {
        Add-BuildTarget "quader_modeling_runtime" "modeling runtime source changed"
        Add-BuildTarget "quader_guardrail_tests" "modeling runtime source changed"
        Add-RuntimeModelingLoopFilter "QdrModelingRuntime*" "modeling runtime tests"
        Add-MetadataModelingLoopFilter "QdrEditorMetadata*" "modeling metadata tests affected by runtime descriptors"
        Add-GuardrailTestsFilter "ModelingInteractionTests.*:ModelingSessionServiceSplitTests.*:ModelingContractHeaderGuardrailTests.*" "modeling runtime source policy"
        return
    }

    if ($Path -match '^src/editor/(modeling/services|projection|selection|commands/modeling|commands/modeling_)') {
        Add-BuildTarget "quader_modeling" "editor modeling service or command source changed"
        Add-BuildTarget "quader_guardrail_tests" "editor modeling service or command source changed"
        Add-BuildTarget "quader_tests" "editor modeling service or command source changed"
        Add-RuntimeModelingLoopFilter "QdrModelingRuntime*" "modeling runtime tests"
        Add-MetadataModelingLoopFilter "QdrEditorMetadata*" "modeling metadata tests affected by editor modeling descriptors"
        Add-GuardrailTestsFilter "ProgrammaticApiGuardrailTests.*:ModelingSessionServiceSplitTests.*:ModelingToolCatalogTests.*:SelectionModeAdapterTests.*:ModelingContractHeaderGuardrailTests.*:ConversionMappingGuardrailTests.*" "modeling/API source-policy guardrails"
        Add-QuaderTestsFilter "ModelingSessionService*:ModelingToolCatalogTests.*:SelectionModeAdapterTests.*:EditorApiTests.*Modeling*:EditorApiTests.*Selection*:EditorApiTests.*History*:ProgrammaticBehaviorScenarioTests.*:ProgrammaticApiTests.*:GroupingPublicApiTests.*" "app modeling/API behavior tests"
        return
    }

    if ($Path -match '^src/editor/metadata/') {
        Add-BuildTarget "quader_editor_metadata" "editor metadata source changed"
        Add-MetadataModelingLoopFilter "QdrEditorMetadata*" "editor metadata modeling-domain tests"
        return
    }

    if ($Path -match '^src/editor/pivot/' -or
        $Path -match '^src/scene/scene_transform_pivot\.(cpp|hpp)$') {
        Add-BuildTarget "quader_editor_services" "visible pivot source changed"
        Add-BuildTarget "quader_guardrail_tests" "visible pivot source changed"
        Add-BuildTarget "quader_tests" "visible pivot source changed"
        Add-GuardrailTestsFilter "OopArchitectureGuardrailTests.VisiblePivotResolutionHasOneSceneOwner:ProgrammaticApiGuardrailTests.*" "pivot ownership and API source-policy guardrails"
        Add-QuaderTestsFilter "CopyPivotFromActiveTests.*:EditorApiTests.*Pivot*:ViewportInteractionControllerTests.*Pivot*:ViewportToolRuntimeTests.*Pivot*" "pivot tool and copy-pivot behavior tests"
        return
    }

    if ($Path -match '^src/editor/transform/') {
        Add-BuildTarget "quader_editor_services" "editor transform source changed"
        Add-BuildTarget "quader_guardrail_tests" "editor transform source changed"
        Add-BuildTarget "quader_tests" "editor transform source changed"
        Add-GuardrailTestsFilter "ProgrammaticApiGuardrailTests.*" "transform/API source-policy guardrails"
        Add-QuaderTestsFilter "EditorApiTests.*Transform*:EditorApiTests.*Move*:GroupingPublicApiTests.*:Viewport*" "editor transform behavior tests"
        return
    }

    if ($Path -match '^src/api/') {
        if ($Path -match '^src/api/quader_(editor|render)_api\.(cpp|hpp)$') {
            Add-BuildTarget "quader_editor_api" "editor-wide public API source changed"
        } else {
            Add-BuildTarget "quader_api" "public API source changed"
        }
        Add-BuildTarget "quader_guardrail_tests" "public API source changed"
        Add-BuildTarget "quader_tests" "public API source changed"
        Add-GuardrailTestsFilter "ProgrammaticApiGuardrailTests.*:CommandAndKeybindingOwnershipGuardrailTests.*:ModelingContractHeaderGuardrailTests.*" "public API source-policy guardrails"
        Add-QuaderTestsFilter "EditorApiTests.*:ProgrammaticApiTests.*:ProgrammaticBehaviorScenarioTests.*:GroupingPublicApiTests.*" "public API and behavior tests"
        return
    }

    if ($Path -match '^src/render/') {
        Add-BuildTarget "quader_render" "render source changed"
        Add-BuildTarget "quader_guardrail_tests" "render source changed"
        Add-BuildTarget "quader_tests" "render source changed"
        Add-GuardrailTestsFilter "BuildGuardrailTests.FilamentSdkIsValidatedAtConfigureTime:GroundGridMaterialTests.*:OverlayMaterialTests.*:QuaderMeshMaterialTests.*:RenderLifecycleTests.*:RenderProxyLifecycleTests.*:RenderSystemTests.*:SceneRenderBridgeTests.*:TextureRegistryTests.*:ViewportOverlayPolicyTests.*:ViewportPersistentOverlayStyleSnapshotTests.*:ViewportRendererTests.*" "render source-policy guardrails"
        Add-QuaderTestsFilter "Render*:RenderExtractionTests.*:Viewport*:Overlay*:SceneRenderBridge*:TextureRegistryTests.*:LightRenderableCacheTests.*:GroundGridMaterialTests.*:QuaderMeshMaterialTests.*:ModelingSessionService*Overlay*:ModelingSessionService*Hover*:ModelingSessionService*Pick*" "render and viewport extraction tests"
        return
    }

    if ($Path -match '^src/ui/') {
        Add-BuildTarget "quader_ui" "UI source changed"
        Add-BuildTarget "quader_guardrail_tests" "UI source changed"
        Add-BuildTarget "quader_tests" "UI source changed"
        Add-GuardrailTestsFilter "BuildGuardrailTests.QtWidgetsRuntimeAndLicenseEvidenceAreConfigured:BuildGuardrailTests.ActiveAppAndUiUseQtWithoutSdlAdapters:BuildGuardrailTests.RuntimeUiUsesQtWidgetsWithFusionBaseline:UiLayoutGuardrailTests.*:AppLoopTests.*:ViewportOverlayPolicyTests.*:ViewportRendererTests.*" "UI source-policy guardrails"
        Add-QuaderTestsFilter "UI*:Ui*:Console*:LayerPanelTests.*:Localization*:OutlinerPanelTests.*:Qt*:Viewport*" "UI shell, item-view, and viewport tests"
        return
    }

    if ($Path -match '^src/editor/viewport/' -or
        $Path -match '^src/editor/input/viewport_keybinding_resolver\.(cpp|hpp)$') {
        Add-BuildTarget "quader_editor_viewport" "editor viewport source changed"
        Add-BuildTarget "quader_guardrail_tests" "viewport source changed"
        Add-BuildTarget "quader_tests" "viewport source changed"
        Add-GuardrailTestsFilter "ProgrammaticApiGuardrailTests.*Viewport*:ViewportToolGuardrailTests.*:ViewportOverlayPolicyTests.*:ViewportSettingsTests.*" "viewport source-policy guardrails"
        Add-QuaderTestsFilter "Viewport*:ViewportTool*:SelectionModeAdapterTests.*:EditorApiTests.*Viewport*:EditorApiTests.*Keybinding*:EditorApiTests.*Input*:EditorApiTests.*BoxSelect*:EditorApiTests.*RightClickPick*:GroupingPublicApiTests.*" "viewport and public behavior tests"
        return
    }

    if ($Path -match '^src/viewport/') {
        Add-BuildTarget "quader_viewport_services" "viewport service source changed"
        Add-BuildTarget "quader_guardrail_tests" "viewport source changed"
        Add-BuildTarget "quader_tests" "viewport source changed"
        Add-GuardrailTestsFilter "ProgrammaticApiGuardrailTests.*Viewport*:ViewportCameraTests.*:ViewportInputRouterTests.*:ViewportSettingsTests.*:ViewportWorkspaceServiceTests.*" "viewport source-policy guardrails"
        Add-QuaderTestsFilter "Viewport*:EditorApiTests.*Viewport*:EditorApiTests.*Keybinding*:EditorApiTests.*Input*" "viewport service behavior tests"
        return
    }

    if ($Path -match '^src/editor/tools/') {
        Add-BuildTarget "quader_editor_services" "editor viewport tool source changed"
        Add-BuildTarget "quader_guardrail_tests" "editor viewport tool source changed"
        Add-BuildTarget "quader_tests" "editor viewport tool source changed"
        Add-GuardrailTestsFilter "ViewportToolGuardrailTests.*:ModelingToolCatalogTests.*" "editor viewport tool source policy"
        Add-QuaderTestsFilter "ViewportTool*:ModelingToolCatalogTests.*:EditorApiTests.*Tool*" "editor viewport tool behavior tests"
        return
    }

    if ($Path -match '^src/(app|platform)/') {
        if ($Path -match '^src/app/') {
            Add-BuildTarget "quader_app" "app source changed"
        } else {
            Add-BuildTarget "quader_platform" "platform source changed"
        }
        Add-BuildTarget "quader_guardrail_tests" "app/platform source changed"
        Add-BuildTarget "quader_tests" "app/platform source changed"
        Add-GuardrailTestsFilter "BuildGuardrailTests.AppLoopRunsOnQApplicationEvents:BuildGuardrailTests.QtShellUsesMainWindowDocksAndStableNames:BuildGuardrailTests.RuntimeUiUsesQtWidgetsWithFusionBaseline:WindowGuardrailTests.WindowPlacementPersistenceAndResizeRecoveryStayOwned:AppLoopTests.*:AppStartupTests.*:FatalErrorHandlerTests.*" "app/platform source-policy guardrails"
        Add-QuaderTestsFilter "AppFrameClockTests.*:AppStartupTests.*:FatalErrorHandlerTests.*:QtInputAdapterTests.*" "app/platform behavior tests"
        return
    }

    if ($Path -match '^src/') {
        Add-BuildTarget "quader_guardrail_tests" "unclassified source changed"
        Add-BuildTarget "quader_tests" "unclassified source changed"
        Add-BuildTarget "quader_modeling_domain_tests" "unclassified source changed"
        Add-GuardrailTestsFilter "*" "unclassified source changed"
        Add-QuaderTestsFilter "*" "unclassified source changed"
        Add-ModelingDomainFilter "*" "unclassified source changed"
        return
    }

    Add-FullVerification "unclassified path changed"
}

function Format-CommandLine {
    param([string[]]$Parts)
    return ($Parts -join " ")
}

function Remove-RedundantBuildTargets {
    param([string[]]$Targets)

    $result = New-Object "System.Collections.Generic.List[string]"
    foreach ($target in ($Targets | Sort-Object -Unique)) {
        Add-Unique $result $target
    }

    $quaderDependencies = @(
        "quader_runtime",
        "quader_api",
        "quader_core",
        "quader_modeling",
        "quader_modeling_runtime",
        "quader_modeling_polygon",
        "quader_geometry",
        "quader_editor_metadata",
        "quader_project_qdr_io",
        "quader_modeling_sdk_qdr_document"
    )
    $quaderTestsDependencies = $quaderDependencies
    $modelingDomainDependencies = @(
        "quader_modeling_runtime",
        "quader_modeling_polygon",
        "quader_geometry",
        "quader_editor_metadata"
    )
    $polygonLoopDependencies = @(
        "quader_modeling_polygon",
        "quader_geometry"
    )
    $runtimeLoopDependencies = @(
        "quader_modeling_runtime",
        "quader_modeling_polygon",
        "quader_geometry",
        "quader_editor_metadata"
    )
    $metadataLoopDependencies = @(
        "quader_editor_metadata"
    )

    $covered = @()
    if ($result.Contains("quader")) {
        $covered += $quaderDependencies
    }
    if ($result.Contains("quader_tests")) {
        $covered += $quaderTestsDependencies
    }
    if ($result.Contains("quader_modeling_domain_tests")) {
        $covered += $modelingDomainDependencies
    }
    if ($result.Contains("quader_modeling_domain_polygon_loop_tests")) {
        $covered += $polygonLoopDependencies
    }
    if ($result.Contains("quader_modeling_domain_runtime_loop_tests")) {
        $covered += $runtimeLoopDependencies
    }
    if ($result.Contains("quader_modeling_domain_metadata_loop_tests")) {
        $covered += $metadataLoopDependencies
    }

    foreach ($target in ($covered | Sort-Object -Unique)) {
        [void]$result.Remove($target)
    }

    return @($result | Sort-Object -Unique)
}

function Assert-GTestFilterSelectsTests {
    param(
        [string]$Executable,
        [string]$Filter
    )

    if (-not (Test-Path $Executable -PathType Leaf)) {
        throw "GoogleTest executable was not found: '$Executable'."
    }

    $output = @(& $Executable "--gtest_color=no" "--gtest_filter=$Filter" "--gtest_list_tests" 2>&1)
    if ($LASTEXITCODE -ne 0) {
        throw "GoogleTest enumeration failed with exit code ${LASTEXITCODE}: $Executable --gtest_filter=$Filter"
    }

    $selectedTestCount = @($output | Where-Object { [string]$_ -match '^\s{2}\S' }).Count
    if ($selectedTestCount -eq 0) {
        throw "GoogleTest filter '$Filter' selected zero tests in '$Executable'."
    }
}

function Assert-CommandExecutableExists {
    param([string]$Executable)

    $resolved = @(Get-Command -Name $Executable -CommandType Application -ErrorAction SilentlyContinue)
    if ($resolved.Count -eq 0) {
        throw "Command executable was not found: '$Executable'."
    }
}

function Invoke-CommandLine {
    param(
        [string]$Label,
        [string[]]$Parts
    )
    $command = Format-CommandLine $Parts
    Assert-CommandExecutableExists $Parts[0]
    $filterArgument = @($Parts | Where-Object { $_.StartsWith("--gtest_filter=") } | Select-Object -First 1)
    if ($filterArgument.Count -gt 0) {
        $filter = $filterArgument[0].Substring("--gtest_filter=".Length)
        Assert-GTestFilterSelectsTests $Parts[0] $filter
    }
    Write-Host ""
    Write-Host "==> $Label"
    Write-Host $command
    $timer = [System.Diagnostics.Stopwatch]::StartNew()
    Push-Location $SourceRoot
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        $arguments = @($Parts | Select-Object -Skip 1)
        $ErrorActionPreference = "Continue"
        $global:LASTEXITCODE = $null
        & $Parts[0] @arguments 2>&1 | ForEach-Object {
            if ($_ -is [System.Management.Automation.ErrorRecord]) {
                Write-Host $_.Exception.Message
            } else {
                Write-Host ([string]$_)
            }
        }
        $commandExitCode = $LASTEXITCODE
        $ErrorActionPreference = $previousErrorActionPreference
        if ($null -eq $commandExitCode) {
            throw "Command did not report an exit code: $command"
        }
        if ($commandExitCode -ne 0) {
            throw "Command failed with exit code ${commandExitCode}: $command"
        }
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
        Pop-Location
        $timer.Stop()
    }
    return [Math]::Round($timer.Elapsed.TotalSeconds, 2)
}

if ($MyInvocation.InvocationName -eq ".") {
    return
}

$script:BuildTargets = New-Object "System.Collections.Generic.List[string]"
$script:GuardrailTestsFilters = New-Object "System.Collections.Generic.List[string]"
$script:QuaderTestsFilters = New-Object "System.Collections.Generic.List[string]"
$script:ModelingDomainFilters = New-Object "System.Collections.Generic.List[string]"
$script:SdkConsumerFilters = New-Object "System.Collections.Generic.List[string]"
$script:SdkParityFilters = New-Object "System.Collections.Generic.List[string]"
$script:ModelingLoopTargets = New-Object "System.Collections.Generic.List[string]"
$script:ModelingLoopFilters = @{}
$script:Reasons = New-Object "System.Collections.Generic.List[string]"
$script:NeedsConfigure = $false
$script:NeedsFullCtest = $false

$changedPaths = @(Get-ChangedPaths)
if ($changedPaths.Count -eq 0) {
    Add-FullVerification "no changed paths detected"
} else {
    foreach ($path in $changedPaths) {
        Add-PathPlan $path
    }
}

if ($IncludeApp) {
    Add-BuildTarget "quader" "manual in-app verification requested"
}

if (-not $NoTest) {
    if ($GuardrailTestsFilters.Count -gt 0) {
        Add-Unique $script:BuildTargets "quader_guardrail_tests"
    }
    if ($QuaderTestsFilters.Count -gt 0) {
        Add-Unique $script:BuildTargets "quader_tests"
    }
    if ($ModelingDomainFilters.Count -gt 0) {
        Add-Unique $script:BuildTargets "quader_modeling_domain_tests"
    }
    if ($SdkConsumerFilters.Count -gt 0) {
        Add-Unique $script:BuildTargets "quader_modeling_sdk_consumer_tests"
    }
    if ($SdkParityFilters.Count -gt 0) {
        Add-Unique $script:BuildTargets "quader_modeling_sdk_parity_tests"
    }
    foreach ($target in $ModelingLoopTargets) {
        Add-Unique $script:BuildTargets $target
    }
}
$buildTargets = @(Remove-RedundantBuildTargets @($BuildTargets))

$guardrailTestsFilter = Join-GTestFilter @($GuardrailTestsFilters)
$quaderTestsFilter = Join-GTestFilter @($QuaderTestsFilters)
$modelingDomainFilter = Join-GTestFilter @($ModelingDomainFilters)
$sdkConsumerFilter = Join-GTestFilter @($SdkConsumerFilters)
$sdkParityFilter = Join-GTestFilter @($SdkParityFilters)
$modelingLoopJoinedFilters = @{}
foreach ($target in @($ModelingLoopTargets | Sort-Object -Unique)) {
    $modelingLoopJoinedFilters[$target] = Join-GTestFilter @($script:ModelingLoopFilters[$target])
}

$commands = New-Object "System.Collections.Generic.List[object]"
if (-not $NoConfigure -and $NeedsConfigure) {
    $commands.Add(@("cmake", "--preset", $ConfigurePreset))
}
if (-not $NoBuild) {
    foreach ($target in $buildTargets) {
        $commands.Add(@("cmake", "--build", "--preset", $BuildPreset, "--target", $target))
    }
}
if (-not $NoTest) {
    if ($NeedsFullCtest) {
        $commands.Add(@("ctest", "--preset", $TestPreset, "--output-on-failure"))
    } else {
        if ($guardrailTestsFilter -ne "") {
            $commands.Add(@((Join-Path $BuildDir "quader_guardrail_tests.exe"), "--gtest_color=no", "--gtest_filter=$guardrailTestsFilter"))
        }
        if ($quaderTestsFilter -ne "") {
            $commands.Add(@((Join-Path $BuildDir "quader_tests.exe"), "--gtest_color=no", "--gtest_filter=$quaderTestsFilter"))
        }
        if ($modelingDomainFilter -ne "") {
            $commands.Add(@((Join-Path $BuildDir "quader_modeling_domain_tests.exe"), "--gtest_color=no", "--gtest_filter=$modelingDomainFilter"))
        }
        if ($sdkConsumerFilter -ne "") {
            $commands.Add(@((Join-Path $BuildDir "quader_modeling_sdk_consumer_tests.exe"), "--gtest_color=no", "--gtest_filter=$sdkConsumerFilter"))
        }
        if ($sdkParityFilter -ne "") {
            $commands.Add(@((Join-Path $BuildDir "quader_modeling_sdk_parity_tests.exe"), "--gtest_color=no", "--gtest_filter=$sdkParityFilter"))
        }
        foreach ($target in @($ModelingLoopTargets | Sort-Object -Unique)) {
            $filter = $modelingLoopJoinedFilters[$target]
            if ($filter -ne "") {
                $commands.Add(@((Join-Path $BuildDir "$target.exe"), "--gtest_color=no", "--gtest_filter=$filter"))
            }
        }
    }
}

$baselineCommands = @()
if (-not $NoConfigure -and $NeedsConfigure) {
    $baselineCommands += "cmake --preset $ConfigurePreset"
}
$baselineCommands += @(
    "cmake --build --preset $BuildPreset --target quader_guardrail_tests",
    "cmake --build --preset $BuildPreset --target quader_tests",
    "cmake --build --preset $BuildPreset --target quader_modeling_domain_tests",
    "cmake --build --preset $BuildPreset --target quader_modeling_sdk_consumer_tests",
    "cmake --build --preset $BuildPreset --target quader_modeling_sdk_parity_tests",
    "ctest --preset $TestPreset --output-on-failure"
)
if ($IncludeApp) {
    $baselineCommands = @("cmake --build --preset $BuildPreset --target quader") + $baselineCommands
}

Write-Host "Changed paths:"
if ($changedPaths.Count -eq 0) {
    Write-Host "  <none>"
} else {
    foreach ($path in $changedPaths) {
        Write-Host "  $path"
    }
}

Write-Host ""
Write-Host "Reasoning:"
foreach ($reason in @($Reasons | Sort-Object -Unique)) {
    Write-Host "  - $reason"
}

Write-Host ""
Write-Host "Before full workflow:"
foreach ($command in $baselineCommands) {
    Write-Host "  $command"
}

Write-Host ""
Write-Host "After affected workflow:"
if ($commands.Count -eq 0) {
    Write-Host "  <nothing to run>"
} else {
    foreach ($commandParts in $commands) {
        Write-Host ("  " + (Format-CommandLine $commandParts))
    }
}

if ($PlanOnly) {
    return
}

$affectedSeconds = 0.0
foreach ($commandParts in $commands) {
    $affectedSeconds += Invoke-CommandLine "affected" $commandParts
}

Write-Host ""
Write-Host ("Affected workflow elapsed: {0:N2}s" -f $affectedSeconds)

if ($MeasureFull) {
    $fullCommands = @()
    if (-not $NoConfigure -and $NeedsConfigure) {
        $fullCommands += ,@("cmake", "--preset", $ConfigurePreset)
    }
    if ($IncludeApp) {
        $fullCommands += ,@("cmake", "--build", "--preset", $BuildPreset, "--target", "quader")
    }
    $fullCommands += ,@("cmake", "--build", "--preset", $BuildPreset, "--target", "quader_guardrail_tests")
    $fullCommands += ,@("cmake", "--build", "--preset", $BuildPreset, "--target", "quader_tests")
    $fullCommands += ,@("cmake", "--build", "--preset", $BuildPreset, "--target", "quader_modeling_domain_tests")
    $fullCommands += ,@("cmake", "--build", "--preset", $BuildPreset, "--target", "quader_modeling_sdk_consumer_tests")
    $fullCommands += ,@("cmake", "--build", "--preset", $BuildPreset, "--target", "quader_modeling_sdk_parity_tests")
    $fullCommands += ,@("ctest", "--preset", $TestPreset, "--output-on-failure")

    $fullSeconds = 0.0
    foreach ($commandParts in $fullCommands) {
        $fullSeconds += Invoke-CommandLine "full baseline" $commandParts
    }
    $saved = $fullSeconds - $affectedSeconds
    Write-Host ""
    Write-Host ("Before -> after: {0:N2}s -> {1:N2}s ({2:N2}s saved)" -f $fullSeconds, $affectedSeconds, $saved)
}
