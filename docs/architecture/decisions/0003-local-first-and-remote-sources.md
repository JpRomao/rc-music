# ADR-0003: núcleo local-first e fontes remotas isoladas

**Estado:** Proposto  
**Data:** 2026-08-23  
**Decisor:** mantenedor do projeto

## Contexto

O valor imediato está em arquivos locais. Reprodução por URL, especialmente YouTube, é uma possibilidade futura com riscos técnicos, operacionais, legais e de política.

## Decisão proposta

- `MediaSource` é tipado e começa somente com `LocalFile`;
- o domínio e o fluxo local não dependem de rede;
- uma porta `MediaSourceResolver` permite adapters futuros;
- resolver remoto produz descriptor efêmero com URL, headers, validade, metadata e capabilities;
- YouTube exige ADR próprio e revisão de termos antes de código/distribuição;
- conteúdo remoto nunca é carregado no WebView privilegiado;
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
- mantenedor aceita explicitamente o limite de YouTube no roadmap.

## Gatilhos de revisão

- requisito remoto sobe para MVP;
- engine escolhida já fornece uma abstração segura superior;
- termos/políticas ou caminho oficial mudam;
- fonte autenticada exige modelo de credenciais.
