# Máquinas de estados e propriedade dos dados

`MasterController` e `SlaveController` são independentes do SDK. A aplicação chama seus métodos exclusivamente da tarefa `quiz_game`. O rádio nunca acessa `masterState`, `winnerID` ou `roundID`; entrega cópias em FIFO. Isso elimina a necessidade de mutex na arbitragem e assegura um único escritor dos dados do jogo.

## MASTER

| Estado | winnerID | Aceita BUTTON? | Saída por evento |
| --- | --- | --- | --- |
| INITIALIZING | 0 | Não | Boot automático ou botão de nova rodada → ARMED |
| ARMED | 0 | Sim, se pacote válido da rodada | Primeiro BUTTON → LOCKED |
| LOCKED | 1..8 | Não | Botão de nova rodada → ARMED |

Durante a inicialização de hardware não há controlador ativo. Se `AUTO_ARM_ON_BOOT=false`, INITIALIZING também representa a espera pela primeira liberação manual, após o rádio estar pronto. Nesse período o MASTER sincroniza SLAVES com LOCK_ROUND e vencedor 0.

Em ARMED, o MASTER acende a fita externa e apaga o LED do botão. Em LOCKED, apaga a fita e acende o botão para indicar RESET/nova rodada. O controlador em INITIALIZING, aguardando liberação manual, também acende o botão e mantém a fita apagada. Essas indicações inversas orientam o operador; o botão físico ainda pode iniciar nova rodada em ARMED.

Na vitória: atribuir vencedor, mudar para LOCKED, substituir pendências de estado e tentar transmitir WINNER. Logs e LEDs vêm depois. Dois pacotes presentes no mesmo lote de RX passam pelo controlador em ordem; após o primeiro vencer, o segundo já encontra LOCKED.

No reset: incrementar `roundID` (e a geração se houver wrap), apagar vencedor, mudar para ARMED e substituir pendências antigas por RESET. A tarefa trata o botão de reset antes de consumir o lote RX; pacotes enfileirados de rodadas anteriores são descartados pela identidade de rodada. O reset é o comando explícito do operador para abandonar a disputa anterior.

Falha de ACK não muda o estado nem o vencedor. Um peer offline não bloqueia o reset dos demais. `confirmed(id)` informa confirmação do estado atual; ONLINE informa somente comunicação recente. Inicialmente todas as bancadas são registradas como OFFLINE aguardando contato.

## SLAVE

| Estado | LED do botão | Fita externa | Entrada de botão |
| --- | --- | --- | --- |
| INITIALIZING, aguardando sincronização | Apagado | Apagada | Ignorada até sincronizar |
| READY, sincronizado | Aceso | Apagada | Primeira borda tenta enviar BUTTON |
| WAITING_MASTER, sincronizado | Aceso | Apagada | Ignorada na mesma rodada |
| WINNER | Pisca a cada 100 ms | Pisca na mesma fase do botão | Ignorada até nova rodada |
| LOCKED | Apagado, sem pulsos | Apagada | Ignorada até nova rodada |

O boot de hardware usa uma indicação transitória diferente: somente o botão pisca a cada 150 ms. Erros produzem dois pulsos curtos no botão, com a fita desligada.

Separadamente, o controlador mantém `synchronized`, `pressedThisRound` e `roundLocked`. Esses atributos preservam a semântica durante perdas de comunicação. Voltar a sincronizar não significa liberar uma segunda resposta da mesma bancada na mesma rodada.

- RESET de identidade nova limpa o estado anterior e permite READY.
- RESET repetido mantém WAITING_MASTER se já houve envio; não reabre LOCKED/WINNER.
- WINNER exige destino e boot locais, MASTER cadastrado e rodada corrente conhecida. Somente ele entra em WINNER.
- LOCK sincroniza uma rodada bloqueada. `winnerID` informativo não autoriza sinalizar vitória.
- Um timeout não define vencedor, não rearma e não tenta enviar BUTTON para outra rodada.
- Ausência de tráfego do MASTER durante 8 s, com supervisão ativa, invalida a sincronização. READY volta a INITIALIZING; sem sincronização, READY/WAITING_MASTER não mantêm a luz do botão acesa. A vitória já confirmada permanece visível nos dois LEDs até uma nova rodada.

## Botões, tarefas e filas

Todas as placas usam a mesma entrada `BUTTON_PIN` (GPIO25) e o mesmo circuito de botão. `initializeButton()` configura essa entrada sem receber um papel. O `ROLE_SELECT_PIN` lido no boot escolhe exclusivamente `runMaster()` ou `runSlave()`: o primeiro interpreta a borda como nova rodada; o segundo como resposta. O papel permanece fixo durante a execução.

O debounce envia na primeira borda e trava imediatamente novos eventos locais. Para destravar, exige nível liberado estável por 30 ms. O botão não é reinicializado na troca de rodada; mantê-lo pressionado durante RESET não dispara automaticamente. Uma entrada já pressionada no boot começa travada.

A ISR de GPIO apenas guarda uma borda e notifica a tarefa. A tarefa também lê o nível, evitando dependência exclusiva da interrupção. A flag é atômica; RX e conclusão de TX usam filas FreeRTOS sem espera nos callbacks.

O loop do SLAVE trata botão antes de receber até 16 mensagens por lote e verifica bordas dentro do lote. O envio do botão antecede estado de LED e logs. Com o rádio ocupado, a intenção de BUTTON fica no controlador e é a primeira candidata quando o envio em voo termina. STATUS não acumula uma fila de transmissões.

`quiz_game` usa prioridade 5, no núcleo 1 do ESP32 de dois núcleos. `quiz_led` e `quiz_log` usam prioridade 1. As esperas por notificação cedem a CPU; não são temporizações bloqueantes do jogo. Nenhuma animação, escrita NVS ou saída de console roda dentro do callback ESP-NOW.

`LedManager::levels()` calcula os dois níveis lógicos a partir do mesmo modo e do mesmo instante. A tarefa `quiz_led` aplica as polaridades independentes de botão/fita e escreve os GPIOs em sequência. Assim a animação tem uma fase única, sem deriva entre dois temporizadores. Mudanças de modo notificam a tarefa de LED; repetições do mesmo modo não reiniciam a fase. No vencedor, a sincronia é visual, com a diferença mínima das duas escritas de GPIO.

Se o rádio deixar de produzir callback de envio, a aplicação mantém a tarefa viva em espera, preservando o handle que as ISR/callbacks notificam, e passa a LED de erro. Ela não continua arbitrando com transporte inconsistente.
