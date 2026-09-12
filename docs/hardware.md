# Hardware

Base inicial: nove placas equivalentes ESP32-WROOM-32/ESP32 DevKit, alvo ESP32 clássico e flash de 4 MB. Todas possuem LED no botão e uma fita LED externa de 12 V para iluminar a caixa/bancada de acrílico, acionada por MOSFET em GPIO independente. Pinagem provisória, a confirmar na PCB definitiva. O mesmo hardware suporta os dois papéis.

## Sinais

| Sinal | GPIO inicial | Direção | Função |
| --- | ---: | --- | --- |
| ROLE_SELECT_PIN | 27 | Entrada com pull-up | LOW=MASTER / HIGH=SLAVE, somente no boot |
| BUTTON_PIN | 25 | Entrada com pull-up | Botão único: resposta SLAVE / nova rodada MASTER, ativo em LOW |
| SLAVE_LED_PIN | 26 | Saída | LED do botão do SLAVE ou do MASTER, ativo em HIGH |
| EXTERNAL_LED_PIN | 32 | Saída | Gate/driver do MOSFET da fita externa de 12 V, ativo em HIGH |

Os pinos estão exclusivamente em [include/pins.h](../include/pins.h), incluído por `config.h`, com `TODO: confirmar GPIO conforme PCB definitiva`. O firmware inicializa a mesma entrada de botão e as duas saídas de LED nos dois papéis. Cada bancada tem somente um botão físico; sua função depende do `ROLE_SELECT_PIN` lido no boot. Apesar do nome `SLAVE_LED_PIN`, GPIO26 também controla o LED do botão do MASTER. Não una GPIO26 e GPIO32: as indicações são independentes.

## Ligações básicas

```text
3V3 ---- resistor 10 kohm ---- GPIO27 ---- jumper ---- GND

GPIO25 ---- botão único normalmente aberto ---- GND

ROLE LOW:  botão = RESET / nova rodada (MASTER)
ROLE HIGH: botão = resposta do participante (SLAVE)
```

O pull-up interno é habilitado nas duas entradas: ROLE e botão. O resistor externo de 10 kΩ em ROLE ajuda a fixar a função durante energização; o jumper deve estar estável antes de ligar. Nunca deixe duas placas configuradas como MASTER. Alterar o jumper em funcionamento não altera o papel nem a função do botão até o próximo boot.

Nos botões, pull-up externo de aproximadamente 10 kΩ para 3,3 V é recomendável quando houver chicotes. Utilize fios curtos, retorno de GND próximo e proteção apropriada a ESD no produto final. O software reconhece a primeira borda imediatamente e exige 30 ms de liberação estável para aceitar outra; pulsos elétricos indevidos podem ser interpretados como pressão. Evite filtros RC longos que adicionem atraso desigual entre participantes.

## LED do botão, GPIO26

Para um LED discreto de baixa corrente, dimensione resistor série pela tensão direta e corrente admissível do circuito. Para iluminação embutida em botão, confirme a tensão nominal e a existência de resistor interno.

LEDs de botão de 5/12 V devem ser alimentados pela fonte correspondente e acionados por um estágio compatível com comando de 3,3 V no GPIO26. Esse acionamento é separado do MOSFET da fita. `LED_ACTIVE_HIGH` ajusta a polaridade do LED do botão.

## Fita externa de 12 V, GPIO32

Cada uma das nove bancadas, incluindo o MASTER, recebe um MOSFET para ligar e desligar sua fita de 12 V. O GPIO32 fornece apenas o comando de 3,3 V; a potência vem da fonte de 12 V. Exemplo de MOSFET N chaveando o retorno da fita:

```text
+12 V da fonte ---- (+) fita LED 12 V (-) ---- dreno MOSFET N
                                               source ---- GND
GPIO32 ---- resistor de gate ---- gate
                                   |
                           resistor de pull-down
                                   |
                                  GND

GND da fonte 12 V ---- GND do ESP32 ---- source do MOSFET
```

Escolha um MOSFET com resistência de condução especificada para comando de 3,3 V, tensão de dreno adequada à alimentação e capacidade térmica/corrente compatíveis com a fita. A tensão de limiar do gate sozinha não garante condução suficiente. Dimensione fonte, fios, conectores e proteção pela potência total da fita; não faça a corrente da fita passar pelo regulador de 3,3 V do ESP32.

O resistor entre gate e source mantém o MOSFET desligado durante reset, antes do firmware configurar o pino. O firmware pré-carrega as duas saídas no nível apagado antes de habilitá-las e mantém a fita apagada durante boot/erro. Se usar um módulo de driver com entrada invertida, ajuste `EXTERNAL_LED_ACTIVE_HIGH` e o circuito de polarização conforme esse módulo.

Compartilhe o GND da fonte de 12 V com o ESP32 da própria bancada. Os 12 V alimentam a fita; não devem ser aplicados ao GPIO nem diretamente ao gate comandado pelo ESP32.

## Indicação por bancada

| Situação | LED do botão | Fita externa |
| --- | --- | --- |
| SLAVE conectado e liberado | Aceso | Apagada |
| SLAVE aguardando decisão, ainda sincronizado | Aceso | Apagada |
| SLAVE perdedor/bloqueado | Apagado | Apagada |
| SLAVE vencedor | Pisca | Pisca em sincronia com o botão |
| MASTER aguardando resposta | Apagado | Acesa |
| MASTER com vencedor, apto a nova rodada | Aceso | Apagada |
| MASTER aguardando primeira liberação manual | Aceso | Apagada |

No boot de hardware, somente o botão pisca. Aguardando sincronização ou com conexão perdida, o SLAVE apaga ambos, exceto se já houver vitória confirmada: nesse caso conserva a sinalização até a nova rodada.

## Restrições de GPIO e placa

No ESP32 clássico, GPIOs 0, 2, 5, 12 e 15 participam de strapping; GPIOs 6..11 normalmente atendem à flash; GPIOs 34..39 são somente entrada e não têm pull-up interno. Os pinos provisórios escolhidos evitam esses grupos e deixam UART0 para monitoramento. Confirme o esquemático do módulo e da placa antes de redistribuir funções. A [referência GPIO da Espressif](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32/api-reference/peripherals/gpio.html) documenta as restrições.

ESP32-S2/S3/C3/C6 têm mapas de pinos, periféricos e características diferentes. Suporte à série ESP-IDF 6 não significa que esta pinagem seja válida nessas variantes. Uma portabilidade exige revisar pinos, alvo e testes.

## Alimentação e rádio

Use regulador/fonte dimensionados para o ESP32 transmitindo, o LED do botão e a fita externa de 12 V, com desacoplamento junto à placa e cabeamento adequado. Quedas de tensão podem reiniciar a placa ou comprometer a transmissão. Mantenha a antena afastada de metal, fiação de potência e planos de cobre incompatíveis com o módulo.

Todas as bancadas precisam usar o mesmo canal Wi-Fi e uma tabela de MACs coerente; não há roteador. Valide o equipamento no recinto e com as bancadas na posição final. Não há garantia de alcance ou latência antes dessa medição.

## Verificação antes do primeiro jogo

1. Confirme a tensão dos LEDs e os estágios de acionamento com alimentação desligada.
2. Confirme continuidade dos botões para GND, ausência de curtos e jumper de um único MASTER.
3. Ligue cada placa e registre o MAC STA impresso.
4. Atualize os nove MACs e regrave o mesmo firmware nas nove placas.
5. Confira os oito IDs ONLINE e LEDs READY antes de iniciar a disputa.
6. Confira a fita acesa no MASTER quando ARMED, o botão RESET aceso após vitória e a fita apagada nesse estado.
7. Execute os cenários de [testes físicos](testes.md), incluindo sincronia de botão/fita no vencedor e saídas totalmente apagadas nos perdedores.
