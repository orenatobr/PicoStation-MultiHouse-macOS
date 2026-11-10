# PicoStation Multi House V3 para macOS

Port nativo para macOS (Apple Silicon, arm64) do **PicoStation Multi House V3 Manager**, o programa
que monta o cartão SD no layout "V2" do firmware PicoStation Multi House: cases, discos, capas,
cheats GameShark, menu e música, gravados direto no SD.

O original é um programa Windows x64. Este repositório o reimplementa em C11 (núcleo) e
AppKit (interface), a partir de engenharia reversa do executável. Mesmos formatos de dados, mesmas
regras de validação, mesmos textos de tela.

## Créditos

O programa original, o firmware Multi House e os formatos de dados são obra de **ALBERONISSE**:

- [alberonisse/PicoStation_MultiHouse](https://github.com/alberonisse/PicoStation_MultiHouse)
- [alberonisse/PicoStation_Manager](https://github.com/alberonisse/PicoStation_Manager)
- [alberonisse/PicoStation](https://github.com/alberonisse/PicoStation) (firmware, fork de [paulocode/picostation](https://github.com/paulocode/picostation))

Este port é independente e não oficial. O design do Multi House é todo do ALBERONISSE.
Os bancos de dados embutidos (identificação de discos, hashes Redump, cheats GameShark V5, menu do
console, template de capa, música) foram extraídos do executável original com `tools/extract_embedded.py`;
veja `docs/EMBEDDED_DATA.md`.

## Instalar

Requer um Mac com Apple Silicon (M1 ou mais novo) e macOS 12 ou superior.

```sh
curl -L -o PicoStation-Multi-House-V3-macOS-arm64.zip \
  https://github.com/orenatobr/PicoStation-MultiHouse-macOS/releases/latest/download/PicoStation-Multi-House-V3-macOS-arm64.zip \
  && ditto -x -k PicoStation-Multi-House-V3-macOS-arm64.zip . \
  && open "PicoStation Multi House V3.app"
```

O `curl` não marca o arquivo como baixado da internet, então o Gatekeeper não bloqueia o app (ele é
assinado ad-hoc, sem certificado de desenvolvedor). Se você baixar pelo navegador, o macOS vai
recusar na primeira abertura: use clique direito, Abrir, ou rode
`xattr -dr com.apple.quarantine "PicoStation Multi House V3.app"`.

A soma SHA-256 do zip está no arquivo `.sha256` da mesma release.

### Capas padrão (opcional)

As capas de jogos (`database/art/default/<SERIAL>-CASE.art` e `-CD.art`) não vão dentro do app. Se você
tem a pasta `database` do pacote original, o app a procura nesta ordem:

1. `$MH_DATA_DIR/database`
2. ao lado do `.app` (por exemplo `Managers/database`)
3. dentro do app, em `Contents/Resources`
4. `~/Library/Application Support/PicoStation Multi House/database`

Sem essas pastas tudo funciona; as capas vêm do gerador do próprio programa ou ficam em branco.

## Usar

1. Arraste pastas de jogos ou arquivos `.cue` para a janela, ou use **+ Adicionar Jogo** e
   **+ Adicionar Disco**. Cada CUE é verificado (estrutura, serial, hash Redump) se
   "Verificar ao adicionar" estiver marcado.
2. Ajuste nomes, capas, cheats e o menu (botão **Menu**).
3. Escolha o **Cartão SD** e clique em **CRIAR CARTÃO V2**. O macOS pede a senha de administrador para
   abrir o disco bruto. Também dá para salvar tudo como imagem (**Imagem**) e abrir imagens e cartões
   V2 existentes para edição.

### Segurança da gravação

A gravação é bruta e destrói o conteúdo do cartão. O app só oferece discos inteiros que parecem
cartões (USB, SD ou MMC, removíveis, até 130 GB), nunca o disco do sistema nem discos internos.
Antes de gravar ele confere de novo a identidade do disco (número, tamanho, modelo, série), desmonta
os volumes e exige a caixa de confirmação. Mesmo assim, confira o nome do disco na lista.

### Estado de verificação

- O layout do cartão foi conferido campo a campo contra o firmware V3 (`docs/FIRMWARE_CROSSCHECK.md`).
- Imagens e dispositivos de disco virtual (`hdiutil attach -nomount`) foram gravados, relidos e validados.
- **Ainda não foi testado com um cartão SD físico nem com o console.** Se for o primeiro uso, teste com
  um cartão sem dados importantes e relate o resultado.

## Limitações conhecidas (herdadas do original ou do firmware)

- Cheats personalizados em formato v4 com mais de 256 linhas são aceitos pelo programa, mas o firmware
  os ignora (o disco fica sem cheats).
- Títulos com acentos aparecem com espaços no menu do console (o firmware só exibe ASCII).
- Várias mensagens ainda citam "Windows" ou "PhysicalDrive": os textos foram mantidos como no original.

## Compilar

```sh
make app                # gera ~/.cache/picostation-mh-build/PicoStation Multi House V3.app
tools/package.sh 0.1.0  # gera dist/PicoStation-Multi-House-V3-macOS-arm64.zip
make test               # testes do núcleo (ASan/UBSan), leva alguns minutos
make test-v2            # um módulo: base, disc, cheat, art, menu, v2, blockdev
```

Precisa das Xcode Command Line Tools. A saída vai para `~/.cache/picostation-mh-build` (mude com `BUILD=`).
Os testes usam jogos reais de `/Volumes/Renato/PicoStation BR` quando existem e pulam o resto.
Teste de disco sem hardware: `tools/test-disk.sh --images`.

## Como o port foi feito

O executável original (mingw-w64, com símbolos) foi decompilado com Ghidra e cada função foi
reescrita e auditada por um segundo revisor. As tabelas "função original para função do port" estão em
`docs/map/`, os relatórios de auditoria em `docs/audit/`, as convenções em `docs/PORTING.md`.

```
core/        núcleo portátil em C11 (base, disc, cheat, art, menu, v2)
platform/mac acesso a disco (DiskArbitration, authopen) e imagens (ImageIO)
app/         interface AppKit
resources/   dados extraídos do executável original e ícone
docs/        mapas, formatos, auditorias, checagem contra o firmware
tools/       extração de dados, empacotamento, testes de disco
```

## Licença

Apache License 2.0, a mesma dos repositórios PicoStation de origem. Veja `LICENSE`.
