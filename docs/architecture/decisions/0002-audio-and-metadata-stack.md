# ADR-0002: Rodio/Symphonia e Lofty para áudio local

**Estado:** Proposto  
**Data:** 2026-08-23  
**Decisor:** mantenedor do projeto

## Contexto

O MVP precisa tocar e buscar MP3, M4A/AAC e formatos locais comuns, exibir tags/propriedades e permanecer leve. Cobertura total de codecs e streaming complexo não são requisitos iniciais.

## Decisão proposta

- usar Rodio como API de reprodução e controle;
- usar CPAL como camada de saída trazida pelo stack;
- usar a versão Symphonia resolvida pelo Rodio e habilitar somente features correspondentes ao corpus aprovado; não adicionar uma segunda versão direta sem decisão;
- usar Lofty para tags, arte e propriedades, e filesystem para tamanho/mtime;
- executar áudio em owner/worker único atrás de `PlaybackBackend`;
- executar metadata em tarefas canceláveis separadas;
- não empacotar GStreamer, libmpv, libVLC ou segundo engine no MVP.

O owner/worker não garante, por si só, que Rodio deixe o callback CPAL livre de leitura/decode. O M0 deve medir underruns e stalls; se relevantes, o adapter adiciona prefetch/ring buffer limitado ou o backend é rejeitado.

## Motivos

- API pequena com posição e seek atende o player local;
- componentes Rust integram-se ao core e aos testes;
- features seletivas reduzem bundle e superfície;
- Lofty lê metadata sem carregar a faixa no engine;
- porta própria mantém substituição possível.

## Consequências

Positivas:

- implementação e distribuição menores;
- um único modelo de estado;
- boa segurança de memória no código próprio e decoders Rust;
- testes de contrato permitem evolução.

Negativas:

- nem todo perfil dentro de M4A/Opus será coberto;
- Rodio pode puxar decoder/arquivo no callback de saída; tempo real precisa de medição e talvez buffering próprio;
- dispositivos e backends Linux variam;
- streams HTTP avançados podem exigir engine diferente;
- fixtures e matriz por codec são obrigatórias antes de anunciar suporte.

## Alternativas

- **Qt Multimedia/FFmpeg:** excelente se ADR-0001 migrar todo o app para Qt.
- **GStreamer:** escolher se buffering, rede e codecs amplos virarem centrais.
- **libmpv:** escolher como substituto se fontes complexas justificarem custo/licença.
- **FFmpeg direto:** rejeitado enquanto não houver DSP/controle que exija pipeline próprio.

## Condições para aceitar

- corpus MP3 CBR/VBR, M4A/AAC-LC, FLAC, WAV e Ogg/Vorbis passa play, duração, seek e fim;
- Opus e ALAC recebem resultado explícito;
- seek satisfaz precisão/latência publicadas;
- fixtures corrompidas e arte/tag nos limites publicados retornam erro controlado sem derrubar processo;
- sessão de oito horas não apresenta crescimento sustentado;
- bundle e licenças das features são registrados;
- `cargo tree -d -e features` não mostra duplicação Symphonia acidental;
- arquivos em armazenamento lento não produzem underruns além do budget aceito, ou prefetch/fallback é decidido.

## Gatilhos de revisão

- formato obrigatório sem suporte;
- falhas recorrentes de seek VBR;
- troca de dispositivo ou suspensão não recuperável;
- fontes HTTP tornam-se P0/P1;
- custo de manter decode/output separado excede benefício.
