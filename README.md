# win-rio-client
Simple multicast client test program that uses the RIO library

## Build instructions

### Build with Github Actions
If you need to build this repo, slack your Azure admin to start the runner (gh-runner-w-b).
The self-hosted runner is shutdown since this repo is built infrequently. 
There is a self-hosted runner dedicated to building this repo, since building XDP has some requirements that aren't accommodated by GH runners, like dual-NICs and the ability to reboot after installing XDP driver. 
These are required now, but may not be required by later versions.

### Pre-requisites

#### Visual Studio
Install the community edition of visual studio, C++ version.
Last verified version: "Visual Studio 17 2022"

#### CMake
```
Invoke-WebRequest -Uri https://github.com/Kitware/CMake/releases/download/v3.22.1/cmake-3.22.1-windows-x86_64.msi -OutFile .\cmake-3.22.1-windows-x86_64.msi
./cmake-3.22.1-windows-x86_64.msi
```

### Clone repo

If you haven't setup your github key and configured windows for SSH, see this link:
https://swxtchio.atlassian.net/wiki/spaces/SDMC/pages/556302354/Windows+Server+in+Azure#Install-git-for-windows

```
git clone git@github.com:swxtchio/win-rio-client.git
git submodule init
git submodule update
```

### Build
```
cd <repo-root>
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -- /consoleloggerparameters:Nosummary
```
#### Optional
* Change `Release` to `Debug` as needed.
* Add `-v` _before_ the `' -- '` for more build details
  
#### Output
Output files are in `<repo-root>/bin`.

```
bin\Release\swxtch-perf-rio.exe
```

## Usage

```
swxtch-perf-rio.exe -h
Usage: C:\Users\alex\source\repos\win-rio-client\bin\Release\swxtch-perf-rio.exe [options]

Positional arguments:
[producer|consumer] Produce or Consume multicast packets.

Optional arguments:
-h --help       shows help message and exits [default: false]
-v --version    prints version information and exits [default: false]
--nic           IfIndex or Name of NIC to use [default: "Ethernet"]
--mcast_ip      multicast group IP or range of groups IPs. [default: "239.5.69.2"]
--mcast_port    multicast port [default: 10000]
--pps           Packets per seconds to produce. [default: 1]
--total_pkts    Total packets to receive [default: 20000000]
--seconds       Number of seconds to run the application. Insert 0 if you do not want to a use a time limit.
                [default: 0]
```
### How to produce traffic with another application and consume with RIO App

The RIO application will consume UDP Multicast traffic with 100 Bytes of payload. Other packet sizes
will be consumed but ignored in the statistics calculation.
The UDP Payload needs to have a packet sequence of 64 bits stored at offset +32 (Sequence starts at byte 32
up to 63). The other fields could be filled with zeroes.

### Using it alongside Windows swxtch-xnic2
* Set --nic to the swxtch-xnic2 Data network interface
* Set --mcast_ip to the multicast group IP or range of multicast groups to Join
```
Example of one multicast group: --mcast_ip 239.5.69.2
Example of a range of 32 multicast groups: --mcast_ip 239.5.69.0-31
```
* Set --total_pkts to the total packets expected to be received to end the run
