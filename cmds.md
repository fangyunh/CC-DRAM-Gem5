Build a img based on Dockerfile:
docker build -t fyhgem5_img .

Create container gem5_v0 based on img:
docker run --name gem5_v0 -it -v D:\RPI\Compression_Capable_DRAM\:/home/root fyhgem5_img

Start a Container:
docker start -ai gem5_v0

Compile gem5:
scons build/{ISA}/gem5.{variants} -j 12    //ISA includes x86, arm, RISCV; variants includes opt, debug, fast

Clean Compiled file:
scons build/{ISA}/gem5.{variants} -c

Run Scripts:
./build/{ISA}/gem5.{variant} [gem5 options] {simulation script} [script options]    