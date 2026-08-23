# ADR-0003: isolar fontes remotas atrás de um resolver

**Estado:** Proposto  
**Data:** 2026-08-23  
**Decisor:** mantenedor do projeto

## Contexto

Local-first, funcionamento offline no MVP e independência de rede já são requisitos confirmados, não decisões pendentes deste ADR. O que permanece proposto é o mecanismo arquitetural para uma possível reprodução por URL, especialmente YouTube, com riscos técnicos, operacionais, legais e de política.

## Restrições já confirmadas

- arquivos locais e operação offline formam o MVP;
- rede futura não pode tornar o fluxo local indisponível;
- YouTube só existe “se possível” e não é promessa;
- nenhuma implementação remota começa antes de revisão técnica e de políticas.

## Decisão proposta

- representar origem por `MediaSource` tipado, começando somente com `LocalFile`;
- manter o domínio e o fluxo local sem dependência de rede;
- uma porta `MediaSourceResolver` permite adapters futuros;
- resolver remoto produz descriptor efêmero com URL, headers, validade, metadata e capabilities;
- YouTube exige ADR próprio e revisão de termos antes de código/distribuição;
- no candidato Tauri, conteúdo remoto nunca é carregado no WebView privilegiado;
- falhas remotas nunca mudam a disponibilidade do player local.

## Consequências positivas

- MVP menor, offline e confiável;
- risco remoto fica contido;
- podcasts/HTTP autorizados podem evoluir sem acoplar provedor;
- engine pode informar seek/duração por capability.

## Consequências negativas

- abstração de origem existe antes da segunda implementação;
- URL de página não toca diretamente;
- YouTube talvez nunca seja compatível com o conceito audio-only/tray;
- um resolver externo futuro terá ciclo de atualização próprio.

## Regras para uma implementação remota futura

- timeouts, cancelamento, cota de cache e limites de resposta;
- argumentos tipados; nenhuma concatenação de shell;
- allowlist de esquemas e validação contra acesso indevido a recursos locais;
- re-resolução limitada de URLs expiradas;
- cookies/credenciais nunca persistidos sem requisito e threat model;
- estado de live/não-seekable refletido em capabilities;
- logs redigidos;
- licença e termos registrados no release.

## Condições para aceitar

- API não torna o fluxo local mais complexo;
- threat model da origem local e remota documentado;
- testes provam que resolver indisponível não afeta playback local;
- mantenedor aceita que a porta proposta não adiciona complexidade prematura ao MVP.

## Gatilhos de revisão

- requisito remoto sobe para MVP;
- engine escolhida já fornece uma abstração segura superior;
- termos/políticas ou caminho oficial mudam;
- fonte autenticada exige modelo de credenciais.
