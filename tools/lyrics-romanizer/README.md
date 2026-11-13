# Romanização local de letras

O RC Music envia as linhas japonesas para um processo Python local e conserva os tempos no catálogo do player. O conversor pykakasi usa a forma Hepburn. A leitura dos kanji segue o dicionário do conversor e pode precisar de correção na revisão da letra.

## Preparação no Windows

Use uma instalação x64 de CPython 3.12, 3.13 ou 3.14. Execute o script abaixo a partir do checkout e use a pasta de configuração do RC Music no destino.

```powershell
pwsh -NoProfile -File tools/lyrics-romanizer/Prepare-RcMusicLyricsRomanizer.ps1 `
    -PythonExecutable 'C:\Python313\python.exe' `
    -OutputDirectory (Join-Path $env:LOCALAPPDATA 'MusicPlayer/MusicPlayer/lyrics-romanizer')
```

O script cria um venv no destino indicado e baixa os wheels fixados por SHA-256 em `requirements.lock`. A instalação de Python usada para criar o ambiente precisa continuar disponível no mesmo caminho. O venv depende do caminho em que foi criado. O diretório de saída precisa ser novo.

A preparação usa a rede para obter os wheels. A conversão de letras executa somente o Python do venv e o script locais, com dados passados por stdin e stdout.

## Protocolo

Uma chamada recebe um objeto JSON UTF-8 com o campo `lines`. As linhas são strings sem quebras de linha ou caracteres de controle, com tabulações permitidas. O processo retorna um único objeto `{"lines":[...]}` com a mesma quantidade e ordem de linhas. Em falha, o código de saída é 1 e o stdout permanece vazio. A requisição e a resposta aceitam até 8192 linhas e 256 KiB de texto UTF-8. Cada envelope JSON pode ocupar até 2 MiB.

Para testar no ambiente preparado, execute:

```powershell
& '<destino>/Scripts/python.exe' -I -B tools/lyrics-romanizer/test_romanize.py
```

## Origem e licenças

O lock usa os wheels oficiais [pykakasi 2.3.0](https://pypi.org/project/pykakasi/2.3.0/), [jaconv 0.5.0](https://pypi.org/project/jaconv/0.5.0/), [Deprecated 3.0.1](https://pypi.org/project/Deprecated/3.0.1/) e [wrapt 2.5.2](https://pypi.org/project/wrapt/2.5.2/). As licenças são mantidas nos metadados e arquivos de licença instalados em `Lib/site-packages`. O pykakasi usa GPL-3.0-or-later. O preparo local instala os wheels separadamente. A redistribuição do ambiente exige revisar as licenças e fornecer as fontes correspondentes do componente GPL. [Código e licença do pykakasi](https://github.com/miurahr/pykakasi)
