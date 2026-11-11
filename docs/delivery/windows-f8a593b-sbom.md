# Inventário de componentes dos setups Windows de f8a593b

Os dois SBOMs descrevem os instaladores privados de `f8a593b`, compilados pela receita `8785938`. Cada documento vincula o SHA-256 do setup ao manifesto do runtime, ao recibo do instalador e aos arquivos de payload previstos após a instalação. [SBOM Lite](rc-music-f8a593b-lite.cdx.json) · [SBOM Completa experimental](rc-music-f8a593b-complete-experimental.cdx.json) · [Setups verificados](../windows-installer.md#setups-de-f8a593b)

## Combinações do instalador

O campo `rcmusic:installer:required-mask` identifica a seleção necessária para instalar os arquivos de cada componente. O arquivo aparece quando todos os bits requeridos estão presentes na seleção. As contagens incluem o marcador `download-tools/installed-v1` a partir da máscara `1`. Os dois hosts e seus manifestos são copiados junto com as ferramentas. Cada extensão registra o próprio canal quando escolhida.

| Máscara | Opções selecionadas | Lite, arquivos / bytes | Completa, arquivos / bytes |
| ---: | --- | ---: | ---: |
| 0 | Nenhuma | 554 / 98.426.487 | 557 / 98.648.206 |
| 1 | Ferramentas | 604 / 232.216.822 | 607 / 232.438.541 |
| 3 | Ferramentas e extensão principal | 613 / 232.266.577 | 616 / 232.488.296 |
| 5 | Ferramentas e extensão YouTube | 611 / 232.240.821 | 614 / 232.462.540 |
| 7 | Ferramentas e ambas as extensões | 620 / 232.290.576 | 623 / 232.512.295 |

O motor NSIS é registrado separadamente por estar embutido no setup. A inspeção estática dos dois setups conferiu os bytes de `System.dll`, `nsDialogs.dll` e `modern-wizard.bmp` com a toolchain NSIS 3.12. O desinstalador e o Registro gerados em execução pertencem à aceitação instalada. Windows x64 e o Microsoft Visual C++ Redistributable compatível são pré-requisitos externos. O loader do WebView2 está ligado estaticamente à Completa, que inclui seus três avisos e usa o Evergreen x64 já disponível na máquina. As ferramentas opcionais trazem cópias privadas de CRT para o worker de rede e o resolvedor.

## Proveniência e avisos de licença

- O caminho, o tamanho e o SHA-256 de **553 arquivos** coincidem com os do inventário anterior de `3f22fda`. Isso vincula a atribuição técnica anterior a esses arquivos nas duas edições. O player atual tem outro hash, e as ressalvas sobre o Qtbase reconstruído, os SPDX do fornecedor e a cobertura dos avisos permanecem. [Inventário e notices anteriores](runtime-3f22fda-notices.md)
- A revisão 2 dos SBOMs vincula as cinco DLLs do FFmpeg **7.1.5** ao arquivo oficial Qt Multimedia, à receita e à fonte fixada. Registra a atribuição **7.1.3** do SPDX do fornecedor e inclui **zlib 1.3.1** incorporado em `avcodec-61.dll` e `avformat-61.dll`. Os avisos ADX e VobSub permanecem abertos para correção. [Proveniência e avisos do FFmpeg](windows-f8a593b-ffmpeg.md)
- Os arquivos opcionais do resolvedor conferem com o recibo de produção, incluindo CPython **3.13.1**, Node.js **24.20.0**, yt-dlp **2026.3.17** e EJS **0.8.0**. A inspeção estática do arquivo embutido do resolvedor confirmou módulos yt-dlp/EJS e o bootstrap do PyInstaller, cuja receita fixa **6.22.3**. Os avisos de Python, Node, yt-dlp, EJS e PyInstaller têm caminho e SHA-256 no SBOM. [Receita do resolvedor](../youtube-audio-and-lyrics.md#runtime-real-no-ensaio-offline)
- Os arquivos dos workers de download e conversão estão autenticados por hash no recibo de ferramentas. A revisão exata de suas fontes e a versão precisa de `libffi-8.dll` ainda precisam de evidência. O nome da DLL identifica a ABI 8. Os metadados PE identificam OpenSSL **3.0.15** e SQLite **3.45.3.0** no resolvedor. [Perfil de ferramentas](../packaged-download-profile.md)

## Verificação e pendências

Os inventários físicos dos runtimes, os arquivos das extensões e o marcador gerado passaram na conferência de tamanho e SHA-256. A auditoria também conferiu os hashes dos setups, reconstruiu a seleção de arquivos e os totais das cinco máscaras a partir dos recibos e rejeitou um hash adulterado em memória. Os JSONs passaram no schema oficial do CycloneDX 1.6. Os logs e scripts locais ficam em `out/product/test-runs/windows-bom-f8a593b-20261111/`. [Especificação CycloneDX 1.6](https://cyclonedx.org/docs/1.6/json/)

A revisão 2 passou novamente na conferência de arquivos, nos totais das cinco seleções e no schema. Os novos recibos ficam em `out/product/test-runs/windows-ffmpeg-f8a593b-20261111/`. [Relatório de proveniência](windows-f8a593b-ffmpeg.md)

Os SBOMs registram imports PE observados no runtime base, o zlib incorporado ao FFmpeg e os pré-requisitos declarados. A composição é marcada como incompleta porque há dependências transitivas e fontes correspondentes a confirmar. A revisão dos demais avisos e da aplicabilidade, a revisão final de licenças, os notices completos e a aceitação instalada continuam pendentes em M5.7 e nos gates de distribuição. [Processo de release](release-process.md)
