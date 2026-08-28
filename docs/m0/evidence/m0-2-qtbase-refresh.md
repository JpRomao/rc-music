# M0.2 — Rebuild autenticado e isolado do Qtbase

**Data:** 2026-08-28

**Estado:** Qtbase 6.11.2 reconstruído e autenticado com o backport FreeType `5a280ec`; SDK composto, shell/pacote e validações nativas ainda pendentes; `CHK-SEC` e M0.2 permanecem abertos

## Resultado

O launcher de produção commitado em `fff4ceaa4cfb6cec687596c1babe3f68d6136b95` executou uma única vez a receita v2 a partir de worktree limpo e publicou o `RunRoot` privado `q/96bvah3m0t0t71m990olv5gpvs`. A execução configurou, compilou e instalou o Qtbase 6.11.2 com o backport upstream `5a280ecde6f324de0d226261036e736e0cb49a71`, sem modificar o SDK oficial, o pacote `msvc-63717e5-clean-01` ou qualquer evidência histórica.

| Âncora | Bytes | SHA-256 |
|---|---:|---|
| Contrato v1 histórico | 15.475 | `2023dd7621015906d86d5956474e828f4e1ef74bedda52907c5e827aa378c2c6` |
| Contrato v2 de produção | 20.464 | `fd2cc91ae7824c117a93f69e78d792c500f30fd2d112c3ecb2f0404de3b0d818` |
| `experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefresh.psm1` | 150.065 | `95245cb3cf812e0174840a73b94eb86c8217b4ee90131cbbae92f45469d54f0d` |
| `experiments/m0/provisioning/qtbase-refresh/M0.QtBaseRefreshNative.psm1` | 121.297 | `0c8aafc9d24bd95b2a82bff74311450c10e459306e8a8dc94646b346a844f85d` |
| `experiments/m0/provisioning/qtbase-refresh/Invoke-M0QtBaseRefresh.ps1` | 11.950 | `c7e1c66a3db82c6937f28c4600f4d9a4b9529cde8ec7fc73712d6ec4a75875b3` |
| Manifesto v2 | 13.866 | `dc26c0962e927145466b879e352a4c99d9fc7da54d335567373c353739afbf6b` |
| Receipt v2 | 4.369 | `8a4e3c13b2cbf1103ef2a7c417160eddc17db4fdc00dcea505e249eccab28619` |

O manifesto foi capturado em `2026-08-28T03:14:51.9355382-03:00`; o receipt, após as revalidações físicas finais, em `2026-08-28T06:34:36.7811447+00:00`. O launcher capturou externamente o hash do receipt acima. Em um segundo processo PowerShell, iniciado depois do encerramento do runner, o verificador público recebeu explicitamente o path e o hash externo do receipt, o contrato v2 e sua identidade, o commit esperado e a raiz do repositório. Ele aprovou `schema_version=2`, `receipt_id=m0.2-qtbase-freetype-backport-v2-receipt`, `execution_exit_code=0`, o manifesto vinculado e todas as evidências físicas.

## Red, Green e falha fechada

A primeira execução real, `q/hla2221pa11rncc9e2fhsta6r4`, configurou, compilou e instalou, mas falhou fechada no relatório de exports: o `dumpbin` real acrescenta à entrada decorada de `QColor::QColor(int,int,int,int)` um alias e a forma demangled. Nenhum manifesto ou receipt foi publicado. A auditoria encontrou somente seis logs, sete relatórios e as árvores de trabalho; o inventário de instalação tinha 4.289 folhas, 526.194.008 bytes e SHA-256 `ab8267ff9e52de1dde125f89b0059327cc20978b8369481d39b04d4f7a12f4f5`.

O Red fixou a linha real e produziu **59 passagens/1 falha**. A implementação passou a aceitar somente a forma histórica simples ou o sufixo real exato, mantendo comparação sensível a maiúsculas e rejeitando sete variantes indevidas. O Green final foi **60/60** no módulo central e **25/25** no runner nativo; o parser PowerShell aprovou os três arquivos da receita sem erros. O commit `fff4cea` contém essa correção e a execução autenticada posterior usou exatamente seus bytes.

## Fontes, patch e árvores

A receita preservou a árvore original e aplicou o patch somente à cópia isolada. `src/3rdparty/freetype/src/truetype/ttgxvar.c` mudou de `b5ca576f480437e59f84a31ba2b569c7aadd31871f608034f6ea576340b88e10` para `9126fee413fabeb2f9f78b60548065dda2a8365ef95da43cb2a0edc55813e346`; nenhum outro path de fonte mudou.

| Árvore | Arquivos | Bytes | SHA-256 do inventário |
|---|---:|---:|---|
| Qtbase original | 22.856 | 276.454.784 | `1d994ab02aaa31b0ee3d96143fb3abeef3fc41818968b48f44ac82faf13338a7` |
| Qtbase corrigido | 22.856 | 276.459.640 | `04f8cfd9fc0cbc581be485941d178b91ef0c6f0ffb9f95d1758bc1fb6c5a04d4` |
| Vulkan-Headers | 53 | 12.968.619 | `06d27b1e3d9d14479ec50ea583d9d42d24ad67e86dc7f9ca51c608f18a76d25e` |
| Instalação Qtbase | 4.289 | 526.177.624 | `b489401f06a97de9c8017a5dca6df626a3b12605a4974ff680d5fd25856e9e09` |

O verificador reconstruiu cada inventário, reabriu cada folha por path físico e rejeitaria arquivo ausente/extra, reparse point, ADS ou mais de um link. Sua aprovação independente comprova que as 4.289 folhas autenticadas da instalação estavam materializadas com link físico único no momento da verificação. O scratch de normalização não permaneceu no run.

## Configuração e comandos

O build usou MSVC 19.44.35228.0/toolset 14.44.35207, Windows SDK `10.0.26100.0`, CMake 3.31.5, Ninja 1.12.1 e `Ninja Multi-Config`. A configuração instalada é `RelWithDebInfo`; Qt é compartilhado, Debug+RelWithDebInfo, com FreeType/HarfBuzz/PCRE2/zlib/PNG/JPEG embutidos, OpenGL dinâmico, Vulkan, DirectWrite/Direct2D, acessibilidade, bandeja, Schannel, CET e informação de debug. LTCG, PCH, OpenSSL, PostgreSQL, testes do Qtbase e geração oficial de SBOM ficaram desativados pelo contrato. O build concluiu as **2.581/2.581** ações; o checkpoint crítico observado havia alcançado **1.662/2.581** sem falha.

| Comando | Exit | stdout: bytes / SHA-256 | stderr: bytes / SHA-256 |
|---|---:|---|---|
| `configure` | 0 | 3.564 / `42b90546d54bbe1d467aa49d4d5c2790cd47671e29dcc123a5998629002bc066` | 367 / `7237530595c51b7b2bb3f4cf95bb257c7ecef2c57d1c2b8cca90b31f6f3089c1` |
| `cmake --build` | 0 | 261.518 / `c7a986695ac4b1ecc21e210693d1d2db9cb6608856e43689b7fbe11454a65d7d` | 0 / `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `cmake --install` | 0 | 624.902 / `ccaefd2dd129914e07101f8b4132aff83dc2f862ed25909dc01c8c99fe8ca127` | 0 / `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

## Relatórios autenticados

| Relatório | Bytes | SHA-256 |
|---|---:|---|
| `evidence/reports/configure-summary.txt` | 9.497 | `2b923562c1b0a117424e47043acbf5df7eff6ad4abc016f8b49a0df9e38ad352` |
| `evidence/reports/cmake-cache.txt` | 170.633 | `19e5d91d4acaca5792434e6e5ae781f25af9638cc73f7e5ff6e3a4f44619dcb8` |
| `evidence/reports/qtgui-freetype-dependency.txt` | 100 | `56a2fb87b62d63ea71885c4c4c8d11033138f83d7820fbecd2306ba51e90d7d0` |
| `evidence/reports/qt6gui-headers.txt` | 6.323 | `7668d29aa45a51df0978e1fe082170c28f3039584b7140f9558d4ce0956cee8f` |
| `evidence/reports/qt6gui-imports.txt` | 117.884 | `bd4d273d13004d80d44dde727e823e8ea3751feccc98963c442b7c5330f702e4` |
| `evidence/reports/qt6gui-exports.txt` | 2.356.947 | `9c2ae1ecae2f9236b31b0bf31174875bc2b84671b16478b2626f3faee7070de2` |
| `evidence/reports/qminimal-imports.txt` | 26.590 | `f99a7b957583f5dd78c9bc3d9393d02cd3fb98938960f998a2c58ce1529f5fee` |

Os dois primeiros relatórios são cópias byte a byte dos produtores. O relatório Ninja autentica o vínculo direto de `Qt6Gui.dll` e `qminimal.dll` a `Qt6BundledFreetype.lib`; os quatro relatórios `dumpbin` autenticam os PEs observados sem carregar DLL não confiável.

## PEs autenticados

Os 15 artefatos fixados pelo contrato e o consumidor direto adicional são:

| Papel | Path relativo | Bytes | SHA-256 |
|---|---|---:|---|
| artefato | `install/bin/Qt6Core.dll` | 10.351.104 | `3d472b9249aa3a91dabf453c7e84b2f172d2a26ec4266b0919d6bb985438497a` |
| artefato | `install/bin/Qt6Gui.dll` | 9.534.464 | `2327a7314ab8e2dea6a9f753594e6aa2723dcec7f46763ef030cd1057f550b25` |
| artefato | `install/bin/Qt6Network.dll` | 1.729.024 | `5b98c10688dd00791d43b518404c3c3378f3996a67350516b5a939ecd4e4982a` |
| artefato | `install/bin/Qt6OpenGL.dll` | 1.976.320 | `d780d2404d898bf96171a6b7943fbb68e1ae66c8d58841428c9957b24c5dd8be` |
| artefato | `install/bin/Qt6Widgets.dll` | 6.484.992 | `b7575bc98c91ad4aa372ffc85ccd08ee2fe7a8ed921d9a86937516a1a4033662` |
| artefato | `install/plugins/generic/qtuiotouchplugin.dll` | 89.088 | `cefc94ca5e9066ded571b561b250726da1f910214860a6ea45f9e06d77fa2755` |
| artefato | `install/plugins/imageformats/qgif.dll` | 35.840 | `5ad20084ca63fbb754f650fef427720aeb48ffde0d04df18a8418a6907a93921` |
| artefato | `install/plugins/imageformats/qico.dll` | 35.328 | `381074202e4a84104f3520e0e8bb43031dfae173790c3c4167694a4a7882dadd` |
| artefato | `install/plugins/imageformats/qjpeg.dll` | 579.584 | `aaaa0f615a08cd91340b3573683706f2a8ae997ac296ab9881732e85d534b350` |
| artefato | `install/plugins/networkinformation/qnetworklistmanager.dll` | 67.072 | `53509e37390fb4a5ed0fd7f0e97a5ac3300029e9d996fdec689eb0eb6ec3c703` |
| artefato | `install/plugins/platforms/qoffscreen.dll` | 98.816 | `4bb5a1cac3cff815d448c8e2287cee61d242bf128adb57749e2fbf042a31ea28` |
| artefato | `install/plugins/platforms/qwindows.dll` | 978.944 | `06fa77d9be690bfcf41ab543085702566192580c2c08099bbdea6ae41d1b8eae` |
| artefato | `install/plugins/styles/qmodernwindowsstyle.dll` | 228.352 | `1b9eea8ae975a2a0cc4169a09ad8762c293d717896952c7a7b449d0b7c8134dc` |
| artefato | `install/plugins/tls/qcertonlybackend.dll` | 93.184 | `6593150f03d01ee67b6849231ce2aca005e61e0d554cbab579f4a2e7ad7bfab3` |
| artefato | `install/plugins/tls/qschannelbackend.dll` | 235.520 | `20cf46eade0721b035e3001f99a37af033bd7a92fedf9cd174077923061a7934` |
| consumidor direto | `install/plugins/platforms/qminimal.dll` | 49.664 | `76dcbef83c7c20c534eb05648d2ba937fdb221c66f144b3da398c159b208740d` |

## Limites e sequência

Esta evidência autentica o rebuild **isolado** do Qtbase e o vínculo estático do FreeType corrigido nos alvos observados. Ela não comprova carregabilidade em runtime, equivalência ABI/funcional com o SDK oficial, um SDK Qt completo, integração com QML/Quick/Svg, build do shell, pacote original/relocado, máquina limpa, CRT, PCRE2 corrigido, notices, licenças, SBOM ou redistribuição.

O próximo incremento deve criar uma identidade e um contrato próprios para o SDK composto: a instalação Qtbase autenticada deve fornecer Core/Gui/Widgets/Test, plugins base e ferramentas correspondentes, enquanto QML/Quick/Svg e ferramentas suplementares vêm somente das origens oficiais autenticadas, sem colisão ou fallback. O contrato precisa resolver explicitamente o delta entre o Windows SDK `10.0.26100.0` deste rebuild e o `10.0.22621.0` fixado pela receita histórica do shell antes de gerar e testar um novo pacote.

`CHK-SEC` e M0.2 continuam abertos. A interação desktop nativa permanece pausada; foco, NVDA, alto contraste/DPI reais, bandeja/Explorer e encerramento só serão retomados quando houver interação verificável. Nenhum áudio pessoal foi lido ou usado neste rebuild.

Esta atualização de checkpoint é somente documental, sem comportamento novo ou Red artificial. O validador passou com **41 Markdown, 83 requisitos, 17 checks e 76 etapas**; `git diff --check` também passou.
