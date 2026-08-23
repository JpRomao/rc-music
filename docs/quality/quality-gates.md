# Gates de qualidade

**Estado:** contrato inicial; comandos definitivos serão ativados após o M0
**Última revisão:** 2026-08-23

Gates de comportamento, segurança e plataforma são invariantes. Durante o M0, cada candidato executa seus checks nativos e o protocolo comum NFR-STACK. Depois do ADR, os comandos definitivos pertencem apenas à stack vencedora; toolchains experimentais não são acumulados no produto.

## 1. Regra geral

O gate é proporcional ao risco, mas nenhuma mudança fica sem validação. Se um comando não existir ainda ou o ambiente impedir a execução, registre “não executado” e o risco; não converta ausência em sucesso.

Gates locais validam o working tree que será commitado. CI e release validam commit identificável, manifests e lockfiles commitados. Todos devem chamar os mesmos scripts de alto nível quando existirem.

## 2. Gates por tipo de mudança

| Mudança | Gates mínimos |
|---|---|
| Documentação | links relativos, estrutura, `git diff --check`, revisão de estado Proposto/Aceito |
| Domínio/application | format, lint, unitários focados, workspace tests, cobertura afetada |
| UI/componente | format/lint/typecheck, testes do componente, acessibilidade automatizada, build |
| Adapter de áudio/metadata | unitários, contrato, corpus afetado, leak/error paths |
| Boundary/IPC/segurança | schema/limites, input hostil, superfície QML/objetos expostos ou capabilities/CSP, integração |
| Plataforma | testes não-plataforma + build nativo + smoke específico |
| Persistência/migração | round-trip, versões anteriores, corrupção, falhas atômicas |
| Dependência | todos os testes afetados, advisories, licença, bundle e changelog upstream |
| Release | todos os gates, E2E, package smoke, performance e checklist manual |

## 3. Comandos previstos

Enquanto não houver scaffold, estes comandos são contrato de desenho e podem ser ajustados no primeiro commit de build. A raiz deve oferecer scripts equivalentes e documentados.

### C++/Qt no M0

- format/check de C++ e QML;
- configuração e build CMake Release reproduzíveis;
- Catch2 via CTest e Qt Test/Qt Quick Test onde aplicável;
- inspeção dos módulos, plugins, DLLs Qt e configuração FFmpeg distribuídos.

### Rust/Tauri no M0

```powershell
cargo fmt --all -- --check
cargo clippy --workspace --all-targets --all-features -- -D warnings
cargo test --workspace --all-features
```

### Svelte no M0

```powershell
pnpm install --frozen-lockfile
pnpm format:check
pnpm lint
pnpm check
pnpm test
pnpm build
```

### Gate agregado após a decisão

O bootstrap da stack aceita deve expor na raiz um comando `validate` documentado. O runner concreto pode ser PowerShell/CMake/CTest no ramo Qt ou o gerenciador adotado no ramo Tauri. Ele executa apenas checks determinísticos e adequados a todo commit; E2E, packaging, soak e benchmarks usam comandos separados.

## 4. Pipeline recomendado

### `fast`

Em todo commit/PR:

- verificação documental;
- format, análise estática e testes nativos da stack em escopo;
- testes do domínio/application e da apresentação afetada;
- build/check proporcional;
- auditoria de boundary, capabilities ou objetos expostos à UI quando a superfície privilegiada mudar.

### `windows-integration`

Em mudanças relevantes e antes de merge/release:

- build Release Windows x64;
- corpus de áudio;
- IPC/single-instance;
- E2E principais;
- instalador NSIS e smoke silencioso/interativo controlado;
- file associations e desinstalação em ambiente descartável;
- SMTC, tray e lifecycle.

### `linux-integration`

- build na distribuição de referência;
- corpus;
- MPRIS;
- Wayland/X11 conforme matriz;
- `.desktop`/MIME;
- `.deb` e smoke; AppImage quando suportado.

### `security-and-licenses`

- advisories C++/Qt/FFmpeg ou Rust/JS conforme o candidato;
- dependências duplicadas e features inesperadas;
- licenças permitidas/negadas;
- Qt LGPL/configuração FFmpeg ou CSP/capabilities Tauri;
- SBOM/notices no release.

### `performance`

- agendado ou antes de release, em runner controlado;
- compara com baseline e guarda artefatos;
- não bloqueia por ruído pequeno; bloqueia regressão confirmada além da tolerância.

## 5. Critérios bloqueadores

Um commit/release não está pronto se houver:

- teste, lint, typecheck ou build obrigatório falhando;
- formato declarado sem corpus aprovado;
- crash, panic ou exceção não tratada em entrada inválida conhecida;
- capability Tauri ou objeto/operação Qt exposto à UI além do necessário sem justificativa;
- regressão de teclado/leitor de tela em fluxo P0;
- corrupção/perda de sessão em teste de falha;
- associação de arquivo que toma default sem consentimento;
- dependência com licença incompatível não resolvida;
- código próprio não commitado;
- validação omitida apresentada como sucesso.

## 6. Performance gates

As metas vêm dos [requisitos](../product/requirements.md). Protocolo:

1. build Release limpo e identificável pelo commit;
2. máquina e versão do SO registradas;
3. warm-up separado de cold start;
4. ao menos 20 amostras curtas para P50/P95 quando aplicável;
5. medição de memória após janela estável definida;
6. CPU medida por intervalo, não valor instantâneo;
7. comparação com baseline da mesma máquina;
8. regressão confirmada em segunda execução.

Tolerância inicial sugerida: alerta em 5% e bloqueio em 10% para métrica estável, além do teto absoluto. Ajustar somente com evidência e commit documental.

## 7. Revisão de dependências

Antes de adicionar uma dependência:

- confirmar que a standard library/stack atual não resolve de modo simples;
- verificar atividade, manutenção e issues relevantes;
- registrar licença e features habilitadas;
- evitar `default-features` amplas sem necessidade;
- comparar efeito no lockfile, tempo de build e bundle Release;
- definir adapter se a dependência tocar domínio externo;
- adicionar teste que justifica a dependência;
- commitar manifest e lockfile juntos.

Atualizações automatizadas devem ser pequenas e passar pelos mesmos gates. Major versions não são misturadas com features.

## 8. Gate de documentação

Verificar:

- links relativos existem;
- nenhum ADR proposto é descrito como aceito;
- IDs de requisito continuam únicos;
- README e índice apontam para documentos reais;
- comandos correspondem ao repositório atual;
- formatos e plataformas não são alegados sem validação;
- riscos residuais e checks pulados aparecem no handoff;
- `git diff --check` está limpo.

## 9. Gate de release

Uma release exige:

- tag/versionamento consistente em todos os manifests, executáveis e metadados do bundle vencedor;
- changelog e lista de mudanças/riscos conhecidos;
- todos os critérios P0 rastreados para testes;
- corpus e matriz atualizados;
- Windows Tier 1 completo; Linux Tier 2 sem alegação além do testado;
- instalador/pacote instalado, atualizado e removido em ambiente limpo;
- arquivos/configurações do usuário preservados no upgrade e tratados no uninstall conforme política;
- hashes e manifesto dos artefatos;
- assinatura Windows para distribuição pública;
- notices/licenças/SBOM;
- malware scan quando público;
- working tree limpa e commit/tag publicados.

## 10. Evidência mínima de handoff

Todo handoff informa:

- resultado concreto;
- commits criados;
- comandos executados e resultado;
- testes manuais executados;
- checks não executados e motivo;
- riscos residuais;
- próximo passo seguro.
