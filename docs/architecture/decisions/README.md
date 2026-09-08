# Registros de decisão arquitetural

ADRs registram escolhas que alteram estrutura, dependências, distribuição ou riscos relevantes.

## Estados

- **Proposto:** recomendação em análise; não é contrato aceito.
- **Aceito:** aprovado pelo mantenedor ou adotado explicitamente na implementação.
- **Rejeitado:** avaliado e não escolhido.
- **Substituído:** decisão histórica substituída por outro ADR, com link.

## Índice

| ADR | Estado | Assunto |
|---|---|---|
| [0001](0001-language-and-ui-stack.md) | Substituído | Proposta histórica de comparação A/B; ver ADR-0005 |
| [0002](0002-audio-and-metadata-stack.md) | Substituído | Proposta histórica de comparação de áudio; ver ADR-0005 |
| [0003](0003-local-first-and-remote-sources.md) | Proposto | Mecanismo para isolar fontes remotas; local-first já é requisito |
| [0004](0004-rc-music-identity.md) | Aceito | Nome definitivo RC Music e continuidade dos dados locais |
| [0005](0005-windows-product-stack.md) | Aceito | C++20/Qt Quick, Qt Multimedia/FFmpeg e TagLib; A/B deixa de bloquear o produto Windows |
| [0006](0006-browser-editions-and-extensions.md) | Aceito | Escopo Lite/Completa, Brave/Chromium e extensões principal/YouTube independentes; ferramentas e implementação pendentes |
| [0007](0007-main-extension-boundary.md) | Aceito | Cliente MV3 adotado com detecção temporária, seleção explícita e fronteira nativa; host/download ainda separados |
| [0008](0008-download-job-service.md) | Aceito | Coordenação pura e apresentação de jobs, separadas do backend de execução |
| [0009](0009-download-execution-backends.md) | Proposto | Avaliação de Qt Network/FFmpeg, segurança e distribuição; sem habilitar downloads reais |

## Processo

1. criar ADR como `Proposto` antes de uma mudança estrutural;
2. validar alternativas e consequências com evidência;
3. obter decisão do mantenedor ou registrar aceitação na implementação;
4. nunca reescrever a história: substituir por novo ADR;
5. atualizar índice, arquitetura e roadmap no mesmo commit.
