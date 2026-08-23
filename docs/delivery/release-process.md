# Processo de release e distribuição

**Estado:** proposta operacional; ainda não há executável  
**Última revisão:** 2026-08-23

NSIS e os detalhes de runtime/toolchain de Qt/C++ ou Tauri/Rust só viram contrato definitivo após o ADR-0001. As regras de evidência, consentimento, assinatura e validação de pacote valem para qualquer stack escolhida.

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
| Windows x64 | NSIS por usuário | portátil `.zip`; MSI somente se houver caso real |
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
5. versão proposta e changelog;
6. documentos, matriz de formatos e suporte atualizados;
7. suite rápida Windows/Linux verde;
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

## 7. Validação Windows

Em VM/máquina descartável para cada caminho relevante:

### Instalação limpa

- instalar por usuário sem admin quando possível;
- iniciar pelo menu e por arquivo;
- verificar ícone, versão, diretórios e runtime distribuído: WebView2 no Tauri ou DLLs/plugins Qt/FFmpeg no candidato Qt;
- validar que nenhuma extensão virou default sem escolha;
- inspecionar que o template/hook NSIS registrou apenas o ProgID, `Capabilities` e `RegisteredApplications` do app; não usar `bundle.fileAssociations` isoladamente como prova;
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

- instalar `.deb` na versão mínima e atual da distribuição de referência;
- validar dependências Qt ou WebKitGTK e o backend de áudio efetivamente distribuído;
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
- checks omitidos, se excepcionalmente aceitos.

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
