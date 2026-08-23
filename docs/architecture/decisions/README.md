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
| [0001](0001-language-and-ui-stack.md) | Proposto | Rust, Tauri 2 e Svelte |
| [0002](0002-audio-and-metadata-stack.md) | Proposto | Rodio/Symphonia e Lofty |
| [0003](0003-local-first-and-remote-sources.md) | Proposto | Mecanismo para isolar fontes remotas; local-first já é requisito |

## Processo

1. criar ADR como `Proposto` antes de uma mudança estrutural;
2. validar alternativas e consequências com evidência;
3. obter decisão do mantenedor ou registrar aceitação na implementação;
4. nunca reescrever a história: substituir por novo ADR;
5. atualizar índice, arquitetura e roadmap no mesmo commit.
