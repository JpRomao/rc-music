# Corpus de áudio e fixtures

**Estado:** especificação; fixtures ainda não criadas  
**Última revisão:** 2026-08-23

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

Usar duração de 1–5 s nas fixtures comuns. Fixtures específicas de seek podem ter 30–60 s e ser geradas sob demanda se o tamanho ficar alto.

## 4. Geração

- preferir sinais sintéticos e texto gravado especificamente para o projeto;
- registrar ferramenta, versão e linha de argumentos como dados, sem concatenar input não confiável;
- quando FFmpeg for usado apenas para geração de fixture, documentar build/versão para reprodução;
- scripts devem ser determinísticos na medida possível;
- não exigir que todo desenvolvedor tenha encoders patenteados para rodar unitários;
- fixtures commitadas precisam de licença explícita do conteúdo e dos arquivos resultantes.

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
