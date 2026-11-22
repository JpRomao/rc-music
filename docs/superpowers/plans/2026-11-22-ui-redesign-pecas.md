# Redesenho da interface, fatia 1: peças comuns — plano de implementação

> **Para agentes:** SUB-SKILL OBRIGATÓRIA: use superpowers:subagent-driven-development (recomendado) ou superpowers:executing-plans para executar este plano tarefa por tarefa. Os passos usam caixas (`- [ ]`) para acompanhamento.

**Objetivo:** criar as peças visuais do modo Aurora (botões, campos, escolhas, selos, avatar, menu, diálogo, cabeçalho de gaveta, avisos e estado vazio), trocar os ícones pelos Phosphor e registrar a licença, tudo com testes, sem mudar nenhuma tela ainda.

**Arquitetura:** cada peça é um arquivo QML novo em `src/ui/qml/` com o prefixo `Aurora`, registrado no módulo `MusicPlayer.M0`. Toda peça recebe `required property ShellUiContract contract` e tira cores e medidas só dele. Os componentes atuais (`AccessibleActionButton` e os demais) não mudam nesta fatia: as telas passam a usar as peças nas fatias seguintes, e o caminho do fixture M0 continua com os componentes antigos.

**Stack:** C++20, Qt 6.11.2 Quick/QML com estilo Basic, Qt Test e Qt Quick Test, CMake/Ninja.

**Spec:** [2026-11-22-ui-redesign-design.md](../specs/2026-11-22-ui-redesign-design.md) e o anexo de textos [2026-11-22-ui-redesign-copy.md](../specs/2026-11-22-ui-redesign-copy.md).

## Restrições globais

- Sem módulo Qt novo no runtime: nada de `QtQuick.Effects`, `QtQuick.Shapes`, `Qt5Compat` ou shaders. Sombra e recorte redondo são feitos com `Rectangle`.
- Texto visível só em catálogos `*Text.qml` com `qsTr`. Fora deles, literais só como identificador (`objectName`, `iconName`, `controlId`). O teste `rc_music.ui.contract` varre `src/ui/qml` inteiro, comentários incluídos.
- Nenhuma cor literal nas peças, exceto `"transparent"`. Cores e medidas vêm do `ShellUiContract`.
- Texto secundário (`textSecondaryColor`) nunca sobre `selectionColor` nem `hoverColor`: no tema claro dá 4,37:1. Nesses fundos, use `textPrimaryColor`.
- Toda peça interativa tem área clicável de pelo menos `contract.controlTargetMinimum` (40 px) de altura.
- Foco: anel de `contract.focusRingWidth` (3 px) em `contract.focusColor`, 2 px para fora do controle, visível só com `activeFocus`.
- Movimento: animações usam `contract.feedbackTransitionMs` (controles) ou `contract.panelTransitionMs` (diálogos e menus), que valem 0 com movimento reduzido.
- Estado desabilitado: `opacity: 0.4` nos temas normais. Em alto contraste, `opacity: 1`, sem preenchimento, texto e contorno em `disabledTextColor`.
- Alto contraste (`contract.highContrast`):
  - controles tonais e cartões: fundo `canvasColor` e contorno de 2 px em `textPrimaryColor`;
  - principal, escolhido e ligado: `accentColor` com `accentTextColor` (o highlight do sistema);
  - sem sombra;
  - marcas de estado em `accentTextColor` sobre o highlight.
- Marcas de estado (decisão 14 do spec), sempre em `accentColor`:
  - ponto de 4 px sob o ícone ligado;
  - traço de 3 px sob a opção escolhida;
  - linha de 1 px sob o campo, que vira 2 px em `focusColor` com foco ou em `errorColor` com erro, em `textSecondaryColor` no estado normal.
- Testes Quick rodam com `QT_FATAL_WARNINGS=1`: qualquer aviso de binding, anchors ou propriedade é falha.
- Testes Quick usam `testProductUiContract` (paleta Aurora) e `testScenarioDriver.setSystemAppearance(dark, reducedMotion)` / `setHighContrast(enabled, light)`. Todo `cleanup()` volta para claro, sem movimento reduzido e sem alto contraste.
- Nenhum passo toca som. Se algum teste novo precisar tocar, pare e peça permissão ao mantenedor antes.
- Não mexer em `EmptyState.qml`, `AccessibleActionButton.qml` e nos demais componentes do M0, nem em `tests/ui/fixtures/`.
- Commits pequenos, Conventional Commits em português no imperativo, terminando com `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Antes de cada commit: `git status`, revisão do diff e o check de formato.

### Comandos

Uma vez por terminal, a partir da raiz:

```powershell
$t = Import-PowerShellDataFile tools/windows.example.psd1
Import-Module (Join-Path $t.VisualStudioRoot 'Common7/Tools/Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $t.VisualStudioRoot -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64 -vcvars_ver=14.44 -winsdk=10.0.22621.0'
$b = 'out/product/ui-redesign-tests'
$env:QT_QPA_PLATFORM = 'offscreen'; $env:QSG_RHI_BACKEND = 'software'; $env:QT_QUICK_CONTROLS_STYLE = 'Basic'
$env:QT_FATAL_WARNINGS = '1'; $env:QT_QPA_FONTDIR = "$env:WINDIR\Fonts"
$env:PATH = "$PWD\out\product\qt-network-sdk\bin;$env:PATH"
```

`$b` é o build Release com `BUILD_TESTING=ON` já configurado para o redesenho. Se não existir, crie-o com `pwsh -NoProfile -File scripts/develop.ps1 lint -ToolchainFile tools/windows.example.psd1 -BuildDirectory out/product/ui-redesign-tests`: configura, compila e roda o lint, sem a suíte CTest inteira.

| Para | Comando | Passou quando |
|---|---|---|
| Compilar | `cmake --build $b --target rc_music_ui_quick_tests rc_music_ui_contract_tests` | termina sem erro |
| Um arquivo Quick | `& "$b/tests/ui/rc_music_ui_quick_tests.exe" -input tests/ui/quick/<arquivo>.qml -import "$b/tests/ui/qml" -o -,txt` | `Totals: N passed, 0 failed, 0 skipped` |
| Uma função Quick | o mesmo, acrescentando `<NomeDoTestCase>::<função>` | idem |
| Contrato de UI | `ctest --test-dir $b -R '^rc_music\.ui\.contract$' --output-on-failure --no-tests=error` | `100% tests passed` |
| qmllint | `cmake --build $b --target all_qmllint` | nenhum aviso novo em arquivo `Aurora*` |
| Formato | `pwsh -NoProfile -File scripts/develop.ps1 format -ToolchainFile tools/windows.example.psd1` (com `-Apply` para corrigir) | sem diferenças |

### Esqueleto dos testes de peças

Todo arquivo `tst_aurora_*.qml` desta fatia segue o padrão de `tst_shell_icons.qml`: a peça vive numa `Window` própria, criada com `createTemporaryObject`.

```qml
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtTest
import MusicPlayer.M0

TestCase {
    id: testCase
    name: "AuroraButtons"          // muda por arquivo
    when: windowShown
    readonly property var contract: testProductUiContract // qmllint disable unqualified
    readonly property var driver: testScenarioDriver // qmllint disable unqualified

    function cleanup() {
        driver.setHighContrast(false, false);
        driver.setSystemAppearance(false, false);
    }
    function themes() {
        return [{ tag: "claro", dark: false }, { tag: "escuro", dark: true }];
    }
    function sameColor(actual, expected, context) {
        verify(Qt.colorEqual(actual, expected), context + ": " + actual + " != " + expected);
    }

    Component { id: spyComponent; SignalSpy {} }
}
```

Nos trechos de teste abaixo, `host(...)` cria a `Window` (480×320, cor `contract.canvasColor`) com a peça e devolve a peça; cada arquivo tem o seu (`hostIcon`, `hostField` e assim por diante) e os `controlId` que os testes citam (`nickname`, `view`, `kick`, `settings-notice`, `queue-empty`, `queue-drawer`) vêm desses hosts. `child(item, name)` é `findChild(item, name)` seguido de `verify`.

## Foco da revisão

Cinco situações que o spec implica e que nenhum teste de tela cobre ainda. Cada uma tem um teste na tarefa dona do código.

1. **Rótulo longo em português num botão estreito.** O texto é cortado com reticências e o nome acessível continua inteiro. Teste: tarefa 3, `test_longLabelElidesButKeepsName`.
2. **Tema trocado com menu ou diálogo aberto.** As cores acompanham na hora, sem reabrir. Teste: tarefa 9, `test_themeChangeWhileOpen`.
3. **Movimento reduzido.** Interruptor, menu e diálogo não animam. Testes: tarefa 6, `test_switchMotionFollowsContract`, e tarefa 9, `test_dialogMotionFollowsContract`.
4. **Esc devolve o foco.** Fechar menu ou diálogo com Esc devolve o foco a quem abriu. Testes: tarefa 8, `test_escReturnsFocusToOpener`, e tarefa 9, `test_escClosesAndRestoresFocus`.
5. **Janela mínima (720×520).** O diálogo cabe na janela e rola por dentro em vez de vazar. Teste: tarefa 9, `test_dialogFitsMinimumWindow`.

## Decisões deste plano

- **Nomes:** prefixo `Aurora` em todas as peças, para não colidir com os componentes do M0, que continuam.
- **Peças que ficam para depois:** linha e cartão de faixa (com o menu da faixa) entram no plano da Biblioteca, porque a API delas segue os modelos de biblioteca, fila e playlist. A gaveta em si (deslizar, véu, prender o foco) fica no plano das gavetas, onde está a lógica de hoje. Aqui entra só o cabeçalho da gaveta.
- **Ícones:**
  - vêm do repositório `phosphor-icons/core`, commit fixo `2b75f3ad12b420c9504ef05df8d2564a28f8500e`, baixados de `raw.githubusercontent.com`, porque o registro npm está bloqueado neste ambiente;
  - mantêm os nomes de arquivo atuais em `assets/icons/`, para as telas de hoje passarem a usá-los sem mudar QML;
  - `inspect.svg` não é usado por nenhum QML nem C++ e sai.
- **Aviso de licença:** o aviso do Phosphor vai para `packaging/windows/notices/phosphor/`, onde ficam os avisos que acompanham o runtime, e não para `docs/delivery/licenses/`. O spec é corrigido no mesmo commit.
- **Tokens novos no contrato:**
  - `radiusControl` = 10, para controles;
  - `radiusLarge` = 16, para cartões, diálogos e gavetas (menus usam o `radiusMedium` atual, 12);
  - `focusRingWidth` = 3;
  - `shadowColor` = `#1A3C2814` (claro), `#59000000` (escuro) e `#00000000` (alto contraste).
- **Hover e pressionado:** uma camada da cor do texto sobre a cor de repouso:
  - `Qt.tint(repouso, Qt.rgba(t.r, t.g, t.b, 0.06))` para hover;
  - o mesmo com `0.12` para pressionado;
  - `t` é `Qt.color(contract.textPrimaryColor)`.

  Funciona igual nos dois temas. O botão discreto usa `hoverColor` no hover.

---

### Tarefa 1: Tokens das peças no ShellUiContract

**Arquivos:**
- Modificar: `src/presentation/ShellUiContract.hpp` (Q_PROPERTYs perto de `radiusMedium`, linha 32; `scrimColor`, linha 101)
- Modificar: `src/presentation/ShellUiContract.cpp` (perto de `radiusMedium()`, linha 152, e `scrimColor()`, linha 253)
- Teste: `tests/ui/qtest/ShellUiContractTests.cpp`

**Interfaces:**
- Produz, no QML: `contract.radiusControl: int`, `contract.radiusLarge: int`, `contract.focusRingWidth: int` (CONSTANT) e `contract.shadowColor: string` (`#AARRGGBB`, NOTIFY `appearanceChanged`). Os mesmos valores valem no construtor do fixture.

- [ ] **Passo 1: Escrever o teste que falha.** Declare o slot `exposesAuroraPieceTokens()` junto dos outros (linha 166 em diante). Para o alto contraste, use `HighContrastPalette{}` como os testes existentes do arquivo (linha 536).

```cpp
void ShellUiContractTests::exposesAuroraPieceTokens() {
    ShellUiContract contract{nullptr};
    QCOMPARE(contract.radiusControl(), 10);
    QCOMPARE(contract.radiusLarge(), 16);
    QCOMPARE(contract.focusRingWidth(), 3);
    contract.updateAppearance(ThemeMode::Light, MotionMode::System, false, false);
    QCOMPARE(contract.shadowColor(), QStringLiteral("#1A3C2814"));
    contract.updateAppearance(ThemeMode::Dark, MotionMode::System, true, false);
    QCOMPARE(contract.shadowColor(), QStringLiteral("#59000000"));
    contract.updateAppearance(ThemeMode::Light, MotionMode::System, false, false, HighContrastPalette{});
    QCOMPARE(contract.shadowColor(), QStringLiteral("#00000000"));
}
```

- [ ] **Passo 2: Rodar e ver falhar.** Compile e rode o contrato de UI. Esperado: erro de compilação, porque `radiusControl` não existe.
- [ ] **Passo 3: Implementar.**
  - Os três inteiros são `[[nodiscard]] int ... const noexcept` com os valores fixos, fora de `LayoutTokens`, porque o fixture não os tem.
  - `shadowColor()` segue o padrão de `scrimColor()`: transparente em alto contraste, e o valor do tema nos demais.
- [ ] **Passo 4: Rodar e ver passar.** Contrato de UI: `100% tests passed`.
- [ ] **Passo 5: Commit.** Rode o check de formato e faça `git add src/presentation/ShellUiContract.hpp src/presentation/ShellUiContract.cpp tests/ui/qtest/ShellUiContractTests.cpp`, com a mensagem `feat: expõe tokens das peças Aurora no contrato de UI`.

### Tarefa 2: Ícones Phosphor e aviso de licença

**Arquivos:**
- Substituir: os 28 SVGs de `assets/icons/` usados hoje
- Criar: `assets/icons/{more,caret-down,caret-right,warning,warning-circle,check-circle}.svg`
- Apagar: `assets/icons/inspect.svg`
- Criar: `packaging/windows/notices/phosphor/NOTICE.txt` e `packaging/windows/notices/phosphor/Phosphor-LICENSE.txt`
- Modificar: `packaging/windows/New-RcMusicLocalRuntime.ps1:78-90` (lista `$names`), `packaging/windows/tests/RuntimeRecipe.Tests.ps1:19` e `packaging/windows/tests/LocalRuntime.Tests.ps1:42`
- Modificar: `docs/superpowers/specs/2026-11-22-ui-redesign-design.md`, nas seções “Restrições” e “Ordem das fatias”, item 1 (local do aviso e commit de origem)
- Teste: `tests/ui/quick/tst_shell_icons.qml`

**Interfaces:**
- Produz os nomes de ícone que as peças usam: `more`, `caret-down`, `caret-right`, `warning`, `warning-circle`, `check-circle`, além dos 28 atuais menos `inspect`.

Origem: `https://raw.githubusercontent.com/phosphor-icons/core/2b75f3ad12b420c9504ef05df8d2564a28f8500e/assets/<pasta>/<nome>.svg`. Na pasta `fill`, o nome termina em `-fill`.

| Arquivo em `assets/icons/` | Phosphor |
|---|---|
| activity | regular/download-simple |
| arrow-down, arrow-up, check, info, minus, moon, playlist, plus, queue, repeat, shuffle, sun | regular/ com o mesmo nome |
| arrow | regular/arrow-right |
| browser | regular/globe-simple |
| close | regular/x |
| home | regular/house |
| library | regular/books |
| lyrics | regular/microphone-stage |
| mute | regular/speaker-slash |
| volume | regular/speaker-high |
| people | regular/users-three |
| search | regular/magnifying-glass |
| settings | regular/sliders-horizontal |
| play, pause | fill/play-fill, fill/pause-fill |
| previous, next | fill/skip-back-fill, fill/skip-forward-fill |
| more | regular/dots-three |
| caret-down, caret-right, warning, warning-circle, check-circle | regular/ com o mesmo nome |

- [ ] **Passo 1: Escrever os testes que falham.**
  - Em `tst_shell_icons.qml`, troque `catalog` para a lista nova: os 28 atuais sem `inspect`, mais os 6 novos.
  - Em `RuntimeRecipe.Tests.ps1:19` e `LocalRuntime.Tests.ps1:42`, acrescente `'notices/phosphor/NOTICE.txt'` e `'notices/phosphor/Phosphor-LICENSE.txt'` à lista esperada, depois de `notices/taglib/utfcpp-LICENSE.txt`.
- [ ] **Passo 2: Rodar e ver falhar.**
  - `tst_shell_icons.qml`: esperado `6 failed`, nos ícones novos que ainda não existem.
  - `pwsh -NoProfile -File packaging/windows/tests/RuntimeRecipe.Tests.ps1`: esperado falha pela falta dos dois avisos.
- [ ] **Passo 3: Baixar os ícones.** Baixe cada SVG com `curl -fsSL <origem> -o assets/icons/<arquivo>.svg`, conforme a tabela. Antes, confirme que nenhum QML nem C++ usa `inspect` com `rg -n '"inspect"|inspect\.svg' src` (sem resultado) e apague `inspect.svg`.
- [ ] **Passo 4: Criar o aviso e registrá-lo no runtime.**
  - `Phosphor-LICENSE.txt` é o `LICENSE` do mesmo commit, sem alteração.
  - `NOTICE.txt` segue o formato de `notices/taglib/NOTICE.txt`: o RC Music inclui ícones do Phosphor Icons (MIT), compilados no recurso `:/rc_music/icons`; a origem é `phosphor-icons/core` no commit acima.
  - Acrescente os dois caminhos ao `$names` de `New-RcMusicLocalRuntime.ps1`, depois de `utfcpp-LICENSE.txt`.
  - No spec, troque `docs/delivery/licenses/` por `packaging/windows/notices/phosphor/` e registre o commit de origem.
- [ ] **Passo 5: Rodar e ver passar.**
  - Compile e rode `tst_shell_icons.qml`: `0 failed`. O teste de tingimento prova que cada SVG carrega e aceita a cor.
  - `RuntimeRecipe.Tests.ps1` passa.
  - `LocalRuntime.Tests.ps1` precisa de um runtime montado: não rode agora e registre isso como pendente no commit.
- [ ] **Passo 6: Commit.** Formato, `git add assets/icons packaging/windows/notices/phosphor packaging/windows/New-RcMusicLocalRuntime.ps1 packaging/windows/tests/RuntimeRecipe.Tests.ps1 packaging/windows/tests/LocalRuntime.Tests.ps1 tests/ui/quick/tst_shell_icons.qml docs/superpowers/specs/2026-11-22-ui-redesign-design.md`, com a mensagem `feat: troca os ícones pelos Phosphor e inclui o aviso de licença`.

### Tarefa 3: Catálogo de controles, anel de foco e botão

**Arquivos:**
- Criar: `src/ui/qml/ControlsText.qml`, `src/ui/qml/AuroraFocusRing.qml` e `src/ui/qml/AuroraButton.qml`
- Modificar: `cmake/RcMusicUi.cmake`, na lista `foreach(qml_file IN ITEMS ...)`, depois de `ProfileSetupDialog.qml`. As próximas tarefas acrescentam seus arquivos no mesmo lugar.
- Teste: `tests/ui/quick/tst_aurora_buttons.qml`

**Interfaces:**
- `ControlsText` (QtObject): `close: qsTr("Fechar")` e `cancel: qsTr("Cancelar")`. Quem usa cria uma instância (`ControlsText { id: controlsText }`) e lê `controlsText.close`, como nos outros catálogos.
- `AuroraFocusRing` (Rectangle, `objectName: "focus-ring"`):
  - `required property Item target`, `required property ShellUiContract contract`, `property real targetRadius`;
  - fica 2 px para fora do alvo, com `radius: targetRadius + 2`, `border.width: contract.focusRingWidth`, `border.color: contract.focusColor`, cor `transparent` e `visible: target.activeFocus`.
- `AuroraButton` (Button):
  - `enum Kind { Primary, Tonal, Quiet, Danger, DangerFilled }`;
  - `required property ShellUiContract contract`, `required property string controlId`;
  - `property int kind: AuroraButton.Primary`, `property string iconName`, `property bool compact`;
  - filhos com `objectName`: `button-background`, `button-label` e `button-icon` (um `ShellIcon`);
  - Enter e Return clicam, como no `AccessibleActionButton`;
  - `Accessible.name: text`; o rótulo usa `elide: Text.ElideRight`, numa linha só;
  - altura `controlTargetMinimum`; com `compact`, o fundo fica com 32 px, centralizado.

Cores em repouso, nos temas normais:

| kind | fundo | texto e ícone |
|---|---|---|
| Primary | accentColor | accentTextColor |
| Tonal | selectionColor | textPrimaryColor |
| Quiet | transparent | textPrimaryColor |
| Danger | selectionColor | errorColor |
| DangerFilled | errorColor | accentTextColor |

- [ ] **Passo 1: Escrever os testes que falham** em `tst_aurora_buttons.qml`.

```qml
function test_kindsUseTheirColors_data() {
    const kinds = [
        ["Primary", AuroraButton.Primary, "accentColor", "accentTextColor"],
        ["Tonal", AuroraButton.Tonal, "selectionColor", "textPrimaryColor"],
        ["Quiet", AuroraButton.Quiet, "transparent", "textPrimaryColor"],
        ["Danger", AuroraButton.Danger, "selectionColor", "errorColor"],
        ["DangerFilled", AuroraButton.DangerFilled, "errorColor", "accentTextColor"]];
    const rows = [];
    for (const theme of themes())
        for (const k of kinds)
            rows.push({ tag: k[0] + "-" + theme.tag, dark: theme.dark, kind: k[1], bg: k[2], fg: k[3] });
    return rows;
}
function test_kindsUseTheirColors(data) {
    driver.setSystemAppearance(data.dark, false);
    const button = host(data.kind);
    sameColor(child(button, "button-background").color, data.bg === "transparent" ? "transparent" : contract[data.bg], "fundo");
    sameColor(child(button, "button-label").color, contract[data.fg], "texto");
}
function test_targetIsAtLeast40() {      // normal e compact
    for (const compact of [false, true]) {
        const button = host(AuroraButton.Tonal, { compact: compact });
        verify(button.height >= contract.controlTargetMinimum);
        compare(child(button, "button-background").height, compact ? 32 : button.height);
    }
}
function test_keyboardActivates() {      // Enter, Return e Espaço: um clicked por tecla
    const button = host(AuroraButton.Primary);
    const spy = createTemporaryObject(spyComponent, testCase, { target: button, signalName: "clicked" });
    button.forceActiveFocus(Qt.TabFocusReason);
    keyClick(Qt.Key_Return); keyClick(Qt.Key_Enter); keyClick(Qt.Key_Space);
    compare(spy.count, 3);
}
function test_focusRingOnlyWithFocus() {
    const button = host(AuroraButton.Primary);
    const ring = child(button.parent, "focus-ring");
    verify(!ring.visible);
    button.forceActiveFocus(Qt.TabFocusReason);
    verify(ring.visible);
    compare(ring.border.width, 3);
    sameColor(ring.border.color, contract.focusColor, "anel");
}
function test_disabledIsVisibleAndInert() {
    const button = host(AuroraButton.Primary, { enabled: false });
    compare(button.opacity, 0.4);
    mouseClick(button);                   // nenhum clicked
}
function test_highContrastOutlinesTonal() {
    driver.setHighContrast(true, true);
    const tonal = host(AuroraButton.Tonal);
    sameColor(child(tonal, "button-background").color, contract.canvasColor, "fundo");
    compare(child(tonal, "button-background").border.width, 2);
    sameColor(child(tonal, "button-background").border.color, contract.textPrimaryColor, "contorno");
    sameColor(child(host(AuroraButton.Primary), "button-background").color, contract.accentColor, "principal");
}
function test_longLabelElidesButKeepsName() {
    const text = "Abrir pasta de downloads e conferir os arquivos";
    const button = host(AuroraButton.Tonal, { text: text, width: 160 });
    verify(child(button, "button-label").truncated);
    compare(button.Accessible.name, text);
}
function test_colorsFollowThemeChange() {
    const button = host(AuroraButton.Primary);
    driver.setSystemAppearance(true, false);
    sameColor(child(button, "button-background").color, contract.accentColor, "depois da troca");
}
```

- [ ] **Passo 2: Rodar e ver falhar.** Esperado: o arquivo não carrega, porque `AuroraButton` não existe.
- [ ] **Passo 3: Implementar** `ControlsText.qml`, `AuroraFocusRing.qml` e `AuroraButton.qml`, e registrar os três no CMake.
  - Hover e pressionado seguem as “Decisões deste plano”.
  - O anel de foco é irmão visual do fundo dentro do botão (`z` acima), com `targetRadius: contract.radiusControl`.
  - Em alto contraste com hover, o contorno do fundo fica com 2 px em `accentColor`.
- [ ] **Passo 4: Rodar e ver passar** o arquivo Quick, o contrato de UI e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria o botão Aurora com cinco tipos e anel de foco`.

### Tarefa 4: Botão de ícone

**Arquivos:**
- Criar: `src/ui/qml/AuroraIconButton.qml`, registrado no CMake
- Teste: `tests/ui/quick/tst_aurora_buttons.qml`

**Interfaces:**
- `AuroraIconButton` (AbstractButton):
  - `required contract`, `required controlId`, `required property string iconName`;
  - `text`, que é o nome acessível e o tooltip;
  - `property bool tonal`, `property int iconSize: 20`, mais `checkable` e `checked` herdados;
  - tamanho de 40×40 e raio `radiusControl`;
  - filhos: `icon-button-background`, `button-icon` e `state-mark` (ponto de 4 px, 4 px acima da borda de baixo, centralizado).
- Cores:
  - repouso: fundo `transparent`, ou `selectionColor` se `tonal`; ícone `textPrimaryColor`;
  - ligado: fundo `selectionColor`, ícone e ponto em `accentColor`;
  - alto contraste ligado: fundo `accentColor`, ícone e ponto em `accentTextColor`.
- `ToolTip.text: text` e `ToolTip.visible: hovered && text.length > 0`, com `ToolTip.delay: 600`.

- [ ] **Passo 1: Escrever os testes que falham.**

```qml
function test_iconButtonIs40AndNamed() {
    const b = hostIcon({ iconName: "more", text: "Mais ações" });
    compare(b.width, 40); compare(b.height, 40);
    compare(b.Accessible.name, "Mais ações");
    compare(child(b, "button-icon").iconName, "more");
}
function test_checkedShowsMark_data() { return themes(); }
function test_checkedShowsMark(data) {
    driver.setSystemAppearance(data.dark, false);
    const b = hostIcon({ iconName: "shuffle", text: "Aleatório", checkable: true });
    verify(!child(b, "state-mark").visible);
    sameColor(child(b, "button-icon").color, contract.textPrimaryColor, "desligado");
    b.toggle();
    verify(child(b, "state-mark").visible);
    compare(child(b, "state-mark").width, 4);
    sameColor(child(b, "state-mark").color, contract.accentColor, "ponto");
    sameColor(child(b, "icon-button-background").color, contract.selectionColor, "fundo");
    sameColor(child(b, "button-icon").color, contract.accentColor, "ícone");
    compare(b.Accessible.checked, true);
}
function test_checkedInHighContrastUsesHighlight() {
    driver.setHighContrast(true, true);
    const b = hostIcon({ iconName: "repeat", text: "Repetir", checkable: true, checked: true });
    sameColor(child(b, "icon-button-background").color, contract.accentColor, "fundo");
    sameColor(child(b, "state-mark").color, contract.accentTextColor, "ponto");
}
function test_tooltipShowsName() {
    const b = hostIcon({ iconName: "close", text: "Fechar fila" });
    mouseMove(b);
    tryVerify(() => b.ToolTip.toolTip.visible, 2000);
    compare(b.ToolTip.text, "Fechar fila");
}
```

- [ ] **Passo 2: Rodar e ver falhar.** Esperado: o tipo `AuroraIconButton` não existe.
- [ ] **Passo 3: Implementar** com o mesmo `AuroraFocusRing` e as mesmas teclas (Enter, Return e Espaço) do botão.
- [ ] **Passo 4: Rodar e ver passar** `tst_aurora_buttons.qml`, o contrato de UI e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria o botão de ícone Aurora com estado ligado marcado`.

### Tarefa 5: Campo de texto e caixa de seleção

**Arquivos:**
- Criar: `src/ui/qml/AuroraTextField.qml` e `src/ui/qml/AuroraSelect.qml`, registrados no CMake
- Teste: `tests/ui/quick/tst_aurora_fields.qml`

**Interfaces:**
- `AuroraTextField` (ColumnLayout que contém um TextField):
  - `required contract`, `required controlId` (vira o `objectName` do TextField interno), `required property string label` (o `Accessible.name` do campo);
  - `property alias text`, `property alias placeholderText`, `property alias readOnly`, `property string errorText`, `property string leadingIconName`;
  - `property alias input` (o TextField) e `signal accepted()`.
  - O campo tem fundo `surfaceColor`, raio `radiusControl` e nenhum contorno; altura 40.
  - Filhos com `objectName`:
    - `field-line`: a linha das marcas de estado, ancorada embaixo;
    - `field-icon`: o ícone à esquerda; com ícone, `leftPadding` fica em 40;
    - `<controlId>-error`: o texto de erro abaixo, em `errorColor`, visível só com `errorText`.
  - Com erro, `Accessible.description` do campo é o `errorText`.
  - `placeholderTextColor` é `textSecondaryColor`.
  - Em alto contraste: sem linha; contorno de 2 px em `textPrimaryColor`, que vira `accentColor` com foco ou erro.
- `AuroraSelect` (ComboBox):
  - `required contract`, `required controlId`, `required property string label`;
  - fundo e `field-line` iguais aos do campo;
  - indicador `ShellIcon` com o ícone `caret-down`;
  - popup em `surfaceRaisedColor`, raio `radiusMedium`, com `AuroraShadow` (tarefa 8; até lá, sem sombra);
  - delegado de 40 px, com `highlighted` em `hoverColor`;
  - a opção atual leva o ícone `check` em `accentColor` (`objectName: "select-current-mark"`).

- [ ] **Passo 1: Escrever os testes que falham.**

```qml
function test_fieldHasLineNotBorder_data() { return themes(); }
function test_fieldHasLineNotBorder(data) {
    driver.setSystemAppearance(data.dark, false);
    const f = hostField({ label: "Apelido" });
    compare(f.input.background.border.width, 0);
    const line = child(f, "field-line");
    compare(line.height, 1);
    sameColor(line.color, contract.textSecondaryColor, "linha");
    f.input.forceActiveFocus(Qt.TabFocusReason);
    compare(line.height, 2);
    sameColor(line.color, contract.focusColor, "linha com foco");
}
function test_errorShowsBelowAndColorsLine() {
    const f = hostField({ label: "Apelido", errorText: "Use de 1 a 32 caracteres." });
    const error = child(f, "nickname-error");
    verify(error.visible);
    sameColor(error.color, contract.errorColor, "erro");
    sameColor(child(f, "field-line").color, contract.errorColor, "linha");
    compare(f.input.Accessible.description, "Use de 1 a 32 caracteres.");
}
function test_leadingIconMakesRoom() {
    const f = hostField({ label: "Filtrar", leadingIconName: "search" });
    verify(child(f, "field-icon").visible);
    compare(f.input.leftPadding, 40);
}
function test_fieldInHighContrastHasOutline() {
    driver.setHighContrast(true, true);
    const f = hostField({ label: "Apelido" });
    verify(!child(f, "field-line").visible);
    compare(f.input.background.border.width, 2);
}
function test_selectOpensAndChoosesWithKeyboard() {
    const s = hostSelect({ label: "Saída de áudio", model: ["Padrão do sistema", "Fones"] });
    s.forceActiveFocus(Qt.TabFocusReason);
    keyClick(Qt.Key_Space);
    tryVerify(() => s.popup.opened);
    keyClick(Qt.Key_Down); keyClick(Qt.Key_Return);
    compare(s.currentIndex, 1);
    sameColor(child(s, "field-line").color, contract.textSecondaryColor, "linha");
}
```

- [ ] **Passo 2: Rodar e ver falhar.** Esperado: tipos inexistentes.
- [ ] **Passo 3: Implementar.** O ColumnLayout não aceita foco (`activeFocusOnTab: false`): o Tab cai direto no TextField interno.
- [ ] **Passo 4: Rodar e ver passar** o arquivo, o contrato e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria campo de texto e caixa de seleção Aurora`.

### Tarefa 6: Chave segmentada, interruptor, “− valor +” e caixinha de marcar

**Arquivos:**
- Criar: `src/ui/qml/AuroraSegmented.qml`, `AuroraSwitch.qml`, `AuroraStepper.qml` e `AuroraCheckBox.qml`, registrados no CMake
- Teste: `tests/ui/quick/tst_aurora_choices.qml`

**Interfaces:**
- `AuroraSegmented` (FocusScope):
  - `required contract`, `required controlId`, `required property string label`, `required property var options` (lista de textos);
  - `property int currentIndex`, `signal activated(int index)`.
  - Cada opção é um Item de 40 px de altura, `objectName: controlId + "-" + index`, com `Accessible.role: Accessible.RadioButton`, `Accessible.name` igual ao texto e `Accessible.checked`.
  - Só a opção escolhida tem `activeFocusOnTab`, no padrão de grupo de rádio.
  - ←/→ movem e escolhem, sem dar a volta; Home/End vão às pontas.
  - Visual:
    - trilho em `selectionColor`;
    - opção escolhida com pílula interna (3 px de margem) em `surfaceColor`, texto `accentColor` semibold e traço `state-mark` de 3 px sob o texto;
    - opções não escolhidas com texto `textPrimaryColor`.
  - Em alto contraste: trilho `canvasColor` com contorno de 2 px; escolhida em `accentColor` com texto e traço em `accentTextColor`.
- `AuroraSwitch` (Switch):
  - `required contract`, `required controlId`, `required property string label` (`Accessible.name`; o texto visível fica na linha de quem usa);
  - indicador de 40×24, raio 12, dentro de altura 40;
  - desligado: trilho transparente, contorno de 2 px e polegar de 14 px, os dois em `textSecondaryColor`;
  - ligado: trilho `accentColor` e polegar de 18 px em `accentTextColor`;
  - o polegar anda com `Behavior on x { NumberAnimation { duration: contract.feedbackTransitionMs } }`;
  - filhos: `switch-track` e `switch-thumb`.
- `AuroraStepper` (SpinBox, `editable: true`):
  - `required contract`, `required controlId`, `required property string label`, `property string suffix` (texto do catálogo de quem usa, como `s` ou `ms`);
  - `textFromValue` acrescenta `" " + suffix` quando há sufixo; `valueFromText` lê o número;
  - indicadores de 40×40 com `ShellIcon` `minus` e `plus`;
  - centro com fundo `surfaceColor` e `field-line`.
- `AuroraCheckBox` (CheckBox):
  - `required contract`, `required controlId`;
  - indicador de 20×20, raio 6;
  - desmarcado: contorno de 2 px em `textSecondaryColor`;
  - marcado: preenchimento `accentColor` e ícone `check` de 14 px em `accentTextColor`;
  - texto em `textPrimaryColor` com quebra de linha; altura de pelo menos 40.

- [ ] **Passo 1: Escrever os testes que falham.**

```qml
function test_segmentedIsOneTabStop() {
    // Antes e depois do segmentado há um AuroraButton.
    const h = hostSegmented({ options: ["Lista", "Capas"], currentIndex: 1 });
    h.before.forceActiveFocus(Qt.TabFocusReason);
    keyClick(Qt.Key_Tab);
    verify(child(h.segmented, "view-1").activeFocus);
    keyClick(Qt.Key_Tab);
    verify(h.after.activeFocus);
}
function test_segmentedArrowsSelect() {
    const h = hostSegmented({ options: ["Sistema", "Claro", "Escuro"], currentIndex: 0 });
    const spy = createTemporaryObject(spyComponent, testCase, { target: h.segmented, signalName: "activated" });
    child(h.segmented, "view-0").forceActiveFocus(Qt.TabFocusReason);
    keyClick(Qt.Key_Right);
    compare(h.segmented.currentIndex, 1); compare(spy.count, 1);
    verify(child(h.segmented, "view-1").activeFocus);
    keyClick(Qt.Key_End); compare(h.segmented.currentIndex, 2);
    keyClick(Qt.Key_Right); compare(h.segmented.currentIndex, 2);
    keyClick(Qt.Key_Home); compare(h.segmented.currentIndex, 0);
}
function test_segmentedMarksTheChoice_data() { return themes(); }
function test_segmentedMarksTheChoice(data) {
    driver.setSystemAppearance(data.dark, false);
    const h = hostSegmented({ options: ["Lista", "Capas"], currentIndex: 0 });
    const mark = child(child(h.segmented, "view-0"), "state-mark");
    verify(mark.visible); compare(mark.height, 3);
    sameColor(mark.color, contract.accentColor, "traço");
    verify(!child(child(h.segmented, "view-1"), "state-mark").visible);
    sameColor(child(child(h.segmented, "view-1"), "segment-label").color, contract.textPrimaryColor, "não escolhida");
    compare(child(h.segmented, "view-0").height, 40);
    compare(child(h.segmented, "view-0").Accessible.checked, true);
}
function test_switchStates_data() { return themes(); }
function test_switchStates(data) {
    driver.setSystemAppearance(data.dark, false);
    const s = hostSwitch({ label: "Iniciar minimizado" });
    compare(child(s, "switch-track").border.width, 2);
    sameColor(child(s, "switch-track").border.color, contract.textSecondaryColor, "trilho desligado");
    sameColor(child(s, "switch-thumb").color, contract.textSecondaryColor, "polegar desligado");
    s.forceActiveFocus(Qt.TabFocusReason);
    keyClick(Qt.Key_Space);
    verify(s.checked); compare(s.Accessible.name, "Iniciar minimizado");
    sameColor(child(s, "switch-track").color, contract.accentColor, "trilho ligado");
    sameColor(child(s, "switch-thumb").color, contract.accentTextColor, "polegar ligado");
    verify(s.height >= 40);
}
function test_switchMotionFollowsContract() {
    driver.setSystemAppearance(false, true);
    const s = hostSwitch({ label: "Restaurar última sessão" });
    s.toggle();
    compare(child(s, "switch-thumb").x, child(s, "switch-thumb").checkedX);   // sem animação
}
function test_stepperStepsAndEdits() {
    const st = hostStepper({ label: "Segundos para avançar", from: 1, to: 60, value: 5, suffix: "s" });
    compare(st.displayText, "5 s");
    st.forceActiveFocus(Qt.TabFocusReason);
    keyClick(Qt.Key_Up); compare(st.value, 6);
    st.contentItem.selectAll(); keyClick(Qt.Key_1); keyClick(Qt.Key_2); keyClick(Qt.Key_Return);
    compare(st.value, 12);
    st.value = 60; keyClick(Qt.Key_Up); compare(st.value, 60);
    verify(st.up.indicator.width >= 40 && st.down.indicator.height >= 40);
}
function test_checkBoxStates() {
    const c = hostCheck({ text: "Avisar ao fechar para a bandeja nesta sessão" });
    sameColor(c.indicator.border.color, contract.textSecondaryColor, "desmarcada");
    c.forceActiveFocus(Qt.TabFocusReason); keyClick(Qt.Key_Space);
    verify(c.checked);
    sameColor(c.indicator.color, contract.accentColor, "marcada");
    verify(c.height >= 40);
}
```

`checkedX` é uma propriedade de leitura do polegar (a posição final com o interruptor ligado), exposta para este teste. Com movimento reduzido, a posição precisa chegar a ela já no mesmo frame.

- [ ] **Passo 2: Rodar e ver falhar.** Esperado: tipos inexistentes.
- [ ] **Passo 3: Implementar as quatro peças.**
- [ ] **Passo 4: Rodar e ver passar** o arquivo, o contrato e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria chave segmentada, interruptor, “− valor +” e caixinha Aurora`.

### Tarefa 7: Selo e avatar redondo

**Arquivos:**
- Criar: `src/ui/qml/AuroraChip.qml` e `src/ui/qml/AuroraAvatar.qml`, registrados no CMake
- Teste: `tests/ui/quick/tst_aurora_badges.qml`

**Interfaces:**
- `AuroraChip` (Control, não focável):
  - `enum Tone { Neutral, Accent, Warning, Success, Error }`;
  - `required contract`, `text`, `property string iconName`, `property int tone: AuroraChip.Neutral`;
  - altura 24, raio 12, fundo `selectionColor`;
  - cor do texto e do ícone por tom: `textPrimaryColor`, `accentColor`, `warningColor`, `successColor` ou `errorColor`;
  - `Accessible.role: Accessible.StaticText`, `Accessible.name: text`;
  - em alto contraste: fundo `canvasColor` com contorno de 1 px em `textPrimaryColor`;
  - filhos: `chip-background` e `chip-label`.
- `AuroraAvatar` (Item, `Accessible.ignored: true`):
  - `required contract`, `property url source`, `property int size: 40`, `property color backgroundColor: contract.surfaceColor`, `property bool selected`.
  - O recorte redondo é feito sem efeitos: um Item com `clip: true` do tamanho do avatar contém a imagem e uma máscara de cantos.
    - A máscara (`objectName: "avatar-corner-cover"`) é um `Rectangle` transparente com `border.width = b`, `border.color = backgroundColor`, x e y em `-b`, lado `size + 2b` e `radius = size/2 + b`, com `b = Math.ceil(size / 4)`.
  - Com `selected`: anel de 2 px em `accentColor`, 2 px para fora (`avatar-selection-ring`).

- [ ] **Passo 1: Escrever os testes que falham.**

```qml
function test_chipTones_data() {
    return [["Neutral", AuroraChip.Neutral, "textPrimaryColor"], ["Accent", AuroraChip.Accent, "accentColor"],
            ["Warning", AuroraChip.Warning, "warningColor"], ["Success", AuroraChip.Success, "successColor"],
            ["Error", AuroraChip.Error, "errorColor"]].map(r => ({ tag: r[0], tone: r[1], fg: r[2] }));
}
function test_chipTones(data) {
    const c = hostChip({ text: "Sincronizada", tone: data.tone });
    compare(c.height, 24);
    sameColor(child(c, "chip-background").color, contract.selectionColor, "fundo");
    sameColor(child(c, "chip-label").color, contract[data.fg], "texto");
    compare(c.Accessible.name, "Sincronizada");
}
function test_avatarIsRound() {
    const a = hostAvatar({ source: "qrc:/rc_music/avatars/avatar-01.svg", size: 64 });   // janela na cor surfaceColor
    verify(waitForRendering(a));
    const image = grabImage(a);
    const corner = image.pixel(1, 1);
    sameColor(corner, contract.surfaceColor, "canto coberto");
    verify(!Qt.colorEqual(image.pixel(32, 32), contract.surfaceColor), "centro mostra a imagem");
}
function test_avatarSelectionRing() {
    const a = hostAvatar({ source: "qrc:/rc_music/avatars/avatar-02.svg", size: 56, selected: true });
    const ring = child(a, "avatar-selection-ring");
    verify(ring.visible); compare(ring.border.width, 2);
    sameColor(ring.border.color, contract.accentColor, "anel");
}
```

- [ ] **Passo 2: Rodar e ver falhar.**
- [ ] **Passo 3: Implementar.** Se o pixel do centro de `avatar-01.svg` coincidir com `surfaceColor`, use outro avatar do mesmo diretório. Não afrouxe a comparação.
- [ ] **Passo 4: Rodar e ver passar** o arquivo, o contrato e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria selo e avatar redondo Aurora`.

### Tarefa 8: Sombra e menu

**Arquivos:**
- Criar: `src/ui/qml/AuroraShadow.qml`, `AuroraMenu.qml`, `AuroraMenuItem.qml` e `AuroraMenuSeparator.qml`, registrados no CMake
- Modificar: `src/ui/qml/AuroraSelect.qml` (o popup ganha `AuroraShadow`)
- Teste: `tests/ui/quick/tst_aurora_menus.qml`

**Interfaces:**
- `AuroraShadow` (Item, `objectName: "surface-shadow"`):
  - `required contract`, `property real radius`;
  - três `Rectangle` atrás da superfície, com 2, 6 e 12 px de espalhamento, deslocados 1, 3 e 6 px para baixo;
  - cor `contract.shadowColor` com opacidades 1, 0,6 e 0,3;
  - `visible: !contract.highContrast`.
- `AuroraMenu` (Menu):
  - `required contract`;
  - fundo `surfaceRaisedColor`, raio `radiusMedium` e `AuroraShadow`, com 6 px de padding;
  - entra com fade de `contract.panelTransitionMs`;
  - `delegate` cria `AuroraMenuItem { contract: menu.contract }`, inclusive para submenus.
- `AuroraMenuItem` (MenuItem):
  - `required contract`, `property string iconName`, `property string shortcutText`, `property bool danger`;
  - altura 40;
  - texto e ícone em `textPrimaryColor`, ou `errorColor` com `danger`;
  - `highlighted` em `hoverColor`; em alto contraste, `accentColor` com texto `accentTextColor`;
  - `shortcutText` à direita, em `textSecondaryColor`, ou `textPrimaryColor` quando `highlighted`;
  - submenu indicado pelo ícone `caret-right`;
  - filhos: `menu-item-background`, `menu-item-label` e `menu-item-shortcut`.
- `AuroraMenuSeparator` (MenuSeparator): `required contract`, linha de 1 px em `dividerColor` com 6 px de margem vertical.

- [ ] **Passo 1: Escrever os testes que falham.**

```qml
function test_menuSurface_data() { return themes(); }
function test_menuSurface(data) {
    driver.setSystemAppearance(data.dark, false);
    const m = openMenu();               // três itens; o último com danger: true, antes dele um separador
    sameColor(m.background.color, contract.surfaceRaisedColor, "fundo");
    compare(m.background.radius, contract.radiusMedium);
    verify(child(m.background, "surface-shadow").visible);
}
function test_itemsAre40AndNavigable() {
    const m = openMenu();
    const spy = createTemporaryObject(spyComponent, testCase, { target: m.itemAt(0), signalName: "triggered" });
    compare(m.itemAt(0).height, 40);
    keyClick(Qt.Key_Down); keyClick(Qt.Key_Return);
    compare(spy.count, 1);
}
function test_dangerItemUsesError() {
    const m = openMenu();
    sameColor(child(m.itemAt(3), "menu-item-label").color, contract.errorColor, "perigo");
}
function test_shortcutTextStaysReadable() {
    const m = openMenu();               // o item 1 tem shortcutText "Alt+↑"
    sameColor(child(m.itemAt(1), "menu-item-shortcut").color, contract.textSecondaryColor, "repouso");
    m.currentIndex = 1;
    sameColor(child(m.itemAt(1), "menu-item-shortcut").color, contract.textPrimaryColor, "destacado");
}
function test_noShadowInHighContrast() {
    driver.setHighContrast(true, true);
    const m = openMenu();
    verify(!child(m.background, "surface-shadow").visible);
}
function test_escReturnsFocusToOpener() {
    const h = hostMenuWithOpener();     // um AuroraButton que abre o menu em onClicked
    h.opener.forceActiveFocus(Qt.TabFocusReason);
    keyClick(Qt.Key_Return);
    tryVerify(() => h.menu.opened);
    keyClick(Qt.Key_Escape);
    tryVerify(() => !h.menu.visible);
    verify(h.opener.activeFocus);
}
```

- [ ] **Passo 2: Rodar e ver falhar.**
- [ ] **Passo 3: Implementar.** Se o Menu do Qt não devolver o foco sozinho, guarde `Window.activeFocusItem` em `aboutToShow` e devolva-o em `closed`.
- [ ] **Passo 4: Rodar e ver passar** `tst_aurora_menus.qml` e `tst_aurora_fields.qml`, já que o popup da caixa de seleção mudou, mais o contrato e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria menu Aurora com sombra e itens de perigo`.

### Tarefa 9: Diálogo e confirmação

**Arquivos:**
- Criar: `src/ui/qml/AuroraDialog.qml` e `src/ui/qml/AuroraConfirmDialog.qml`, registrados no CMake
- Teste: `tests/ui/quick/tst_aurora_dialogs.qml`

**Interfaces:**
- `AuroraDialog` (Dialog):
  - `required contract`, `required controlId`, `property bool closable: true`, `property int preferredWidth: 480`, `property alias actions` (o `data` da linha de botões do rodapé), mais o `title` herdado;
  - `parent: Overlay.overlay`, centralizado, `modal: true` e `closePolicy: Popup.CloseOnEscape`;
  - largura `Math.min(preferredWidth, parent.width - 48)`; altura máxima `parent.height - 48`, com o conteúdo num `ScrollView`;
  - `Overlay.modal` é um `Rectangle` com `objectName: "dialog-scrim"` e cor `contract.scrimColor`;
  - fundo `surfaceRaisedColor`, raio `radiusLarge` e `AuroraShadow`;
  - título com `headingFontFamily`, tamanho `typeSection`, em `textPrimaryColor` (`objectName: "dialog-title"`);
  - com `closable`: `AuroraIconButton` com `iconName: "close"` e o texto `controlsText.close`, `objectName: controlId + "-close"`;
  - rodapé com divisória de 1 px e botões alinhados à direita, com 8 px entre eles;
  - fade de entrada e saída com `contract.panelTransitionMs`;
  - em `aboutToShow`, guarda `Window.activeFocusItem`; em `closed`, devolve o foco a ele.
- `AuroraConfirmDialog` (AuroraDialog com `closable: false`):
  - `required property string question` (o título), `required property string consequence` (o corpo), `required property string confirmText`, `property bool destructive`;
  - `signal confirmed()`;
  - botões: `AuroraButton` Quiet com `controlsText.cancel` (`controlId + "-cancel"`) e o de confirmar (`controlId + "-confirm"`), em `DangerFilled` se `destructive` e em `Primary` nos demais casos;
  - ao abrir, o foco fica em Cancelar;
  - confirmar emite `confirmed()` e fecha; Cancelar e Esc só fecham.

- [ ] **Passo 1: Escrever os testes que falham.**

```qml
function test_scrimCoversWholeWindow() {
    const h = openDialog();                     // janela 960×640
    const scrim = child(h.window.Overlay.overlay, "dialog-scrim");
    compare(scrim.width, h.window.width); compare(scrim.height, h.window.height);
    sameColor(scrim.color, contract.scrimColor, "véu");
}
function test_escClosesAndRestoresFocus() {
    const h = openDialogFromButton();
    keyClick(Qt.Key_Escape);
    tryVerify(() => !h.dialog.visible);
    verify(h.opener.activeFocus);
}
function test_closeButtonIsNamed() {
    const h = openDialog({ controlId: "track-details" });
    compare(child(h.dialog.header, "track-details-close").Accessible.name, "Fechar");
}
function test_confirmOpensOnCancelAndConfirms() {
    const h = openConfirm({ controlId: "kick", question: "Expulsar Ana?", consequence: "Ana sai da sala agora.", confirmText: "Expulsar", destructive: true });
    verify(child(h.dialog, "kick-cancel").activeFocus);
    compare(child(h.dialog, "kick-confirm").kind, AuroraButton.DangerFilled);
    const spy = createTemporaryObject(spyComponent, testCase, { target: h.dialog, signalName: "confirmed" });
    keyClick(Qt.Key_Tab); keyClick(Qt.Key_Return);
    compare(spy.count, 1);
    tryVerify(() => !h.dialog.visible);
}
function test_titleUsesHeadingFont() {
    const h = openDialog();
    compare(child(h.dialog.header, "dialog-title").font.family, contract.headingFontFamily);
}
function test_themeChangeWhileOpen() {
    const h = openDialog();
    driver.setSystemAppearance(true, false);
    sameColor(h.dialog.background.color, contract.surfaceRaisedColor, "fundo depois da troca");
}
function test_dialogMotionFollowsContract() {
    driver.setSystemAppearance(false, true);
    const h = openDialog();
    compare(h.dialog.enter.animations[0].duration, 0);
}
function test_dialogFitsMinimumWindow() {
    const h = openDialog({ windowWidth: 720, windowHeight: 520, preferredWidth: 800, tallContent: true });
    verify(h.dialog.width <= 720 - 48);
    verify(h.dialog.height <= 520 - 48);
}
```

- [ ] **Passo 2: Rodar e ver falhar.**
- [ ] **Passo 3: Implementar.**
- [ ] **Passo 4: Rodar e ver passar** o arquivo, o contrato e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria diálogo e confirmação Aurora com véu na janela inteira`.

### Tarefa 10: Cabeçalho de gaveta, avisos e estado vazio

**Arquivos:**
- Criar: `src/ui/qml/AuroraDrawerHeader.qml`, `AuroraNotice.qml`, `AuroraFloatingCard.qml`, `AuroraFeedback.qml` e `AuroraEmptyState.qml`, registrados no CMake
- Teste: `tests/ui/quick/tst_aurora_surfaces.qml`

**Interfaces:**
- `AuroraDrawerHeader` (Item):
  - `required contract`, `required controlId`, `required property string closeText`, `property string eyebrow`, `property string title`, `property string subtitle`, `signal closeRequested()`;
  - rótulo em caixa alta, `typeEyebrow`, `textSecondaryColor`; título com serifa em `typeTitle`; subtítulo em `textSecondaryColor`;
  - à direita, `AuroraIconButton` `close` com `text: closeText` e `objectName: controlId + "-close"`.
- `AuroraNotice` (Control):
  - `enum Tone { Warning, Error, Info }`;
  - `required contract`, `required controlId`, `property int tone`, `property string title`, `property string text`, `property string primaryActionText`, `property string secondaryActionText`;
  - `signal primaryTriggered()`, `signal secondaryTriggered()`;
  - fundo `selectionColor`, raio `radiusLarge`, sem contorno;
  - ícone por tom (`notice-icon`): `warning` em `warningColor`, `warning-circle` em `errorColor`, `info` em `accentColor`;
  - título semibold e texto em `textPrimaryColor` (fundo Seleção: nada de texto secundário);
  - ação primária em `AuroraButton` Primary e secundária em Tonal; cada uma só aparece com texto;
  - `Accessible.role: Accessible.AlertMessage`, com `Accessible.name` igual a título e texto;
  - em alto contraste: fundo `canvasColor` com contorno de 2 px.
- `AuroraFloatingCard` (Control):
  - `required contract`, `default property alias content`;
  - fundo `surfaceRaisedColor`, raio `radiusLarge`, `AuroraShadow` e padding de 16.
- `AuroraFeedback` (Row):
  - `enum Tone { Success, Warning, Error }`;
  - `required contract`, `property string text`, `property int tone: AuroraFeedback.Success`;
  - ícone (`feedback-icon`) `check-circle`, `warning` ou `warning-circle`, de 16 px, mais o texto, os dois na cor do tom;
  - `visible: text.length > 0`; `Accessible.role: Accessible.StaticText`.
- `AuroraEmptyState` (ColumnLayout):
  - `required contract`, `required controlId`, `property string iconName`, `property string title`, `property string hint`, `property string primaryActionText`, `property string secondaryActionText`;
  - `signal primaryTriggered()`, `signal secondaryTriggered()`;
  - ícone de 28 px em `accentColor` num círculo de 64 px em `selectionColor`;
  - título com serifa em `typeTitle`; dica em `textSecondaryColor` sobre o fundo de quem usa;
  - ações em Primary e Tonal.

- [ ] **Passo 1: Escrever os testes que falham.**

```qml
function test_drawerHeaderCloses() {
    const d = hostDrawerHeader({ eyebrow: "Reprodução", title: "Fila", subtitle: "12 faixas · 48 min", closeText: "Fechar fila" });
    const spy = createTemporaryObject(spyComponent, testCase, { target: d, signalName: "closeRequested" });
    const close = child(d, "queue-drawer-close");
    compare(close.Accessible.name, "Fechar fila");
    mouseClick(close); compare(spy.count, 1);
}
function test_noticeTones_data() {
    return [["Warning", AuroraNotice.Warning, "warning", "warningColor"],
            ["Error", AuroraNotice.Error, "warning-circle", "errorColor"],
            ["Info", AuroraNotice.Info, "info", "accentColor"]].map(r => ({ tag: r[0], tone: r[1], icon: r[2], color: r[3] }));
}
function test_noticeTones(data) {
    const n = hostNotice({ tone: data.tone, title: "Configurações recuperadas", text: "Algumas opções voltaram ao padrão.", primaryActionText: "Abrir configurações" });
    compare(child(n, "notice-icon").iconName, data.icon);
    sameColor(child(n, "notice-icon").color, contract[data.color], "ícone");
    sameColor(n.background.color, contract.selectionColor, "fundo");
    sameColor(child(n, "notice-text").color, contract.textPrimaryColor, "texto");
    compare(n.Accessible.role, Accessible.AlertMessage);
    verify(!child(n, "settings-notice-secondary").visible);
}
function test_floatingCardShadowFollowsContrast() {
    const c = hostFloatingCard();
    verify(child(c, "surface-shadow").visible);
    driver.setHighContrast(true, true);
    verify(!child(c, "surface-shadow").visible);
}
function test_feedbackHidesWhenEmpty() {
    const f = hostFeedback({ text: "" });
    verify(!f.visible);
    f.text = "Caminho copiado.";
    verify(f.visible);
    sameColor(child(f, "feedback-icon").color, contract.successColor, "ícone");
}
function test_emptyStateActions() {
    const e = hostEmpty({ iconName: "queue", title: "A fila está vazia", hint: "Adicione arquivos para começar.", primaryActionText: "Adicionar arquivos" });
    const spy = createTemporaryObject(spyComponent, testCase, { target: e, signalName: "primaryTriggered" });
    mouseClick(child(e, "queue-empty-primary")); compare(spy.count, 1);
    verify(!child(e, "queue-empty-secondary").visible);
}
```

Os `objectName` das ações seguem `controlId + "-primary"` e `controlId + "-secondary"`. O texto do aviso é `notice-text`.

- [ ] **Passo 2: Rodar e ver falhar.**
- [ ] **Passo 3: Implementar.**
- [ ] **Passo 4: Rodar e ver passar** o arquivo, o contrato e o qmllint.
- [ ] **Passo 5: Commit**: `feat: cria cabeçalho de gaveta, avisos e estado vazio Aurora`.

### Tarefa 11: Contraste das peças, galeria de capturas e marco da suíte

**Arquivos:**
- Criar: `tests/ui/quick/tst_aurora_pieces_visual.qml`
- Modificar: `docs/ui-tests.md`, com uma seção curta sobre os testes `tst_aurora_*`

**Interfaces:**
- Consome todas as peças das tarefas 3 a 10, sem API nova.

- [ ] **Passo 1: Escrever o teste de contraste.** Ele mede os pares que as peças usam, a partir do contrato, nos dois temas, com a mesma função de luminância de `tst_color_contrast.qml`.

```qml
function test_piecePairsInBothThemes_data() { return themes(); }
function test_piecePairsInBothThemes(data) {
    driver.setSystemAppearance(data.dark, false);
    const pairs = [
        ["accentTextColor", "accentColor", 4.5], ["textPrimaryColor", "selectionColor", 4.5],
        ["errorColor", "selectionColor", 4.5], ["accentTextColor", "errorColor", 4.5],
        ["accentColor", "selectionColor", 4.5], ["accentColor", "surfaceColor", 4.5],
        ["textPrimaryColor", "hoverColor", 4.5], ["textPrimaryColor", "surfaceRaisedColor", 4.5],
        ["textSecondaryColor", "surfaceRaisedColor", 4.5], ["textSecondaryColor", "surfaceColor", 4.5],
        ["warningColor", "selectionColor", 4.5], ["successColor", "selectionColor", 4.5],
        ["errorColor", "surfaceRaisedColor", 4.5], ["accentColor", "surfaceRaisedColor", 4.5],
        ["textSecondaryColor", "canvasColor", 3], ["accentColor", "canvasColor", 3],
        ["focusColor", "selectionColor", 3], ["focusColor", "canvasColor", 3]];
    for (const p of pairs)
        verifyContrast(Qt.color(contract[p[0]]), Qt.color(contract[p[1]]), p[2], p[0] + " sobre " + p[1]);
}
```

- [ ] **Passo 2: Escrever o teste da galeria.**
  - `test_galleryRenders_data` tem as linhas `claro`, `escuro` e `alto-contraste`.
  - A galeria é um `Flow` com cada peça nos estados principais:
    - os cinco botões, um desabilitado e um compacto;
    - botão de ícone ligado e desligado;
    - campo vazio, com foco e com erro;
    - caixa de seleção;
    - segmentado;
    - interruptor ligado e desligado;
    - “− valor +”;
    - caixinha marcada;
    - os cinco selos;
    - avatar normal e escolhido;
    - os três avisos;
    - mensagem de retorno;
    - estado vazio;
    - cabeçalho de gaveta;
    - cartão flutuante.

    Tudo fica dentro de `ColumnLayout` e `RowLayout` sem tamanho explícito, para que qualquer aviso de layout derrube o teste.
  - O teste espera `waitForRendering` e, se `driver.artifactPath("aurora-pecas-" + data.tag + ".png")` não estiver vazio, salva `grabImage(galeria)` nesse caminho.
- [ ] **Passo 3: Rodar o arquivo.** Esperado: `0 failed`. Se um par falhar, troque o token que a peça usa nesse par, como manda o spec. Nunca baixe o mínimo.
- [ ] **Passo 4: Rodar o marco da suíte** (fim das peças, conforme o spec):
  - `pwsh -NoProfile -File scripts/develop.ps1 validate -ToolchainFile tools/windows.example.psd1 -BuildDirectory out/product/ui-redesign-tests`;
  - esperado: todas as etapas com código 0;
  - registre separadamente o que passou, o que falhou e o que ficou pendente (como `LocalRuntime.Tests.ps1`, que precisa de runtime montado).
- [ ] **Passo 5: Gerar as capturas para o aceite visual.**
  - Rode `tst_aurora_pieces_visual.qml` com `$env:RC_MUSIC_UI_ARTIFACT_DIR` apontando para `out/product/ui-redesign-captures/pecas`.
  - Entregue ao mantenedor as três imagens `aurora-pecas-*.png`. Elas são evidência para o aceite dele, não o substituem.
- [ ] **Passo 6: Documentar.** Em `docs/ui-tests.md`, acrescente uma seção “Peças Aurora” com os arquivos `tst_aurora_*`, o que cada um cobre e as contagens do passo 4.
- [ ] **Passo 7: Commit**: `test: confere contraste e galeria das peças Aurora`.

## Próximos planos

Cada plano é escrito quando o anterior fecha, para usar as interfaces reais das peças. Ordem e escopo seguem a seção “Ordem das fatias” do spec.

| Plano | Fatias do spec | Entrega |
|---|---|---|
| 2. Casca e player | 2 | lateral sem Fila e com Navegar e perfil, topo com tema, player C |
| 3. Linha do verso e gavetas | 3 | linha do verso, gaveta da Fila e gaveta da Letra com o cabeçalho desta fatia |
| 4. Início, Biblioteca e menu da faixa | 4, 5, 6 | linha e cartão de faixa, menu da faixa, Início, Biblioteca, caso de uso “Tocar agora” |
| 5. Buscar e Playlists | 7, 8 | Buscar, Playlists, “Tocar agora” de playlist e de faixa de playlist |
| 6. Atividade | 9 | cartões de download, aviso único, filtros |
| 7. Ouvir junto | 10 | cartões da sala, diálogo de cargos, confirmações |
| 8. Configurações, perfil, detalhes, letras e avisos | 11, 12, 13 | as telas restantes e a bandeja |
| 9. Bordas e fechamento | 14 | 720×520, alto contraste, escala, limpeza do que morreu, guia do usuário e status |

Os textos revisados de cada área entram no plano da área, junto com os testes que os fixam.
