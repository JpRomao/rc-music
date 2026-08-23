# ADR-0002: Comparação de áudio e metadados por stack

**Estado:** Proposto  
**Data:** 2026-08-23  
**Decisor:** mantenedor do projeto

## Contexto

O MVP precisa tocar e buscar MP3, M4A/AAC e formatos locais comuns, exibir tags e propriedades e permanecer leve. Cobertura total de codecs e streaming complexo não são requisitos iniciais.

O ADR-0001 passou a comparar dois shells. O desempenho da UI não comprova o desempenho do áudio: decoder, leitura, buffering, callback de saída e dispositivo dominam open-to-audible, seek e CPU de reprodução. Por isso, cada ramo deve validar seu backend no mesmo corpus, com a mesma porta e os mesmos critérios observáveis.

## Decisão proposta

Comparar no M0 dois ramos coerentes com os candidatos do ADR-0001:

### Ramo C++/Qt

- Qt Multimedia usando o backend FFmpeg distribuído e validado pelo projeto;
- TagLib para tags e propriedades que não dependam do estado do player;
- filesystem para tamanho e data de modificação;
- adapter C++ implementando `PlaybackBackend`, sem expor tipos Qt ao domínio ou à aplicação;
- leitura de metadata e arte em tarefas separadas da UI e do caminho de áudio, com cancelamento/geração no boundary; leitura síncrona já iniciada pode terminar, mas seu resultado é descartado e a concorrência permanece limitada.

### Ramo Rust/Tauri

- Rodio como API de reprodução e controle;
- CPAL como camada de saída trazida pelo stack;
- versão Symphonia resolvida pelo Rodio, com somente as features correspondentes ao corpus aprovado e sem segunda versão direta acidental;
- Lofty para tags, arte e propriedades, e filesystem para tamanho e data de modificação;
- owner/worker único atrás de `PlaybackBackend` e metadata em tarefas separadas com a mesma regra de cancelamento/geração e concorrência limitada.

Os dois ramos usam a mesma semântica de `PlaybackBackend`, a mesma matriz de formatos e o mesmo manifesto de fixtures. A comparação não autoriza empacotar dois engines na release: o candidato aceito deve distribuir exatamente um engine de reprodução e um caminho deliberado de metadata. Uma combinação cruzada ou troca futura exige evidência e atualização deste ADR; não deve ser introduzida como fallback silencioso.

Não empacotar GStreamer, libmpv, libVLC, FFmpeg direto adicional ou outro engine no MVP. Fontes remotas continuam fora deste experimento.

## Protocolo comum

Executar cada backend em build Release, preferencialmente por harness mínimo sem UI real, no mesmo hardware e contra o corpus versionado. Para cada fixture aplicável, validar:

- detecção, load, play, pause, duração, posição, seek, fim e unload;
- MP3 CBR/VBR, M4A/AAC-LC, FLAC, WAV e Ogg/Vorbis; Opus e ALAC recebem resultado explícito;
- tags ausentes, Unicode e inválidas, capa ausente/corrompida e limites NFR-LIMIT;
- extensão enganosa, arquivo truncado e formato não suportado com erro controlado;
- troca rápida, cancelamento e descarte de evento atrasado;
- open-to-audible e seek-to-audible P50/P95;
- CPU, private working set/private bytes da árvore, handles, underruns e stalls em reprodução comum; memória compartilhada é registrada separadamente;
- 500 trocas/seek e sessão de oito horas sem crescimento sustentado;
- armazenamento lento controlado e recuperação de device lost/suspend quando suportada.

O relatório identifica versão, features, backend efetivamente carregado, pacote testado, ambiente e limitações. Formato só pode ser anunciado após passar pelas capacidades exigidas no corpus.

## Gates específicos do ramo C++/Qt

- confirmar em runtime que o backend Qt Multimedia esperado usa a distribuição FFmpeg deliberada no Windows e no Linux de referência;
- testar o aplicativo empacotado, incluindo plugins, DLLs e codecs necessários, sem depender do ambiente de desenvolvimento;
- registrar módulos Qt distribuídos, configuração e versão do FFmpeg, codecs habilitados e notices;
- revisar obrigações LGPL do Qt e licenças aplicáveis à configuração FFmpeg/codec antes de distribuir;
- comprovar que o adapter de TagLib/propriedades descarta resultados obsoletos, limita concorrência e não acumula trabalho; registrar quando uma leitura síncrona já iniciada não puder ser interrompida;
- verificar que callbacks, signals e workers não executam I/O ou trabalho bloqueante na thread da UI ou no caminho de tempo real.

## Gates específicos do ramo Rust/Tauri

- `cargo tree -d -e features` não apresenta duplicação Symphonia acidental e registra as features efetivamente distribuídas;
- medir se Rodio/Symphonia realiza leitura ou decode capaz de bloquear o callback CPAL;
- se houver underruns ou stalls relevantes, testar prefetch/ring buffer limitado; rejeitar o backend se o budget continuar violado;
- testar o pacote sem dependências presentes apenas no ambiente de desenvolvimento;
- registrar backends CPAL efetivos no Windows e Linux e suas limitações de dispositivo/suspensão;
- revisar licenças e proveniência de Rodio, CPAL, Symphonia, Lofty e features habilitadas.

## Motivos

- Qt Multimedia/FFmpeg é o caminho integrado e maduro do candidato Qt;
- Rodio/Symphonia/Lofty mantém o ramo Tauri em Rust e oferece uma API pequena para o MVP;
- TagLib e Lofty desacoplam leitura de metadata do estado do engine;
- a porta comum permite testes de contrato e comparação de comportamento sem forçar uma implementação compartilhada;
- um único engine na release reduz tamanho, licenças, estados divergentes e carga de manutenção.

## Consequências

Positivas:

- codecs e performance são decididos por corpus e medidas, não por reputação da linguagem;
- cada stack é avaliada como seria distribuída;
- testes de contrato preservam a possibilidade de substituição futura;
- a matriz publicada diferencia contêiner, codec e capacidades observadas;
- a release não carrega fallback redundante.

Negativas:

- o M0 implementa dois adapters descartáveis;
- resultados podem variar por backend de plataforma, versão e dispositivo;
- Qt/FFmpeg amplia a revisão de distribuição e licenças;
- Rodio pode exigir buffering próprio ou ser rejeitado se o caminho de tempo real bloquear;
- nenhum ramo garante todos os perfis encontrados dentro de M4A, Opus ou ALAC.

## Condições para aceitar um ramo

- corpus P0 passa play, duração, seek e fim nos ambientes declarados;
- NFR-PERF-002, NFR-PERF-003 e NFR-PERF-007 são atendidos, e o backend completo permanece dentro de NFR-PERF-004/005, ou desvios recebem aprovação explícita;
- fixtures inválidas e nos limites publicados retornam erro controlado sem derrubar o processo;
- não há underrun acima do budget aceito nem I/O adicional do código próprio no callback de saída;
- cancelamento e troca rápida não publicam estado antigo;
- pacote, dependências, features e licenças são reproduzíveis e registrados;
- o ramo de UI correspondente também satisfaz o ADR-0001;
- a escolha recebe decisão explícita do mantenedor; até lá, ambos permanecem experimentos.

## Alternativas

- **GStreamer:** reconsiderar se buffering, rede e codecs amplos virarem centrais.
- **libmpv:** reconsiderar se fontes complexas justificarem custo, superfície e licença.
- **FFmpeg direto:** rejeitado enquanto não houver DSP ou controle que exija pipeline próprio.
- **Dois engines em fallback:** rejeitado no MVP por aumentar pacote, matriz de testes, licenças e estados de falha.

## Gatilhos de revisão

- formato P0 sem suporte consistente;
- falhas recorrentes de seek VBR ou M4A;
- troca de dispositivo ou suspensão não recuperável;
- nenhum ramo respeita budgets sem arquitetura de buffering adicional;
- fontes HTTP tornam-se P0/P1;
- custo de manter decode e output separados excede o benefício.
