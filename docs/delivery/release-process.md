# Processo de release e distribuição

**Estado:** processo de release da stack aceita; runtime e instaladores locais gerados, sem RC aprovada
**Última revisão:** 2026-09-06

O [ADR-0005](../architecture/decisions/0005-windows-product-stack.md) aceita C++20/Qt Quick, Qt Multimedia/FFmpeg e TagLib. O produto possui [build gráfico raiz](../desktop-build.md), [receita de runtime](../windows-runtime.md) e [receita NSIS por usuário](../windows-installer.md). Tauri/Rust e a seleção A/B são históricos, não pré-requisitos desta entrega. Linux e benchmarks permanecem adiados, não aprovados; os gates abaixo continuam necessários para alegar release.

## Estado local e limites de autorização

Em 2026-09-06, o runtime privado Release/OFF `3f22fda` foi montado e passou no [smoke automatizado](../windows-runtime.md#smoke-automatizado-do-runtime). Foram compilados o setup OFF e um setup separado com [formatos v2 declarados](../windows-installer.md#instalador-com-formatos-v2), preservando o anterior. Inventários, hashes e recibos estão nos respectivos guias; a compilação não equivale a instalar, registrar tipos ou escolher defaults.

A autorização recebida foi **“Sim, gerar sem instalar”**. Nenhum desses setups foi executado; não houve alteração das associações do host. Ambos continuam experimentais e sem assinatura. Instalação/lifecycle, interação nativa interrompida e publicação exigem autorização específica; esse processo documentado não a concede. O [manual local](../user-guide.md) cobre uso e solução de problemas, não comprova RC, acessibilidade nativa ou execução em máquina limpa.

Antes de propor RC, ainda faltam evidências de cobertura/CI e dos gates de corpus, segurança/licenças/notices/SBOM, pré-requisitos externos, acessibilidade e lifecycle aplicáveis. Não usar resultados históricos de outro pacote para preencher esses gates. Adiamento de benchmarks/Linux não é waiver ou resultado de aprovação.

O [inventário e rascunho de notices do runtime 3f22fda](runtime-3f22fda-notices.md) já vinculam os arquivos atuais aos componentes identificados e preservam textos locais de terceiros. São entregas parciais de M5.7, fora do payload existente: divergências de fornecedor e atribuições/fontes faltantes continuam explícitas, sem alegação de SBOM completo ou conformidade de distribuição.

Os [SBOMs dos setups f8a593b](windows-f8a593b-sbom.md) vinculam as edições Lite e Completa aos seus hashes e às cinco combinações do instalador. A investigação de dependências transitivas, advisories e licenças permanece aberta.

## 1. Princípios

- release é resultado reproduzível de um commit/tag verde;
- Windows é Tier 1 e recebe validação completa primeiro;
- Linux é Tier 2 e só recebe alegações do ambiente realmente testado;
- formatos publicados correspondem ao corpus aprovado;
- instalador não toma associações sem consentimento;
- build local não assinado não é chamado de release pública;
- nenhuma publicação, upload ou criação de release externa ocorre sem autorização explícita do mantenedor.

## 2. Versionamento

Usar SemVer:

- `0.y.z` enquanto contratos e distribuição ainda evoluem;
- patch para bug compatível;
- minor para feature/alteração compatível dentro do 0.x;
- mudança incompatível durante 0.x precisa ser destacada no changelog;
- `1.0.0` somente após estabilidade real de formato de configuração, comportamento e atualização.

Versão deve coincidir em todos os manifests, executáveis, bundle e metadata dos artefatos da stack escolhida. Um script/check deve impedir divergência.

Esse gate de versão ainda está pendente: o executável local continua identificado como `M0 experimental`, e o nome do artefato inclui o commit de origem. Isso não constitui versão SemVer/RC nem autoriza reetiquetar os binários existentes. Definir a versão de release e sincronizar seus campos exige incremento próprio antes do candidato a release.

## 3. Canais

| Canal | Uso | Garantia |
|---|---|---|
| development | execução local/CI | sem estabilidade/distribuição |
| preview | teste do mantenedor em artefato empacotado | pode ser não assinado e não publicado |
| stable | uso diário/distribuição | gates completos, assinatura quando público e notas |

Não implementar auto-update no MVP. Download/instalação são manuais até existir threat model e necessidade.

## 4. Artefatos previstos

| Plataforma | Artefato inicial | Futuro/condicional |
|---|---|---|
| Windows x64 | NSIS por usuário, receita local implementada; gates de release pendentes | portátil `.zip`; MSI somente se houver caso real |
| Linux x64 | `.deb` da distribuição de referência | AppImage; `.rpm` após demanda |

Cada artefato acompanha:

- SHA-256;
- tamanho;
- versão/commit;
- SO/arquitetura;
- toolchains e modo de build;
- lista de formatos testados;
- assinatura/status;
- SBOM/notices/licenças;
- checks executados e riscos conhecidos.

## 5. Preparação

1. confirmar escopo e critérios do marco;
2. triagem de bugs P0/P1;
3. working tree limpa;
4. dependências/lockfiles revisados;
5. versão proposta e changelog; a versão experimental atual não substitui esse gate;
6. documentos, [manual e troubleshooting](../user-guide.md), matriz de formatos e suporte atualizados;
7. [validação local e CI Windows](../development-commands.md) verdes; Linux somente quando retomado com ambiente e escopo definidos, sem inferir resultado;
8. corpus completo, E2E e smoke de plataforma verdes;
9. performance/soak dentro do baseline aceito;
10. segurança, licenças e acessibilidade concluídas.

## 6. Build

- usar ambiente limpo e toolchains pinados;
- restore apenas por lockfiles;
- gerar todos os componentes da stack aceita em Release;
- empacotar com configuração commitada;
- gerar SBOM/notices e manifesto;
- assinar executável/instalador Windows quando público;
- calcular hashes depois da assinatura/empacotamento final;
- preservar logs de build sem secrets.

Um rebuild de verificação deve produzir comportamento/conteúdo equivalente; builds bit-for-bit são desejáveis, mas só podem ser alegados quando medidos.

Montagem/compilação autorizadas não devem disparar automaticamente a seção seguinte. Os builders atuais usam ferramentas locais explícitas e destinos novos; não instalam dependências, não executam setup e não alteram os padrões do Windows. Repetir a geração somente quando houver mudança relevante ou necessidade de validação aprovada.

## 7. Validação Windows

Com autorização específica, em VM/máquina descartável para cada caminho relevante. Os harnesses históricos que executam setup ou usam Registry isolado também ficam fora da autorização “gerar sem instalar”.

### Instalação limpa

- instalar por usuário sem admin quando possível;
- iniciar pelo menu e por arquivo;
- verificar ícone, versão, diretórios e DLLs/plugins Qt/FFmpeg/TagLib realmente distribuídos, além dos pré-requisitos externos;
- validar que nenhuma extensão virou default sem escolha;
- inspecionar ProgIDs, `Capabilities`, `RegisteredApplications` e `OpenWithProgids` próprios, apenas para os formatos declarados, sem modificar `UserChoice` ou padrões de terceiros;
- abrir página oficial de defaults;
- escolher o app e abrir MP3/M4A com paths Unicode/espaços;
- validar tray, SMTC e saída real.

### Upgrade

- instalar sobre última stable suportada;
- preservar preferências/sessão e associações elegíveis;
- migrar schema;
- verificar que executáveis/DLLs antigos não ficam ativos;
- rollback manual documentado quando migração for irreversível.

### Uninstall

- encerrar processo com consentimento;
- remover binários, atalhos e somente registros do próprio ProgID;
- não alterar defaults de terceiros;
- preservar dados do usuário por padrão ou oferecer opção clara;
- não remover arquivos de mídia;
- confirmar ausência de processo/serviço/tarefa residual.

### Portátil

- rodar sem instalador;
- explicar limitações de associação/upgrade;
- manter dados em local definido, não implicitamente ao lado do exe sem decisão;
- remover pasta não deve afetar dados externos.

## 8. Validação Linux

Etapa adiada. Os itens abaixo são obrigações futuras de uma distribuição Linux, não comandos para executar durante a prioridade Windows nem resultados já obtidos.

- instalar `.deb` na versão mínima e atual da distribuição de referência;
- validar dependências Qt/FFmpeg/TagLib e o backend de áudio efetivamente distribuído;
- abrir por menu, CLI e MIME;
- testar MPRIS e media keys;
- testar tray ou fallback visível no ambiente;
- smoke Wayland e X11 conforme matriz;
- upgrade/downgrade suportado e remoção;
- dados do usuário preservados;
- AppImage, quando existir, não promete integração MIME automática.

## 9. Acessibilidade e UX de release

- NVDA no Windows nas jornadas P0;
- teclado completo, 100/150/200%, alto contraste e reduced motion;
- temas sem flash e contraste validado;
- labels do instalador e mensagens de erro legíveis;
- Orca/AT-SPI no Linux conforme suporte declarado;
- nenhum bloqueador conhecido de leitor de tela sem risco aceito.

## 10. Publicação

Somente após autorização explícita:

1. criar commit de release com versão/changelog;
2. validar gates novamente;
3. criar tag assinada quando infraestrutura permitir;
4. produzir artefatos do commit/tag;
5. validar hashes e assinatura;
6. publicar notas, formatos, plataformas e limitações;
7. anexar artefatos/notices/SBOM;
8. executar smoke de download/instalação do artefato publicado;
9. registrar resultado e link no repositório.

## 11. Notas de release

Devem conter:

- resumo orientado ao usuário;
- novidades e correções;
- plataformas e formatos validados;
- mudanças de configuração/migração;
- limitações e bugs conhecidos;
- hashes e estado de assinatura;
- instrução de upgrade/uninstall quando mudou;
- checks não bloqueadores omitidos, se excepcionalmente aceitos; nunca usar omissão para declarar P0 vigente como aprovado.

Não anunciar “suporte a Linux”, “M4A” ou “app padrão” de forma ampla se somente parte da matriz foi testada.

## 12. Falha, rollback e hotfix

Se artefato publicado apresentar problema crítico:

- interromper divulgação e marcar release afetada;
- preservar evidência e reproduzir;
- não apagar silenciosamente artefato sem nota, salvo risco de segurança imediato;
- orientar rollback para última versão compatível;
- corrigir por TDD e gerar patch;
- verificar migrações/dados antes de downgrade;
- atualizar threat model/matriz quando aplicável.

Hotfix passa pelos gates afetados e smoke completo de pacote; urgência não autoriza publicar build não verificado como estável.

## 13. Checklist resumido

```text
[ ] escopo/bugs/version/changelog
[ ] working tree e commits limpos
[ ] fmt/lint/typecheck/test/build
[ ] corpus/E2E/plataforma
[ ] performance/soak
[ ] acessibilidade
[ ] security/licenças/SBOM
[ ] install/upgrade/uninstall
[ ] versões sincronizadas
[ ] assinatura (se público)
[ ] hashes/manifesto/notas
[ ] autorização explícita para publicar
[ ] smoke do artefato publicado
```
