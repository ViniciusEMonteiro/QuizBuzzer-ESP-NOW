# Protocolo QuizBuzzer, versão 1

Transporte: ESP-NOW unicast, interface STA, canal comum. Tamanho fixo: **42 bytes**. Nenhum timestamp remoto é usado na arbitragem. A versão deste protocolo de aplicação é independente da versão do protocolo ESP-NOW da Espressif.

## Formato no fio

Inteiros usam little-endian. `QuizMessage` é a estrutura lógica; `encodeMessage`/`decodeMessage` fazem a serialização explicitamente. Não há `memcpy` de uma struct C++ para o rádio, dependência de padding ou leitura desalinhada.

| Offset | Bytes | Campo | Regra |
| ---: | ---: | --- | --- |
| 0 | 2 | magic | `0x4251`, bytes `51 42` |
| 2 | 1 | protocolVersion | `PROTOCOL_VERSION = 1` |
| 3 | 1 | command | 1 a 7, tabela abaixo |
| 4 | 1 | senderID | 0 MASTER, 1..8 SLAVE |
| 5 | 1 | targetID | ID do destino unicast |
| 6 | 2 | roundID | Incrementado a cada nova rodada |
| 8 | 4 | sequence | Contador de transmissões do remetente |
| 12 | 8 | sessionID | Contador persistente de boot do MASTER |
| 20 | 8 | deviceBootID | Contador persistente de boot do SLAVE envolvido |
| 28 | 4 | roundGeneration | Incrementado quando `roundID` passa de 65535 para 0 |
| 32 | 4 | ackSequence | Sequência exata da transmissão confirmada, apenas em ACK |
| 36 | 1 | winnerID | 0 sem vencedor; ID do vencedor nos estados/pings do MASTER |
| 37 | 1 | flags | Bit 0 = `FLAG_SYNC_REQUEST`, somente STATUS |
| 38 | 1 | ackCommand | Comando confirmado em ACK; `Command::ACK` nos demais |
| 39 | 1 | reservado | Deve ser zero |
| 40 | 2 | CRC16 | CRC-16/CCITT-FALSE dos bytes 0..39 |

CRC: polinômio `0x1021`, inicialização `0xFFFF`, sem reflexão, XOR final zero, resultado transmitido little-endian. CRC detecta corrupção acidental; não autentica o emissor.

`deviceBootID` é enviado pelo SLAVE e ecoado pelo MASTER. Isso impede que um SLAVE reiniciado aceite um RESET ou WINNER destinado ao seu boot anterior. O MASTER só registra boots novos por STATUS, nunca por um acionamento. Boots menores que o último conhecido para aquele SLAVE são rejeitados.

`sessionID`, `roundGeneration` e `roundID`, nesta ordem, identificam e ordenam a rodada. O SLAVE não retrocede para uma identidade menor. O MASTER exige igualdade exata para BUTTON e ACK. STATUS pode carregar uma identidade antiga ou zero para solicitar sincronização. Um novo boot válido do MASTER inicia outra sessão e pode começar novamente em rodada 1.

## Comandos

| Valor | Comando | Origem → destino | Significado | Resposta |
| ---: | --- | --- | --- | --- |
| 1 | BUTTON_PRESSED | SLAVE → MASTER | Primeira borda local, rodada conhecida | WINNER ou LOCK_ROUND |
| 2 | WINNER | MASTER → SLAVE vencedor | Confirmação explícita de vitória | ACK de WINNER |
| 3 | RESET_ROUND | MASTER → cada SLAVE | Liberação da rodada informada | ACK de RESET_ROUND |
| 4 | LOCK_ROUND | MASTER → SLAVE | Impede novos acionamentos na rodada | ACK de LOCK_ROUND |
| 5 | ACK | SLAVE → MASTER | Confirma comando processado | Nenhuma |
| 6 | PING | MASTER → SLAVE | Descoberta e supervisão | STATUS |
| 7 | STATUS | SLAVE → MASTER | Presença e pedido de sincronização | Estado atual quando solicitado/necessário |

### BUTTON_PRESSED

É válido somente de um MAC/ID cadastrado, com boot previamente anunciado por STATUS e identidade de rodada igual à do MASTER. Não precisa aguardar que o MASTER tenha recebido o ACK do RESET: esse ACK pode estar pendente quando o botão é acionado.

Em ARMED, o MASTER registra `winnerID` e muda para LOCKED na mesma execução, antes de transmitir ou registrar logs. Qualquer BUTTON posterior não muda o resultado. O critério usa a ordem FIFO dos pacotes válidos entregues pelo callback; não usa relógio, posição de ID ou ordem de cadastro dos peers.

O SLAVE tenta enviar imediatamente, até seis transmissões com 40 ms entre tentativas. Cada tentativa usa nova `sequence`, mas representa o mesmo evento lógico `(sessionID, roundGeneration, roundID, senderID, deviceBootID)`. Em caso de perda de todas as tentativas, não há vencedor atribuível a esse acionamento. Após 1,2 s o SLAVE pede sincronização, sem permitir outro acionamento da mesma rodada. O operador pode iniciar nova rodada.

### WINNER

Unicast obrigatório com `targetID == winnerID`. O SLAVE exige seu próprio ID e boot, remetente MASTER cadastrado e rodada já sincronizada exatamente igual à atual. Uma mensagem WINNER não estabelece sozinha uma rodada desconhecida.

Após processar, entra em WINNER, pisca o LED do botão e a fita externa juntos e agenda ACK. Repetições preservam o estado e a fase da animação e confirmam novamente; nunca criam outra vitória. O MASTER não muda o vencedor se o ACK for perdido. Se o vencedor reiniciar, o MASTER primeiro sincroniza esse boot com LOCK_ROUND; após o ACK de LOCK envia WINNER.

### RESET_ROUND

Enviado individualmente a cada boot conhecido. O MASTER incrementa `roundID`, limpa o vencedor, torna-se ARMED e substitui as pendências da rodada anterior. Peers ainda desconhecidos recebem PING de descoberta e sincronizam após STATUS.

Uma identidade nova limpa WINNER/LOCKED/acionamento anterior e torna o SLAVE READY. Um RESET repetido da **mesma** rodada não rearma um botão já enviado, não apaga WINNER e não reabre uma rodada já bloqueada. Repetições aceitas geram ACK; pacotes antigos ou conflitantes são descartados. O debounce físico continua existindo entre rodadas, exigindo liberação do botão.

Não há liberação simultânea garantida pelo rádio: os unicasts e ACKs chegam em instantes diferentes. O operador deve conferir a disponibilidade das bancadas antes de fazer a pergunta.

### LOCK_ROUND

Informa a rodada e o vencedor atual, ou `winnerID=0` quando o MASTER aguarda liberação manual. Bloqueia o SLAVE, apaga o LED do botão e a fita externa e recebe ACK. Também permite sincronizar uma placa que entrou quando a rodada já estava encerrada.

Repetições são idempotentes. LOCK da mesma rodada não apaga a indicação de uma vitória já confirmada. O campo winnerID em LOCK é informativo: mesmo que corresponda ao ID local, o SLAVE aguarda WINNER para sinalizar vitória. A perda de LOCK nunca reabre o MASTER.

### ACK

Contém identidade da rodada, boot do SLAVE, `ackCommand` e `ackSequence`. Sua própria `sequence` também é nova. O MASTER só aceita uma confirmação correspondente a uma das últimas oito tentativas efetivamente enviadas àquela bancada, do comando atualmente esperado e da rodada atual.

ACK antigo, comando errado, sequência não enviada ou boot errado não conclui a pendência. Duplicatas são descartadas. Não existe ACK de ACK nem ACK lógico de BUTTON; a decisão do MASTER responde ao acionamento.

O callback de envio `ESP_NOW_SEND_SUCCESS` informa sucesso na camada MAC. Ele incrementa uma estatística de rádio, mas **não substitui ACK de aplicação**. Essa distinção segue a [documentação ESP-NOW da Espressif](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/network/esp_now.html).

### PING

Enviado a um peer por vez a cada 250 ms, ciclo nominal de 2 s para oito peers. Transporta o estado de rodada conhecido pelo MASTER, mas não arma o SLAVE. `deviceBootID=0` permite descobrir uma placa ainda não registrada; mesmo um PING com boot desatualizado pode provocar STATUS do boot atual.

Sessão superior à conhecida faz o SLAVE solicitar sincronização. Sessão inferior ou sequência antiga é ignorada. Não há resposta a repetições antigas. Um PING de outra rodada provoca pedido de estado atual, sem decidir vitória.

### STATUS

É a primeira mensagem do SLAVE, responde a PING e é repetido a cada 1 s durante sincronização. Carrega seu boot e a última rodada conhecida, ou zero antes da primeira sincronização. Bit 0 em flags solicita explicitamente o estado atual.

O MASTER registra boots maiores, rejeita menores e atualiza presença apenas para sequência nova. Se a rodada não coincide ou o bit de sincronização está ativo, agenda RESET quando ARMED ou LOCK/WINNER quando encerrada. Um STATUS não escolhe vencedor nem abre rodada. Repetições com a mesma sequência não reiniciam retries.

## Confiabilidade e concorrência

- Cada submissão aceita pelo rádio incrementa `sequence`, inclusive retries. Rádio ocupado/erro imediato não consome uma sequência. A comparação usa distância modular menor que `2^31`, inclusive no wrap de 32 bits; pressupõe menos de `2^31` transmissões entre observações de um mesmo boot.
- Comandos de estado têm seis tentativas rápidas, depois continuam a cada 1 s até ACK ou substituição pelo estado novo. Não existe número ilimitado de objetos acumulados: uma pendência e oito referências de tentativas por peer.
- Somente a tarefa do jogo chama `esp_now_send`, com uma transmissão em voo e conclusão via mailbox. Callbacks não enviam dados, não fazem logs e não alteram a máquina de estados. Os pacotes de RX são copiados para FIFO de 64 entradas, sem espera.
- O SLAVE processa botão antes do lote de RX e confere bordas dentro do lote. BUTTON pendente precede ACK/STATUS. O MASTER tenta WINNER antes dos demais estados. Não é possível cancelar um frame que já ocupa o rádio.
- Se a FIFO saturar, o novo frame é descartado e um contador registra overflow; retries podem recuperar a comunicação. A ordenação vale para frames retidos e aceitos. A perda de frames no rádio ou na fila impede garantir quem pressionou fisicamente primeiro.
- Se o callback de envio não vier em 1 s, o rádio entra em falha persistente e o jogo da placa para com LED de erro. Não se libera o rádio para novo envio com callback antigo ainda pendente. Reinicie após corrigir a causa.

## Limites e integridade

MAC de origem, MAC de destino, ID, direção de comando, comprimento, versão, campos reservados, CRC e rodada são verificados. A sessão é persistente por placa, não um relógio de parede. Limpar NVS exige o procedimento de manutenção; uma sessão menor não substitui automaticamente uma maior.

Não há criptografia nesta versão. Uma whitelist de MACs não impede um transmissor malicioso de falsificar frames. Para uso em ambiente adversarial, adicionar provisionamento de PMK/LMKs e CCMP, verificando o limite configurado de peers criptografados para oito SLAVES. Não colocar chaves reais num repositório público.
