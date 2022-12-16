## Set the network environment for case-1

Router
```
tc qdisc del dev eth0 root
tc qdisc del dev eth1 root
tc qdisc add dev eth0 root handle 1: netem delay 5ms drop 1%
tc qdisc add dev eth0 parent 1: handle 2: tbf rate 100mbit burst 90155 latency 0.001ms
tc qdisc add dev eth1 root handle 1: netem delay 5ms drop 1%
tc qdisc add dev eth1 parent 1: handle 2: tbf rate 100mbit burst 90155 latency 0.001ms
```

Client & Server
```
sudo tc qdisc del dev ens5 root
sudo tc qdisc add dev ens5 root tbf rate 100mbit latency 0.001ms burst 90155
sudo ifconfig ens5 mtu 1500 up
```

## Set the network environment for case-2

Router
```
tc qdisc del dev eth0 root
tc qdisc del dev eth1 root
tc qdisc add dev eth0 root handle 1: netem delay 100ms drop 20%
tc qdisc add dev eth0 parent 1: handle 2: tbf rate 100mbit burst 90155 latency 0.001ms
tc qdisc add dev eth1 root handle 1: netem delay 100ms drop 20%
tc qdisc add dev eth1 parent 1: handle 2: tbf rate 100mbit burst 90155 latency 0.001ms
```

Client & Server
```
sudo tc qdisc del dev ens5 root
sudo tc qdisc add dev ens5 root tbf rate 100mbit latency 0.001ms burst 90155
sudo ifconfig ens5 mtu 1500 up
```

## Set the network environment for case-3

Router
```
tc qdisc del dev eth0 root
tc qdisc del dev eth1 root
tc qdisc add dev eth0 root handle 1: netem delay 100ms
tc qdisc add dev eth0 parent 1: handle 2: tbf rate 80mbit burst 90155 latency 0.001ms
tc qdisc add dev eth1 root handle 1: netem delay 100ms
tc qdisc add dev eth1 parent 1: handle 2: tbf rate 80mbit burst 90155 latency 0.001ms
```

Client
```
sudo tc qdisc del dev ens5 root
sudo tc qdisc add dev ens5 root tbf rate 100mbit latency 0.001ms burst 90155
sudo ifconfig ens5 mtu 1500 up
```

Server
```
sudo tc qdisc del dev ens5 root
sudo tc qdisc add dev ens5 root tbf rate 80mbit latency 0.001ms burst 90155
sudo ifconfig ens5 mtu 1500 up
```

## Compile
g++ -o client client.cpp -pthread 
g++ -o server server.cpp -pthread


## Prepare file
```
client:
cd test/
dd if=/dev/urandom of=data.bin bs=1M count=1024

server:
cd test/
touch data.bin
```

## Run
```
./client <file path> <server address> <server port#>
./server <file path> <server port#>
```

## Check file
```
md5sum data.bin

```

