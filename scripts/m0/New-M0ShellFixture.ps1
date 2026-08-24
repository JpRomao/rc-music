[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string] $DestinationRoot,
    [switch] $AllowCommittedFixtureOverwrite
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($PSVersionTable.PSVersion -ne [Version]'7.6.5') {
    throw [System.PlatformNotSupportedException]::new("M0-E1 requires PowerShell 7.6.5; found $($PSVersionTable.PSVersion).")
}

$utf8NoBom = [Text.UTF8Encoding]::new($false)
$repositoryRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')).TrimEnd('\', '/')
Import-Module (Join-Path $repositoryRoot 'experiments\m0\harness\M0.Harness.psd1') -Force -ErrorAction Stop
$destination = Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath $DestinationRoot
$comparison = if ($IsWindows) { [StringComparison]::OrdinalIgnoreCase } else { [StringComparison]::Ordinal }
$committedRoot = Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'experiments\m0\shared\shell-fixture'
$outputRoot = Resolve-M0ScopedPath -BasePath $repositoryRoot -CandidatePath 'out\m0'
$isCommittedRoot = $destination.Equals($committedRoot, $comparison)
$isOutputChild = $destination.StartsWith($outputRoot.TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar, $comparison)
if (($isCommittedRoot -and -not $AllowCommittedFixtureOverwrite) -or (-not $isCommittedRoot -and -not $isOutputChild)) {
    throw [System.ArgumentException]::new('Fixture generation is limited to out/m0 children or the committed fixture root with -AllowCommittedFixtureOverwrite.')
}
if (-not $isCommittedRoot -and (Test-Path -LiteralPath $destination)) {
    if (-not (Test-Path -LiteralPath $destination -PathType Container) -or
        @(Get-ChildItem -LiteralPath $destination -Force).Count -ne 0) {
        throw [IO.IOException]::new('M0G001 output fixture destination must not exist or must be empty.')
    }
}
$assetsRoot = Join-Path $destination 'assets'
New-Item -ItemType Directory -Path $assetsRoot -Force | Out-Null

function Write-M0TextFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] [string] $Content
    )

    $normalized = $Content.Replace("`r`n", "`n").Replace("`r", "`n").TrimEnd("`n") + "`n"
    [IO.File]::WriteAllText($LiteralPath, $normalized, $utf8NoBom)
}

function Write-M0JsonFile {
    param(
        [Parameter(Mandatory)] [string] $LiteralPath,
        [Parameter(Mandatory)] $Value
    )

    $json = $Value | ConvertTo-Json -Depth 100
    Write-M0TextFile -LiteralPath $LiteralPath -Content $json
}

function New-M0QueueItem {
    param([Parameter(Mandatory)] [int] $Index)

    $id = '{0:D4}' -f $Index
    $artist = (($Index - 1) % 23) + 1
    $album = [int][Math]::Ceiling($Index / 25.0)
    $albumName = 'Coleção sintética {0:D2}' -f $album
    $fileName = "faixa-$id.mp3"
    $item = [ordered]@{
        order_index = $Index - 1
        occurrence_id = "queue-$id"
        source = [ordered]@{
            semantic_key = "semantic-source-$id"
            presentation_form = 'nfc'
            synthetic_path_segments = @($albumName, $fileName)
            file_name = $fileName
        }
        metadata = [ordered]@{
            title = "Faixa de teste $id"
            artist = 'Artista sintético {0:D2}' -f $artist
            album = $albumName
        }
        duration_ms = 120000 + (($Index * 7919) % 240000)
        availability = 'available'
        error_code = $null
    }

    switch ($Index) {
        { $_ -in 7, 707 } {
            $item.source = [ordered]@{
                semantic_key = 'semantic-source-duplicate-0007'
                presentation_form = 'nfc'
                synthetic_path_segments = @('Coleção sintética 01', 'duplicata-cafe-e-chuva.m4a')
                file_name = 'duplicata-cafe-e-chuva.m4a'
            }
            $item.metadata = [ordered]@{
                title = 'Café, chuva e coração'
                artist = 'Artista Sintética 07'
                album = 'Coleção sintética 01'
            }
            $item.duration_ms = 215000
        }
        17 {
            $title = 'Canção nº 17 — São João, ação e café ☕'
            $albumTitle = 'Coleção Ação e Emoção'
            $fileName = "$title.m4a"
            $item.source = [ordered]@{
                semantic_key = 'semantic-source-unicode-normalized-0017'
                presentation_form = 'nfc'
                synthetic_path_segments = @($albumTitle, $fileName)
                file_name = $fileName
            }
            $item.metadata = [ordered]@{
                title = $title
                artist = 'Artista Sintética Ç'
                album = $albumTitle
            }
            $item.duration_ms = 248000
        }
        18 {
            $title = 'Canção nº 17 — São João, ação e café ☕'.Normalize([Text.NormalizationForm]::FormD)
            $albumTitle = 'Coleção Ação e Emoção'.Normalize([Text.NormalizationForm]::FormD)
            $artistName = 'Artista Sintética Ç'.Normalize([Text.NormalizationForm]::FormD)
            $fileName = "$title.m4a"
            $item.source = [ordered]@{
                semantic_key = 'semantic-source-unicode-normalized-0017'
                presentation_form = 'nfd'
                synthetic_path_segments = @($albumTitle, $fileName)
                file_name = $fileName
            }
            $item.metadata = [ordered]@{
                title = $title
                artist = $artistName
                album = $albumTitle
            }
            $item.duration_ms = 248000
        }
        23 {
            $item.metadata = [ordered]@{
                title = 'Olá, mundo — 東京・مرحبا・música 🎧'
                artist = 'Conjunto Sintético Ω'
                album = 'Unicode sem fronteiras'
            }
            $item.source.synthetic_path_segments = @('Unicode sem fronteiras', 'ola-mundo-unicode-0023.m4a')
            $item.source.file_name = 'ola-mundo-unicode-0023.m4a'
        }
        42 {
            $item.metadata = [ordered]@{
                title = '<b>Não interpretar</b> & "aspas" ''simples'''
                artist = '<script>não executar()</script>'
                album = 'Texto literal de teste'
            }
            $item.source.synthetic_path_segments = @('Texto literal de teste', 'texto-com-markup-literal-0042.mp3')
            $item.source.file_name = 'texto-com-markup-literal-0042.mp3'
        }
        512 {
            $longTitle = 'Faixa longa 0512 — ' + ('ação, café, coração; ' * 40)
            $item.metadata.title = $longTitle.Substring(0, 512)
            $item.metadata.artist = 'Artista de texto longo'
            $item.metadata.album = 'Teste de elipse e nome acessível'
            $item.source.synthetic_path_segments = @('Teste de texto longo', 'faixa-longa-0512.m4a')
            $item.source.file_name = 'faixa-longa-0512.m4a'
        }
        999 {
            $item.metadata = [ordered]@{
                title = 'Faixa indisponível de teste'
                artist = 'Artista sintético indisponível'
                album = 'Falhas recuperáveis'
            }
            $item.source.synthetic_path_segments = @('Falhas recuperáveis', 'faixa-indisponivel-0999.mp3')
            $item.source.file_name = 'faixa-indisponivel-0999.mp3'
            $item.duration_ms = $null
            $item.availability = 'unavailable'
            $item.error_code = 'fixture_source_unavailable'
        }
    }

    return $item
}

$strings = [ordered]@{
    app_name = 'Player de música'
    action_open_audio = 'Abrir áudio'
    empty_title = 'Abra um áudio para começar'
    empty_hint = 'ou arraste arquivos para esta janela'
    action_play = 'Reproduzir'
    action_pause = 'Pausar'
    action_previous = 'Faixa anterior'
    action_next = 'Próxima faixa'
    skip_backward_short = '−{seconds}'
    skip_forward_short = '+{seconds}'
    skip_backward_name = 'Voltar {seconds} segundos'
    skip_forward_name = 'Avançar {seconds} segundos'
    progress_name = 'Posição da faixa'
    volume_name = 'Volume'
    action_mute = 'Silenciar'
    action_unmute = 'Ativar som'
    queue_title = 'Fila'
    preferences_title = 'Preferências'
    details_title = 'Detalhes da faixa'
    section_general = 'Geral'
    section_playback = 'Reprodução'
    section_appearance = 'Aparência'
    theme_system = 'Sistema'
    theme_light = 'Claro'
    theme_dark = 'Escuro'
    motion_system = 'Sistema'
    motion_reduced = 'Reduzido'
    error_title = 'Não foi possível abrir esta faixa'
    error_body = 'A faixa de teste está indisponível.'
    action_skip = 'Pular'
    action_retry = 'Tentar novamente'
    not_informed = 'Não informado'
}

$descriptor = [ordered]@{
    schema_id = 'm0.shell-fixture.v1'
    schema_version = 1
    fixture_id = 'M0-SHELL-F1'
    locale = 'pt-BR'
    default_state_id = 'empty'
    window = [ordered]@{
        client_width_px = 960
        client_height_px = 640
        minimum_width_px = 720
        minimum_height_px = 520
    }
    layout_tokens = [ordered]@{
        queue_panel_min_width_px = 320
        queue_panel_max_width_px = 380
        control_target_min_px = 40
        control_target_preferred_px = 44
        radius_small_px = 8
        radius_medium_px = 12
        spacing_unit_px = 4
    }
    strings = $strings
    controls = @(
        [ordered]@{ control_id = 'open-audio'; role = 'button'; name_key = 'action_open_audio' },
        [ordered]@{ control_id = 'play-pause'; role = 'button'; name_key = 'action_pause' },
        [ordered]@{ control_id = 'previous'; role = 'button'; name_key = 'action_previous' },
        [ordered]@{ control_id = 'next'; role = 'button'; name_key = 'action_next' },
        [ordered]@{ control_id = 'skip-backward'; role = 'button'; name_key = 'skip_backward_name' },
        [ordered]@{ control_id = 'skip-forward'; role = 'button'; name_key = 'skip_forward_name' },
        [ordered]@{ control_id = 'progress'; role = 'slider'; name_key = 'progress_name' },
        [ordered]@{ control_id = 'volume'; role = 'slider'; name_key = 'volume_name' },
        [ordered]@{ control_id = 'mute'; role = 'button'; name_key = 'action_mute' },
        [ordered]@{ control_id = 'queue'; role = 'button'; name_key = 'queue_title' },
        [ordered]@{ control_id = 'preferences'; role = 'button'; name_key = 'preferences_title' }
    )
    themes = [ordered]@{
        available_modes = @('system', 'light', 'dark')
        palettes = [ordered]@{
            light = [ordered]@{
                canvas = '#F6F7F9'; surface = '#FFFFFF'; 'surface-raised' = '#FFFFFF'
                'text-primary' = '#15171A'; 'text-secondary' = '#4F5660'; accent = '#4F46E5'
                'on-accent' = '#FFFFFF'; border = '#8A929E'; focus = '#2457D6'
                error = '#B42318'; warning = '#8A4B08'; success = '#167344'
            }
            dark = [ordered]@{
                canvas = '#111318'; surface = '#181B21'; 'surface-raised' = '#21252D'
                'text-primary' = '#F5F7FA'; 'text-secondary' = '#BBC2CC'; accent = '#8B83FF'
                'on-accent' = '#0B0C10'; border = '#69717D'; focus = '#A9C2FF'
                error = '#FF8A80'; warning = '#FFC46B'; success = '#6DDA9B'
            }
        }
    }
    motion = [ordered]@{
        available_modes = @('system', 'reduced')
        normal = [ordered]@{ panel_transition_ms = 160; feedback_transition_ms = 120; continuous_animation = $false }
        reduced = [ordered]@{ panel_transition_ms = 0; feedback_transition_ms = 0; continuous_animation = $false }
    }
    preferences = [ordered]@{
        defaults = [ordered]@{
            theme = 'system'; motion = 'system'; volume_percent = 80; muted = $false
            skip_backward_seconds = 5; skip_forward_seconds = 5
            close_behavior = 'minimize_to_tray'; restore_last_session = $true
            start_minimized = $false; queue_end_behavior = 'stop_keep_selection'
        }
        constraints = [ordered]@{
            volume_percent = [ordered]@{ minimum = 0; maximum = 100 }
            skip_seconds = [ordered]@{ minimum = 1; maximum = 300 }
            close_behavior = @('minimize_to_tray', 'exit')
            queue_end_behavior = @('stop_keep_selection', 'stop_clear_selection')
        }
        rules = [ordered]@{
            close_without_tray = 'minimize_window'
            start_minimized_requires_tray = $true
            restored_playback_state = 'paused'
            unmute_restores_last_nonzero_volume = $true
        }
    }
    fake_backend = [ordered]@{
        clock = [ordered]@{ kind = 'deterministic'; initial_monotonic_ms = 0 }
        snapshots = [ordered]@{
            frequency_hz = 5; interval_ms = 200; start_position_ms = 84000
            duration_ms = 248000; increment_ms = 200; end_behavior = 'clamp_paused'
        }
        seek = [ordered]@{
            target_ms = 124000; preview_immediate = $true; ack_after_ticks = 1; ack_position_ms = 124000
        }
    }
    states = @(
        [ordered]@{
            state_id = 'empty'; playback_state = 'unloaded'; queue_loaded = $false
            current_occurrence_id = $null; context_occurrence_id = $null
            position_ms = $null; duration_ms = $null; seekable = $false
            asset_id = 'cover-placeholder'; message_key = 'empty_title'; primary_action_id = 'open-audio'; error_code = $null
        },
        [ordered]@{
            state_id = 'playing'; playback_state = 'playing'; queue_loaded = $true
            current_occurrence_id = 'queue-0017'; context_occurrence_id = $null
            position_ms = 84000; duration_ms = 248000; seekable = $true
            asset_id = 'cover-track'; message_key = $null; primary_action_id = 'play-pause'; error_code = $null
        },
        [ordered]@{
            state_id = 'error'; playback_state = 'error'; queue_loaded = $true
            current_occurrence_id = 'queue-0999'; context_occurrence_id = 'queue-0017'
            position_ms = $null; duration_ms = $null; seekable = $false
            asset_id = 'cover-track'; message_key = 'error_body'; primary_action_id = 'skip'; error_code = 'fixture_source_unavailable'
        }
    )
    queue = [ordered]@{
        relative_path = 'queue.v1.json'
        queue_id = 'M0-SHELL-Q1000'
        expected_item_count = 1000
        load_action_id = 'load-queue-1000'
    }
    fixed_cases = @(
        [ordered]@{ case_id = 'duplicate-exact'; occurrence_ids = @('queue-0007', 'queue-0707'); expectation = 'distinct_occurrences_same_source' },
        [ordered]@{ case_id = 'unicode-normalization'; occurrence_ids = @('queue-0017', 'queue-0018'); expectation = 'distinct_presentations_same_nfc_source' },
        [ordered]@{ case_id = 'unicode-non-bmp'; occurrence_ids = @('queue-0023'); expectation = 'preserve_unicode_scalars' },
        [ordered]@{ case_id = 'literal-markup'; occurrence_ids = @('queue-0042'); expectation = 'render_as_plain_text' },
        [ordered]@{ case_id = 'long-text'; occurrence_ids = @('queue-0512'); expectation = '512_unicode_scalars' },
        [ordered]@{ case_id = 'unavailable'; occurrence_ids = @('queue-0999'); expectation = 'recoverable_error_preserves_queue' }
    )
    track_details = [ordered]@{
        occurrence_id = 'queue-0017'; synthetic = $true; size_bytes = 7483921
        modified_utc = '2026-08-23T00:00:00Z'; container = 'M4A'; codec = 'AAC-LC'
        bitrate_bps = 256000; sample_rate_hz = 48000; channels = 2; bit_depth = $null
    }
    assets = @(
        [ordered]@{ asset_id = 'cover-placeholder'; relative_path = 'assets/cover-placeholder.svg'; alt_key = 'empty_title' },
        [ordered]@{ asset_id = 'cover-track'; relative_path = 'assets/cover-track.svg'; alt_key = 'details_title' }
    )
}

$queueItems = [Collections.Generic.List[object]]::new(1000)
for ($index = 1; $index -le 1000; $index++) {
    $queueItems.Add((New-M0QueueItem -Index $index))
}
$queue = [ordered]@{
    schema_id = 'm0.shell-queue.v1'
    schema_version = 1
    queue_id = 'M0-SHELL-Q1000'
    generation_recipe_id = 'M0-Q1'
    item_count = 1000
    items = $queueItems
}

$license = @'
LicenseRef-MusicPlayer-M0-Private

Copyright (c) 2026, maintainer of the music-player project.

These synthetic fixture files may be used, copied, and modified solely inside
this private local project and its M0 experiments. No permission is granted for
public redistribution. No third-party media, font, recording, or personal file
is included in this fixture set.
'@

$placeholderSvg = @'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <rect width="256" height="256" rx="24" fill="#21252D"/>
  <circle cx="128" cy="128" r="64" fill="#69717D"/>
  <circle cx="128" cy="128" r="18" fill="#181B21"/>
</svg>
'@

$trackSvg = @'
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 256 256" width="256" height="256">
  <rect width="256" height="256" rx="24" fill="#4F46E5"/>
  <circle cx="128" cy="128" r="72" fill="#8B83FF"/>
  <circle cx="128" cy="128" r="22" fill="#0B0C10"/>
  <path d="M151 68v91c0 17-14 29-31 29-13 0-24-8-24-19 0-12 12-21 27-21 6 0 11 1 16 4V82l12-14z" fill="#F5F7FA"/>
</svg>
'@

$descriptorPath = Join-Path $destination 'fixture.v1.json'
$queuePath = Join-Path $destination 'queue.v1.json'
$licensePath = Join-Path $destination 'LICENSE.assets.txt'
$placeholderPath = Join-Path $assetsRoot 'cover-placeholder.svg'
$trackPath = Join-Path $assetsRoot 'cover-track.svg'
Write-M0JsonFile -LiteralPath $descriptorPath -Value $descriptor
Write-M0JsonFile -LiteralPath $queuePath -Value $queue
Write-M0TextFile -LiteralPath $licensePath -Content $license
Write-M0TextFile -LiteralPath $placeholderPath -Content $placeholderSvg
Write-M0TextFile -LiteralPath $trackPath -Content $trackSvg

$roles = @{
    'LICENSE.assets.txt' = @('license', 'text/plain', 'project-authored-synthetic')
    'assets/cover-placeholder.svg' = @('asset', 'image/svg+xml', 'project-authored-synthetic')
    'assets/cover-track.svg' = @('asset', 'image/svg+xml', 'project-authored-synthetic')
    'fixture.v1.json' = @('descriptor', 'application/json', 'generated-from-repository-script')
    'queue.v1.json' = @('queue', 'application/json', 'generated-from-repository-script')
}
$relativeFiles = [string[]]$roles.Keys
[Array]::Sort($relativeFiles, [StringComparer]::Ordinal)
$manifestFiles = foreach ($relativePath in $relativeFiles) {
    $literalPath = Join-Path $destination ($relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))
    $meta = $roles[$relativePath]
    [ordered]@{
        relative_path = $relativePath
        role = $meta[0]
        media_type = $meta[1]
        size_bytes = (Get-Item -LiteralPath $literalPath).Length
        sha256 = (Get-FileHash -LiteralPath $literalPath -Algorithm SHA256).Hash.ToLowerInvariant()
        origin = $meta[2]
        license_id = 'LicenseRef-MusicPlayer-M0-Private'
    }
}
$manifest = [ordered]@{
    schema_id = 'm0.fixture-manifest.v1'
    schema_version = 1
    manifest_id = 'M0-SHELL-F1-MANIFEST'
    fixture_id = 'M0-SHELL-F1'
    hash_algorithm = 'sha256'
    files = @($manifestFiles)
}
$manifestPath = Join-Path $destination 'manifest.v1.json'
Write-M0JsonFile -LiteralPath $manifestPath -Value $manifest
$manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
Write-M0TextFile -LiteralPath (Join-Path $destination 'manifest.v1.sha256') -Content "$manifestHash  manifest.v1.json"

[pscustomobject]@{
    fixture_id = 'M0-SHELL-F1'
    fixture_sha256 = $manifestHash
    destination = $destination
}
