# CLAUDE.md — RC Music

Siga o contrato operacional do projeto, que continua sendo a fonte das regras de trabalho, testes, commits e validação proporcional: @agents.md

## Limpeza de arquivos sem uso

Quando houver muitos arquivos que não são mais usados, faça a limpeza de tudo, não apenas de parte. Não deixe o acúmulo crescer à espera de um pedido.

**Quando aplicar**

- Ao perceber acúmulo: dezenas de pastas em `out/product`, worktrees detached cujo commit já está em `main`, builds de commits antigos, recibos e logs de frentes encerradas.
- Ao terminar uma frente que gerou artefatos temporários: capturas, logs, sondas, builds e scripts de diagnóstico.

**O que conta como lixo**

- Saídas de build, instaladores e runtimes de candidatos superados.
- Worktrees descartáveis e cópias de checkout.
- Evidências e scripts de diagnóstico de frentes concluídas.
- Arquivos temporários, `*.stackdump`, logs soltos, backups e cópias substituídas.
- Código, testes, assets e documentação mortos que permaneçam versionados.

**Como fazer**

1. Confirme que cada item não é usado: cache do build ativo, scripts, `CMakeLists`/`cmake`, testes, documentos vigentes e o plano de empacotamento.
2. Trabalho não commitado nunca é apagado. Preserve-o antes em commit ou branch próprios.
3. Nunca apague músicas ou arquivos pessoais, configurações do usuário fora do repositório, SDKs/toolchains usados pelo build ativo, nem o histórico Git (sem reescrita, `reset --hard` ou `clean` amplo).
4. Liste o que será removido, com tamanho e motivo, e confirme essa lista uma única vez com o mantenedor quando a remoção for permanente. Depois remova tudo de uma vez.
5. Arquivos versionados mortos saem em commit próprio (`chore:`), com as validações relevantes verdes.
6. Relate o espaço recuperado, o que foi removido e o que ficou, com o motivo.
