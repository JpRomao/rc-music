# ADR-0005: Stack definitiva do RC Music e desenvolvimento Windows

**Estado:** Aceito

**Data:** 2026-09-05

**Decisor:** mantenedor do projeto

**Substitui:** [ADR-0001](0001-language-and-ui-stack.md) e [ADR-0002](0002-audio-and-metadata-stack.md), como processo de seleção de stack/engine.

## Contexto e autorização

O mantenedor priorizou o desenvolvimento funcional Windows e adiou benchmarks e Linux. Depois, respondeu explicitamente **“Sim”** à adoção definitiva de C++20 + Qt Quick/QML, Qt Multimedia/FFmpeg e TagLib e à atualização do plano/ADRs para retirar o A/B como pré-requisito. Essa é uma decisão de produto e manutenção, não o resultado de uma vitória comparativa medida.

O código existente em `experiments/m0/candidates/qt/shell` já materializa reprodução local, fila, preferências/sessão, diagnóstico e integrações Windows, com evidências por incremento no [registro Qt](../../../experiments/m0/candidates/qt/README.md). Ainda depende estruturalmente de recursos e composição do experimento; escolher a stack não transforma automaticamente esse diretório em scaffold final ou release.

## Decisão

1. Adotar **C++20** para domínio, aplicação e portas, sem dependência de Qt, codecs ou sistema operacional nessas camadas.
2. Adotar **Qt 6 Quick/QML**, estilo Basic customizado e boundary/presenter C++ estreito para a UI. A composição de áudio usa **Qt Multimedia com FFmpeg** e **TagLib** para metadata, mantendo trabalho bloqueante fora da UI e um único engine deliberado.
3. Promover incrementalmente a implementação existente para uma unidade de build/distribuição do produto. Reutilizar lógica e testes, sem uma segunda implementação equivalente; separar dependências de fixture/harness M0 antes de declarar o executável de produto independente.
4. Retirar a comparação Qt/Tauri e a conclusão dos benchmarks/Linux de M0 como pré-requisitos para a decisão de stack e entrada em M1. A parte decisória de M0.10 é atendida por este ADR commitado; os ensaios não executados continuam sem resultado, não recebem aprovação retroativa.
5. Manter Windows como plataforma primária. Linux e medições de desempenho ficam para etapas posteriores, sem excluir seus requisitos, elevar budgets ou afirmar suporte validado. O A/B deixa de ser obrigatório para construir o produto; eventual retomada será pesquisa explicitamente solicitada, não dependência de todo commit.
6. Não exigir toolchains/checks Rust, Tauri, Svelte, Rodio ou Lofty para o produto. Preservar os artefatos M0 existentes fora da unidade do produto como registro de método, proveniência e regressões; não apagar nem alterar fixtures, manifests, protocolos autenticados ou pacotes históricos para fazer a migração passar.
7. Preservar **RC Music** na apresentação e **MusicPlayer** nos identificadores que localizam dados e coordenam instâncias, conforme [ADR-0004](0004-rc-music-identity.md). A promoção não autoriza migração implícita de dados, registro real, alteração de `UserChoice` ou instalação no host.

## O que permanece pendente

- `NFR-PERF-001–007`: metas mantidas; medições/soak posteriores, sem alegar budgets atingidos ou aprovar desvios por esta decisão.
- Requisitos funcionais, corpus/perfis efetivamente declarados, limites, recuperação, acessibilidade e evidências de plataforma continuam sujeitos aos checks proporcionais e aos gates de release. Nenhum P0 falho é dispensado pela escolha da stack.
- Distribuição permanece privada/experimental até seus próprios gates: segurança, licenças/configuração Qt/FFmpeg/TagLib, notices/SBOM, CRT/pré-requisitos, máquina limpa e validação do pacote. Não foi escolhida licença pública nem autorizada publicação.
- A interação nativa interrompida por Esc não está reautorizada por esta confirmação de stack. Computer Use, instalação/ativação real e cenários físicos continuam nos limites anteriores.
- [ADR-0003](0003-local-first-and-remote-sources.md) permanece `Proposto`; fontes remotas, telemetria, contas e serviços não entram no MVP local.

## Alternativas consideradas

- Concluir o A/B antes de desenvolver: substituído pela prioridade explícita do mantenedor; preserva valor diagnóstico, mas não permanece bloqueio de entrada em M1.
- Continuar indefinidamente como candidato sem decisão: rejeitado porque mantém o produto bloqueado formalmente e mistura histórico experimental com critérios atuais.
- Reescrever o app ou aceitar um wrapper sobre o experimento como promoção concluída: rejeitado; reutilizar uma única implementação e remover acoplamentos de forma verificável.
- Distribuir dois engines/toolchains como fallback: não adotado; aumenta manutenção e superfície sem requisito documentado.

## Consequências e validação

A direção técnica deixa de ser uma pergunta pendente. O trabalho passa da seleção comparativa para promoção e conclusão funcional Windows com TDD, preservando os testes determinísticos e os dados existentes. A decisão assume o risco de ainda precisar ajustar implementação, dependências ou arquitetura quando surgirem as medições e os gates nativos; não afirma que C++/Qt é superior em desempenho ou que a release está pronta.

Este incremento é documental: validar links, IDs/rastreabilidade, estados dos ADRs, coerência das fontes de verdade e diff antes do commit. Não repetir builds, suites de áudio/UI, benchmarks ou instalação somente para registrar a aprovação. Os próximos commits de migração exigem configure/build/test proporcionais e evidência de independência da unidade promovida.

## Critérios de revisão

Revisar por falha funcional P0 sem solução sustentável, incompatibilidade de licença/distribuição, risco de segurança material, custo de manutenção incompatível com o projeto pessoal ou medições posteriores que demonstrem orçamento inviável. Qualquer troca de toolkit, engine, identificadores de dados ou relaxamento de requisito exige decisão explícita e evidência; não introduzir fallback silencioso.
