# Corpus de áudio e fixtures

**Estado:** M0.5 em andamento; matriz básica, truncados, capas em quatro contêineres e limites UTF-8 MP3 autenticados; hardening e catálogo global pendentes
**Última revisão:** 2026-11-05

## 1. Objetivo

O corpus prova as capacidades anunciadas por contêiner e codec. Ele deve ser pequeno o suficiente para CI, reproduzível e legalmente redistribuível.

## 2. Manifesto obrigatório

Cada arquivo terá uma entrada versionada com:

```text
id
relative_path
sha256
size_bytes
source_or_generation_recipe
license
container
codec_and_profile
duration_ms
sample_rate_hz
channels
bit_depth_if_known
bitrate_mode
expected_tags
expected_artwork
expected_capabilities
```

O teste valida hash antes de usar a fixture. Mudança intencional exige atualização do manifesto no mesmo commit.

## 3. Conjunto mínimo

| ID proposto | Conteúdo | Finalidade |
|---|---|---|
| `mp3-cbr-stereo` | tom/voz sintética, MP3 CBR | decode, duração, seek e tags básicas |
| `mp3-vbr-stereo` | sinal sintético, MP3 VBR | seek e duração VBR |
| `mp3-unicode-tags` | tags pt-BR/Unicode | encoding e UI |
| `m4a-aac-lc` | AAC-LC em ISO MP4 | requisito M4A principal |
| `m4a-aac-art` | AAC-LC com capa pequena | tags/arte MP4 |
| `flac-16-stereo` | FLAC 16-bit | lossless e propriedades |
| `wav-pcm-mono` | WAV PCM mono | baseline sem tags |
| `ogg-vorbis` | Ogg/Vorbis | formato aberto comum |
| `opus-candidate` | Ogg/Opus | decidir suporte |
| `m4a-alac-candidate` | ALAC em MP4 | decidir suporte |
| `truncated-*` | cópia truncada de cada contêiner | erro sem crash |
| `wrong-extension` | conteúdo válido com extensão enganosa | sniffing/diagnóstico |
| `invalid-art` | tag aponta para bytes inválidos | resiliência |
| `oversized-art` | casos exatamente em/fora de 10 MiB, 4.096 px/eixo e 16 MP | NFR-LIMIT-001 e memória |
| `oversized-tags` | 16 KiB/campo e 256 KiB total, em/fora do limite | NFR-LIMIT-002, sanitização e UI |
| `empty` | zero bytes | erro básico |

### 3.1 Corpus executável atual

Os dois primeiros incrementos de M0.5 materializam 12 fixtures sintéticas em [`experiments/m0/shared/audio-corpus`](../../experiments/m0/shared/audio-corpus):

- `wav-pcm-mono`: onda quadrada inteira de 500 Hz, PCM S16LE, mono, 48 kHz e 1.000 ms;
- `wrong-extension-wav-as-mp3`: os mesmos bytes RIFF/WAVE sob extensão `.mp3`;
- `truncated-wav-pcm-mono`: prefixo exato de 40 bytes do WAV válido;
- `empty`: zero bytes.

O segundo incremento acrescenta sinais estéreo determinísticos de 2 s/48 kHz em `mp3-cbr-stereo`, `mp3-vbr-stereo`, `mp3-unicode-tags`, `m4a-aac-lc`, `flac-16-stereo`, `ogg-vorbis`, `opus-candidate` e `m4a-alac-candidate`. Todos carregam tags Unicode fixas; Opus e ALAC continuam apenas candidatos. Os bytes foram gerados por uma ferramenta FFmpeg local autenticada e reproduzidos duas vezes, mas os checks cotidianos validam somente os arquivos commitados, receitas, recibos e hashes, sem exigir FFmpeg.

O [manifesto](../../experiments/m0/shared/audio-corpus/manifest.v1.json), as receitas/recibo, a licença privada e o catálogo completo de IDs `PlaybackBackend`/`MetadataReader` são versionados. O catálogo histórico permanece `partial`; ele não declara suporte nem é alterado pelas extensões do produto.

O [corpus do produto](../audio-tests.md#prefixos-truncados-canônicos) acrescenta quatro prefixos canônicos de 64 bytes para MP3, M4A/AAC, FLAC e Ogg/Vorbis, autenticados antes do teste real de metadados. Seu manifesto registra a receita e a expectativa de rejeição, sem inventar propriedades do áudio truncado. O WAV truncado já existente cobre o quinto contêiner básico. M0.5 continua aberto pelos casos canônicos de arte e limites e demais critérios; os arquivos históricos permanecem intactos.

O recorte de [capas canônicas](../audio-tests.md#capas-canônicas-e-bordas-reais) acrescenta 14 MP3 com arte válida/inválida e casos reais abaixo/no/acima dos três limites de imagem. A extensão de [capas e textos](../audio-tests.md#capas-e-textos-no-corpus-dos-codecs) acrescenta três capas nos demais contêineres básicos e dez MP3 nos limites de título/artista/álbum e no agregado alcançável. Receitas, propriedades esperadas e hashes são versionados; os testes validam capa, diagnóstico e preservação dos metadados. O manifesto histórico permanece intacto.

Usar duração de 1–5 s nas fixtures comuns. Fixtures específicas de seek podem ter 30–60 s e ser geradas sob demanda se o tamanho ficar alto.

## 4. Geração

- preferir sinais sintéticos e texto gravado especificamente para o projeto;
- registrar ferramenta, versão e linha de argumentos como dados, sem concatenar input não confiável;
- quando FFmpeg for usado apenas para geração de fixture, documentar build/versão para reprodução;
- scripts devem ser determinísticos na medida possível;
- não exigir que todo desenvolvedor tenha encoders patenteados para rodar unitários;
- fixtures commitadas precisam de licença explícita do conteúdo e dos arquivos resultantes.

A fundação WAV é gerada apenas por APIs inteiras/binárias do .NET. Os codecs usam somente caminhos explícitos para executáveis FFmpeg/ffprobe com versão, tamanho e SHA-256 fixados; não há rede nem descoberta ambiental. Esse build GPL é ferramenta de autoria de fixtures e não integra runtime ou pacote. O conteúdo é original e sintético, sob `LicenseRef-MusicPlayer-M0-Private`; redistribuição pública permanece proibida até o mantenedor escolher uma licença pública.

O gerador aceita somente um stage novo ou vazio sob `out/m0`; ele não possui capability de sobrescrever o corpus commitado. Uma atualização é gerada em stage fresco, validada e promovida como diff revisável.

Checks rápidos atuais:

```powershell
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Invoke-M0AudioCorpusTests.ps1
pwsh -NoLogo -NoProfile -NonInteractive -File .\scripts\m0\Test-M0AudioCorpus.ps1
```

## 5. Suites

### Rápida

Em todo commit de adapter:

- abrir/probe;
- duração dentro da tolerância;
- primeiro frame decodificado;
- seek central quando suportado;
- tags/propriedades;
- erros de fixture hostil.

### Completa

Antes de release:

- tocar até o fim;
- múltiplos seeks;
- pausa/retomada;
- troca repetida de faixa;
- memória/handles;
- backend/dispositivo por plataforma.

### Soak

Arquivos longos são gerados/encadeados fora do Git. Registrar hash da receita, duração, contagem de loops e medidas.

## 6. Matriz publicada

O resultado atualiza a tabela de [requisitos](../product/requirements.md) com:

- versão do engine;
- plataforma;
- play, duração, seek, tags e capa;
- limitações por perfil;
- fixture usada;
- data/commit da validação.

Não marcar um formato como suportado apenas porque a biblioteca declara feature correspondente.

## 7. Segurança

- arquivos hostis são tratados como entrada não confiável;
- limites NFR-LIMIT de tamanho, dimensões, pixels e budget são aplicados a metadata/arte;
- fuzzing futuro pode usar fixtures mutadas sem redistribuir corpus de terceiros;
- crashes geram caso mínimo de regressão, quando legalmente redistribuível;
- paths de fixtures são construídos por APIs de path, nunca por shell.
