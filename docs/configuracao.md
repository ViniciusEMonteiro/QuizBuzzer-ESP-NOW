# Configuração

## Ambiente ESP-IDF

Use SDK **ESP-IDF 6.x**, validado com **6.0.2**, alvo `esp32`. Ative o terminal da instalação correta e confirme com `idf.py --version`. Os componentes da aplicação não usam Arduino ou PlatformIO.

```sh
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p COM5 flash monitor
```

`set-target` recria a configuração de build; não é necessário a cada compilação. `sdkconfig.defaults` fornece os padrões para configurações novas. Um `sdkconfig` já existente tem precedência sobre esses padrões; ajuste pelo menuconfig quando necessário.

## Arquivos de configuração

| Arquivo | Responsabilidade |
| --- | --- |
| `include/pins.h` | Única definição dos GPIOs e verificações de conflito |
| `include/config.h` | Parâmetros do jogo, rádio, retries, debug e LEDs |
| `src/device_config.cpp` | Tabela comum com IDs e MACs STA |
| `include/protocol.h` | Versão, comandos, campos e tamanho no fio |
| `sdkconfig.defaults` | Configuração padrão do SDK, tick, flash e logs |

`BUTTON_PIN=25` é a única entrada de botão, comum a MASTER e SLAVE. `ROLE_SELECT_PIN=27` determina no boot qual controlador recebe os acionamentos: LOW para RESET/nova rodada no MASTER, HIGH para resposta no SLAVE. Não há GPIO separado para RESET de rodada. Os parâmetros de debounce continuam configuráveis por função.

## MAC e identificação

`DEVICES[0]` é o MASTER, `DEVICES[1]` a `DEVICES[8]` são SLAVE_01 a SLAVE_08. Substitua todos os MACs fictícios. Índices e IDs precisam corresponder; a validação rejeita duplicatas, MAC multicast e MAC zero. O MAC local deve corresponder à entrada de seu ID.

Com `CONFIGURED_SLAVE_ID=0`, `ConfigDeviceIdProvider` procura o MAC STA local na tabela. Assim o mesmo binário pode ser gravado em todas as placas. O papel continua definido exclusivamente pelo jumper ROLE no boot.

Se necessário, configure `CONFIGURED_SLAVE_ID` entre 1 e 8 para selecionar um ID por compilação. Isso requer binários configurados para cada SLAVE; não altera a rotina crítica. O MASTER ignora esse parâmetro. O MAC correspondente continua obrigatório.

Para DIP switch/jumpers/NVS/interface de configuração, implemente `DeviceIdProvider::slaveId()` e substitua a instanciação do provider em `gameTask`. A leitura deve acontecer antes de iniciar o rádio e deve retornar `INVALID_ID` para configuração inválida. Não adicione leitura de flash/DIP no caminho do botão.

## Parâmetros principais

| Parâmetro | Padrão | Efeito |
| --- | ---: | --- |
| MAX_SLAVES | 8 | Quantidade de SLAVES, exige tabela compatível |
| WIFI_CHANNEL | 6 | Mesmo canal em todas as placas |
| AUTO_ARM_ON_BOOT | true | Inicia automaticamente a rodada 1 |
| BUTTON_DEBOUNCE_MS | 30 | Bloqueio e liberação estável do botão SLAVE |
| MASTER_BUTTON_DEBOUNCE_MS | 30 | Debounce de nova rodada |
| BUTTON_RETRY_MS / BUTTON_MAX_ATTEMPTS | 40 / 6 | Recuperação de BUTTON perdido |
| DECISION_TIMEOUT_MS | 1200 | Solicitar estado após ausência de decisão |
| STATE_RETRY_MS / STATE_FAST_ATTEMPTS | 80 / 6 | Primeiras tentativas de estado crítico |
| STATE_SLOW_RETRY_MS | 1000 | Continuação de estado sem ACK |
| SUPERVISION_ENABLED | true | PING periódico e timeout de presença |
| SUPERVISION_POLL_MS | 250 | Intervalo entre peers da supervisão |
| ONLINE_TIMEOUT_MS | 8000 | Prazo para considerar contato ausente |
| SYNC_INTERVAL_MS | 1000 | Repetição de pedido de sincronização |
| RADIO_CALLBACK_TIMEOUT_MS | 1000 | Falha persistente se TX não concluir |
| DEBUG_ENABLED | true | Logs informativos e avisos |
| VERBOSE_DEBUG_ENABLED | false | Diagnósticos periódicos adicionais |
| LED_ACTIVE_HIGH | true | Polaridade do LED do botão, GPIO26 em MASTER/SLAVE |
| EXTERNAL_LED_ACTIVE_HIGH | true | Polaridade do gate/driver do MOSFET da fita, GPIO32 |
| WINNER_BLINK_MS | 100 | Intervalo de alternância comum ao botão e à fita do vencedor |
| LED_TASK_POLL_MS | 10 | Atualização da animação na tarefa dedicada |

`EXTERNAL_LED_PIN=32` está em `pins.h`. Todas as placas possuem essa saída além de `SLAVE_LED_PIN=26`, utilizado para o LED do botão nos dois papéis. Os perdedores mantêm ambos apagados; não existe mais pulso de presença em LOCKED. O MASTER usa botão apagado/fita acesa em ARMED e botão aceso/fita apagada em LOCKED ou esperando liberação manual. A inversão visual do MASTER é definida pelo estado, independentemente da polaridade elétrica de cada driver.

Desabilitar a supervisão mantém a descoberta de boots desconhecidos e os pedidos de sincronização necessários ao jogo. Sem PING periódico, a classificação ONLINE não é continuamente renovada; o timeout de link no SLAVE fica desativado. O MASTER continua respondendo a STATUS e mantendo retries de estado.

Para expandir além de oito SLAVES, altere a tabela, `MAX_SLAVES`, prazo do ciclo de supervisão e testes. O limite da aplicação é 19 SLAVES, abaixo do teto de 20 peers não criptografados da API; a carga de rádio e o limite criptografado precisam de avaliação própria.

## NVS e reinicializações

Namespace `quiz`, chave `boot_id`, tipo `uint64_t`. Cada placa incrementa e confirma esse valor uma vez por boot, antes do jogo. A gravação precisa concluir para a placa operar. Não há gravação NVS no botão, na recepção, no RESET de rodada ou nos retries.

O MASTER usa seu boot como `sessionID`; cada SLAVE anuncia o próprio boot e exige que as respostas o ecoem. Isso permite recuperar ordem de energização arbitrária e reboot de MASTER/SLAVE durante a operação. O MASTER reiniciado abre nova sessão, sem preservar um vencedor anterior: com auto-arm habilitado inicia outra rodada; desabilitado aguarda o operador. O MASTER que continua ligado preserva o vencedor se um SLAVE reiniciar.

Não apagar NVS automaticamente em `ESP_ERR_NVS_NO_FREE_PAGES` ou mudança de formato: apagar faria o contador retroceder. Erros de armazenamento param o jogo e indicam manutenção. O wrap de `roundID` é tratado por `roundGeneration` em RAM; o valor antigo de `roundID` sozinho nunca identifica uma rodada.

Se for necessário apagar NVS de uma placa em manutenção:

1. Retire o sistema de operação e desligue todas as bancadas.
2. Faça backup de outros dados persistentes relevantes antes de qualquer apagamento.
3. Apague/regrave apenas as placas necessárias, com o comando de manutenção apropriado e a porta confirmada. `erase-flash` apaga todo o conteúdo, inclusive firmware, e exige nova gravação.
4. Desligue novamente as placas em manutenção e religue o conjunto inteiro. Isso limpa a memória dos peers que poderiam conservar um contador maior.
5. Inicie uma nova rodada e execute os testes básicos antes de devolver o sistema ao jogo.

Contadores não são autenticação criptográfica. Não há proteção contra reprodução intencional de tráfego após restauração de backups antigos da flash.
