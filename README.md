# Chat TCP — medição de throughput (clientes → servidor)

Versões modificadas de `chat_server.c` e `chat_client.c` (C, POSIX sockets +
pthreads). O modo chat original continua funcionando; o **modo benchmark** é
ativado por argumentos extras e mede a taxa agregada (Mbps) recebida pelo
servidor com 1 a 10 clientes transmitindo continuamente.

## Estrutura do repositório

```
.
├── src/                    código-fonte (C)
│   ├── chat_server.c       servidor: uma thread por cliente, modo --bench
│   └── chat_client.c       cliente: modo --bench envia blocos sem digitação
├── scripts/
│   ├── build.sh            compila para bin/
│   ├── run_experiments.sh  N=1..10 com repetições -> results/*.csv
│   ├── run_all.sh          experimento completo (principal + blocos)
│   ├── analyze.py          gera tabelas e gráficos
│   ├── perf_run.sh         perfilamento de um cenário com perf (Linux, root)
│   ├── perf_all.sh         perfilamento dos cenários do relatório
│   └── make_pdf.sh         compila o relatório (pdflatex)
├── results/                dados brutos e tabelas
│   ├── results.csv         N=1..10, bloco 64 KiB, 5 repetições
│   ├── blocks.csv          varredura de blocos, N=10
│   ├── tables.md           tabelas geradas por analyze.py
│   ├── sysinfo.txt         informações do ambiente
│   └── perf/               saídas do perf (stat, cpu, report)
├── figures/                gráficos (PNG)
├── report/                 relatório técnico
│   ├── relatorio.tex
│   └── relatorio.pdf
└── bin/                    binários (gerados; ignorados pelo git)
```

> O código usa a API POSIX (`sys/socket.h`, `pthread`), portanto **não compila
> nativamente no Windows** (MSVC/MinGW). No Windows, use o **WSL2** (seção 3),
> que foi o ambiente usado nos experimentos. Todos os comandos abaixo são
> executados **a partir da raiz do repositório**.

---

## 1. Linux (Debian/Ubuntu e derivados)

### Pré-requisitos

```sh
sudo apt update
sudo apt install -y gcc make python3 python3-matplotlib
```

(Fedora: `sudo dnf install gcc make python3 python3-matplotlib`.)

### Compilar

```sh
sh scripts/build.sh
```

Equivale a:

```sh
mkdir -p bin
gcc -O2 -Wall -Wextra src/chat_server.c -o bin/chat_server -pthread
gcc -O2 -Wall -Wextra src/chat_client.c -o bin/chat_client -pthread
```

### Executar manualmente (2 clientes)

Terminal 1 — o servidor espera 2 clientes, 1 s de aquecimento e 3 s de medição:

```sh
bin/chat_server 5000 --bench 2 1 3
```

Terminais 2 e 3 — um cliente em cada (nomes diferentes; bloco de 64 KiB):

```sh
bin/chat_client 127.0.0.1 5000 --bench c1 65536
bin/chat_client 127.0.0.1 5000 --bench c2 65536
```

O servidor imprime:

```
RESULT clients=2 bytes=... seconds=3.0001 mbps=...
```

### Experimento completo

```sh
sh scripts/run_all.sh                          # compila e roda tudo (~20 min)
# ou apenas o principal:
REPS=5 sh scripts/run_experiments.sh results.csv   # N=1..10, 5 repetições
python3 scripts/analyze.py                     # tabelas + gráficos
```

Variáveis de `run_experiments.sh`: `REPS` (5), `WARMUP` (2 s), `MEASURE`
(10 s), `BLOCK` (65536), `MINN` (1), `MAXN` (10), `HOST` (127.0.0.1),
`PORT` (5000). O argumento é o nome do CSV dentro de `results/`.
Varredura de bloco com 10 clientes:

```sh
BLOCK=4096 REPS=3 MINN=10 sh scripts/run_experiments.sh blocks.csv
```

### Perfilamento (opcional, só Linux)

```sh
sudo apt install -y linux-tools-generic linux-tools-common
sudo PERF=perf sh scripts/perf_all.sh   # PERF = caminho do binário do perf
```

### Gerar o relatório em PDF (opcional)

```sh
sudo apt install -y texlive-latex-recommended texlive-latex-extra texlive-lang-portuguese
sh scripts/make_pdf.sh                  # gera report/relatorio.pdf
```

---

## 2. macOS

### Pré-requisitos

```sh
xcode-select --install            # clang e ferramentas de linha de comando
brew install python matplotlib    # opcional, só para analyze.py
```

### Compilar e executar

`cc`/`gcc` é um alias do `clang` no macOS; os comandos são os mesmos da seção
Linux:

```sh
sh scripts/build.sh
sh scripts/run_experiments.sh results.csv
python3 scripts/analyze.py
```

Observações:
- O código define `MSG_NOSIGNAL` como 0 quando ausente (macOS); o `SIGPIPE` já
  é ignorado no programa.
- O `perf` **não existe no macOS** (use o Instruments, se quiser perfilar).
- Os números de throughput dependem do hardware e do *loopback* do macOS e
  **não são comparáveis** com os do relatório (obtidos no Linux/WSL2).
- A compilação no macOS não foi testada pelo autor (somente Linux/WSL2).

---

## 3. Windows (via WSL2)

### 3.1 Instalar o WSL2 com Ubuntu

No **PowerShell como Administrador**:

```powershell
wsl --install -d Ubuntu
```

Reinicie se solicitado, abra o "Ubuntu" no menu Iniciar e crie usuário e senha.

### 3.2 Instalar as ferramentas dentro do Ubuntu

```sh
sudo apt update
sudo apt install -y gcc make python3 python3-matplotlib
```

### 3.3 Acessar a pasta do projeto

Os discos do Windows ficam em `/mnt/`. Por exemplo, para
`C:\Users\SEU_USUARIO\Downloads\repo`:

```sh
cd /mnt/c/Users/SEU_USUARIO/Downloads/repo
```

(Se preferir, clone/copie o repositório para o `~` do Ubuntu.)

### 3.4 Compilar e executar

Igual à seção Linux:

```sh
sh scripts/build.sh
REPS=5 sh scripts/run_experiments.sh results.csv
python3 scripts/analyze.py
```

### 3.5 Atalho a partir do PowerShell (sem abrir o Ubuntu)

```powershell
cd C:\Users\SEU_USUARIO\Downloads\repo
wsl -e sh scripts/build.sh
wsl -e sh -c "REPS=1 WARMUP=1 MEASURE=3 MAXN=3 sh scripts/run_experiments.sh teste.csv"
```

Se um `.sh` falhar com `\r: command not found`, o arquivo foi salvo com finais
de linha do Windows (CRLF); converta com `sed -i 's/\r$//' scripts/*.sh`.

> Importante: rode **uma única instância** do experimento por vez (todas usam
> a mesma porta); duas simultâneas invalidam as medições.

---

## 4. Teste com máquinas diferentes (rede real)

1. Máquina A (servidor): `bin/chat_server 5000 --bench 10 2 10`
2. Máquina B (10 clientes), apontando para o IP de A:

   ```sh
   for i in 1 2 3 4 5 6 7 8 9 10; do
     bin/chat_client IP_DO_SERVIDOR 5000 --bench c$i 65536 &
   done; wait
   ```

Libere a porta no firewall da máquina A. O servidor imprime a linha `RESULT`.

## 5. Protocolo do modo benchmark

1. Cliente conecta e envia `/bench <nome>\n`.
2. Quando os N clientes chegam (barreira), o servidor envia `GO <segundos>\n`
   a todos e dispara a thread medidora.
3. Cada cliente transmite blocos fixos continuamente por `<segundos>`
   (= aquecimento + medição + 1 s de folga) e encerra com
   `shutdown(SHUT_WR)`.
4. O medidor lê a soma dos contadores por thread em `t0` (após o aquecimento)
   e em `t1` (fim da medição):
   `Mbps = (B1 − B0) · 8 / (t1 − t0) / 10^6`.

Sem `--bench`, o servidor e o cliente funcionam como o chat original:

```sh
bin/chat_server 5000
bin/chat_client 127.0.0.1 5000
```

## 6. Resultados resumidos (WSL2, Ryzen 5 5600X, loopback, bloco 64 KiB)

Veja `results/tables.md`, `figures/` e `report/relatorio.pdf`. Com 1 cliente:
≈ 24 Gbps; saturação a partir de 4 clientes, em ≈ 76–85 Gbps.
