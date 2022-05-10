# win-rio-client
Simple multicast client test program that uses the RIO library

## Build instructions

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
cd <repo-root>/RIOIOCPUDP
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

Optional arguments:
-h --help       shows help message and exits [default: false]
-v --version    prints version information and exits [default: false]
--nic           IfIndex or Name of NIC to use [default: "Ethernet"]
--mcast_ip      multicast group IP or range of groups IPs. [default: "239.5.69.2"]
--mcast_port    multicast port [default: 10000]
--total_pkts    Total packets to receive [default: 20000000]
```

### Using it alongside Windows swxtch-xnic2
* Set --nic to the swxtch-xnic2 Data network interface
* Set --mcast_ip to the multicast group IP or range of multicast groups to Join
* Set --total_pkts to the total packets expected to be received to end the run
