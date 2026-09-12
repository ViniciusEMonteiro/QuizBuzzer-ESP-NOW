# Estratégia de testes

Há duas camadas: testes automatizados do núcleo real em ESP32 emulado e testes físicos do sistema completo. O transporte controlado dos testes não emula ESP-NOW nem atesta alcance, interferência, corrente do LED ou latência real.

## Executar testes automatizados

No terminal ESP-IDF 6.x ativado, a partir da raiz:

```sh
idf.py -C tests/firmware build
python tools/run_qemu_tests.py
```

Pré-requisito adicional: QEMU Xtensa da Espressif no PATH. Pode ser instalado com `python $IDF_PATH/tools/idf_tools.py install qemu-xtensa` em shell POSIX ou `python "$env:IDF_PATH/tools/idf_tools.py" install qemu-xtensa` em PowerShell. Reative o ambiente após instalar. O executor também aceita `--qemu caminho/para/qemu-system-xtensa` e `--build-dir`.

No Windows, se QEMU sair com `0xC0000135` sem console, há DLL ausente. Na instalação usada para validar, foi necessário disponibilizar `libiconv-2.dll` já presente no diretório `mingw64/bin` do Git for Windows pelo PATH do processo. Não é necessário alterar o PATH global ou copiar DLLs para pastas do sistema.

O script cria uma imagem de flash de teste de 4 MiB, inicia QEMU sem interface gráfica e sem rede, aguarda o resumo Unity, encerra apenas o processo que criou e retorna código diferente de zero se houver falha, teste ignorado ou timeout. Salva a saída em `tests/firmware/build/qemu-results.log`.

Também é possível executar a aplicação Unity num ESP32: `idf.py -C tests/firmware -p COM5 flash monitor`. Esse comando substitui o firmware da placa; para voltar ao jogo, grave novamente o projeto da raiz.

## Cobertura automatizada

| Teste | O que verifica |
| --- | --- |
| Codec e frames inválidos | Tamanho, endian, sessão/boot de 64 bits, corrupção em cada byte, versão/direção/destino |
| Primeiro recebido | Estado já LOCKED antes de enviar, duplicatas e pressões posteriores sem efeito |
| Oito IDs | Cada ID pode vencer se seu pacote for o primeiro aceito |
| Rodada e boot antigos | RESET libera nova rodada; pacotes da anterior e boots obsoletos são rejeitados |
| Auto-arm e wrap | Início manual e transição 65535→0 protegida pela geração |
| Confirmação e LED | WINNER somente do MASTER, alvo/rodada corretos, padrão de vitória e retorno a READY |
| RESET repetido | Não rearma quem já pressionou e não reabre rodada bloqueada |
| Retry e ACK | Novas sequências por envio e confirmação de tentativa exata da rodada atual |
| Reboot do vencedor | Primeiro LOCK para sincronização, depois WINNER após ACK |
| Reboot do MASTER | Nova sessão não aceita estado atrasado da sessão anterior |
| Rádio ocupado | BUTTON precede supervisão e possui tentativas limitadas |
| Debounce | Primeira borda imediata, bouncing, botão mantido, boot pressionado e wrap de tempo |
| Supervisão | ONLINE/OFFLINE e wrap de sequence |
| Identidade | Tabela, lookup de ID por MAC e MAC desconhecido |
| Perdas integradas | Dois SLAVES e MASTER, perda de RESET/ACK/WINNER, duplicação e convergência |
| Timeout e ressincronização | ACK continua funcionando sem rearmar um acionamento da mesma rodada |
| LEDs dos perdedores | Botão e fita permanecem apagados, inclusive após RESET antigo, até nova rodada válida |
| Sincronia do vencedor | Mesma fase nas duas saídas, preservada em atualizações repetidas e wrap de tempo |
| LEDs do MASTER | Saídas inversas em início manual, ARMED, vitória e nova rodada |
| Conexão e saídas de diagnóstico | Botão aceso enquanto sincronizado, apagado no timeout e fita desligada no boot/erro |

Os testes usam `quiz_core`, o mesmo componente compilado no firmware. `FakeRadio` substitui apenas a interface `Transport`; não há uma segunda implementação da arbitragem. O cenário integrado impõe uma ordem de entrega conhecida; a regra de vitória é conferida contra essa ordem.

## Preparação física

Monte um MASTER e oito SLAVES com a tabela final e o mesmo binário. Registre revisão da PCB, versão do SDK, hash do binário, canal, alimentação, posição das bancadas e parâmetros alterados. Confirme todos os LEDs READY antes de cada rodada. Capture o monitor serial do MASTER e, nos testes de perda, também o do SLAVE envolvido.

Conecte o único botão de cada placa entre GPIO25 (`BUTTON_PIN`) e GND, inclusive no MASTER. Confirme que, após o boot com ROLE em LOW, esse botão inicia nova rodada; nas placas com ROLE em HIGH, envia a resposta. O LED do botão continua em GPIO26 e a fita externa em GPIO32.

| Nº | Ação | Resultado obrigatório |
| ---: | --- | --- |
| 1 | SLAVE 01 pressiona sozinho | MASTER registra SLAVE 01, somente ele sinaliza vitória |
| 2 | SLAVES 01 e 02 pressionam quase juntos, repetir com ordens e posições invertidas | Exatamente uma bancada vencedora, com seus dois LEDs piscando; as outras permanecem apagadas após LOCK |
| 3 | Outro SLAVE pressiona após o vencedor | O resultado permanece igual |
| 4 | Pressionar nova rodada no MASTER | roundID incrementa, vencedor apaga e SLAVES retornam a READY |
| 5 | Injetar BUTTON válido da rodada anterior | Nenhum vencedor definido por esse pacote |
| 6 | Injetar novamente o mesmo BUTTON, com mesma sequência, e depois um retry com sequência nova | Resultado não se altera |
| 7 | Observar o destinatário de WINNER | Somente targetID correto entra em WINNER; botão e fita piscam juntos |
| 8 | Resetar a rodada após vitória | SLAVES voltam a botão contínuo/fita apagada; MASTER volta a botão apagado/fita acesa |

Para 5 e 6, use uma placa de teste identificada com o MAC/ID de uma bancada retirada do sistema, capturando e repetindo seus próprios frames de teste com a identidade/CRC adequados. Nunca ligue dois transmissores com a mesma identidade durante um jogo. Esses cenários já são injetados automaticamente no teste do núcleo; o ensaio físico valida o caminho de rádio e fila.

## Casos adicionais obrigatórios antes de uso real

1. **Botão mantido:** segurar durante RESET, durante boot e por vários segundos. Só uma borda válida; liberar estavelmente por 30 ms antes de tentar de novo.
2. **Bouncing:** aplicar uma sequência de transições curtas no contato. O primeiro acionamento é imediato e as transições seguintes não criam outra resposta.
3. **Perda de WINNER/RESET/ACK:** interromper temporariamente a recepção ou usar instrumentação de teste para descartar frames. O estado converge por retries e o vencedor do MASTER não muda.
4. **Boot em ordens diferentes:** MASTER antes/depois dos SLAVES, SLAVE entrando em rodada encerrada e vencedor reiniciando. A sincronização deve recuperar o estado correto.
5. **MASTER reiniciado:** nova sessão deve substituir a anterior; pacotes anteriores não podem vencer. Com `AUTO_ARM_ON_BOOT=false`, aguardar liberação manual.
6. **SLAVE desconectado:** verificar OFFLINE após timeout e recuperação ao religar. Outras bancadas continuam operando.
7. **Campos inválidos:** CRC, tamanho, versão, comando, target, MAC/ID, boot ou rodada inválidos não devem alterar resultado.
8. **Saturação:** observar contador de overflow RX sob carga artificial. Mesmo descartando pacotes, nunca dois vencedores. Corrigir carga antes do uso competitivo.
9. **Erros de rádio:** falha de callback deve parar a placa em modo de erro; callback de rádio bem-sucedido sem ACK não deve concluir confirmação lógica.
10. **NVS:** testar falha de inicialização sem apagamento automático e o procedimento de manutenção de contadores.
11. **Alimentação e alcance:** testar LEDs acesos, distância máxima prevista, participantes e interferência presentes. Registrar resets, perdas e tempos.
12. **Fita e bloqueio:** observar os dois LEDs do vencedor com analisador lógico e visualmente na caixa de acrílico; as duas saídas devem alternar na mesma fase. Confirmar perdedores sem qualquer pulso no botão ou na fita.
13. **MASTER:** com rodada aberta, botão apagado/fita acesa; após vitória, botão aceso/fita apagada; após RESET, inverter novamente. Repetir com `AUTO_ARM_ON_BOOT=false`, quando o botão deve acender aguardando a primeira liberação.
14. **Carga de 12 V:** verificar MOSFET, corrente, temperatura e fonte com a fita real. Ela deve ficar desligada durante reset/boot/erro e não causar queda de alimentação quando piscar.

## Medir latência e equidade

Use analisador lógico/osciloscópio e, se necessário, GPIOs temporários de instrumentação para marcar borda local, chamada de envio e aceitação no MASTER. Não use logs serial como medição precisa. A instrumentação deve ser idêntica nas oito bancadas e retirada ou mantida simétrica na versão de produção.

Registre mediana, percentis altos e máximo observado com supervisão ligada/desligada e imediatamente após RESET. Alterne posições físicas e IDs para separar propagação/interferência de erros de implementação. Não estabeleça um limite de milissegundos sem medir o hardware final: a garantia de software é a prioridade do caminho crítico e o vencedor único pela ordem de recepção aceita.

## Registro da validação desta entrega

- SDK real: ESP-IDF **6.0.2**, compilador Xtensa GCC **15.2.0**, alvo **esp32**.
- Compilação do firmware e da aplicação Unity executadas localmente.
- QEMU Espressif **9.2.2**, testes do núcleo: **20 aprovados, 0 falhas, 0 ignorados**, incluindo as duas saídas de LED.
- Os componentes `quiz_core` e `quiz_hardware` são compilados com `-Wall -Wextra -Werror`.
- Testes de rádio, pinagem, latência e LEDs em nove placas físicas **não executados**; precisam ser feitos com os MACs e a PCB definitivos.
