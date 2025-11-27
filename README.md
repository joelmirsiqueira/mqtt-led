# Discentes: Daniel Ferreira, Jalmir Silva de Siqueira & Joelmir Silva de Siqueira.

## 1 - Configuração do circuito
Perna positiva do LED está conectada ao resistor, o resistor está conectado na porta 5 e a outra perna do LED está conectada ao GND. Já o botão está com uma perna conectada a porta 23 e a outra ao GND.

![alt text](assets/circuit.jpg)


## 2 - Conexão do ESP32 com Wifi
Adaptamos a conexão a partir do template ESP-IDF "Wifi getting started", removendo as partes que não foram necessárias para o nosso projeto. Fizemos o include do módulo apartir do arquivo modificado disponível em [wifi.c](main/includes/wifi.c) e a função a ser importada pelo app_main.c está disponível em [wifi.h](main/includes/wifi.h). Utilizamos uma wifi privada, criada a partir de um Hotspot móvel (credenciais alteradas a partir do sdkconfig).


## 3 - Broker MQTT
Utilizamos o HiveMQ como Broker, no cluster público mqtt://broker.hivemq.com. Usamos conexão não criptografada pela porta 1883, portanto, não foi necessário baixar certificado TLS. Também não utilizamos autenticação.
Utilizamos os tópicos: 

![alt text](assets/topics.png)

## 4 - Funcionamento do código